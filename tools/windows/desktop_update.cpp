/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_update.hpp"
#include "desktop_update_release.hpp"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <process.h>

#include <array>
#include <filesystem>
#include <mutex>
#include <new>
#include <optional>
#include <utility>
#include <vector>

#ifndef DESKTOP_MELEE_VERSION
#define DESKTOP_MELEE_VERSION "0.1.17"
#endif
#ifndef DESKTOP_MELEE_GITHUB_OWNER
#define DESKTOP_MELEE_GITHUB_OWNER "Bluesguitartravis-droid"
#endif
#ifndef DESKTOP_MELEE_GITHUB_REPO
#define DESKTOP_MELEE_GITHUB_REPO "desktop-melee"
#endif

namespace desktop_update {
namespace {

struct Event {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (value) CloseHandle(value); }
    Event() = default;
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};

struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET handle) : value(handle) {}
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};

struct Completion {
    DWORD status = 0, error = 0, bytes = 0;
};

struct IoState {
    Event event;
    std::mutex mutex;
    Completion completion;
    /* WinHTTP may still be writing into this buffer after cancellation. Its
     * callback context keeps it alive until HANDLE_CLOSING, the final callback. */
    std::array<char, 8192> buffer{};
};

struct CallbackContext { std::shared_ptr<IoState> io; };

void CALLBACK completion_callback(HINTERNET, DWORD_PTR opaque, DWORD status,
                                  void* information, DWORD length) noexcept {
    auto* context = reinterpret_cast<CallbackContext*>(opaque);
    if (!context) return;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        delete context;
        return;
    }
    if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
        status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE &&
        status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE &&
        status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return;
    const auto io = context->io;
    Completion completion{status, 0, 0};
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        completion.error = information && length >= sizeof(WINHTTP_ASYNC_RESULT)
            ? static_cast<const WINHTTP_ASYNC_RESULT*>(information)->dwError
            : ERROR_WINHTTP_INTERNAL_ERROR;
    } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) completion.bytes = length;
    {
        std::lock_guard lock(io->mutex);
        io->completion = completion;
    }
    SetEvent(io->event.value);
}

UpdateResult unavailable(std::string detail) {
    UpdateResult result;
    result.detail = std::move(detail);
    return result;
}

bool cancelled(HANDLE event) {
    return WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

std::wstring widen_ascii(std::string_view text) {
    return std::wstring(text.begin(), text.end());
}

struct Waiter {
    HANDLE cancel;
    std::shared_ptr<IoState> io;
    ULONGLONG deadline;
    DWORD error = 0;

    template<class Start> bool operation(DWORD expected, Start start) {
        if (cancelled(cancel)) { error = ERROR_OPERATION_ABORTED; return false; }
        if (GetTickCount64() >= deadline) { error = ERROR_WINHTTP_TIMEOUT; return false; }
        ResetEvent(io->event.value);
        {
            std::lock_guard lock(io->mutex);
            io->completion = {};
        }
        if (!start()) {
            const DWORD code = GetLastError();
            if (code != ERROR_IO_PENDING) { error = code; return false; }
        }
        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = now < deadline ? static_cast<DWORD>(deadline - now) : 0;
        const HANDLE events[] = {cancel, io->event.value};
        const DWORD outcome = WaitForMultipleObjects(2, events, FALSE, remaining);
        if (outcome == WAIT_OBJECT_0) { error = ERROR_OPERATION_ABORTED; return false; }
        if (outcome == WAIT_TIMEOUT) { error = ERROR_WINHTTP_TIMEOUT; return false; }
        if (outcome != WAIT_OBJECT_0 + 1) { error = GetLastError(); return false; }
        std::lock_guard lock(io->mutex);
        if (io->completion.status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            error = io->completion.error; return false;
        }
        if (io->completion.status != expected) { error = ERROR_WINHTTP_INTERNAL_ERROR; return false; }
        return true;
    }
    DWORD bytes() const {
        std::lock_guard lock(io->mutex);
        return io->completion.bytes;
    }
    UpdateResult failure() const {
        if (error == ERROR_OPERATION_ABORTED) return unavailable("Update check cancelled.");
        if (error == ERROR_WINHTTP_TIMEOUT) return unavailable("Update check timed out. Try again later.");
        return unavailable("Could not reach GitHub. Check your connection or try again later.");
    }
};

UpdateResult fetch_release(HANDLE cancel) {
    constexpr std::string_view owner = DESKTOP_MELEE_GITHUB_OWNER;
    constexpr std::string_view repository = DESKTOP_MELEE_GITHUB_REPO;
    constexpr std::string_view version = DESKTOP_MELEE_VERSION;
    if (!release::valid_repository(owner, repository) || !release::parse_version(version))
        return unavailable("This build has no valid update configuration.");
    if (cancelled(cancel)) return unavailable("Update check cancelled.");
    const ULONGLONG deadline = GetTickCount64() + 15000;
    const auto user_agent = widen_ascii("DesktopMelee/" + std::string(version));
    InternetHandle session(WinHttpOpen(user_agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session.value || !WinHttpSetTimeouts(session.value, 2000, 3000, 3000, 3000))
        return unavailable("Could not start the update connection.");
    InternetHandle connection(WinHttpConnect(session.value, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection.value) return unavailable("Could not start the update connection.");
    const auto path = widen_ascii("/repos/" + std::string(owner) + "/" + std::string(repository) +
                                  "/releases?per_page=20&page=1");
    InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) return unavailable("Could not start the update request.");

    /* Only the fixed HTTPS GitHub API endpoint is contacted. A repository
     * redirect is an error, never permission to send a request elsewhere.
     * No cookies, authorization token or automatic Windows credentials. */
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD headers_limit = 32768;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headers_limit, sizeof(headers_limit)))
        return unavailable("Could not configure the update request.");

    auto io = std::make_shared<IoState>();
    if (!io->event.value) return unavailable("Could not create the update worker event.");
    auto context = std::make_unique<CallbackContext>(CallbackContext{io});
    DWORD_PTR context_value = reinterpret_cast<DWORD_PTR>(context.get());
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONTEXT_VALUE, &context_value, sizeof(context_value)))
        return unavailable("Could not configure the update worker.");
    constexpr DWORD notifications = WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
        WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE | WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
        WINHTTP_CALLBACK_FLAG_REQUEST_ERROR | WINHTTP_CALLBACK_FLAG_HANDLES;
    if (WinHttpSetStatusCallback(request.value, completion_callback, notifications, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        return unavailable("Could not configure update notifications.");
    /* Ownership passes to the final HANDLE_CLOSING callback. The worker is
     * the only thread issuing/closing WinHTTP calls for this request. */
    context.release();
    Waiter wait{cancel, io, deadline};
    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n";
    if (!wait.operation(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, [&]() {
            return WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1),
                WINHTTP_NO_REQUEST_DATA, 0, 0, context_value);
        }) || !wait.operation(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, [&]() {
            return WinHttpReceiveResponse(request.value, nullptr);
        })) return wait.failure();

    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
        return unavailable("GitHub returned an unreadable update response.");
    if (status == 403 || status == 429)
        return unavailable("GitHub temporarily limited update checks. Try again later.");
    if (status == 404) return unavailable("No public Desktop Melee release is available yet.");
    if (status >= 300 && status < 400)
        return unavailable("The update repository moved. Check the project page for a newer launcher.");
    if (status != 200) return unavailable("GitHub could not complete the update check. Try again later.");

    DWORD content_length = 0;
    size = sizeof(content_length);
    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &content_length, &size, WINHTTP_NO_HEADER_INDEX) &&
        content_length > release::kMaxResponseBytes)
        return unavailable("The update response was larger than expected.");
    std::string body;
    for (;;) {
        if (!wait.operation(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, [&]() {
                return WinHttpReadData(request.value, io->buffer.data(), static_cast<DWORD>(io->buffer.size()), nullptr);
            })) return wait.failure();
        const DWORD bytes = wait.bytes();
        if (bytes == 0) break;
        if (bytes > io->buffer.size() || bytes > release::kMaxResponseBytes - body.size())
            return unavailable("The update response was larger than expected.");
        body.append(io->buffer.data(), bytes);
    }
    if (cancelled(cancel)) return unavailable("Update check cancelled.");
    const auto selection = release::select(body, version, owner, repository);
    if (selection.status == release::Status::Invalid)
        return unavailable("GitHub returned invalid update metadata.");
    if (selection.status == release::Status::NoCompatibleRelease)
        return unavailable("No compatible stable Desktop Melee release is available yet.");
    UpdateResult result;
    result.version = selection.version;
    if (selection.status == release::Status::UpToDate) {
        result.status = UpdateStatus::UpToDate;
        result.detail = "Desktop Melee is up to date.";
    } else {
        result.status = UpdateStatus::Available;
        result.url = selection.url;
        result.sha256 = selection.sha256;
        result.bytes = selection.bytes;
        result.direct_download = selection.direct_download;
        result.installer = selection.installer;
        result.detail = "Desktop Melee " + result.version + " is available.";
    }
    return result;
}

std::optional<std::string> sha256_file(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_bytes = 0, digest_bytes = 0, size = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};
    auto close_algorithm = [&]() { BCryptCloseAlgorithmProvider(algorithm, 0); };
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes), &size, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digest_bytes), sizeof(digest_bytes), &size, 0) != 0 ||
        digest_bytes != 32) {
        close_algorithm();
        return {};
    }
    std::vector<unsigned char> object(object_bytes), digest(digest_bytes);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_bytes, nullptr, 0, 0) != 0) {
        close_algorithm();
        return {};
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        BCryptDestroyHash(hash);
        close_algorithm();
        return {};
    }
    std::array<unsigned char, 64 * 1024> buffer{};
    DWORD bytes = 0;
    bool ok = true;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes, nullptr) && bytes) {
        if (BCryptHashData(hash, buffer.data(), bytes, 0) != 0) { ok = false; break; }
    }
    if (bytes != 0) ok = false;
    CloseHandle(file);
    if (ok && BCryptFinishHash(hash, digest.data(), digest_bytes, 0) != 0) ok = false;
    BCryptDestroyHash(hash);
    close_algorithm();
    if (!ok) return {};
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    text.reserve(64);
    for (unsigned char value : digest) {
        text.push_back(hex[value >> 4]);
        text.push_back(hex[value & 15]);
    }
    return text;
}

InstallResult download_installer(const UpdateResult& update, HANDLE cancel) {
    InstallResult result;
    result.status = InstallStatus::Failed;
    if (!update.installer || !update.direct_download || update.sha256.size() != 64 ||
        update.bytes == 0 || update.bytes > 256ull * 1024ull * 1024ull) {
        result.detail = "This release does not provide a verifiable in-app Windows installer.";
        return result;
    }
    if (cancelled(cancel)) {
        result.detail = "Update download cancelled.";
        return result;
    }

    const std::wstring url = widen_ascii(update.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || !parts.lpszHostName || !parts.dwHostNameLength) {
        result.detail = "The update download URL is invalid.";
        return result;
    }
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring resource(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
        resource.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

    InternetHandle session(WinHttpOpen(L"DesktopMelee-InAppUpdater/1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value || !WinHttpSetTimeouts(session.value, 3000, 5000, 5000, 15000)) {
        result.detail = "Could not start the update download.";
        return result;
    }
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), parts.nPort, 0));
    if (!connection.value) {
        result.detail = "Could not connect to the update server.";
        return result;
    }
    InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.value) {
        result.detail = "Could not start the update download.";
        return result;
    }
    DWORD disabled = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled))) {
        result.detail = "Could not secure the update request.";
        return result;
    }
    constexpr wchar_t headers[] = L"Accept: application/octet-stream\r\n";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        result.detail = "Could not download the update. Check your connection and try again.";
        return result;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) {
        result.detail = "The update server did not return the installer.";
        return result;
    }

    std::array<wchar_t, 32768> temp{};
    const DWORD temp_length = GetTempPathW(static_cast<DWORD>(temp.size()), temp.data());
    if (!temp_length || temp_length >= temp.size()) {
        result.detail = "Windows could not create an update folder.";
        return result;
    }
    std::filesystem::path folder = std::filesystem::path(temp.data()) / L"DesktopMelee" / L"Updates";
    std::error_code filesystem_error;
    std::filesystem::create_directories(folder, filesystem_error);
    if (filesystem_error) {
        result.detail = "Windows could not create an update folder.";
        return result;
    }
    const auto path = folder / (L"Desktop-Melee-Setup-x64-" + widen_ascii(update.version) + L".exe");
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        result.detail = "Windows could not save the update installer.";
        return result;
    }

    std::array<unsigned char, 64 * 1024> buffer{};
    uint64_t total = 0;
    bool ok = true;
    for (;;) {
        if (cancelled(cancel)) { ok = false; result.detail = "Update download cancelled."; break; }
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            ok = false; result.detail = "The update download was interrupted."; break;
        }
        if (read == 0) break;
        total += read;
        if (total > update.bytes) {
            ok = false; result.detail = "The update download was larger than expected."; break;
        }
        DWORD written = 0;
        if (!WriteFile(output, buffer.data(), read, &written, nullptr) || written != read) {
            ok = false; result.detail = "Windows could not save the complete update."; break;
        }
    }
    if (ok && total != update.bytes) {
        ok = false;
        result.detail = "The update download was incomplete.";
    }
    if (ok && !FlushFileBuffers(output)) {
        ok = false;
        result.detail = "Windows could not finish saving the update.";
    }
    CloseHandle(output);
    if (!ok) {
        DeleteFileW(path.c_str());
        return result;
    }
    const auto digest = sha256_file(path);
    if (!digest || *digest != update.sha256) {
        DeleteFileW(path.c_str());
        result.detail = "Update verification failed; the downloaded installer was deleted.";
        return result;
    }
    result.status = InstallStatus::Ready;
    result.installer_path = path;
    result.detail = "Update downloaded and verified.";
    return result;
}


} // namespace

struct UpdateChecker::Shared {
    mutable std::mutex mutex;
    UpdateResult result;
    bool running = false;
    HANDLE cancel_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Shared() { if (cancel_event) CloseHandle(cancel_event); }
};

UpdateChecker::UpdateChecker() : shared_(std::make_shared<Shared>()) {}
UpdateChecker::~UpdateChecker() { cancel(); }

bool UpdateChecker::start() {
    std::lock_guard lock(shared_->mutex);
    if (shared_->running) return false;
    if (!shared_->cancel_event) {
        shared_->result = unavailable("Could not start the update worker.");
        return false;
    }
    auto* context = new (std::nothrow) std::shared_ptr<Shared>(shared_);
    if (!context) {
        shared_->result = unavailable("Could not start the update worker.");
        return false;
    }
    ResetEvent(shared_->cancel_event);
    shared_->result = {};
    shared_->result.status = UpdateStatus::Checking;
    shared_->result.detail = "Checking for updates...";
    shared_->running = true;
    const uintptr_t thread = _beginthreadex(nullptr, 0, [](void* raw) -> unsigned {
        std::unique_ptr<std::shared_ptr<Shared>> context(static_cast<std::shared_ptr<Shared>*>(raw));
        run(*context);
        return 0;
    }, context, 0, nullptr);
    if (!thread) {
        delete context;
        shared_->running = false;
        shared_->result = unavailable("Could not start the update worker.");
        return false;
    }
    CloseHandle(reinterpret_cast<HANDLE>(thread));
    return true;
}

void UpdateChecker::run(std::shared_ptr<Shared> shared) noexcept {
    UpdateResult result;
    try {
        result = fetch_release(shared->cancel_event);
        if (cancelled(shared->cancel_event)) result = unavailable("Update check cancelled.");
    } catch (...) {
        result = unavailable("Could not finish the update check.");
    }
    std::lock_guard lock(shared->mutex);
    shared->result = std::move(result);
    shared->running = false;
}

UpdateResult UpdateChecker::poll() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

void UpdateChecker::cancel() noexcept {
    if (shared_ && shared_->cancel_event) SetEvent(shared_->cancel_event);
}

struct UpdateInstaller::Shared {
    mutable std::mutex mutex;
    InstallResult result;
    bool running = false;
    HANDLE cancel_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~Shared() { if (cancel_event) CloseHandle(cancel_event); }
};

struct InstallThreadContext {
    std::shared_ptr<UpdateInstaller::Shared> shared;
    UpdateResult update;
};

UpdateInstaller::UpdateInstaller() : shared_(std::make_shared<Shared>()) {}
UpdateInstaller::~UpdateInstaller() { cancel(); }

bool UpdateInstaller::start(const UpdateResult& update) {
    std::lock_guard lock(shared_->mutex);
    if (shared_->running) return false;
    if (!shared_->cancel_event) {
        shared_->result.status = InstallStatus::Failed;
        shared_->result.detail = "Could not start the update downloader.";
        return false;
    }
    if (update.status != UpdateStatus::Available || !update.installer || !update.direct_download ||
        update.sha256.size() != 64 || update.bytes == 0) {
        shared_->result.status = InstallStatus::Failed;
        shared_->result.detail = "This release cannot be installed from inside Desktop Melee.";
        return false;
    }
    auto* context = new (std::nothrow) InstallThreadContext{shared_, update};
    if (!context) {
        shared_->result.status = InstallStatus::Failed;
        shared_->result.detail = "Could not start the update downloader.";
        return false;
    }
    ResetEvent(shared_->cancel_event);
    shared_->result = {};
    shared_->result.status = InstallStatus::Downloading;
    shared_->result.detail = "Downloading and verifying the update...";
    shared_->running = true;
    const uintptr_t thread = _beginthreadex(nullptr, 0, [](void* raw) -> unsigned {
        std::unique_ptr<InstallThreadContext> context(static_cast<InstallThreadContext*>(raw));
        run(context->shared, std::move(context->update));
        return 0;
    }, context, 0, nullptr);
    if (!thread) {
        delete context;
        shared_->running = false;
        shared_->result.status = InstallStatus::Failed;
        shared_->result.detail = "Could not start the update downloader.";
        return false;
    }
    CloseHandle(reinterpret_cast<HANDLE>(thread));
    return true;
}

void UpdateInstaller::run(std::shared_ptr<Shared> shared, UpdateResult update) noexcept {
    InstallResult result;
    try {
        result = download_installer(update, shared->cancel_event);
    } catch (...) {
        result.status = InstallStatus::Failed;
        result.detail = "Could not finish downloading the update.";
    }
    std::lock_guard lock(shared->mutex);
    shared->result = std::move(result);
    shared->running = false;
}

InstallResult UpdateInstaller::poll() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

void UpdateInstaller::cancel() noexcept {
    if (shared_ && shared_->cancel_event) SetEvent(shared_->cancel_event);
}

} // namespace desktop_update

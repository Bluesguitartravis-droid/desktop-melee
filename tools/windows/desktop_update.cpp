/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_update.hpp"
#include "desktop_update_release.hpp"

#include <windows.h>
#include <winhttp.h>
#include <process.h>

#include <array>
#include <mutex>
#include <new>
#include <utility>

#ifndef DESKTOP_MELEE_VERSION
#define DESKTOP_MELEE_VERSION "0.1.6"
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
        result.direct_download = selection.direct_download;
        result.installer = selection.installer;
        result.detail = "Desktop Melee " + result.version + " is available.";
    }
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

} // namespace desktop_update

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_update.hpp"

#include <curl/curl.h>
#include <thread>

namespace desktop_linux::updates {
namespace {
struct Transfer {
    std::string body;
    std::shared_ptr<Shared> shared;
};

size_t receive(char* bytes, size_t size, size_t count, void* context) {
    auto& transfer = *static_cast<Transfer*>(context);
    if (size && count > desktop_update::release::kMaxResponseBytes / size) return 0;
    const size_t length = size * count;
    if (transfer.shared->canceled || length > desktop_update::release::kMaxResponseBytes - transfer.body.size())
        return 0;
    try { transfer.body.append(bytes, length); }
    catch (...) { return 0; }
    return length;
}

int progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<Shared*>(context)->canceled ? 1 : 0;
}

Result request(std::string_view current, const std::shared_ptr<Shared>& shared) {
    Result result;
    CURL* curl = curl_easy_init();
    if (!curl) return result;
    Transfer transfer{{}, shared};
    const std::string agent = "DesktopMelee/" + std::string(current);
    const std::string endpoint = "https://api.github.com/repos/" + std::string(owner) + "/" +
                                 repository + "/releases?per_page=20&page=1";
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2026-03-10");
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_IGNORED);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 8000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, shared.get());
    const auto code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (code == CURLE_OK && status == 200) result = select(transfer.body, current);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
} // namespace

bool Checker::start(std::string current) {
    if (shared_ && poll().status == Status::Checking) return false;
    // SDL, CURL and worker lifetime are independent. Closing the launcher only
    // sets cancellation; it never waits for DNS or a network operation.
    static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!initialized) return false;
    if (shared_) shared_->canceled = true;
    auto shared = std::make_shared<Shared>();
    shared->result.status = Status::Checking;
    shared->result.detail = "Checking GitHub for Linux updates...";
    try {
        std::thread([shared, current = std::move(current)] {
            Result result;
            try { result = request(current, shared); } catch (...) { /* Keep offline play available. */ }
            if (!shared->canceled) {
                std::lock_guard lock(shared->mutex);
                shared->result = std::move(result);
            }
        }).detach();
    } catch (...) { return false; }
    shared_ = std::move(shared);
    return true;
}
} // namespace desktop_linux::updates

/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

// Share the bounded JSON reader and trusted-URL checks with the Windows client.
// Linux has a separate release selector; it never returns a Windows asset.
#include "../windows/desktop_update_release.hpp"

#include <atomic>
#include <memory>
#include <mutex>

namespace desktop_linux::updates {

inline constexpr const char* owner = "Bluesguitartravis-droid";
inline constexpr const char* repository = "desktop-melee";
inline constexpr const char* appimage = "Desktop-Melee-Linux-x86_64.AppImage";
inline constexpr const char* archive = "Desktop-Melee-Linux-x86_64.tar.gz";

struct Version {
    desktop_update::release::Version core;
    uint32_t preview = 0; // zero is the stable version, newer than its previews
    bool operator==(const Version&) const = default;
    bool operator<(const Version& other) const {
        if (!(core == other.core)) return core < other.core;
        if (preview == 0) return false;
        return other.preview == 0 || preview < other.preview;
    }
};

inline std::optional<Version> parse_version(std::string_view text) {
    const auto separator = text.find("-linux.");
    const auto core = desktop_update::release::parse_version(text.substr(0, separator));
    if (!core) return {};
    Version result{*core, 0};
    if (separator != std::string_view::npos) {
        const auto number = text.substr(separator + 7);
        if (number.empty() || number.front() == '0') return {};
        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), result.preview);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || result.preview == 0)
            return {};
    }
    return result;
}

enum class Status { Checking, Available, UpToDate, Unavailable };
struct Result {
    Status status = Status::Unavailable;
    std::string version, url;
    std::string detail = "Updates could not be checked. You can still play offline.";
};

inline Result select(std::string_view json, std::string_view current) {
    namespace release = desktop_update::release;
    using release::detail::Kind;
    Result result;
    const auto installed = parse_version(current);
    release::detail::Json document(json);
    if (!installed || !document.parse() || document.root().kind != Kind::Array ||
        document.root().elements.size() > release::kMaxReleases) return result;
    std::optional<Version> newest;
    for (const auto id : document.root().elements) {
        const auto& item = document.at(id);
        const auto* tag = document.member(item, "tag_name");
        const auto* draft = document.member(item, "draft");
        const auto* prerelease = document.member(item, "prerelease");
        const auto* page = document.member(item, "html_url");
        const auto* assets = document.member(item, "assets");
        if (!tag || tag->kind != Kind::String || !tag->text.starts_with("desktop-v") ||
            !draft || draft->kind != Kind::Boolean || draft->boolean ||
            !prerelease || prerelease->kind != Kind::Boolean ||
            !page || page->kind != Kind::String || !assets || assets->kind != Kind::Array) continue;
        const auto number = std::string_view(tag->text).substr(9);
        const auto version = parse_version(number);
        if (!version || prerelease->boolean != (version->preview != 0) ||
            (newest && !(*newest < *version)) ||
            !release::detail::matching_url(page->text, owner, repository, "releases/tag/" + tag->text)) continue;
        std::string selected;
        for (const auto asset_id : assets->elements) {
            const auto& asset = document.at(asset_id);
            const auto* name = document.member(asset, "name");
            const auto* state = document.member(asset, "state");
            const auto* url = document.member(asset, "browser_download_url");
            const auto* size = document.member(asset, "size");
            if (!name || name->kind != Kind::String || (name->text != appimage && name->text != archive) ||
                !state || state->kind != Kind::String || state->text != "uploaded" ||
                !url || url->kind != Kind::String || !size || size->kind != Kind::Number) continue;
            uint64_t bytes = 0;
            const auto parsed = std::from_chars(size->text.data(), size->text.data() + size->text.size(), bytes);
            const std::string suffix = "releases/download/" + tag->text + "/" + name->text;
            if (parsed.ec != std::errc{} || parsed.ptr != size->text.data() + size->text.size() || !bytes ||
                !release::detail::matching_url(url->text, owner, repository, suffix)) continue;
            selected = "https://github.com/" + std::string(owner) + "/" + repository + "/" + suffix;
            if (name->text == appimage) break;
        }
        if (selected.empty()) continue; // A Windows-only release cannot update Linux.
        newest = version;
        result.version = number;
        result.url = std::move(selected);
    }
    if (newest) {
        result.status = *installed < *newest ? Status::Available : Status::UpToDate;
        result.detail = result.status == Status::Available ? "A newer Linux build is available." :
                                                           "You have the newest compatible Linux build.";
    }
    return result;
}

struct Shared {
    std::mutex mutex;
    std::atomic_bool canceled{false};
    Result result;
};

class Checker {
public:
    ~Checker() { if (shared_) shared_->canceled = true; }
    bool start(std::string current);
    Result poll() const {
        if (!shared_) return {};
        std::lock_guard lock(shared_->mutex);
        return shared_->result;
    }
private:
    std::shared_ptr<Shared> shared_;
};

} // namespace desktop_linux::updates

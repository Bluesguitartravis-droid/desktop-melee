/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace desktop_update::release {

inline constexpr size_t kMaxResponseBytes = 1024 * 1024;
inline constexpr size_t kMaxReleases = 20;
inline constexpr std::string_view kTagPrefix = "desktop-v";
inline constexpr std::string_view kWindowsInstaller = "Desktop-Melee-Setup-x64.exe";
inline constexpr std::string_view kWindowsAsset = "Desktop-Melee-Windows-x64.zip";

struct Version {
    std::array<uint32_t, 3> parts{};
    bool operator==(const Version&) const = default;
    bool operator<(const Version& other) const { return parts < other.parts; }
};

inline std::optional<Version> parse_version(std::string_view text) {
    Version version;
    for (size_t part = 0; part < version.parts.size(); ++part) {
        const size_t end = text.find('.');
        const auto digits = text.substr(0, end);
        if (digits.empty() || (digits.size() > 1 && digits.front() == '0')) return {};
        for (char c : digits) if (c < '0' || c > '9') return {};
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(),
                                             version.parts[part]);
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) return {};
        if (part == 2) {
            if (end != std::string_view::npos) return {};
        } else {
            if (end == std::string_view::npos) return {};
            text.remove_prefix(end + 1);
        }
    }
    return version;
}

inline bool valid_repository(std::string_view owner, std::string_view repository) {
    const auto alnum = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (owner.empty() || owner.size() > 39 || repository.empty() || repository.size() > 100 ||
        !alnum(owner.front()) || !alnum(owner.back()) || repository == "." || repository == "..") return false;
    for (char c : owner) if (!alnum(c) && c != '-') return false;
    for (char c : repository) if (!alnum(c) && c != '-' && c != '_' && c != '.') return false;
    return true;
}

namespace detail {

enum class Kind { Null, Boolean, Number, String, Array, Object };
struct Value {
    Kind kind = Kind::Null;
    bool boolean = false;
    std::string text;
    std::vector<size_t> elements;
    std::vector<std::pair<std::string, size_t>> members;
};

/* The response is small and untrusted. A complete parser prevents nested
 * "tag_name" text, escaped quotes or duplicate keys from impersonating release
 * metadata. Bound input bytes, strings, nesting, nodes and container widths. */
class Json {
public:
    explicit Json(std::string_view text) : text_(text) {}
    bool parse() {
        if (text_.empty() || text_.size() > kMaxResponseBytes) return false;
        const auto root = value(0);
        whitespace();
        return root && *root == 0 && pos_ == text_.size();
    }
    const Value& root() const { return values_[0]; }
    const Value& at(size_t index) const { return values_[index]; }
    const Value* member(const Value& object, std::string_view name) const {
        if (object.kind != Kind::Object) return nullptr;
        for (const auto& field : object.members)
            if (field.first == name) return &values_[field.second];
        return nullptr;
    }

private:
    static constexpr size_t kMaxDepth = 32, kMaxNodes = 20000;
    static constexpr size_t kMaxContainer = 256, kMaxString = 128 * 1024;
    std::string_view text_;
    size_t pos_ = 0;
    std::vector<Value> values_;

    void whitespace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\r' ||
               text_[pos_] == '\n' || text_[pos_] == '\t')) ++pos_;
    }
    bool take(char c) {
        whitespace();
        if (pos_ == text_.size() || text_[pos_] != c) return false;
        ++pos_; return true;
    }
    bool hex4(uint32_t& result) {
        if (text_.size() - pos_ < 4) return false;
        result = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            unsigned v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else return false;
            result = result * 16 + v;
        }
        return true;
    }
    static void utf8(std::string& out, uint32_t c) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        }
    }
    bool string(std::string& out) {
        if (!take('"')) return false;
        while (pos_ < text_.size() && out.size() <= kMaxString) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (pos_ == text_.size()) return false;
                const char escape = text_[pos_++];
                switch (escape) {
                case '"': case '\\': case '/': out.push_back(escape); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t code;
                    if (!hex4(code)) return false;
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (text_.size() - pos_ < 6 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') return false;
                        pos_ += 2;
                        uint32_t low;
                        if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                    utf8(out, code); break;
                }
                default: return false;
                }
            } else if (c < 0x80) out.push_back(static_cast<char>(c));
            else {
                const size_t begin = pos_ - 1;
                unsigned extra;
                uint32_t code, minimum;
                if (c >= 0xc2 && c <= 0xdf) { extra = 1; code = c & 31; minimum = 0x80; }
                else if (c >= 0xe0 && c <= 0xef) { extra = 2; code = c & 15; minimum = 0x800; }
                else if (c >= 0xf0 && c <= 0xf4) { extra = 3; code = c & 7; minimum = 0x10000; }
                else return false;
                if (text_.size() - pos_ < extra) return false;
                for (unsigned i = 0; i < extra; ++i) {
                    const unsigned char next = static_cast<unsigned char>(text_[pos_++]);
                    if ((next & 0xc0) != 0x80) return false;
                    code = (code << 6) | (next & 63);
                }
                if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
                out.append(text_.substr(begin, extra + 1));
            }
        }
        return false;
    }
    bool number(std::string& out) {
        const size_t begin = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') ++pos_;
        if (pos_ == text_.size()) return false;
        if (text_[pos_] == '0') ++pos_;
        else {
            if (text_[pos_] < '1' || text_[pos_] > '9') return false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            const size_t first = ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == first) return false;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            const size_t first = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
            if (pos_ == first) return false;
        }
        if (pos_ - begin > 128) return false;
        out.assign(text_.substr(begin, pos_ - begin));
        return true;
    }
    std::optional<size_t> value(size_t depth) {
        whitespace();
        if (depth > kMaxDepth || pos_ == text_.size() || values_.size() >= kMaxNodes) return {};
        const size_t id = values_.size();
        values_.push_back({});
        const char c = text_[pos_];
        if (c == '"') {
            values_[id].kind = Kind::String;
            if (!string(values_[id].text)) return {};
        } else if (c == '[' || c == '{') {
            const bool object = c == '{';
            values_[id].kind = object ? Kind::Object : Kind::Array;
            ++pos_;
            if (take(object ? '}' : ']')) return id;
            for (size_t count = 0; ; ++count) {
                if (count >= kMaxContainer) return {};
                std::string key;
                if (object) {
                    if (!string(key) || !take(':')) return {};
                    for (const auto& field : values_[id].members) if (field.first == key) return {};
                }
                const auto child = value(depth + 1);
                if (!child) return {};
                if (object) values_[id].members.emplace_back(std::move(key), *child);
                else values_[id].elements.push_back(*child);
                if (take(object ? '}' : ']')) break;
                if (!take(',')) return {};
            }
        } else if (text_.substr(pos_, 4) == "true") {
            pos_ += 4; values_[id].kind = Kind::Boolean; values_[id].boolean = true;
        } else if (text_.substr(pos_, 5) == "false") {
            pos_ += 5; values_[id].kind = Kind::Boolean;
        } else if (text_.substr(pos_, 4) == "null") pos_ += 4;
        else {
            values_[id].kind = Kind::Number;
            if (!number(values_[id].text)) return {};
        }
        return id;
    }
};

inline bool matching_url(std::string_view actual, std::string_view owner,
                         std::string_view repository, std::string_view suffix) {
    const std::string prefix = "https://github.com/" + std::string(owner) + "/" +
                               std::string(repository) + "/";
    if (actual.size() != prefix.size() + suffix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(actual[i]) != lower(prefix[i])) return false;
    }
    return actual.substr(prefix.size()) == suffix;
}

inline std::optional<std::string> sha256_digest(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (!value.starts_with(prefix) || value.size() != prefix.size() + 64) return {};
    std::string digest;
    digest.reserve(64);
    for (char c : value.substr(prefix.size())) {
        if (c >= '0' && c <= '9') digest.push_back(c);
        else if (c >= 'a' && c <= 'f') digest.push_back(c);
        else if (c >= 'A' && c <= 'F') digest.push_back(static_cast<char>(c + ('a' - 'A')));
        else return {};
    }
    return digest;
}

} // namespace detail

enum class Status { Invalid, NoCompatibleRelease, UpToDate, Available };
struct Selection {
    Status status = Status::Invalid;
    std::string version, url, sha256;
    uint64_t bytes = 0;
    bool direct_download = false;
    bool installer = false;
};

inline Selection select(std::string_view json, std::string_view current,
                         std::string_view owner, std::string_view repository) {
    using detail::Kind;
    Selection result;
    const auto installed = parse_version(current);
    if (!installed || !valid_repository(owner, repository)) return result;
    detail::Json document(json);
    if (!document.parse() || document.root().kind != Kind::Array ||
        document.root().elements.size() > kMaxReleases) return result;
    result.status = Status::NoCompatibleRelease;
    std::optional<Version> newest;
    for (const auto id : document.root().elements) {
        const auto& item = document.at(id);
        const auto* tag = document.member(item, "tag_name");
        const auto* draft = document.member(item, "draft");
        const auto* prerelease = document.member(item, "prerelease");
        const auto* page = document.member(item, "html_url");
        if (!tag || tag->kind != Kind::String || !tag->text.starts_with(kTagPrefix) ||
            !draft || draft->kind != Kind::Boolean || draft->boolean ||
            !prerelease || prerelease->kind != Kind::Boolean || prerelease->boolean ||
            !page || page->kind != Kind::String) continue;
        const auto version_text = std::string_view(tag->text).substr(kTagPrefix.size());
        const auto version = parse_version(version_text);
        const std::string page_suffix = "releases/tag/" + tag->text;
        if (!version || !detail::matching_url(page->text, owner, repository, page_suffix) ||
            (newest && !(*newest < *version))) continue;
        newest = version;
        result.version = version_text;
        result.url = "https://github.com/" + std::string(owner) + "/" +
                     std::string(repository) + "/" + page_suffix;
        result.direct_download = false;
        result.installer = false;
        result.sha256.clear();
        result.bytes = 0;
        const auto* assets = document.member(item, "assets");
        if (assets && assets->kind == Kind::Array) {
            for (const auto asset_id : assets->elements) {
                const auto& asset = document.at(asset_id);
                const auto* name = document.member(asset, "name");
                const auto* state = document.member(asset, "state");
                const auto* url = document.member(asset, "browser_download_url");
                const auto* size = document.member(asset, "size");
                const auto* digest = document.member(asset, "digest");
                const auto sha256 = digest && digest->kind == Kind::String
                    ? detail::sha256_digest(digest->text) : std::nullopt;
                if (!name || name->kind != Kind::String ||
                    (name->text != kWindowsInstaller && name->text != kWindowsAsset) ||
                    !state || state->kind != Kind::String || state->text != "uploaded" ||
                    !url || url->kind != Kind::String || !size || size->kind != Kind::Number ||
                    !sha256) continue;
                uint64_t bytes = 0;
                const auto number = std::from_chars(size->text.data(), size->text.data() + size->text.size(), bytes);
                const std::string suffix = "releases/download/" + tag->text + "/" + name->text;
                if (number.ec != std::errc{} || number.ptr != size->text.data() + size->text.size() ||
                    bytes == 0 || !detail::matching_url(url->text, owner, repository, suffix)) continue;
                result.url = "https://github.com/" + std::string(owner) + "/" +
                             std::string(repository) + "/" + suffix;
                result.sha256 = *sha256;
                result.bytes = bytes;
                result.direct_download = true;
                result.installer = name->text == kWindowsInstaller;
                /* Releases may list the portable ZIP first. Keep looking for
                 * the single-file installer, but never trust another name or
                 * URL to supply it. Older ZIP-only releases remain usable. */
                if (result.installer) break;
            }
        }
    }
    if (newest) result.status = *installed < *newest ? Status::Available : Status::UpToDate;
    return result;
}

} // namespace desktop_update::release

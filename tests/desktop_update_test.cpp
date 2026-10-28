/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "desktop_update_release.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace release = desktop_update::release;
namespace {
constexpr std::string_view owner = "Bluesguitartravis-droid", repository = "desktop-melee";
const std::string project = "https://github.com/" + std::string(owner) + "/" + std::string(repository);
unsigned checks;
void check(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
    ++checks;
}
std::string entry(std::string_view version, bool draft = false, bool prerelease = false,
                   bool asset = true, std::string asset_url = {},
                   std::string_view asset_name = release::kWindowsAsset) {
    const std::string tag = "desktop-v" + std::string(version);
    const std::string page = project + "/releases/tag/" + tag;
    if (asset_url.empty()) asset_url = project + "/releases/download/" + tag + "/" + std::string(asset_name);
    std::string value = "{\"tag_name\":\"" + tag + "\",\"draft\":" + (draft ? "true" : "false") +
        ",\"prerelease\":" + (prerelease ? "true" : "false") + ",\"html_url\":\"" + page + "\",\"assets\":[";
    if (asset) value += "{\"name\":\"" + std::string(asset_name) +
        "\",\"state\":\"uploaded\",\"size\":28000000,\"browser_download_url\":\"" + asset_url + "\"}";
    return value + "]}";
}
release::Selection select(std::string_view json, std::string_view current = "0.1.6") {
    return release::select(json, current, owner, repository);
}
void replace_once(std::string& value, std::string_view from, std::string_view to) {
    const auto pos = value.find(from);
    if (pos == std::string::npos) std::abort();
    value.replace(pos, from.size(), to);
}
}

int main() {
    check(release::parse_version("0.1.9").value() < release::parse_version("0.1.10").value(),
          "versions compare numerically, including patch10 > patch9");
    check(release::parse_version("0.99.999").value() < release::parse_version("1.0.0").value(),
          "major/minor components retain numeric precedence");
    for (auto text : {"", "0.1", "1", "1.2.3.4", "1.2.3beta", "1.2.3-rc.1", "1.2.3+build",
                      " 1.2.3", "1.2.3 ", "+1.2.3", "-1.2.3", "01.2.3", "1.02.3", "1.2.03",
                      "4294967296.0.0", "1.2.4294967296", "1..3", "1.2.3/evil"})
        check(!release::parse_version(text), "invalid/partial/prerelease/overflow version rejected");
    check(release::parse_version("4294967295.0.0").has_value(), "largest supported component is valid");

    auto newer = select("[" + entry("0.1.7") + "]");
    check(newer.status == release::Status::Available && newer.version == "0.1.7" && newer.direct_download &&
          !newer.installer &&
          newer.url == project + "/releases/download/desktop-v0.1.7/" + std::string(release::kWindowsAsset),
          "new stable version offers exact Windows ZIP");
    {
        const auto installer_entry = entry("0.1.7", false, false, true, {}, release::kWindowsInstaller);
        const auto installer = select("[" + installer_entry + "]");
        const auto expected = project + "/releases/download/desktop-v0.1.7/" + std::string(release::kWindowsInstaller);
        check(installer.status == release::Status::Available && installer.direct_download &&
              installer.installer && installer.url == expected,
              "single downloadable installer is recognized by exact name and URL");
        const auto portable_entry = entry("0.1.7");
        const auto asset_json = [](const std::string& value) {
            const auto start = value.find("\"assets\":[") + std::string_view("\"assets\":[").size();
            return value.substr(start, value.size() - start - 2);
        };
        auto portable_first = portable_entry;
        replace_once(portable_first, "]}", "," + asset_json(installer_entry) + "]}");
        auto installer_first = installer_entry;
        replace_once(installer_first, "]}", "," + asset_json(portable_entry) + "]}");
        check(select("[" + portable_first + "]").url == expected &&
              select("[" + installer_first + "]").url == expected,
              "installer is preferred regardless of release asset ordering");
        const auto newer_portable = select("[" + installer_entry + "," + entry("0.1.8") + "]");
        check(newer_portable.version == "0.1.8" && newer_portable.direct_download && !newer_portable.installer,
              "installer preference never chooses an older release over a newer portable version");
        const auto bad_installer = entry("0.1.7", false, false, true,
            "https://github.com/other/project/releases/download/desktop-v0.1.7/Desktop-Melee-Setup-x64.exe",
            release::kWindowsInstaller);
        auto fallback = bad_installer;
        replace_once(fallback, "]}", "," + asset_json(portable_entry) + "]}");
        const auto selected_fallback = select("[" + fallback + "]");
        check(selected_fallback.direct_download && !selected_fallback.installer &&
              selected_fallback.url == project + "/releases/download/desktop-v0.1.7/" + std::string(release::kWindowsAsset),
              "invalid installer URL cannot displace a valid portable ZIP");
        const auto no_valid_asset = select("[" + bad_installer + "]");
        check(!no_valid_asset.direct_download && !no_valid_asset.installer &&
              no_valid_asset.url == project + "/releases/tag/desktop-v0.1.7",
              "wrong-repository installer alone falls back to the release page");
        for (const auto& url : {"https://github.com.evil.example/Bluesguitartravis-droid/desktop-melee/releases/download/desktop-v0.1.7/Desktop-Melee-Setup-x64.exe",
                                "http://github.com/Bluesguitartravis-droid/desktop-melee/releases/download/desktop-v0.1.7/Desktop-Melee-Setup-x64.exe",
                                "https://github.com/Bluesguitartravis-droid/desktop-melee/releases/download/desktop-v0.1.7/evil.exe"}) {
            const auto rejected = select("[" + entry("0.1.7", false, false, true, url, release::kWindowsInstaller) + "]");
            check(!rejected.direct_download && !rejected.installer,
                  "installer URLs retain strict HTTPS host and filename validation");
        }
    }
    check(select("[" + entry("0.1.6") + "]").status == release::Status::UpToDate &&
          select("[" + entry("0.1.5") + "]").status == release::Status::UpToDate,
          "equal/older releases do not produce updates");
    auto highest = select("[" + entry("0.1.9") + "," + entry("0.1.10") + "," + entry("0.1.8") + "]");
    check(highest.status == release::Status::Available && highest.version == "0.1.10",
          "release ordering does not override numeric latest version");
    check(select("[" + entry("1.0.0", true) + "," + entry("2.0.0", false, true) + "," +
                 entry("3.0.0-rc.1") + "," + entry("0.1.6") + "]").status == release::Status::UpToDate,
          "drafts and prereleases are excluded");
    check(select("[]").status == release::Status::NoCompatibleRelease,
          "empty public release list is not incorrectly marked up to date");

    auto missing = select("[" + entry("0.1.7", false, false, false) + "]");
    check(missing.status == release::Status::Available && !missing.direct_download &&
          missing.url == project + "/releases/tag/desktop-v0.1.7",
          "missing Windows asset offers only the validated release page");
    for (const auto& url : {"http://github.com/Bluesguitartravis-droid/desktop-melee/releases/download/desktop-v0.1.7/Desktop-Melee-Windows-x64.zip",
                            "https://github.com.evil.example/Bluesguitartravis-droid/desktop-melee/releases/download/desktop-v0.1.7/Desktop-Melee-Windows-x64.zip",
                            "https://github.com@evil.example/a.zip", "file:///C:/evil.exe", "javascript:alert(1)",
                            "https://github.com/999sian/melee-pc/releases/download/desktop-v0.1.7/Desktop-Melee-Windows-x64.zip"}) {
        const auto selection = select("[" + entry("0.1.7", false, false, true, url) + "]");
        check(selection.status == release::Status::Available && !selection.direct_download &&
              selection.url == project + "/releases/tag/desktop-v0.1.7",
              "unexpected URL host/protocol/repository cannot become a download target");
    }
    for (const auto suffix : {"?token=anything", "#fragment", "/../evil.exe", "%00.exe"}) {
        auto value = entry("0.1.7");
        const std::string good = "/" + std::string(release::kWindowsAsset) + "\"";
        replace_once(value, good, "/" + std::string(release::kWindowsAsset) + suffix + "\"");
        check(!select("[" + value + "]").direct_download, "asset path must match exactly without suffixes");
    }
    {
        auto value = entry("0.1.7");
        replace_once(value, "\"name\":\"Desktop-Melee-Windows-x64.zip\"", "\"name\":\"Desktop-Melee-Source.zip\"");
        check(!select("[" + value + "]").direct_download, "source archive is not a Windows download");
        value = entry("0.1.7");
        replace_once(value, "\"state\":\"uploaded\"", "\"state\":\"new\"");
        check(!select("[" + value + "]").direct_download, "incomplete asset cannot be offered");
        value = entry("0.1.7");
        replace_once(value, "\"size\":28000000", "\"size\":0");
        check(!select("[" + value + "]").direct_download, "empty asset cannot be offered");
        value = entry("0.1.7");
        replace_once(value, project + "/releases/tag/", "https://github.com/other/project/releases/tag/");
        check(select("[" + value + "]").status == release::Status::NoCompatibleRelease,
              "release from another repository is ignored entirely");
    }
    {
        auto value = entry("0.1.7");
        replace_once(value, "\"tag_name\":\"desktop-v", "\"tag_name\":\"v");
        check(select("[" + value + "]").status == release::Status::NoCompatibleRelease,
              "upstream/nonproject tag cannot trigger update");
        value = entry("0.1.7");
        replace_once(value, "\"draft\":false", "\"draft\":\"false\"");
        check(select("[" + value + "]").status == release::Status::NoCompatibleRelease,
              "security-relevant release fields must have correct JSON types");
    }
    for (auto text : {"", "{}", "null", "[", "[{}", "[{},]", "[] trailing", "[NaN]", "[01]", "[1.]",
                      "[truefalse]", "[\"\\uD800\"]", "[\"\\uDC00\"]", "[\"\\x00\"]"})
        check(select(text).status == release::Status::Invalid, "malformed JSON must fail closed");
    {
        auto value = entry("0.1.7");
        replace_once(value, "\"draft\":false", "\"draft\":false,\"draft\":true");
        check(select("[" + value + "]").status == release::Status::Invalid, "duplicate metadata keys rejected");
        value = entry("0.1.7");
        replace_once(value, "\"draft\":false", "\"draft\":false,\"\\u0064raft\":true");
        check(select("[" + value + "]").status == release::Status::Invalid, "escaped duplicate keys rejected");
        value = entry("0.1.7");
        value.insert(1, "\"body\":\"Quotes: \\\"tag_name\\\":\\\"desktop-v99.0.0\\\"; \\uD83C\\uDFAE café\",");
        check(select("[" + value + "]").version == "0.1.7", "release-note text and valid Unicode do not impersonate metadata");
        value = entry("0.1.7");
        value.insert(1, std::string("\"body\":\"") + char(0xc0) + char(0xaf) + "\",");
        check(select("[" + value + "]").status == release::Status::Invalid, "overlong UTF8 rejected");
    }
    {
        const auto valid = "[" + entry("0.1.7") + "]";
        bool all_truncated_rejected = true;
        for (size_t length = 0; length < valid.size(); ++length)
            all_truncated_rejected &= select(std::string_view(valid).substr(0, length)).status == release::Status::Invalid;
        check(all_truncated_rejected, "every truncated prefix of a release response is rejected");
        check(select(std::string(release::kMaxResponseBytes + 1, ' ')).status == release::Status::Invalid,
              "response byte cap enforced");
        check(select(std::string(40, '[') + "0" + std::string(40, ']')).status == release::Status::Invalid,
              "JSON nesting bound enforced");
        check(select("[\"" + std::string(128 * 1024 + 1, 'a') + "\"]").status == release::Status::Invalid,
              "individual string bound enforced");
        std::string list = "[";
        for (unsigned i = 0; i < 21; ++i) { if (i) list += ','; list += entry("0.1.7"); }
        check(select(list + "]").status == release::Status::Invalid, "release count bound enforced");
        check(release::select(valid, "broken", owner, repository).status == release::Status::Invalid &&
              release::select(valid, "0.1.6", owner, "../evil").status == release::Status::Invalid,
              "invalid compiled version/repository cannot form URLs");
    }
    std::printf("Desktop update metadata: %u checks passed\n", checks);
}

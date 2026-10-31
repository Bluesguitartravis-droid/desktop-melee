/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace desktop_update {

enum class UpdateStatus { Checking, UpToDate, Available, Unavailable };

struct UpdateResult {
    UpdateStatus status = UpdateStatus::Unavailable;
    std::string version;
    std::string url;
    std::string sha256;
    uint64_t bytes = 0;
    std::string detail = "Updates have not been checked yet.";
    bool direct_download = false;
    bool installer = false;
};

/* One bounded background request at a time. All WinHTTP work stays off the
 * UI thread. poll() copies a small snapshot; callers own how/when to show it
 * and open its validated HTTPS URL. No installer or file download runs here.
 * cancel()/destruction signal cancellation and never join a network thread.
 * In-flight requests retain their own state through WinHTTP HANDLE_CLOSING. */
class UpdateChecker {
public:
    UpdateChecker();
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;
    bool start(); // false if already checking or a worker could not be started
    UpdateResult poll() const;
    void cancel() noexcept;

private:
    struct Shared;
    static void run(std::shared_ptr<Shared> shared) noexcept;
    std::shared_ptr<Shared> shared_;
};


enum class InstallStatus { Idle, Downloading, Ready, Failed };

struct InstallResult {
    InstallStatus status = InstallStatus::Idle;
    std::filesystem::path installer_path;
    std::string detail = "No update is being installed.";
};

class UpdateInstaller {
public:
    UpdateInstaller();
    ~UpdateInstaller();
    UpdateInstaller(const UpdateInstaller&) = delete;
    UpdateInstaller& operator=(const UpdateInstaller&) = delete;
    bool start(const UpdateResult& update);
    InstallResult poll() const;
    void cancel() noexcept;

private:
    struct Shared;
    static void run(std::shared_ptr<Shared> shared, UpdateResult update) noexcept;
    std::shared_ptr<Shared> shared_;
};

} // namespace desktop_update

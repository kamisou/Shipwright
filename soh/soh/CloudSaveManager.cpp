#include "CloudSaveManager.h"

#include "cvar_prefixes.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace {
constexpr const char* CVAR_CLOUD_ENABLED = CVAR_SETTING("CloudSaves.Enabled");
constexpr const char* CVAR_CLOUD_PATH = CVAR_SETTING("CloudSaves.GoogleDrivePath");
constexpr const char* CVAR_CLOUD_AUTO_SYNC = CVAR_SETTING("CloudSaves.AutoSync");
constexpr std::array<const char*, 4> kSaveNames = { "global.sav", "file1.sav", "file2.sav", "file3.sav" };

std::filesystem::path LocalSaveDirectory() {
    return Ship::Context::GetPathRelativeToAppDirectory("Save");
}
} // namespace

CloudSaveManager& CloudSaveManager::Instance() {
    static CloudSaveManager instance;
    return instance;
}

bool CloudSaveManager::IsConfigured() const {
    return CVarGetInteger(CVAR_CLOUD_ENABLED, 0) != 0 && !std::string(CVarGetString(CVAR_CLOUD_PATH, "")).empty();
}

std::filesystem::path CloudSaveManager::GetCloudDirectory() const {
    const std::string root = CVarGetString(CVAR_CLOUD_PATH, "");
    return root.empty() ? std::filesystem::path() : std::filesystem::path(root) / "Ship of Harkinian" / "Save";
}

void CloudSaveManager::SetStatus(Status status, std::string detail) {
    mStatus = status;
    mDetail = std::move(detail);
}

CloudSaveManager::Status CloudSaveManager::GetStatus() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mStatus;
}

std::string CloudSaveManager::GetStatusText() const {
    std::lock_guard<std::mutex> lock(mMutex);
    switch (mStatus) {
        case Status::Disabled:
            return "Cloud saves are disabled";
        case Status::Ready:
            return "Google Drive folder is ready";
        case Status::Syncing:
            return "Syncing saves...";
        case Status::Synced:
            return mDetail.empty() ? "Saves are up to date" : mDetail;
        case Status::Conflict:
            return mDetail.empty() ? "A save conflict needs attention" : mDetail;
        case Status::Error:
            return mDetail.empty() ? "Cloud save sync failed" : mDetail;
    }
    return "Cloud save status unavailable";
}

bool CloudSaveManager::IsSaveFile(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    for (const char* allowed : kSaveNames) {
        if (name == allowed) {
            return true;
        }
    }
    return false;
}

std::string CloudSaveManager::HashFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    // FNV-1a is used only for change detection, not for security.
    uint64_t hash = 14695981039346656037ULL;
    std::array<char, 8192> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<size_t>(i)]);
            hash *= 1099511628211ULL;
        }
    }
    std::ostringstream result;
    result << std::hex << std::setfill('0') << std::setw(16) << hash;
    return result.str();
}

bool CloudSaveManager::AtomicCopy(const std::filesystem::path& source, const std::filesystem::path& destination) {
    try {
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::path temporary = destination;
        temporary += ".syncing";
        std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing);
        if (std::filesystem::exists(destination)) {
            std::filesystem::remove(destination);
        }
        std::filesystem::rename(temporary, destination);
        return true;
    } catch (const std::filesystem::filesystem_error& error) {
        SPDLOG_ERROR("Cloud save copy from {} to {} failed: {}", source.string(), destination.string(), error.what());
        return false;
    }
}

std::filesystem::path CloudSaveManager::ConflictPath(const std::filesystem::path& source,
                                                     const std::filesystem::path& destinationDirectory) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return destinationDirectory /
           (source.stem().string() + "-conflict-" + std::to_string(seconds) + source.extension().string() + ".bak");
}

void CloudSaveManager::SyncOne(const std::filesystem::path& localFile, const std::filesystem::path& cloudFile,
                               Direction direction) {
    const bool hasLocal = std::filesystem::exists(localFile);
    const bool hasCloud = std::filesystem::exists(cloudFile);
    if (!hasLocal && !hasCloud) {
        return;
    }

    if (hasLocal && hasCloud && HashFile(localFile) == HashFile(cloudFile)) {
        return;
    }

    if (direction == Direction::Upload || !hasCloud) {
        if (hasLocal) {
            if (hasCloud && !AtomicCopy(cloudFile, ConflictPath(cloudFile, cloudFile.parent_path()))) {
                throw std::runtime_error("Could not preserve " + cloudFile.filename().string());
            }
            if (!AtomicCopy(localFile, cloudFile)) {
                throw std::runtime_error("Could not upload " + localFile.filename().string());
            }
        }
        return;
    }
    if (direction == Direction::Download || !hasLocal) {
        if (hasCloud) {
            if (hasLocal && !AtomicCopy(localFile, ConflictPath(localFile, localFile.parent_path()))) {
                throw std::runtime_error("Could not preserve " + localFile.filename().string());
            }
            if (!AtomicCopy(cloudFile, localFile)) {
                throw std::runtime_error("Could not download " + cloudFile.filename().string());
            }
        }
        return;
    }

    const auto localTime = std::filesystem::last_write_time(localFile);
    const auto cloudTime = std::filesystem::last_write_time(cloudFile);
    if (cloudTime > localTime) {
        const auto backup = ConflictPath(localFile, localFile.parent_path());
        if (!AtomicCopy(localFile, backup) || !AtomicCopy(cloudFile, localFile)) {
            throw std::runtime_error("Could not preserve and download " + cloudFile.filename().string());
        }
        SetStatus(Status::Conflict, "Downloaded newer cloud save; local copy kept as " + backup.filename().string());
    } else {
        const auto backup = ConflictPath(cloudFile, cloudFile.parent_path());
        if (!AtomicCopy(cloudFile, backup) || !AtomicCopy(localFile, cloudFile)) {
            throw std::runtime_error("Could not preserve and upload " + localFile.filename().string());
        }
        SetStatus(Status::Conflict, "Uploaded newer local save; cloud copy kept as " + backup.filename().string());
    }
}

void CloudSaveManager::Sync(Direction direction) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!IsConfigured()) {
        SetStatus(Status::Disabled);
        return;
    }

    const auto cloudDirectory = GetCloudDirectory();
    try {
        SetStatus(Status::Syncing);
        const auto configuredRoot = cloudDirectory.parent_path().parent_path();
        if (!std::filesystem::is_directory(configuredRoot)) {
            throw std::runtime_error("The configured Google Drive folder does not exist");
        }
        std::filesystem::create_directories(LocalSaveDirectory());
        std::filesystem::create_directories(cloudDirectory);
        for (const char* name : kSaveNames) {
            SyncOne(LocalSaveDirectory() / name, cloudDirectory / name, direction);
        }
        if (mStatus != Status::Conflict) {
            SetStatus(Status::Synced);
        }
    } catch (const std::exception& error) {
        SPDLOG_ERROR("Cloud save sync failed: {}", error.what());
        SetStatus(Status::Error, error.what());
    }
}

void CloudSaveManager::SyncOnStartup() {
    if (CVarGetInteger(CVAR_CLOUD_AUTO_SYNC, 1) != 0) {
        Sync(Direction::Reconcile);
    } else {
        std::lock_guard<std::mutex> lock(mMutex);
        SetStatus(IsConfigured() ? Status::Ready : Status::Disabled);
    }
}

void CloudSaveManager::SyncNow() {
    Sync(Direction::Reconcile);
}

void CloudSaveManager::UploadAll() {
    Sync(Direction::Upload);
}

void CloudSaveManager::DownloadAll() {
    Sync(Direction::Download);
}

void CloudSaveManager::UploadFile(const std::filesystem::path& localFile) {
    if (!IsConfigured() || CVarGetInteger(CVAR_CLOUD_AUTO_SYNC, 1) == 0 || !IsSaveFile(localFile)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    SetStatus(Status::Syncing);
    const auto cloudFile = GetCloudDirectory() / localFile.filename();
    if (AtomicCopy(localFile, cloudFile)) {
        SetStatus(Status::Synced, localFile.filename().string() + " uploaded to Google Drive");
    } else {
        SetStatus(Status::Error, "Could not upload " + localFile.filename().string());
    }
}

void CloudSaveManager::RemoveFile(const std::filesystem::path& localFile) {
    if (!IsConfigured() || CVarGetInteger(CVAR_CLOUD_AUTO_SYNC, 1) == 0 || !IsSaveFile(localFile)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    try {
        const auto cloudFile = GetCloudDirectory() / localFile.filename();
        if (std::filesystem::exists(cloudFile)) {
            std::filesystem::remove(cloudFile);
        }
        SetStatus(Status::Synced, localFile.filename().string() + " removed from Google Drive");
    } catch (const std::filesystem::filesystem_error& error) {
        SPDLOG_ERROR("Cloud save delete failed for {}: {}", localFile.string(), error.what());
        SetStatus(Status::Error, "Could not remove " + localFile.filename().string() + " from Google Drive");
    }
}

#pragma once

#include <filesystem>
#include <mutex>
#include <string>

class CloudSaveManager {
  public:
    enum class Status {
        Disabled,
        Ready,
        Syncing,
        Synced,
        Conflict,
        Error,
    };

    static CloudSaveManager& Instance();

    // Pulls saves from the configured Google Drive folder before save metadata is loaded.
    void SyncOnStartup();
    // Publishes one file after SaveManager has completed its atomic local write.
    void UploadFile(const std::filesystem::path& localFile);
    void RemoveFile(const std::filesystem::path& localFile);
    void SyncNow();
    void UploadAll();
    void DownloadAll();

    bool IsConfigured() const;
    Status GetStatus() const;
    std::string GetStatusText() const;
    std::filesystem::path GetCloudDirectory() const;

  private:
    CloudSaveManager() = default;

    enum class Direction { Reconcile, Upload, Download };

    void Sync(Direction direction);
    void SyncOne(const std::filesystem::path& localFile, const std::filesystem::path& cloudFile, Direction direction);
    void SetStatus(Status status, std::string detail = {});
    static bool IsSaveFile(const std::filesystem::path& path);
    static std::string HashFile(const std::filesystem::path& path);
    static bool AtomicCopy(const std::filesystem::path& source, const std::filesystem::path& destination);
    static std::filesystem::path ConflictPath(const std::filesystem::path& source,
                                              const std::filesystem::path& destinationDirectory);

    mutable std::mutex mMutex;
    Status mStatus = Status::Disabled;
    std::string mDetail;
};

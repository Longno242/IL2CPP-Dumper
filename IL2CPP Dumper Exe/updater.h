#pragma once
#include <functional>
#include <string>

struct UpdateInfo {
    bool ok = false;
    bool update_available = false;
    std::string current_version;
    std::string latest_tag;
    std::string html_url;
    std::string asset_name;
    std::string asset_url;
    std::string error;
};

UpdateInfo CheckForUpdate();
std::string DownloadUpdateAsset(const std::string& url, const std::string& dest_path,
                                std::function<void(int percent)> progress = nullptr);
bool LaunchSelfReplace(const std::string& new_exe_path, std::string& error_out);

#define _CRT_SECURE_NO_WARNINGS
#include "updater.h"
#include "version.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string JsonStringField(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return {};
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return {};
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) ++p;
    if (p >= json.size() || json[p] != '"') return {};
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
            if (json[p] == 'u') p += 4;
            else out.push_back(json[p]);
        } else {
            out.push_back(json[p]);
        }
        ++p;
    }
    return out;
}

int CompareVersion(std::string a, std::string b) {
    auto strip = [](std::string& s) {
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    };
    strip(a);
    strip(b);

    auto parts = [](const std::string& s) {
        std::vector<int> v;
        size_t i = 0;
        while (i < s.size()) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) { ++i; continue; }
            int n = 0;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                n = n * 10 + (s[i] - '0');
                ++i;
            }
            v.push_back(n);
            if (i < s.size() && s[i] == '.') ++i;
        }
        return v;
    };

    auto pa = parts(a), pb = parts(b);
    const size_t n = (std::max)(pa.size(), pb.size());
    pa.resize(n, 0);
    pb.resize(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return 1;
    }
    return 0;
}

bool HttpGet(const std::wstring& host, INTERNET_PORT port, const std::wstring& path,
             bool https, std::string& body, std::string& err,
             std::function<void(int)> progress = nullptr) {
    HINTERNET session = WinHttpOpen(L"IL2CPP-Dumper/" DUMPER_VERSION_W,
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { err = "WinHttpOpen failed"; return false; }

    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        err = "WinHttpConnect failed";
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        err = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    std::wstring headers = L"User-Agent: IL2CPP-Dumper/" DUMPER_VERSION_W L"\r\n"
                           L"Accept: application/vnd.github+json\r\n";

    bool ok = false;
    do {
        if (!WinHttpSendRequest(request, headers.c_str(), (DWORD)-1,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            err = "WinHttpSendRequest failed";
            break;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            err = "WinHttpReceiveResponse failed";
            break;
        }

        DWORD status = 0, statusSize = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if (status < 200 || status >= 300) {
            err = "HTTP status " + std::to_string(status);
            break;
        }

        DWORD totalKnown = 0, totalSize = sizeof(totalKnown);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &totalKnown, &totalSize, WINHTTP_NO_HEADER_INDEX);

        body.clear();
        DWORD downloaded = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(request, buf.data(), avail, &read) || read == 0) break;
            body.append(buf.data(), read);
            downloaded += read;
            if (progress && totalKnown > 0)
                progress(static_cast<int>((downloaded * 100ull) / totalKnown));
        }
        ok = true;
    } while (false);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

bool HttpGetUrl(const std::string& url, std::string& body, std::string& err,
                std::function<void(int)> progress = nullptr) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{}, extra[1024]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1023;

    std::wstring wurl = Utf8ToWide(url);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        err = "invalid URL";
        return false;
    }
    return HttpGet(host, uc.nPort, std::wstring(path) + extra,
                   uc.nScheme == INTERNET_SCHEME_HTTPS, body, err, progress);
}

void PickStaticAsset(const std::string& json, std::string& nameOut, std::string& urlOut) {
    nameOut.clear();
    urlOut.clear();
    size_t pos = 0;
    int bestScore = -1;
    while (true) {
        size_t a = json.find("\"browser_download_url\"", pos);
        if (a == std::string::npos) break;
        size_t objStart = json.rfind('{', a);
        size_t objEnd = json.find('}', a);
        if (objStart == std::string::npos || objEnd == std::string::npos) {
            pos = a + 1;
            continue;
        }

        std::string chunk = json.substr(objStart, objEnd - objStart + 1);
        std::string name = JsonStringField(chunk, "name");
        std::string url = JsonStringField(chunk, "browser_download_url");
        if (name.empty() || url.empty()) {
            pos = a + 1;
            continue;
        }

        std::string lower = name;
        for (char& c : lower) c = (char)std::tolower((unsigned char)c);

        int score = 0;
        if (lower.find("static") != std::string::npos) score += 5;
        if (lower.find("dumper") != std::string::npos) score += 3;
        if (lower.find(".zip") != std::string::npos) score += 2;
        if (lower.find("exe") != std::string::npos) score += 2;
        if (lower.find("runtime") != std::string::npos) score -= 4;
        if (lower.find("dll") != std::string::npos && lower.find("exe") == std::string::npos) score -= 3;

        if (score > bestScore) {
            bestScore = score;
            nameOut = name;
            urlOut = url;
        }
        pos = a + 1;
    }
}

} // namespace

UpdateInfo CheckForUpdate() {
    UpdateInfo info;
    info.current_version = DUMPER_VERSION;

    std::string body, err;
    if (!HttpGetUrl(DUMPER_API_LATEST, body, err)) {
        info.error = err.empty() ? "network error" : err;
        return info;
    }

    info.ok = true;
    info.latest_tag = JsonStringField(body, "tag_name");
    info.html_url = JsonStringField(body, "html_url");
    if (info.html_url.empty()) info.html_url = DUMPER_RELEASES_URL;
    PickStaticAsset(body, info.asset_name, info.asset_url);

    if (info.latest_tag.empty()) {
        info.error = "could not parse latest release tag";
        info.ok = false;
        return info;
    }

    info.update_available = CompareVersion(info.current_version, info.latest_tag) < 0;
    return info;
}

std::string DownloadUpdateAsset(const std::string& url, const std::string& dest_path,
                                std::function<void(int percent)> progress) {
    std::string body, err;
    if (!HttpGetUrl(url, body, err, progress))
        return err.empty() ? "download failed" : err;

    std::ofstream out(dest_path, std::ios::binary);
    if (!out) return "cannot write " + dest_path;
    out.write(body.data(), (std::streamsize)body.size());
    return out ? std::string() : "write failed";
}

bool LaunchSelfReplace(const std::string& new_exe_path, std::string& error_out) {
    namespace fs = std::filesystem;
    wchar_t selfPath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, selfPath, MAX_PATH)) {
        error_out = "GetModuleFileName failed";
        return false;
    }

    const fs::path self = selfPath;
    const fs::path bat = self.parent_path() / "dumper_update.bat";
    const fs::path neu = fs::absolute(new_exe_path);

    std::ofstream out(bat);
    if (!out) {
        error_out = "cannot write updater script";
        return false;
    }
    out << "@echo off\r\n"
        << "timeout /t 2 /nobreak >nul\r\n"
        << "copy /y \"" << neu.string() << "\" \"" << self.string() << "\" >nul\r\n"
        << "start \"\" \"" << self.string() << "\"\r\n"
        << "del \"%~f0\"\r\n";
    out.close();

    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"cmd.exe /c \"" + bat.wstring() + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, self.parent_path().c_str(), &si, &pi)) {
        error_out = "failed to start updater script";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "gui.h"
#include "dumper.h"
#include "dump_config.h"
#include "updater.h"
#include "version.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace {

constexpr COLORREF COL_BG = RGB(18, 18, 18);
constexpr COLORREF COL_PANEL = RGB(28, 28, 28);
constexpr COLORREF COL_PANEL2 = RGB(34, 34, 34);
constexpr COLORREF COL_BORDER = RGB(55, 55, 55);
constexpr COLORREF COL_BORDER_H = RGB(90, 90, 90);
constexpr COLORREF COL_TEXT = RGB(210, 210, 210);
constexpr COLORREF COL_MUTED = RGB(130, 130, 130);
constexpr COLORREF COL_BTN = RGB(42, 42, 42);
constexpr COLORREF COL_BTN_H = RGB(58, 58, 58);
constexpr COLORREF COL_BTN_D = RGB(32, 32, 32);

enum : int {
    ID_DROP_ASM = 1001,
    ID_DROP_META,
    ID_BTN_DUMP,
    ID_BTN_BROWSE_ASM,
    ID_BTN_BROWSE_META,
    ID_BTN_UPDATE,
    ID_BTN_OPEN_OUT,
    ID_LOG,
};

enum : UINT {
    MSG_FLUSH_LOG = WM_APP + 1,
    MSG_REFRESH = WM_APP + 2,
    MSG_OFFER_UPDATE = WM_APP + 3,
};

struct Ui {
    HWND hwnd = nullptr;
    HWND logEdit = nullptr;
    HFONT fontUi = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontMono = nullptr;
    HBRUSH brPanel = nullptr;

    std::wstring assembly;
    std::wstring metadata;
    std::wstring output;

    RECT rcAsm{}, rcMeta{}, rcDump{}, rcUpdate{}, rcOpenOut{};
    RECT rcBrowseAsm{}, rcBrowseMeta{};

    int hover = 0;
    std::atomic<bool> dumping{ false };
    std::atomic<bool> updateBusy{ false };

    std::mutex logMu;
    std::wstring logPending;
    UpdateInfo lastUpdate{};
};

Ui g;

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring DesktopGameDumpW() {
    wchar_t desktop[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop))) {
        return std::wstring(desktop) + L"\\GameDump";
    }
    return L"C:\\GameDump";
}

bool IsMetadataFileW(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), 4);
    return magic == 0xFAB11BAFu;
}

bool FindNamedDll(const fs::path& folder, int depth, fs::path& assembly) {
    if (depth < 0) return false;
    for (const wchar_t* name : { L"GameAssembly.dll", L"UserAssembly.dll" }) {
        const fs::path p = folder / name;
        if (fs::exists(p) && fs::is_regular_file(p)) {
            assembly = p;
            return true;
        }
    }
    if (depth == 0) return false;
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!ent.is_directory(ec)) continue;
        auto name = ent.path().filename().wstring();
        if (name == L"." || name == L".." || name == L"MonoBleedingEdge") continue;
        if (FindNamedDll(ent.path(), depth - 1, assembly)) return true;
    }
    return false;
}

bool FindMetadataNear(const fs::path& folder, fs::path& metadata) {
    const fs::path candidates[] = {
        folder / L"global-metadata.dat",
        folder / L"il2cpp_data" / L"Metadata" / L"global-metadata.dat",
        folder / L"Data" / L"il2cpp_data" / L"Metadata" / L"global-metadata.dat",
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) { metadata = c; return true; }
    }
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!ent.is_directory(ec)) continue;
        auto name = ent.path().filename().wstring();
        if (name.size() > 5 && name.substr(name.size() - 5) == L"_Data") {
            auto m = ent.path() / L"il2cpp_data" / L"Metadata" / L"global-metadata.dat";
            if (fs::exists(m)) { metadata = m; return true; }
        }
    }
    return false;
}

bool FindInFolder(const fs::path& folder, fs::path& assembly, fs::path& metadata) {
    FindNamedDll(folder, 3, assembly);
    FindMetadataNear(folder, metadata);
    if (!assembly.empty() && metadata.empty()) {
        FindMetadataNear(assembly.parent_path(), metadata);
        if (metadata.empty() && assembly.parent_path().has_parent_path())
            FindMetadataNear(assembly.parent_path().parent_path(), metadata);
    }
    return !assembly.empty() && !metadata.empty();
}

void AppendLog(const std::wstring& line) {
    std::lock_guard lock(g.logMu);
    if (!g.logPending.empty() && g.logPending.back() != L'\n') g.logPending.push_back(L'\n');
    g.logPending += line;
    if (!line.empty() && line.back() != L'\n') g.logPending.push_back(L'\n');
    if (g.hwnd) PostMessageW(g.hwnd, MSG_FLUSH_LOG, 0, 0);
}

void AppendLogUtf8(const std::string& line) {
    AppendLog(Utf8ToWide(line));
}

void FlushLogToEdit() {
    std::wstring chunk;
    {
        std::lock_guard lock(g.logMu);
        chunk.swap(g.logPending);
    }
    if (chunk.empty() || !g.logEdit) return;
    int len = GetWindowTextLengthW(g.logEdit);
    SendMessageW(g.logEdit, EM_SETSEL, len, len);
    SendMessageW(g.logEdit, EM_REPLACESEL, FALSE, (LPARAM)chunk.c_str());
}

void Layout(HWND hwnd) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int margin = 20;
    const int gap = 12;
    const int dropH = 88;
    const int btnH = 40;
    const int top = 56;

    g.rcAsm = { margin, top, w - margin, top + dropH };
    g.rcMeta = { margin, top + dropH + gap, w - margin, top + dropH * 2 + gap };

    const int browseW = 72;
    g.rcBrowseAsm = { g.rcAsm.right - browseW - 10, g.rcAsm.bottom - 30, g.rcAsm.right - 10, g.rcAsm.bottom - 10 };
    g.rcBrowseMeta = { g.rcMeta.right - browseW - 10, g.rcMeta.bottom - 30, g.rcMeta.right - 10, g.rcMeta.bottom - 10 };

    const int rowY = g.rcMeta.bottom + gap + 4;
    const int dumpW = 160;
    g.rcDump = { margin, rowY, margin + dumpW, rowY + btnH };
    g.rcUpdate = { margin + dumpW + gap, rowY, margin + dumpW + gap + 140, rowY + btnH };
    g.rcOpenOut = { margin + dumpW + gap + 140 + gap, rowY, margin + dumpW + gap + 140 + gap + 120, rowY + btnH };

    const int logTop = rowY + btnH + gap + 8;
    if (g.logEdit) {
        MoveWindow(g.logEdit, margin, logTop, w - margin * 2, rc.bottom - logTop - margin, TRUE);
    }
}

void FillRectColor(HDC hdc, const RECT& r, COLORREF c) {
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, &r, br);
    DeleteObject(br);
}

void FrameRectColor(HDC hdc, const RECT& r, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(hdc, pen);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

void DrawTextIn(HDC hdc, const RECT& r, const wchar_t* text, COLORREF color, HFONT font, UINT fmt) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HGDIOBJ old = SelectObject(hdc, font);
    RECT rr = r;
    DrawTextW(hdc, text, -1, &rr, fmt);
    SelectObject(hdc, old);
}

bool PtIn(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

int HitTest(int x, int y) {
    if (PtIn(g.rcDump, x, y)) return ID_BTN_DUMP;
    if (PtIn(g.rcUpdate, x, y)) return ID_BTN_UPDATE;
    if (PtIn(g.rcOpenOut, x, y)) return ID_BTN_OPEN_OUT;
    if (PtIn(g.rcBrowseAsm, x, y)) return ID_BTN_BROWSE_ASM;
    if (PtIn(g.rcBrowseMeta, x, y)) return ID_BTN_BROWSE_META;
    if (PtIn(g.rcAsm, x, y)) return ID_DROP_ASM;
    if (PtIn(g.rcMeta, x, y)) return ID_DROP_META;
    return 0;
}

void DrawButton(HDC hdc, const RECT& r, const wchar_t* label, bool hover, bool enabled) {
    COLORREF fill = !enabled ? COL_BTN_D : (hover ? COL_BTN_H : COL_BTN);
    FillRectColor(hdc, r, fill);
    FrameRectColor(hdc, r, hover && enabled ? COL_BORDER_H : COL_BORDER);
    DrawTextIn(hdc, r, label, enabled ? COL_TEXT : COL_MUTED, g.fontUi,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawDropZone(HDC hdc, const RECT& r, const wchar_t* title, const std::wstring& path, bool hover) {
    FillRectColor(hdc, r, hover ? COL_PANEL2 : COL_PANEL);
    FrameRectColor(hdc, r, hover ? COL_BORDER_H : COL_BORDER);

    RECT titleRc = r;
    titleRc.left += 14;
    titleRc.top += 12;
    titleRc.right -= 90;
    titleRc.bottom = titleRc.top + 20;
    DrawTextIn(hdc, titleRc, title, COL_MUTED, g.fontUi, DT_LEFT | DT_SINGLELINE);

    RECT pathRc = r;
    pathRc.left += 14;
    pathRc.top += 36;
    pathRc.right -= 90;
    pathRc.bottom -= 12;
    const wchar_t* shown = path.empty() ? L"Drop file or folder here" : path.c_str();
    DrawTextIn(hdc, pathRc, shown, path.empty() ? COL_MUTED : COL_TEXT, g.fontUi,
               DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    FillRectColor(mem, rc, COL_BG);

    RECT title = { 20, 16, rc.right - 20, 44 };
    DrawTextIn(mem, title, L"IL2CPP Dumper", COL_TEXT, g.fontTitle, DT_LEFT | DT_SINGLELINE);
    RECT ver = { 20, 16, rc.right - 20, 44 };
    std::wstring verText = L"v" DUMPER_VERSION_W;
    DrawTextIn(mem, ver, verText.c_str(), COL_MUTED, g.fontUi, DT_RIGHT | DT_SINGLELINE);

    DrawDropZone(mem, g.rcAsm, L"ASSEMBLY  ·  GameAssembly.dll / UserAssembly.dll / game folder",
                 g.assembly, g.hover == ID_DROP_ASM);
    DrawDropZone(mem, g.rcMeta, L"METADATA  ·  global-metadata.dat",
                 g.metadata, g.hover == ID_DROP_META);

    DrawButton(mem, g.rcBrowseAsm, L"Browse", g.hover == ID_BTN_BROWSE_ASM, !g.dumping);
    DrawButton(mem, g.rcBrowseMeta, L"Browse", g.hover == ID_BTN_BROWSE_META, !g.dumping);

    DrawButton(mem, g.rcDump, g.dumping ? L"Dumping…" : L"Start Dump",
               g.hover == ID_BTN_DUMP, !g.dumping && !g.assembly.empty() && !g.metadata.empty());
    DrawButton(mem, g.rcUpdate, g.updateBusy ? L"Checking…" : L"Check Update",
               g.hover == ID_BTN_UPDATE, !g.updateBusy);
    DrawButton(mem, g.rcOpenOut, L"Open Output", g.hover == ID_BTN_OPEN_OUT, true);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

std::wstring OpenFileDialog(HWND owner, bool assemblySlot) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))) || !dlg) {
        return {};
    }

    DWORD options = 0;
    dlg->GetOptions(&options);
    dlg->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);

    if (assemblySlot) {
        dlg->SetTitle(L"Select GameAssembly.dll or UserAssembly.dll");
        COMDLG_FILTERSPEC filters[] = {
            { L"DLL files", L"*.dll" },
            { L"GameAssembly", L"GameAssembly.dll;UserAssembly.dll" },
            { L"All files", L"*.*" },
        };
        dlg->SetFileTypes(ARRAYSIZE(filters), filters);
        dlg->SetFileTypeIndex(1);
        dlg->SetDefaultExtension(L"dll");
    } else {
        dlg->SetTitle(L"Select global-metadata.dat");
        COMDLG_FILTERSPEC filters[] = {
            { L"DAT files", L"*.dat" },
            { L"Metadata", L"global-metadata.dat" },
            { L"All files", L"*.*" },
        };
        dlg->SetFileTypes(ARRAYSIZE(filters), filters);
        dlg->SetFileTypeIndex(1);
        dlg->SetDefaultExtension(L"dat");
    }

    std::wstring result;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

std::wstring OpenFolderDialog(HWND owner) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))) || !dlg) {
        return {};
    }

    DWORD options = 0;
    dlg->GetOptions(&options);
    dlg->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Select game folder");

    std::wstring result;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void AssignDroppedPath(const fs::path& path, bool preferAsmSlot) {
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        fs::path a, m;
        if (FindInFolder(path, a, m)) {
            if (!a.empty()) g.assembly = a.wstring();
            if (!m.empty()) g.metadata = m.wstring();
            AppendLog(L"[+] resolved from folder: " + path.wstring());
        } else {
            AppendLog(L"[!] could not find assembly/metadata in folder");
        }
        return;
    }
    if (!fs::is_regular_file(path, ec)) return;

    auto name = path.filename().wstring();
    for (auto& c : name) c = (wchar_t)towlower(c);

    if (IsMetadataFileW(path) || name == L"global-metadata.dat") {
        g.metadata = path.wstring();
        AppendLog(L"[+] metadata: " + path.wstring());
        return;
    }
    bool looksDll = false;
    if (name.size() >= 4) {
        auto ext = name.substr(name.size() - 4);
        looksDll = (ext == L".dll");
    }
    if (name == L"gameassembly.dll" || name == L"userassembly.dll" || looksDll) {
        g.assembly = path.wstring();
        AppendLog(L"[+] assembly: " + path.wstring());
        return;
    }
    if (preferAsmSlot || g.assembly.empty()) {
        g.assembly = path.wstring();
        AppendLog(L"[+] assembly: " + path.wstring());
    } else {
        g.metadata = path.wstring();
        AppendLog(L"[+] metadata: " + path.wstring());
    }
}

void StartDump() {
    if (g.dumping) return;
    if (g.assembly.empty() || g.metadata.empty()) {
        AppendLog(L"[!] drop both GameAssembly.dll and global-metadata.dat first");
        return;
    }
    g.dumping = true;
    InvalidateRect(g.hwnd, nullptr, FALSE);

    std::wstring asmPath = g.assembly;
    std::wstring metaPath = g.metadata;
    std::wstring outPath = g.output.empty() ? DesktopGameDumpW() : g.output;
    g.output = outPath;

    std::thread([asmPath, metaPath, outPath]() {
        AppendLog(L"[*] assembly : " + asmPath);
        AppendLog(L"[*] metadata : " + metaPath);
        AppendLog(L"[*] output   : " + outPath);

        DumpConfig cfg;
        const bool ok = GameDumper::DumpFromFiles(
            WideToUtf8(asmPath), WideToUtf8(metaPath), WideToUtf8(outPath), cfg,
            [](const std::string& msg) { AppendLogUtf8(msg); });

        if (ok) AppendLog(L"[+] done");
        else AppendLog(L"[!] dump failed");

        g.dumping = false;
        if (g.hwnd) PostMessageW(g.hwnd, MSG_REFRESH, 0, 0);
    }).detach();
}

void StartUpdateCheck(bool offerDownload) {
    if (g.updateBusy) return;
    g.updateBusy = true;
    InvalidateRect(g.hwnd, nullptr, FALSE);
    AppendLog(L"[*] checking GitHub releases…");

    std::thread([offerDownload]() {
        UpdateInfo info = CheckForUpdate();
        g.lastUpdate = info;
        g.updateBusy = false;

        if (!info.ok) {
            AppendLog(L"[!] update check failed: " + Utf8ToWide(info.error));
        } else if (!info.update_available) {
            AppendLog(L"[+] up to date (v" + Utf8ToWide(info.current_version) +
                      L", latest " + Utf8ToWide(info.latest_tag) + L")");
        } else {
            AppendLog(L"[*] update available: " + Utf8ToWide(info.latest_tag) +
                      L"  (you have v" + Utf8ToWide(info.current_version) + L")");
            if (offerDownload && g.hwnd) PostMessageW(g.hwnd, MSG_OFFER_UPDATE, 0, 0);
        }
        if (g.hwnd) PostMessageW(g.hwnd, MSG_REFRESH, 0, 0);
    }).detach();
}

void OfferUpdateDialog() {
    const UpdateInfo& info = g.lastUpdate;
    if (!info.update_available) return;

    std::wstring msg = L"A newer release is available.\n\n"
                       L"Current:  v" + Utf8ToWide(info.current_version) + L"\n"
                       L"Latest:   " + Utf8ToWide(info.latest_tag) + L"\n\n";
    if (!info.asset_name.empty()) {
        msg += L"Asset: " + Utf8ToWide(info.asset_name) + L"\n\n";
    }
    msg += L"Download and prepare update?\n"
           L"(The app will close and restart after replace.)\n\n"
           L"Press No to open the releases page instead.";

    int r = MessageBoxW(g.hwnd, msg.c_str(), L"Update available", MB_YESNOCANCEL | MB_ICONINFORMATION);
    if (r == IDCANCEL) return;
    if (r == IDNO) {
        ShellExecuteW(g.hwnd, L"open", Utf8ToWide(info.html_url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (info.asset_url.empty()) {
        ShellExecuteW(g.hwnd, L"open", Utf8ToWide(info.html_url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        AppendLog(L"[!] no downloadable asset found — opened releases page");
        return;
    }

    g.updateBusy = true;
    InvalidateRect(g.hwnd, nullptr, FALSE);
    AppendLog(L"[*] downloading " + Utf8ToWide(info.asset_name) + L"…");

    std::string url = info.asset_url;
    std::string assetName = info.asset_name;
    std::thread([url, assetName]() {
        wchar_t tempDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempDir);
        fs::path zipPath = fs::path(tempDir) / assetName;
        auto err = DownloadUpdateAsset(url, zipPath.string(), [](int pct) {
            static int last = -1;
            if (pct != last && pct % 10 == 0) {
                last = pct;
                AppendLog(L"[*] download " + std::to_wstring(pct) + L"%");
            }
        });
        if (!err.empty()) {
            AppendLog(L"[!] download failed: " + Utf8ToWide(err));
            g.updateBusy = false;
            if (g.hwnd) PostMessageW(g.hwnd, MSG_REFRESH, 0, 0);
            return;
        }
        AppendLog(L"[+] downloaded to " + zipPath.wstring());

        fs::path extractDir = fs::path(tempDir) / L"il2cpp-dumper-update";
        std::error_code ec;
        fs::create_directories(extractDir, ec);
        std::wstring ps =
            L"powershell -NoProfile -Command \"Expand-Archive -Force -Path '" +
            zipPath.wstring() + L"' -DestinationPath '" + extractDir.wstring() + L"'\"";
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        std::vector<wchar_t> cmd(ps.begin(), ps.end());
        cmd.push_back(0);
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 120000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }

        fs::path found;
        for (auto& ent : fs::recursive_directory_iterator(extractDir, ec)) {
            if (ec) break;
            if (!ent.is_regular_file(ec)) continue;
            auto n = ent.path().filename().wstring();
            for (auto& c : n) c = (wchar_t)towlower(c);
            if (n == L"dumper.exe") { found = ent.path(); break; }
        }

        if (found.empty()) {
            AppendLog(L"[!] dumper.exe not found in zip — opening folder");
            ShellExecuteW(nullptr, L"open", extractDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            g.updateBusy = false;
            if (g.hwnd) PostMessageW(g.hwnd, MSG_REFRESH, 0, 0);
            return;
        }

        std::string replaceErr;
        if (!LaunchSelfReplace(found.string(), replaceErr)) {
            AppendLog(L"[!] self-replace failed: " + Utf8ToWide(replaceErr));
            g.updateBusy = false;
            if (g.hwnd) PostMessageW(g.hwnd, MSG_REFRESH, 0, 0);
            return;
        }
        AppendLog(L"[+] updater scheduled — closing");
        if (g.hwnd) PostMessageW(g.hwnd, WM_CLOSE, 0, 0);
    }).detach();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        g.brPanel = CreateSolidBrush(COL_PANEL);
        g.fontTitle = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g.fontUi = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        g.fontMono = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));

        g.logEdit = CreateWindowExW(0, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                    0, 0, 0, 0, hwnd, (HMENU)ID_LOG, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(g.logEdit, WM_SETFONT, (WPARAM)g.fontMono, TRUE);

        DragAcceptFiles(hwnd, TRUE);
        g.output = DesktopGameDumpW();
        AppendLog(L"Drop GameAssembly.dll and global-metadata.dat, then Start Dump.");
        AppendLog(L"You can also drop a game folder. Output: " + g.output);
        StartUpdateCheck(false);
        return 0;
    }
    case WM_SIZE:
        Layout(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COL_TEXT);
        SetBkColor(hdc, COL_PANEL);
        return (LRESULT)g.brPanel;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int h = HitTest(x, y);
        if (h != g.hover) {
            g.hover = h;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g.hover) {
            g.hover = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
        int id = HitTest(x, y);
        switch (id) {
        case ID_BTN_DUMP: StartDump(); break;
        case ID_BTN_UPDATE: StartUpdateCheck(true); break;
        case ID_BTN_OPEN_OUT: {
            if (g.output.empty()) g.output = DesktopGameDumpW();
            fs::create_directories(g.output);
            ShellExecuteW(hwnd, L"open", g.output.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case ID_BTN_BROWSE_ASM: {
            const bool folder = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            auto f = folder ? OpenFolderDialog(hwnd) : OpenFileDialog(hwnd, true);
            if (!f.empty()) AssignDroppedPath(f, true);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        case ID_BTN_BROWSE_META: {
            const bool folder = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            auto f = folder ? OpenFolderDialog(hwnd) : OpenFileDialog(hwnd, false);
            if (!f.empty()) AssignDroppedPath(f, false);
            InvalidateRect(hwnd, nullptr, FALSE);
            break;
        }
        }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        POINT pt{};
        DragQueryPoint(drop, &pt);
        bool preferAsm = PtIn(g.rcAsm, pt.x, pt.y) || !PtIn(g.rcMeta, pt.x, pt.y);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            wchar_t path[MAX_PATH]{};
            DragQueryFileW(drop, i, path, MAX_PATH);
            AssignDroppedPath(path, preferAsm && i == 0);
        }
        DragFinish(drop);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case MSG_FLUSH_LOG:
        FlushLogToEdit();
        return 0;
    case MSG_REFRESH:
        InvalidateRect(hwnd, nullptr, FALSE);
        FlushLogToEdit();
        return 0;
    case MSG_OFFER_UPDATE:
        OfferUpdateDialog();
        return 0;
    case WM_DESTROY:
        if (g.fontTitle) DeleteObject(g.fontTitle);
        if (g.fontUi) DeleteObject(g.fontUi);
        if (g.fontMono) DeleteObject(g.fontMono);
        if (g.brPanel) DeleteObject(g.brPanel);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunGui(HINSTANCE instance) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(COL_BG);
    wc.lpszClassName = L"IL2CPPDumperGui";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    const int width = 720;
    const int height = 560;
    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName,
        L"IL2CPP Dumper",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, instance, nullptr);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

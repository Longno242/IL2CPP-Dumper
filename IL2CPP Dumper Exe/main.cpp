#define _CRT_SECURE_NO_WARNINGS
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dumper.h"
#include "dump_config.h"
#include "gui.h"
#include "version.h"

namespace fs = std::filesystem;

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static bool IsMetadataFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), 4);
    return magic == 0xFAB11BAFu;
}

static std::string DesktopGameDump() {
    wchar_t desktop[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop))) {
        return WideToUtf8(std::wstring(desktop) + L"\\GameDump");
    }
    return "C:\\GameDump";
}

static void PrintUsage(const char* argv0) {
    std::cout
        << "IL2CPP Dumper v" << DUMPER_VERSION << " (static / offline)\n"
        << "Usage:\n"
        << "  " << argv0 << "                         (open GUI)\n"
        << "  " << argv0 << " --cli <GameAssembly.dll> <global-metadata.dat> [output-dir]\n"
        << "  " << argv0 << " --cli <game-folder> [output-dir]\n"
        << "  " << argv0 << " --gui\n";
}

static bool FindNamedDll(const fs::path& folder, int depth, fs::path& assembly) {
    if (depth < 0) return false;
    const char* names[] = { "GameAssembly.dll", "UserAssembly.dll" };
    for (const char* name : names) {
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
        const auto name = ent.path().filename().string();
        if (name == "." || name == ".." || name == "MonoBleedingEdge" || name == ".git") continue;
        if (FindNamedDll(ent.path(), depth - 1, assembly)) return true;
    }
    return false;
}

static bool FindMetadataNear(const fs::path& folder, fs::path& metadata) {
    const fs::path candidates[] = {
        folder / "global-metadata.dat",
        folder / "il2cpp_data" / "Metadata" / "global-metadata.dat",
        folder / "Data" / "il2cpp_data" / "Metadata" / "global-metadata.dat",
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) {
            metadata = c;
            return true;
        }
    }
    std::error_code ec;
    for (auto& ent : fs::directory_iterator(folder, ec)) {
        if (ec) break;
        if (!ent.is_directory(ec)) continue;
        const auto name = ent.path().filename().string();
        if (name.size() > 5 && name.substr(name.size() - 5) == "_Data") {
            const auto m = ent.path() / "il2cpp_data" / "Metadata" / "global-metadata.dat";
            if (fs::exists(m)) {
                metadata = m;
                return true;
            }
        }
    }
    return false;
}

static bool FindInFolder(const fs::path& folder, fs::path& assembly, fs::path& metadata) {
    FindNamedDll(folder, 3, assembly);
    FindMetadataNear(folder, metadata);
    if (!assembly.empty() && metadata.empty()) {
        FindMetadataNear(assembly.parent_path(), metadata);
        if (metadata.empty() && assembly.parent_path().has_parent_path()) {
            FindMetadataNear(assembly.parent_path().parent_path(), metadata);
        }
    }
    return !assembly.empty() && !metadata.empty();
}

static bool AttachStdout() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        if (!AllocConsole()) return false;
    }
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio(true);
    return true;
}

static int RunCli(int argc, wchar_t** argv) {
    AttachStdout();
    SetConsoleTitleW(L"IL2CPP Dumper (CLI)");

    std::string assembly;
    std::string metadata;
    std::string output;

    int start = 1;
    if (argc >= 2) {
        std::wstring a0 = argv[1];
        if (a0 == L"--cli" || a0 == L"-c") start = 2;
        if (a0 == L"-h" || a0 == L"--help" || a0 == L"/?") {
            PrintUsage("dumper.exe");
            return 0;
        }
    }

    for (int i = start; i < argc; ++i) {
        fs::path p(argv[i]);
        if (fs::is_directory(p)) {
            if (assembly.empty() && metadata.empty()) {
                fs::path a, m;
                if (FindInFolder(p, a, m)) {
                    assembly = a.string();
                    metadata = m.string();
                } else if (output.empty()) {
                    output = fs::absolute(p).string();
                }
            } else if (output.empty()) {
                output = fs::absolute(p).string();
            }
        } else if (fs::is_regular_file(p)) {
            if (IsMetadataFile(p)) metadata = p.string();
            else if (assembly.empty()) assembly = p.string();
            else if (metadata.empty()) metadata = p.string();
        }
    }

    if (assembly.empty() || metadata.empty()) {
        PrintUsage("dumper.exe");
        return 1;
    }
    if (!fs::exists(assembly) || !fs::exists(metadata)) {
        std::cerr << "[!] missing input file(s)\n";
        return 1;
    }
    if (output.empty()) output = DesktopGameDump();

    std::cout << "[*] assembly : " << assembly << "\n";
    std::cout << "[*] metadata : " << metadata << "\n";
    std::cout << "[*] output   : " << output << "\n";

    DumpConfig cfg;
    const bool ok = GameDumper::DumpFromFiles(assembly, metadata, output, cfg);
    if (!ok) {
        std::cerr << "[!] dump failed\n";
        return 2;
    }
    std::cout << "[+] done\n";
    return 0;
}

static bool WantsCli(int argc, wchar_t** argv) {
    if (argc <= 1) return false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--gui" || a == L"-g") return false;
        if (a == L"--cli" || a == L"-c") return true;
        if (a == L"-h" || a == L"--help" || a == L"/?") return true;
        if (!a.empty() && a[0] != L'-') return true;
    }
    return false;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int code = 0;
    if (argv && WantsCli(argc, argv)) {
        code = RunCli(argc, argv);
    } else {
        code = RunGui(instance);
    }
    if (argv) LocalFree(argv);
    return code;
}

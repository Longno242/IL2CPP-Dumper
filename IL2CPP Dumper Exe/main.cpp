#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dumper.h"
#include "dump_config.h"

namespace fs = std::filesystem;

static bool IsMetadataFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    uint32_t magic = 0;
    in.read(reinterpret_cast<char*>(&magic), 4);
    return magic == 0xFAB11BAFu;
}

static std::string DesktopGameDump() {
    char desktop[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop))) {
        return std::string(desktop) + "\\GameDump";
    }
    return "C:\\GameDump";
}

static void PrintUsage(const char* argv0) {
    std::cout
        << "IL2CPP Dumper (static / offline)\n"
        << "Dumps metadata from GameAssembly.dll + global-metadata.dat without running the game.\n\n"
        << "Usage:\n"
        << "  " << argv0 << " <GameAssembly.dll> <global-metadata.dat> [output-dir]\n"
        << "  " << argv0 << " <game-folder> [output-dir]\n"
        << "  " << argv0 << "            (prompts for paths)\n\n"
        << "For protected/encrypted games, use the runtime DLL injector instead.\n";
}

static bool FindInFolder(const fs::path& folder, fs::path& assembly, fs::path& metadata) {
    const fs::path ga = folder / "GameAssembly.dll";
    const fs::path meta1 = folder / "global-metadata.dat";
    const fs::path meta2 = folder / "il2cpp_data" / "Metadata" / "global-metadata.dat";
    const fs::path meta3 = folder / "Data" / "il2cpp_data" / "Metadata" / "global-metadata.dat";

    if (fs::exists(ga)) assembly = ga;
    if (fs::exists(meta1)) metadata = meta1;
    else if (fs::exists(meta2)) metadata = meta2;
    else if (fs::exists(meta3)) metadata = meta3;

    // Unity *_Data folder sibling
    if (metadata.empty()) {
        for (auto& ent : fs::directory_iterator(folder)) {
            if (!ent.is_directory()) continue;
            const auto name = ent.path().filename().string();
            if (name.size() > 5 && name.substr(name.size() - 5) == "_Data") {
                const auto m = ent.path() / "il2cpp_data" / "Metadata" / "global-metadata.dat";
                if (fs::exists(m)) metadata = m;
            }
        }
    }
    return !assembly.empty() && !metadata.empty();
}

static std::string PromptPath(const char* label) {
    std::cout << label << ": " << std::flush;
    std::string line;
    std::getline(std::cin, line);
    // strip quotes
    if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
        line = line.substr(1, line.size() - 2);
    }
    return line;
}

int main(int argc, char** argv) {
    SetConsoleTitleA("IL2CPP Dumper (static)");

    std::string assembly;
    std::string metadata;
    std::string output;

    if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help" || std::string(argv[1]) == "/?")) {
        PrintUsage(argv[0]);
        return 0;
    }

    if (argc >= 2) {
        for (int i = 1; i < argc; ++i) {
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
    }

    if (assembly.empty() || metadata.empty()) {
        std::cout << "Static IL2CPP dump ÔÇö enter file paths (or drag & drop).\n";
        if (assembly.empty()) assembly = PromptPath("GameAssembly.dll (or game folder)");
        if (!assembly.empty() && fs::is_directory(assembly)) {
            fs::path a, m;
            if (FindInFolder(assembly, a, m)) {
                assembly = a.string();
                if (metadata.empty()) metadata = m.string();
            }
        }
        if (metadata.empty()) metadata = PromptPath("global-metadata.dat");
    }

    if (assembly.empty() || metadata.empty()) {
        PrintUsage(argv[0]);
        std::cout << "Press Enter to exit..." << std::flush;
        std::string _pause; std::getline(std::cin, _pause);
        return 1;
    }
    if (!fs::exists(assembly)) {
        std::cerr << "[!] assembly not found: " << assembly << "\n";
        std::cout << "Press Enter to exit..." << std::flush;
        std::string _pause; std::getline(std::cin, _pause);
        return 1;
    }
    if (!fs::exists(metadata)) {
        std::cerr << "[!] metadata not found: " << metadata << "\n";
        std::cout << "Press Enter to exit..." << std::flush;
        std::string _pause; std::getline(std::cin, _pause);
        return 1;
    }

    if (output.empty()) output = DesktopGameDump();
    DumpConfig cfg;

    std::cout << "[*] assembly : " << assembly << "\n";
    std::cout << "[*] metadata : " << metadata << "\n";
    std::cout << "[*] output   : " << output << "\n";

    const bool ok = GameDumper::DumpFromFiles(assembly, metadata, output, cfg);
    if (!ok) {
        std::cerr << "[!] dump failed\n";
        std::cerr << "    Tip: encrypted/packed games need the runtime DLL instead.\n";
        std::cout << "Press Enter to exit..." << std::flush;
        std::string _pause; std::getline(std::cin, _pause);
        return 2;
    }

    std::cout << "[+] done\n";
    std::cout << "Press Enter to exit..." << std::flush;
    std::string _pause;
    std::getline(std::cin, _pause);
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cstdio>
#include "dumper.h"
#include "rrid.hpp"
#include "experimental/runtime_config.h"
#include "experimental/module_discovery.h"

static HMODULE g_hModule = nullptr;

static DWORD WINAPI DumpThread(LPVOID) {
    runtime_config::apply();

    if (!runtime_config::module_name_override().empty()) {
        rrid::set_module_name(runtime_config::module_name_override());
        printf("[*] module override: %s\n", rrid::get_module_name().c_str());
    }

    if (!GetModuleHandleA(rrid::get_module_name().c_str())) {
        if (rrid::auto_detect_module()) {
            printf("[*] auto-detected module: %s\n", rrid::get_module_name().c_str());
        } else {
            const auto candidates = module_discovery::scan_loaded_modules();
            if (!candidates.empty()) {
                printf("[*] IL2CPP candidates:\n");
                for (const auto& c : candidates) {
                    printf("    %s (score %d%s%s%s)\n",
                        c.name.c_str(),
                        c.score,
                        c.has_domain_get ? ", domain_get" : "",
                        c.has_il2cpp_exports ? ", il2cpp exports" : "",
                        c.looks_renamed ? ", renamed" : "");
                }
            }
        }
    }

    std::string dir;
    if (!runtime_config::dump_output_dir().empty()) {
        dir = runtime_config::dump_output_dir();
    } else {
        char desktop[MAX_PATH] = {};
        HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop);
        dir = SUCCEEDED(hr)
            ? std::string(desktop) + "\\GameDump"
            : "C:\\GameDump";
    }

    const bool ok = GameDumper::DumpAll(dir);

    if (ok) {
        printf("[*] unloading\n");
        FreeLibraryAndExitThread(g_hModule, 0);
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        AllocConsole();
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONIN$",  "r", stdin);
        SetConsoleTitleA("IL2CPP Dumper");
        CreateThread(nullptr, 0, DumpThread, nullptr, 0, nullptr);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        FreeConsole();
    }
    return TRUE;
}

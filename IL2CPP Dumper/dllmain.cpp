#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cstdio>
#include "dumper.h"
#include "rrid.hpp"
#include "experimental/module_discovery.h"

static HMODULE g_module = nullptr;
static bool g_console_open = false;

static void CloseDumperConsole() {
    if (!g_console_open) return;
    g_console_open = false;
    fflush(stdout);
    fflush(stderr);
    FreeConsole();
}

static DWORD WINAPI UnloadProc(LPVOID) {
    // Let DumpThread unwind CRT state before we tear the module down.
    Sleep(250);
    FreeLibraryAndExitThread(g_module, 0);
    return 0;
}

static void ScheduleUnload() {
    printf("[*] unloading\n");
    fflush(stdout);
    CloseDumperConsole();

    HANDLE h = CreateThread(nullptr, 0, UnloadProc, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
}

static DWORD WINAPI DumpThread(LPVOID) {
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

    char desktop[MAX_PATH] = {};
    HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop);
    std::string dir = SUCCEEDED(hr)
        ? std::string(desktop) + "\\GameDump"
        : "C:\\GameDump";

    const bool ok = GameDumper::DumpAll(dir);
    if (ok) printf("[+] dump complete\n");
    else printf("[!] dump failed\n");

    ScheduleUnload();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = hModule;
        DisableThreadLibraryCalls(hModule);
        AllocConsole();
        g_console_open = true;
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONIN$", "r", stdin);
        SetConsoleTitleA("IL2CPP Dumper");

        HANDLE h = CreateThread(nullptr, 0, DumpThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    else if (reason == DLL_PROCESS_DETACH) {
        // Console is usually already closed in ScheduleUnload.
        CloseDumperConsole();
    }
    return TRUE;
}

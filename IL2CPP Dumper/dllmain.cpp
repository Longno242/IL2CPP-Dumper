#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cstdio>
#include "dumper.h"

static HMODULE g_hModule = nullptr;

static DWORD WINAPI DumpThread(LPVOID) {
    Sleep(2000);

    char desktop[MAX_PATH] = {};
    HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop);

    std::string dir = SUCCEEDED(hr)
        ? std::string(desktop) + "\\GameDump"
        : "C:\\GameDump";

    const bool ok = GameDumper::DumpAll(dir, [](const std::string& msg) {
        printf("%s\n", msg.c_str());
    });

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

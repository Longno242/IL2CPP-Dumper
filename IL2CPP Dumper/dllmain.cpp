#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <cstdio>
#include "dumper.h"

static DWORD WINAPI DumpThread(LPVOID) {
    Sleep(2000); // let il2cpp finish booting

    char desktop[MAX_PATH] = {};
    HRESULT hr = SHGetFolderPathA(nullptr, CSIDL_DESKTOP, nullptr, SHGFP_TYPE_CURRENT, desktop);

    std::string path = SUCCEEDED(hr)
        ? std::string(desktop) + "\\GameDump.hpp"
        : "C:\\GameDump.hpp";

    GameDumper::DumpAll(path, [](const std::string& msg) {
        printf("%s\n", msg.c_str());
    });

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
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

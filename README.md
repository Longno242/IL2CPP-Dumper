# IL2CPP Dumper

A runtime dumper for Unity games built with IL2CPP. Inject the DLL, it reads the live IL2CPP metadata out of the process, and writes a single `GameDump.hpp` to your desktop containing every image, class, field and method with its RVA / offset and a C# signature.

You then `#include "GameDump.hpp"` in your own project and call game code by name instead of hunting AOBs or rebuilding offsets every patch.

## Contents

- [Features](#features)
- [Why a runtime dumper](#why-a-runtime-dumper)
- [Requirements](#requirements)
- [Build](#build)
- [Usage](#usage)
- [Example output](#example-output)
- [Using the dump in your project](#using-the-dump-in-your-project)
- [Project layout](#project-layout)
- [How it works](#how-it-works)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Disclaimer](#disclaimer)

## Features

- Dumps every loaded IL2CPP image in one pass.
- Outputs a single self-contained C++ header, no external runtime needed.
- Each class becomes a `namespace`, each field/method becomes a `constexpr uint64_t`.
- C# signatures are emitted as inline comments so the header is also readable docs.
- Static fields get a `_RVA`, instance fields get an `_Offset`, methods get a `_RVA`.
- Duplicate identifiers are auto-suffixed (`_2`, `_3`, ...).
- Works on packed / obfuscated games (Themida, VMProtect, metadata encryption, string encryption, ...) because it runs after the protection has decrypted everything in memory.
- Pure C++20, no third-party dependencies beyond Win32 and the bundled `rrid.hpp` reader.

## Why a runtime dumper

Static dumpers (Il2CppDumper, Il2CppInspector, ...) parse the `global-metadata.dat` file on disk. That works fine on a vanilla build, but falls over the moment a game ships with:

- Themida / VMProtect wrapping `GameAssembly.dll`
- IL2CPP metadata encryption
- String / type-name encryption
- Custom packers

A runtime dumper sidesteps all of that. By the time `GameDumper::DumpAll` runs, the game has already booted IL2CPP, the packer has unwrapped its code in memory, and the metadata is sitting in plain structures the game itself uses. The dumper just walks those structures.

What it does **not** do:

- It will not bypass anti-injection. You still need a working injector.
- It will not hide from anti-debug, integrity checks, or kernel anti-cheats (EAC, BattlEye, Vanguard, ...).
- It will not deobfuscate names that the game itself never has in clear (proguard-style renames stay renamed).

## Requirements

| | |
|---|---|
| OS | Windows 10 / 11, x64 |
| Toolchain | Visual Studio 2022 with the C++ desktop workload |
| SDK | Windows 10 SDK |
| Language | C++20 |
| Target | A Unity game using IL2CPP (i.e. has a `GameAssembly.dll`) |

## Build

1. Open `Testicle.slnx` in Visual Studio 2022.
2. Set the configuration to **Release | x64**.
3. Build the solution (`Ctrl+Shift+B`).

Output: `x64\Release\Testicle.dll`.

The output name is `Testicle.dll` because the internal vcxproj is still called `Testicle`. Rename `TargetName` in `Testicle.vcxproj` if you want something else.

## Usage

1. Launch the target game and let it reach the main menu (so IL2CPP is fully initialised).
2. Inject `Testicle.dll` into the game process. Any injector works:
   - Manual map
   - `LoadLibrary` injector
   - Cheat Engine's "Inject DLL"
   - Your own loader
3. A console window titled **IL2CPP Dumper** pops up and prints progress as each image is dumped.
4. When it finishes the last line is `[+] wrote <path>`. `GameDump.hpp` is now on your desktop.

If the desktop path can't be resolved (very rare), the file is written to `C:\GameDump.hpp` instead.

## Example output

```cpp
namespace GameDump {

// ==== image: Assembly-CSharp   ModuleBase 0x7FF6XXXXXXXX   ImageRVA 0x1234567
namespace Assembly_CSharp {

    constexpr uint64_t ModuleBase = 0x7FF6XXXXXXXX;
    constexpr uint64_t ImageRVA   = 0x1234567;

    // class Game.PlayerController  (ClassRVA 0x1ABCDE0)
    namespace Game_PlayerController {
        constexpr uint64_t ClassRVA = 0x1ABCDE0;

        // fields
        constexpr uint64_t health_Offset    = 0x18;       // public int health
        constexpr uint64_t maxHealth_RVA    = 0x2F0AB00;  // public static int maxHealth
        constexpr uint64_t isDead_Offset    = 0x1C;       // private bool isDead

        // methods
        constexpr uint64_t TakeDamage_RVA   = 0x1C3D4E0;  // public void TakeDamage(int amount)
        constexpr uint64_t Heal_RVA         = 0x1C3D560;  // public void Heal(int amount)
        constexpr uint64_t Update_RVA       = 0x1C3D5A0;  // private void Update()
    }
}

} // GameDump
```

Every numeric constant is one of:

| Suffix | What it is | Add to |
|---|---|---|
| `_RVA` (method) | Method address offset | `GetModuleHandleA("GameAssembly.dll")` |
| `_RVA` (static field) | Static field address offset | `GetModuleHandleA("GameAssembly.dll")` |
| `_Offset` | Instance field offset | The object instance pointer |
| `ClassRVA` | IL2CPP `Il2CppClass*` address | `GetModuleHandleA("GameAssembly.dll")` |

## Using the dump in your project

```cpp
#include "GameDump.hpp"
#include <Windows.h>

const uint64_t base = (uint64_t)GetModuleHandleA("GameAssembly.dll");

// Calling a method
using TakeDamage_t = void(__fastcall*)(void* self, int amount);
auto TakeDamage = (TakeDamage_t)(base +
    GameDump::Assembly_CSharp::Game_PlayerController::TakeDamage_RVA);

TakeDamage(player, 25);

// Reading an instance field
int hp = *(int*)((uint8_t*)player +
    GameDump::Assembly_CSharp::Game_PlayerController::health_Offset);

// Reading a static field
int& maxHp = *(int*)(base +
    GameDump::Assembly_CSharp::Game_PlayerController::maxHealth_RVA);
```

Re-dump whenever the game updates and your offsets are automatically refreshed.

## Project layout

```
IL2CPP DUMPER/
├── Testicle.slnx               Visual Studio solution
├── README.md                   You are here
└── IL2CPP Dumper/
    ├── Testicle.vcxproj        DLL project (x64 / x86, Debug / Release)
    ├── dllmain.cpp             DLL entry, spawns the dump thread on attach
    ├── dumper.h                Public interface (GameDumper::DumpAll)
    ├── dumper.cpp              Header emitter, writes GameDump.hpp
    ├── rrid.hpp                IL2CPP metadata reader (images, classes, fields, methods)
    └── framework.h             Standard Win32 framework header
```

## How it works

1. **`DllMain` (DLL_PROCESS_ATTACH)** allocates a console, retitles it, and spawns `DumpThread`.
2. **`DumpThread`** sleeps for 2 seconds so the game can finish booting IL2CPP, resolves the user's Desktop path with `SHGetFolderPathA`, and calls `GameDumper::DumpAll`.
3. **`GameDumper::DumpAll`**:
   - Calls `rrid::init()` to attach to the live IL2CPP runtime inside `GameAssembly.dll`.
   - Calls `rrid::get_images()` to enumerate every loaded assembly.
   - For each image, walks classes -> fields -> methods and writes them out as `constexpr uint64_t` constants wrapped in nested namespaces.
   - Identifier names are sanitized (`SanitizeIdent`) and de-duplicated (`UniqueName`) so the resulting header always compiles.
   - C# signatures (`BuildSignature`, `BuildFieldSignature`) are emitted as trailing line comments.

The whole thing is single-threaded, single-pass, and writes straight to disk with `std::ofstream`. A medium-sized game (a few hundred MB of IL2CPP) typically dumps in under a second.

## Configuration

There is no config file. The handful of things you might want to tweak are constants in source:

| What | Where |
|---|---|
| Target module name (`GameAssembly.dll`) | `rrid.hpp` (`GetModuleBaseAddress` call in `RridImage`) |
| Boot delay (default `Sleep(2000)`) | `dllmain.cpp` -> `DumpThread` |
| Output file path | `dllmain.cpp` -> `DumpThread` (or pass your own to `GameDumper::DumpAll`) |
| Pretty type table (`int`, `string`, ...) | `dumper.cpp` -> `PrettyType` |

## Troubleshooting

**Console opens but no file is written.**
The dumper probably crashed inside `rrid::init()` because IL2CPP wasn't ready yet. Bump the `Sleep(2000)` in `dllmain.cpp` to `5000` or `10000` and rebuild.

**`[!] rrid::init failed`.**
Either `GameAssembly.dll` isn't loaded yet, or the IL2CPP version inside the game is one `rrid.hpp` doesn't know how to parse. Verify with a debugger that `GameAssembly.dll` is loaded at the moment the dumper runs.

**DLL won't inject.**
That has nothing to do with the dumper, it's your injector vs the game's anti-injection. Try a manual-map injector.

**Header has weird names like `_Generated$$_2`.**
That's expected. IL2CPP keeps compiler-generated names (lambdas, async state machines, ...) and they collide. The `_2`, `_3`, ... suffixes guarantee a valid, unique C++ identifier per scope.

**Compiler complains about `GameDump.hpp` being too large.**
On very big games the header can be tens of MB. Compile it once into its own translation unit and let LTCG sort it out, or split it per image.

## Limitations

- **Module name is hardcoded** to `GameAssembly.dll` in `rrid.hpp`. Change it there if your target uses something else.
- **Only RVAs and offsets are emitted.** No method bodies, no generic instantiations, no attributes, no PInvoke metadata beyond the `extern` modifier.
- **No name demangling.** If the game ships with renamed types (e.g. `<>c__DisplayClass17_0`), that's what you'll get in the dump.
- **x64 only is tested.** The vcxproj has Win32 configurations but the dumper hasn't been exercised against 32-bit Unity builds in a while.
- **vcxproj is still called `Testicle`.** Rename it in the project file if it bothers you.

## Disclaimer

This is a reverse engineering and modding tool. Use it on software you own, software you're paid to audit, single-player titles you want to mod, or your own Unity projects. Don't ship cheats with it into competitive multiplayer games and don't break ToS / EULAs you've agreed to.

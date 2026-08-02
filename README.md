# IL2CPP Dumper

A runtime dumper for Unity games built with IL2CPP. Inject the DLL, it reads the live IL2CPP metadata out of the process, and writes a `GameDump` folder to your desktop with every image, class, field and method — RVAs, offsets, inheritance info, and C# signatures.

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
- Outputs the same dump in **five formats** so you can use whatever language your project is in:
  - `GameDump.hpp` — C/C++
  - `GameDump.cs` — C#
  - `GameDump.rs` — Rust
  - `GameDump.py` — Python
  - `GameDump.json` — JSON (for custom tools / scripts)
- **`Index.json`** — flat search index mapping full type/method names to symbol paths and RVAs.
- **`images/`** — per-assembly C++ headers for faster compiles on large games.
- Each class becomes a nested scope (`namespace`, `static class`, `mod`, etc.).
- C# signatures are emitted as inline comments so the files are also readable docs.
- Static fields get a `_RVA`, instance fields get an `_Offset`, methods get a `_RVA`.
- **MethodInfo RVA** emitted alongside method code RVAs (for hooking frameworks).
- **Inheritance metadata** — parent class, interfaces, and generic flag in comments and JSON.
- Duplicate identifiers are auto-suffixed (`_2`, `_3`, ...).
- Method overloads are disambiguated by **parameter types** (`TakeDamage_int_RVA`, `TakeDamage_string_int_RVA`, ...).
- Nested types are dumped recursively inside their parent class namespace.
- Enum members are dumped as `constexpr int64_t` values.
- **Robust IL2CPP init** — polls until ready (up to 60 s), resolves APIs via exports + pattern scan, auto-fallback return spoofer (legacy / v2 / direct).
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

1. Open `IL2CPPDumper.slnx` in Visual Studio 2022+.
2. Set the configuration to **Release | x64**.
3. Build the solution (`Ctrl+Shift+B`).

Outputs:
- `x64\Release\IL2CPPDumper.dll` — runtime injector
- `x64\Release\IL2CPPDumper.exe` — offline static dumper

## Usage

### Runtime DLL

1. Launch the target game and let it reach the main menu (so IL2CPP is fully initialised).
2. Inject `IL2CPPDumper.dll` into the game process. Any injector works:
   - Manual map
   - `LoadLibrary` injector
   - Cheat Engine's "Inject DLL"
   - Your own loader
3. A console window titled **IL2CPP Dumper** pops up and prints progress as each image is dumped.
4. When it finishes, a `GameDump` folder appears on your desktop with all output files.

If the desktop path can't be resolved (very rare), files are written to `C:\GameDump\` instead.

### Static EXE

```
IL2CPPDumper.exe <GameAssembly.dll> <global-metadata.dat> [output-dir]
IL2CPPDumper.exe <game-folder>
```

Use the runtime DLL if metadata on disk is encrypted/packed.

Pre-built binaries are attached to each [GitHub release](https://github.com/Longno242/IL2CPP-Dumper/releases).

### Output files

| File | Language | Use case |
|---|---|---|
| `GameDump.hpp` | C/C++ | `#include` in a native mod / cheat |
| `GameDump.cs` | C# | Reference in a .NET modding tool |
| `GameDump.rs` | Rust | `use gamedump::...` in a Rust project |
| `GameDump.py` | Python | Scripting / automation |
| `GameDump.json` | JSON | Custom parsers, IDA scripts, etc. |
| `Index.json` | JSON | Flat search index (grep-friendly lookup) |
| `images/*.hpp` | C/C++ | Per-assembly headers (smaller compile units) |
| `README.txt` | — | Quick guide to the folder |

## Example output (C++)

```cpp
namespace GameDump {

// ==== image: Assembly-CSharp   ModuleBase 0x7FF6XXXXXXXX   ImageRVA 0x1234567
namespace Assembly_CSharp {

    constexpr uint64_t ModuleBase = 0x7FF6XXXXXXXX;
    constexpr uint64_t ImageRVA   = 0x1234567;

    // class Game.PlayerController  (ClassRVA 0x1ABCDE0)  extends UnityEngine.MonoBehaviour
    namespace Game_PlayerController {
        constexpr uint64_t ClassRVA = 0x1ABCDE0;

        // fields
        constexpr uint64_t health_Offset    = 0x18;       // public int health
        constexpr uint64_t maxHealth_RVA    = 0x2F0AB00;  // public static int maxHealth
        constexpr uint64_t isDead_Offset    = 0x1C;       // private bool isDead

        // methods
        constexpr uint64_t TakeDamage_int_RVA = 0x1C3D4E0;  // public void TakeDamage(int amount)
        constexpr uint64_t TakeDamage_int_RVA_MethodInfo = 0x1C3D000;
        constexpr uint64_t Heal_int_RVA       = 0x1C3D560;  // public void Heal(int amount)
        constexpr uint64_t Update_0_RVA         = 0x1C3D5A0;  // private void Update()
    }
}

} // GameDump
```

Every numeric constant is one of:

| Suffix | What it is | Add to |
|---|---|---|
| `_RVA` (method) | Method address offset | `GetModuleHandleA("GameAssembly.dll")` |
| `_MethodInfo` | `Il2CppMethodInfo*` offset | `GetModuleHandleA("GameAssembly.dll")` |
| `_RVA` (static field) | Static field address offset | `GetModuleHandleA("GameAssembly.dll")` |
| `_Offset` | Instance field offset | The object instance pointer |
| `ClassRVA` | IL2CPP `Il2CppClass*` address | `GetModuleHandleA("GameAssembly.dll")` |

## Using the dump in your project

### C++

```cpp
#include "GameDump/GameDump.hpp"
#include <Windows.h>

const uint64_t base = (uint64_t)GetModuleHandleA("GameAssembly.dll");

// Calling a method
using TakeDamage_t = void(__fastcall*)(void* self, int amount);
auto TakeDamage = (TakeDamage_t)(base +
    GameDump::Assembly_CSharp::Game_PlayerController::TakeDamage_int_RVA);

TakeDamage(player, 25);

// Reading an instance field
int hp = *(int*)((uint8_t*)player +
    GameDump::Assembly_CSharp::Game_PlayerController::health_Offset);

// Reading a static field
int& maxHp = *(int*)(base +
    GameDump::Assembly_CSharp::Game_PlayerController::maxHealth_RVA);
```

For large games, include only the assembly you need:

```cpp
#include "GameDump/images/Assembly-CSharp.hpp"
```

### C#

```csharp
using GameDump;

ulong baseAddr = (ulong)NativeMethods.GetModuleHandle("GameAssembly.dll");
IntPtr takeDamage = (IntPtr)(baseAddr + Assembly_CSharp.Game_PlayerController.TakeDamage_int_RVA);
```

### Rust

```rust
use gamedump::assembly_csharp::game_playercontroller::TAKE_DAMAGE_INT_RVA;

let base = get_module_base("GameAssembly.dll");
let take_damage = base + TAKE_DAMAGE_INT_RVA;
```

### Python

```python
from GameDump import Assembly_CSharp

base = get_module_base("GameAssembly.dll")
take_damage = base + Assembly_CSharp.Game_PlayerController.TakeDamage_int_RVA
```

Re-dump whenever the game updates and your offsets are automatically refreshed.

## Project layout

```
IL2CPP-Dumper/
├── IL2CPPDumper.slnx
├── README.md
├── IL2CPP Dumper/                 runtime DLL
│   ├── experimental/              renamed-export fallback (opt-in after normal resolve fails)
│   ├── dllmain.cpp
│   ├── dumper.cpp / dumper.h
│   └── rrid.hpp
└── IL2CPP Dumper Exe/             offline static EXE (self-contained)
```

## How it works

1. **`DllMain` (DLL_PROCESS_ATTACH)** allocates a console, retitles it, and spawns `DumpThread`.
2. **`DumpThread`** calls `GameDumper::DumpAll` with `Desktop\GameDump` as the output folder.
3. **`GameDumper::DumpAll`**:
   - Polls `rrid::init()` until IL2CPP is ready (up to 60 s).
   - Resolves IL2CPP APIs via `GetProcAddress` exports, with UnityPlayer pattern-scan fallback.
   - Selects a return-address spoofer automatically (legacy → v2 → direct call).
   - Enumerates every loaded assembly, then writes metadata to C++, C#, Rust, Python, JSON, Index, and per-image headers.
   - For each image, walks classes (including nested types) → fields → methods and writes them out as constants wrapped in nested namespaces.
   - Method names include parameter types so overloads don't collide.
   - Enum types emit `constexpr int64_t` members instead of field offsets.
   - Unloads the DLL automatically after a successful dump.

The whole thing is single-threaded, single-pass, and writes straight to disk with `std::ofstream`. A medium-sized game (a few hundred MB of IL2CPP) typically dumps in under a second.

## Configuration

There is no config file. The handful of things you might want to tweak are constants in source:

| What | Where |
|---|---|
| Target module name (`GameAssembly.dll`) | `rrid.hpp` (`set_module_name` / default in `RridContext`) |
| Init retry count / interval | `dumper.cpp` → `DumpAll` (default 120 × 500 ms) |
| Output folder | `dllmain.cpp` → `DumpThread` (default: Desktop `GameDump\`) |
| Pretty type table (`int`, `string`, ...) | `dumper.cpp` → `PrettyType` |

## Troubleshooting

**Console opens but no file is written.**
Wait until the game reaches the main menu before injecting. The dumper polls for up to 60 seconds — if it still fails, check the error line printed in the console.

**`[!] rrid::init failed: ...`**
The console now prints the exact failure reason, for example:
- `GameAssembly.dll is not loaded` — inject later, or call `rrid::set_module_name()` if your target uses a different module name.
- `missing API: il2cpp_...` — IL2CPP version not supported yet; open an issue with the game name and Unity version.
- `return spoofer probe failed` — APIs resolved but calls failed; try injecting at main menu.
- `no IL2CPP assemblies loaded yet` — IL2CPP isn't fully initialised; wait longer before injecting.

The console also reports whether `GameAssembly.dll` and `UnityPlayer.dll` are loaded.

**DLL won't inject.**
That has nothing to do with the dumper, it's your injector vs the game's anti-injection. Try a manual-map injector.

**Header has weird names like `_Generated$$_2`.**
That's expected. IL2CPP keeps compiler-generated names (lambdas, async state machines, ...) and they collide. The `_2`, `_3`, ... suffixes guarantee a valid, unique C++ identifier per scope.

**Compiler complains about `GameDump.hpp` being too large.**
Use the per-assembly headers in `images/` instead of the monolithic `GameDump.hpp`, or compile the dump once into its own translation unit.

## Limitations

- **Module name defaults to `GameAssembly.dll`.** Change it in `rrid.hpp` or call `rrid::set_module_name()` before `init()` if your target uses something else.
- **Only RVAs and offsets are emitted.** No method bodies, no generic instantiations, no attributes, no PInvoke metadata beyond the `extern` modifier.
- **No name demangling.** If the game ships with renamed types (e.g. `<>c__DisplayClass17_0`), that's what you'll get in the dump.
- **x64 only is tested.** The vcxproj has Win32 configurations but the dumper hasn't been exercised against 32-bit Unity builds in a while.

## Disclaimer

This is a reverse engineering and modding tool. Use it on software you own, software you're paid to audit, single-player titles you want to mod, or your own Unity projects. Don't ship cheats with it into competitive multiplayer games and don't break ToS / EULAs you've agreed to.

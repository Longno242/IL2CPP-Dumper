# PLS STAR THE REPO

# IL2CPP Dumper


Extract and document IL2CPP metadata from Unity games. The runtime component reads live metadata from a running process; the static EXE reads `GameAssembly.dll` and `global-metadata.dat` from disk. Output is written to a `GameDump` folder with type layouts, field offsets, method RVAs, inheritance info, and C#-style signatures.

Typical uses: understanding your own Unity IL2CPP builds, generating reference headers for tooling, comparing builds across versions, and supporting authorized security research or interoperability work.

## Contents

- [Features](#features)
- [Runtime vs static](#runtime-vs-static)
- [Requirements](#requirements)
- [Build](#build)
- [Usage](#usage)
- [Example output](#example-output)
- [Using the export files](#using-the-export-files)
- [Project layout](#project-layout)
- [How it works](#how-it-works)
- [Configuration](#configuration)
- [Troubleshooting](#troubleshooting)
- [Limitations](#limitations)
- [Disclaimer](#disclaimer)

## Features

- Exports every loaded IL2CPP image in one pass.
- Multiple output formats from the same metadata walk:
  - `GameDump.hpp` — C/C++
  - `GameDump.cs` — C#
  - `GameDump.rs` — Rust
  - `GameDump.py` — Python
  - `GameDump.json` — JSON
- **`Index.json`** — flat search index mapping type/method names to symbol paths and RVAs.
- **`images/`** — per-assembly **C#** files named after the assemblies (`Assembly-CSharp.cs`, etc.).
- Nested scopes per class; C# signatures in comments for readability.
- Static fields → `_RVA`, instance fields → `_Offset`, methods → `_RVA`.
- **MethodInfo RVA** alongside method code RVAs.
- Parent class, interfaces, and generic flags in comments and JSON.
- Duplicate identifiers auto-suffixed (`_2`, `_3`, …).
- Method overloads disambiguated by parameter types.
- Nested types and enum members included.
- Runtime init retries until IL2CPP is ready; auto-detects `GameAssembly.dll` / `UserAssembly.dll` and other IL2CPP modules.
- Export resolution: `GetProcAddress` → PE/table scan → UnityPlayer scan → experimental renamed-export patterns.
- Static EXE for unencrypted on-disk metadata (metadata versions supported by the parser).
- C++20, Win32 only — no third-party runtime dependencies.

## Runtime vs static

| Mode | Input | When to use |
|---|---|---|
| **Runtime DLL** | Running Unity IL2CPP process | Encrypted/packed metadata, or you need in-memory structures |
| **Static EXE** | `GameAssembly.dll` + `global-metadata.dat` | Offline analysis when metadata on disk is plain |

Static tools (Il2CppDumper, Il2CppInspector, …) parse `global-metadata.dat` directly. That fails when metadata is encrypted or the binary is wrapped. The runtime path reads the same structures the game already loaded into memory.

## Requirements

| | |
|---|---|
| OS | Windows 10 / 11, x64 |
| Toolchain | Visual Studio 2022+ with C++ desktop workload |
| SDK | Windows 10 SDK |
| Language | C++20 |
| Target | Unity IL2CPP build (`GameAssembly.dll` or `UserAssembly.dll`) |

## Build

1. Open `IL2CPPDumper.slnx` in Visual Studio.
2. Configuration: **Release | x64**.
3. Build (`Ctrl+Shift+B`).

Outputs:
- `x64\Release\dumper.dll` — runtime component
- `x64\Release\dumper.exe` — static/offline tool

Pre-built binaries: [GitHub releases](https://github.com/Longno242/IL2CPP-Dumper/releases).

## Usage

### Runtime DLL

1. Start the target application and wait until IL2CPP has finished initializing (usually main menu).
2. Load `dumper.dll` into the process with your preferred DLL loader.
3. A console titled **IL2CPP Dumper** shows progress.
4. Output appears under `Desktop\GameDump\` (or `C:\GameDump\` if the desktop path is unavailable).

### Static EXE

```
dumper.exe <GameAssembly.dll> <global-metadata.dat> [output-dir]
dumper.exe <game-folder>
```

If metadata on disk is encrypted, use the runtime DLL instead.

### Output files

| File | Format | Purpose |
|---|---|---|
| `GameDump.hpp` | C/C++ | Headers with constexpr offsets/RVAs |
| `GameDump.cs` | C# | Reference constants |
| `GameDump.rs` | Rust | Constants in nested modules |
| `GameDump.py` | Python | Scriptable reference |
| `GameDump.json` | JSON | Structured metadata export |
| `Index.json` | JSON | Flat lookup index |
| `images/*.cs` | C# | One file per assembly |
| `README.txt` | — | Folder guide |

## Example output (C++)

```cpp
namespace GameDump {

namespace Assembly_CSharp {
    namespace Game_PlayerController {
        constexpr uint64_t ClassRVA = 0x1ABCDE0;
        constexpr uint64_t health_Offset    = 0x18;       // public int health
        constexpr uint64_t maxHealth_RVA    = 0x2F0AB00;  // public static int maxHealth
        constexpr uint64_t TakeDamage_int_RVA = 0x1C3D4E0;
    }
}

} // GameDump
```

| Suffix | Meaning | Relative to |
|---|---|---|
| `_RVA` (method/static) | Offset in `GameAssembly.dll` | Module base |
| `_Offset` (instance field) | Offset in object layout | Object pointer |
| `ClassRVA` | `Il2CppClass*` location | Module base |

## Using the export files

Include or import the format that matches your toolchain. For large games, use per-assembly files under `images/` instead of the monolithic exports.

Re-export after a game update to refresh offsets and signatures.

## Project layout

```
IL2CPP-Dumper/
├── IL2CPPDumper.slnx
├── IL2CPP Dumper/              runtime DLL
│   ├── experimental/
│   │   ├── renamed_exports.*   pattern match for obfuscated export names
│   │   ├── export_scan.*       PE export + pointer-table recovery
│   │   └── module_discovery.*  auto-detect IL2CPP module in process
│   └── rrid.hpp                IL2CPP metadata reader
└── IL2CPP Dumper Exe/          static/offline EXE
```

## How it works

1. **Runtime:** `DllMain` spawns a worker thread that calls `GameDumper::DumpAll`.
2. **Init:** Polls until IL2CPP APIs resolve and assemblies are available.
3. **Walk:** Enumerates images → classes → fields/methods.
4. **Emit:** Writes C++, C#, Rust, Python, JSON, index, and per-assembly files.
5. **Static:** Parses PE + metadata from disk, then uses the same emitters.

## Configuration

Tunables in source:

| Setting | Location |
|---|---|
| Module name default | `rrid.hpp` |
| Init retry count / interval | `dumper.cpp` |
| Output folder | `dllmain.cpp` |
| Renamed-export patterns | `experimental/renamed_exports.cpp` |

## Troubleshooting

**No output file**
Wait until IL2CPP is initialized before loading the DLL. Init retries for up to ~60 seconds.

**`rrid::init failed`**
The console prints the reason (`missing API`, module not loaded, etc.). The DLL lists scored module candidates when auto-detect runs. Try a later init point or report the Unity version.

**Renamed / obfuscated exports**
The experimental path pattern-matches export bodies when `il2cpp_*` names are gone. Partial recovery is logged as `api scan: table-scan` or `renamed-export mode`.

**Loader fails**
Process protection or loader compatibility — outside this tool’s scope.

**Odd generated names**
Compiler-generated IL2CPP names are preserved; numeric suffixes avoid collisions.

## Limitations

- Auto-discovery covers common module names; exotic packers may still need a source edit in `rrid.hpp`.
- Metadata and RVAs only — no method bodies or full generic expansion.
- x64 tested; Win32 configs exist but are rarely used.

## Disclaimer

For lawful, authorized use only: your own software, projects you have permission to analyze, or research with explicit consent. You are responsible for complying with applicable licenses and terms. This project exports metadata for analysis and documentation; it does not grant rights to bypass protections or use outputs in unauthorized ways.

# IL2CPP Dumper

[![Release](https://img.shields.io/github/v/release/Longno242/IL2CPP-Dumper?label=release&logo=github)](https://github.com/Longno242/IL2CPP-Dumper/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Longno242/IL2CPP-Dumper/total?label=downloads&logo=github)](https://github.com/Longno242/IL2CPP-Dumper/releases)
[![License](https://img.shields.io/github/license/Longno242/IL2CPP-Dumper)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078D4?logo=windows)](https://github.com/Longno242/IL2CPP-Dumper/releases)

Dump Unity IL2CPP metadata into readable C++, C#, Rust, Python, and JSON — either from a **running process** or from **files on disk**.

| | Runtime DLL | Static EXE |
|---|---|---|
| **Input** | Live game process | `GameAssembly.dll` + `global-metadata.dat` |
| **Best for** | Encrypted / packed metadata | Offline dumps when metadata is plain |
| **Binary** | `dumper.dll` | `dumper.exe` |

**Latest build:** [Releases](https://github.com/Longno242/IL2CPP-Dumper/releases/latest) · Star the repo if it helps · [Open an issue](https://github.com/Longno242/IL2CPP-Dumper/issues) for bugs.

---

## Features

- One-pass dump of IL2CPP images → classes → fields / methods
- Multi-format output from the same walk (`hpp` / `cs` / `rs` / `py` / `json`)
- Flat `Index.json` + per-assembly files under `images/`
- Static fields as `_RVA`, instance fields as `_Offset`, methods as `_RVA`
- Parent types, interfaces, generics, nested types, enums
- Runtime: auto-detect `GameAssembly` / `UserAssembly`, init retries, renamed-export recovery
- Static GUI: drag-drop, output toggles, folder picker, GitHub self-update
- C++20 · Win32 · no third-party runtime deps

---

## Quick start

### Option A — Static EXE (offline)

1. Download [`dumper-*-static-exe.zip`](https://github.com/Longno242/IL2CPP-Dumper/releases/latest)
2. Run `dumper.exe`
3. Drop `GameAssembly.dll` and `global-metadata.dat` (or a game folder)
4. Optionally toggle formats / Unity·System, set the output folder
5. Click **Start Dump**

```text
dumper.exe --cli <GameAssembly.dll> <global-metadata.dat> [output-dir]
dumper.exe --cli <game-folder>
```

If metadata on disk is encrypted, use the runtime DLL instead.

### Option B — Runtime DLL (in-process)

1. Download [`dumper-*-runtime-dll.zip`](https://github.com/Longno242/IL2CPP-Dumper/releases/latest)
2. Launch the game and wait until IL2CPP is up (usually main menu)
3. Load `dumper.dll` with your loader
4. Watch the **IL2CPP Dumper** console — output goes to `Desktop\GameDump\`

---

## Output

| File | Purpose |
|---|---|
| `GameDump.hpp` | C/C++ constexpr offsets / RVAs |
| `GameDump.cs` | C# reference constants |
| `GameDump.rs` | Rust nested modules |
| `GameDump.py` | Python constants |
| `GameDump.json` | Structured dump |
| `Index.json` | Flat name → symbol / RVA index |
| `images/*.cs` | One C# file per assembly |
| `Strings.json` | Optional string scan |
| `scripts/` | IDA / Ghidra / Binary Ninja helpers |
| `README.txt` | Folder guide |

```cpp
namespace GameDump {
namespace Assembly_CSharp {
namespace Game_PlayerController {
    constexpr uint64_t ClassRVA = 0x1ABCDE0;
    constexpr uint64_t health_Offset     = 0x18;      // public int health
    constexpr uint64_t maxHealth_RVA     = 0x2F0AB00; // public static int maxHealth
    constexpr uint64_t TakeDamage_int_RVA = 0x1C3D4E0;
}
}
}
```

| Suffix | Meaning |
|---|---|
| `_RVA` | Offset in `GameAssembly.dll` (methods / static data offset) |
| `_Offset` | Instance field offset from object base |
| `ClassRVA` | `Il2CppClass*` relative to module base |

---

## Build from source

**Needs:** Windows 10/11 x64 · Visual Studio 2022+ (C++ desktop) · Windows SDK · C++20

1. Open `IL2CPPDumper.slnx`
2. Build **Release | x64**

| Output | Path |
|---|---|
| Runtime | `x64\Release\dumper.dll` |
| Static | `x64\Release\dumper.exe` |

---

## Project layout

```text
IL2CPP-Dumper/
├── IL2CPPDumper.slnx
├── IL2CPP Dumper/                 # runtime DLL
│   ├── dllmain.cpp
│   ├── dumper.cpp
│   ├── rrid.hpp                   # live IL2CPP reader
│   └── experimental/
│       ├── module_discovery.*     # find IL2CPP module
│       ├── export_scan.*          # PE / pointer-table recovery
│       └── renamed_exports.*      # stripped export names
└── IL2CPP Dumper Exe/             # static / offline EXE
    ├── gui.cpp                    # Win32 GUI + options
    ├── updater.cpp                # GitHub self-update
    ├── emit.cpp
    └── static/                    # PE + metadata parser
```

---

## How it works

1. **Runtime** — worker thread waits for IL2CPP APIs + assemblies, then walks images.
2. **Resolve** — `GetProcAddress` → export / pointer scan → UnityPlayer → renamed-export patterns.
3. **Static** — parse PE + `global-metadata.dat`, then the same emitters.
4. **Emit** — write the formats you enabled (GUI chips or `DumpConfig` defaults).

---

## Troubleshooting

| Symptom | What to try |
|---|---|
| No dump / waits forever | Inject after IL2CPP init (main menu). Retries ~60s. |
| `rrid::init failed` | Read the console reason; check listed module candidates. |
| Renamed / stripped exports | Experimental scan should log `table-scan` or `renamed-export mode`. |
| Static dump fails | Metadata may be encrypted or tables reordered — use runtime, or repair metadata first. |
| Odd generated names | Build-time obfuscation can’t be undone; suffixes avoid collisions. |

---

## Limitations

- Metadata and RVAs only — no method bodies or full generic expansion
- Exotic packers may still need a module-name tweak in `rrid.hpp`
- Primary target is **x64**

---

## Disclaimer

For lawful, authorized use only: your own software, projects you have permission to analyze, or research with explicit consent. You are responsible for complying with licenses and terms. This tool exports metadata for analysis and documentation — it does not authorize bypassing protections or unauthorized use of dumps.

MIT · [Longno242](https://github.com/Longno242)

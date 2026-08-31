# Changelog

All notable changes to this project are documented here.

## [v1.4.0] - 2026-09-01

### Added
- **Module auto-discovery** — runtime DLL scans loaded modules for `GameAssembly.dll`, `UserAssembly.dll`, renamed export tables, and `il2cpp_domain_get`.
- **Export table + pointer-table scan** — `experimental/export_scan` recovers `il2cpp_*` APIs when `GetProcAddress` fails but symbols still exist in the PE.
- **Expanded renamed-export recovery** — lower export threshold, partial-missing API trigger, alternate `domain_get` / `il2cpp_free` patterns, nested-type and interface iterator detection.
- **Static EXE** — folder discovery also checks `UserAssembly.dll`.

### Changed
- Init no longer hard-fails when a jmp gadget is missing; falls back to direct API calls.
- Gadget search also scans `UnityPlayer.dll`.
- Init logs module name, spoofer backend, and API scan path.

## [v1.3.1] - 2026-08-25

### Changed
- Release binaries renamed to **`dumper.dll`** (runtime) and **`dumper.exe`** (static).
- `images/` emits per-assembly **C#** files (`.cs`) instead of C++ headers.
- Docs reframed for metadata analysis / authorized research use.

## [v1.3.0] - 2026-08-02

### Added
- **Static offline EXE** — dump from `GameAssembly.dll` + `global-metadata.dat` without injecting (`IL2CPP Dumper Exe/`).
- **Experimental renamed-export resolve** — `IL2CPP Dumper/experimental/` recovers APIs when export *names* are stripped or obfuscated. Only runs after the normal resolve path fails.

### Notes
- Encrypted on-disk metadata still requires the runtime DLL.
- Normal IL2CPP games keep the existing export / UnityPlayer resolve path.

## [v1.2.0] - 2026-07-03

### Added
- **`Index.json`** — flat search index mapping full type/method names to symbol paths, RVAs, and signatures.
- **`images/` folder** — per-assembly C++ headers (`Assembly-CSharp.hpp`, etc.) for faster compiles on large games.
- **MethodInfo RVA** — `_MethodInfo` constants emitted next to method code RVAs (C++ and C#).
- **Inheritance metadata** — parent class, interfaces, and `[generic]` flag in dump comments and JSON.
- **Init diagnostics** — `rrid::get_init_error()` and console output showing the exact failure reason.
- **Export-based API resolution** — `GetProcAddress` fallback when UnityPlayer pattern scan fails.
- **Return spoofer v2** — new rbx/r15 stub with automatic fallback: legacy → v2 → direct call.
- **Multiple jmp gadgets** — tries `[rdi]`, `[rbx]`, `[rsi]`, `[rcx]` trampolines in `GameAssembly.dll`.
- **Optional parent/interface APIs** — `il2cpp_class_get_parent` and `il2cpp_class_get_interfaces` when available.

### Changed
- **Init polling** — retries `rrid::init()` up to 120 times (500 ms apart) instead of a fixed 2 s sleep in `dllmain.cpp`.
- **Method overload names** — now use parameter types (`TakeDamage_int_RVA`) instead of param count only (`TakeDamage_1_RVA`).
- **README** — updated with new output files, troubleshooting for detailed init errors, and v1.2.0 examples.
- **Legacy spoofer tried first** — more compatible with existing games before attempting the new v2 stub.

### Fixed
- **API scan cache** — reset on each init attempt so early failures don't permanently block retries.
- **Duplicate console output** — removed double logging from `printf` + `std::cout`.

---

## [v1.1.0] - 2026-06-24

### Added
- Multi-format dump output: **C#**, **Rust**, **Python**, and **JSON** in addition to C++.
- `GameDump.cs`, `GameDump.rs`, `GameDump.py`, `GameDump.json`, and `README.txt` in the output folder.
- Project renamed from Testicle to **IL2CPPDumper**.

### Changed
- `dumper.cpp` refactored to emit all five formats from a single dump pass.
- Console progress logging for each image during the dump.

---

## [v1.0.0]

### Added
- Initial release: runtime IL2CPP dumper injected as a DLL.
- `GameDump.hpp` with nested namespaces, field offsets, method RVAs, and C# signature comments.
- `rrid.hpp` IL2CPP metadata reader with return-address spoofing.
- Support for nested types, enums, static fields, and method overload disambiguation.

IL2CPP Dumper static tool (v1.5.0)

Contents:
  dumper-x64.exe  — 64-bit GameAssembly
  dumper-x86.exe  — 32-bit GameAssembly

GUI: double-click the matching exe
  - drop GameAssembly.dll + global-metadata.dat
  - toggle output formats / Unity-System / native Bodies
  - Set Folder for dump output
  - Updates (GitHub releases)

CLI:
  dumper-x64.exe --cli <GameAssembly.dll> <global-metadata.dat> [output-dir]
  dumper-x64.exe --cli <game-folder>

Encrypted/packed metadata needs the runtime DLL instead.

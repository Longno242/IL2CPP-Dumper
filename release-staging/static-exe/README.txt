IL2CPP Dumper static tool (v1.5.0)

GUI: double-click dumper.exe
  - drop GameAssembly.dll + global-metadata.dat
  - toggle output formats / Unity-System / native Bodies
  - Set Folder for dump output
  - Updates (GitHub releases)

CLI:
  dumper.exe --cli <GameAssembly.dll> <global-metadata.dat> [output-dir]
  dumper.exe --cli <game-folder>

Use the x86 build for 32-bit GameAssembly, x64 for 64-bit.
Encrypted/packed metadata needs the runtime DLL instead.

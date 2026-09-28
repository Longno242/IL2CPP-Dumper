IL2CPP Dumper static tool (v1.4.2)

GUI: double-click dumper.exe
  - drop GameAssembly.dll + global-metadata.dat
  - Start Dump
  - Updates (GitHub releases)

CLI:
  dumper.exe --cli <GameAssembly.dll> <global-metadata.dat> [output-dir]
  dumper.exe --cli <game-folder>

Encrypted/packed metadata needs the runtime DLL instead.

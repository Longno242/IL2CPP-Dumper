IL2CPP Dumper static tool (v1.4.1)

GUI: double-click dumper.exe
  - drop GameAssembly.dll + global-metadata.dat
  - Start Dump
  - Check Update (GitHub releases)

CLI:
  dumper.exe --cli <GameAssembly.dll> <global-metadata.dat> [output-dir]
  dumper.exe --cli <game-folder>

Encrypted/packed metadata needs the runtime DLL instead.

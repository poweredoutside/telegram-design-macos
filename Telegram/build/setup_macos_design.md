# Telega Desktop installer

Build the custom per-user Windows x64 installer with Inno Setup 6.7.4:

```powershell
& 'C:\path\to\ISCC.exe' 'Telegram\build\setup_macos_design.iss'
```

The script reads the existing Release build from `out\Release` and writes
`out\setup\Telega-Desktop-Setup-x64.exe`. Use `/OPATH` to change the output
directory or `/DPayloadDir=PATH` to select another build. Setup reads the app
version from the selected executable.

Only the client, Updater.exe, the D3D compiler DLL, licensing files and the user
guide are packaged. Account data, logs and PDB files are excluded.
Setup creates an empty TelegramForcePortable folder for separate account data.
Uninstall preserves that folder and its contents.

This installer has a separate AppId from the official app. An upgrade from
Telegram macOS Design keeps the previous installation folder and account data,
and creates Telega Desktop shortcuts. Setup closes only the copy being upgraded.
The installer does not register Telegram URL protocols and is unsigned.

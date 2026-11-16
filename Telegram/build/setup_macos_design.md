# Telegram macOS Design installer

Build the custom per-user Windows x64 installer with Inno Setup 6.7.4:

```powershell
& 'C:\path\to\ISCC.exe' 'Telegram\build\setup_macos_design.iss'
```

The script reads the existing Debug build from `out\Debug` and writes
`out\setup\Telegram-macOS-Design-Setup-x64.exe`. Use `/OPATH` to change
the output directory or `/DPayloadDir=PATH` to select another Debug payload.
The app version is defined at the top of the script and must match the payload.

Only Telegram.exe, the D3D compiler DLL, licensing files and the user guide
are packaged. Account data, logs, PDB files and Updater.exe are excluded.
Setup creates an empty TelegramForcePortable folder for separate account data.
Uninstall preserves that folder and its contents.

This installer has a separate AppId, installation folder and shortcut names.
It does not register Telegram URL protocols. It does not close running apps.
Close the custom app before updating or uninstalling it.
The installer is unsigned and contains a test Debug build.

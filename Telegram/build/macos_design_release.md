# Telega Desktop for Windows x64

Build a Release application using the Windows x64 Release dependencies and the
fork's public update trust bundle. The executable built with
`TDESKTOP_API_TEST=ON` is for local testing
only. Configure with `DESKTOP_APP_SPECIAL_TARGET=win64`,
`DESKTOP_APP_ENABLE_LTO=OFF`, `DESKTOP_APP_DISABLE_AUTOUPDATE=OFF`,
`TDESKTOP_UPDATE_CHANNEL=stable`, and
`TDESKTOP_MACOS_DESIGN_UPDATE_KEYS_DIR=<public bundle directory>`.

For the Telega installer, set `TDESKTOP_API_RUNTIME=OFF` and
`TDESKTOP_API_TEST=OFF`. Leave the cache entries `TDESKTOP_API_ID=0` and
`TDESKTOP_API_HASH=` and supply the application credentials with the private
`TDESKTOP_RELEASE_API_ID` and `TDESKTOP_RELEASE_API_HASH` environment variables
of the build process. The installed client opens the normal sign-in screen.

The resulting binary contains the application credentials and they can be
extracted from it. Do not publish the private build tree, generated sources,
credentials, or logs. Before pushing or tagging source, scan tracked content
and the commits to be published for both credential values.

The public bundle is `root-public.pem` (Ed25519), `manifest.min.json` (V2
trust manifest) and `manifest.sig` (raw Ed25519 signature over the exact
JSON bytes). Keep the separate root and release private keys outside the
repository and the package. Preserve this trust root across releases; changing
it requires migrating installed clients. Back up the keys securely.

```powershell
cmake --build out --config Release --target Telegram
ISCC.exe Telegram/build/setup_macos_design.iss
```

Setup reads `out/Release` by default and writes the installer directly to
`out/setup-release/Telega-Desktop-Setup-x64.exe`.
It installs per user and keeps `TelegramForcePortable` separate from the
official app. The setup language can be chosen on every run. During an upgrade,
Setup asks the matching installed client to quit normally and checks that its
files are writable before installing. If Setup still reports a locked file,
exit that copy from the tray and retry. Setup logs its result to a `Setup Log`
file in `%TEMP%`. The unchecked Tg Ws Proxy task opts in to starting the bundled
loopback helper with Telegram. Connect from the helper tray on first use;
Setup does not replace existing proxy settings. Do not distribute `tdata`,
test profiles, signing keys or debug symbols with any artifact.

## Publish an update

1. Increase `Telegram/build/telega_version` before building every new Telega
   release. Use `MAJOR.MINOR.PATCH` (for example, `1.0.0` → `1.0.1` for a fix).
   Do not change the upstream Telegram version files for a Telega-only release.
   The fork maps the version to `10000000 + MAJOR * 1000000 + MINOR * 1000 + PATCH`
   so the first numbered Telega build is newer than the old `7004004` builds.
2. In a clean packaging directory, stage `Telegram.exe`, `Updater.exe` and
   `modules/x64/d3d/d3dcompiler_47.dll` from the Release output. Use the Packer
   built in the same tree; it defaults to that build's Telega update number:

   ```powershell
   & $packer -channel stable -target win64 `
     -keys-loc $publicTrust -local-key $releasePrivateKey `
     -local-key-id design-release-1 -path Telegram.exe -path Updater.exe `
     -path modules/x64/d3d/d3dcompiler_47.dll
   ```

   Stage only application files. With Packer, the first `-path` establishes
   the common root for paths inside the archive, so keep `Telegram.exe` first.
   The release key ID must match a key authorized for the stable channel.
3. Attach the generated `td-update-win-x64-<TelegaUpdateVersion>` file **without
   renaming it** to a public, non-prerelease GitHub Release tagged `v<MAJOR.MINOR.PATCH>`
   (for example `v1.0.0`) and published as latest in
   `poweredoutside/telegram-design-macos`. Attach the Setup file
   for new installations. The Setup file is not an automatic-update payload.
4. On a separate installation, check from the preceding fork build that the
   package downloads, installs and restarts into the newer version, preserving
   the account profile, design and installer proxy choice.

The fork reads only the project's GitHub release feed. The local updater
understands the standard Telegram V2 package format but accepts only updates
signed by the fork and stages them in `td-design-updates`, separate from
Telegram's official update cache. To bring in an upstream Telegram version,
merge it into the fork, rebuild and publish a new signed fork package.

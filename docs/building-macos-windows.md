# Building Fairfax for macOS and Windows

Fairfax is an Omarchy/Linux app first. The macOS and Windows builds are best
effort: they build and run, but they have not had the same testing, and
nothing in them changes how the Linux build behaves. There is no automatic
CI for them; you run the scripts below on your own machines (or start the
manual GitHub workflow, see the end of this page).

Supported systems are deliberately recent: **Apple Silicon Macs on macOS 14.4
(Sonoma) or later**, and **Windows 11, 64-bit**. Intel Macs, older macOS and
Windows 10 are not supported; Windows 10 may still run it.

Signing is optional. Without certificates the scripts produce unsigned
builds.

## macOS (14.4 or later, Apple Silicon)

### Prerequisites

```sh
xcode-select --install          # Xcode command line tools
brew install qt                 # Qt 6.8 or later
```

Or use the Qt online installer and point the script at its macOS kit:
`export QTDIR=$HOME/Qt/6.12.0/macos`. For a nicer icon, `brew install librsvg`.

### Build

```sh
bin/build-macos
```

It builds into `build-macos/`, runs the unit tests and `--demo --selftest`,
makes `Fairfax.app` (bundle id `casa.lacy.fairfax`, the `fairfax://` URL
scheme), runs `macdeployqt` and makes `Fairfax-<version>.dmg`. It stops at
the first failure and prints the path of its log. Options, as environment
variables:

| Variable | Effect |
|---|---|
| `FAIRFAX_UNIVERSAL=1` | for your own builds only (Intel Macs are not supported): ask for an arm64 + x86_64 binary; if the Qt in use is not universal (Homebrew's is native only, the Qt online installer's is universal) the script builds natively instead |
| `FAIRFAX_SKIP_TESTS=1` | skip the tests and the selftest |
| `FAIRFAX_SKIP_DMG=1` | stop after `Fairfax.app` |
| `FAIRFAX_JOBS=4` | compile jobs |

The `frfx` terminal shorthand is the same program started under another
name. The script prints the command; it does not run it:

```sh
sudo ln -s /path/to/build-macos/Fairfax.app/Contents/MacOS/Fairfax /usr/local/bin/frfx
```

### Signing and notarising (optional)

You need an Apple Developer ID Application certificate in your login
keychain. Find its name with `security find-identity -v -p codesigning`.

```sh
export FAIRFAX_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
bin/build-macos
```

This signs with the hardened runtime and `pkgbuild/macos/fairfax.entitlements`.
To notarise as well, store credentials once (an app-specific password from
appleid.apple.com), then name the profile:

```sh
xcrun notarytool store-credentials fairfax-notary --apple-id you@example.com --team-id TEAMID
export FAIRFAX_NOTARY_PROFILE=fairfax-notary
bin/build-macos
```

The script submits the DMG with `notarytool submit --wait` and staples the
ticket. Unsigned builds open after right-click > Open, or
`xattr -dr com.apple.quarantine Fairfax.app`.

## Windows 11 (x64)

### Prerequisites

1. Visual Studio 2022 Build Tools with the C++ workload:
   `winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"`
2. Qt 6.8 or later for MSVC 2022 64-bit, from the Qt online installer, or
   `pip install aqtinstall` then `aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -O C:\Qt`.
   Then `$env:QTDIR = "C:\Qt\6.10.3\msvc2022_64"` (or have `qmake6` on PATH).
   (aqtinstall cannot fetch Windows Qt 6.11 or later yet; the Qt online
   installer can.)
3. zlib, for importing zipped backups: with vcpkg,
   `vcpkg install zlib:x64-windows-static-md`, then
   `$env:ZLIB_DIR = "<vcpkg>\installed\x64-windows-static-md"`. Without it the
   build works, but a zipped backup has to be unzipped before importing.
4. Optional: Inno Setup for the installer (`winget install JRSoftware.InnoSetup`),
   `jom` for parallel compiles (it ships with Qt Creator), and ImageMagick
   (otherwise the app draws its own icon).

### Build

From a normal PowerShell (no administrator rights needed):

```powershell
.\bin\build-windows.ps1
```

The script finds Visual Studio through `vswhere` and sets up the compiler
environment itself. It builds into `build-windows\`, runs the unit tests and
`--demo --selftest`, runs `windeployqt`, and leaves:

- `build-windows\Fairfax\`, the deployed folder (with `fairfax.exe` and a copy,
  `frfx.exe`, for the terminal shorthand);
- `Fairfax-<version>-windows-x64.zip` of that folder;
- `Fairfax-<version>-setup.exe` when Inno Setup is found. It installs per user,
  adds a Start-menu entry, registers `fairfax://` under HKCU, and offers
  checkboxes for starting at sign-in (alarms need Fairfax running), adding
  `frfx` to PATH, and a Quick Entry shortcut. The uninstaller removes the
  registry keys.

Options: `-SkipTests`, `-SkipInstaller`, or the variables `FAIRFAX_SKIP_TESTS=1`
and `FAIRFAX_SKIP_INSTALLER=1`; `FAIRFAX_JOBS` sets jom's job count.

### Signing (optional)

With a code-signing certificate in a `.pfx` file:

```powershell
$env:FAIRFAX_SIGN_CERT = "C:\keys\codesign.pfx"
$env:FAIRFAX_SIGN_PASSWORD = "..."
.\bin\build-windows.ps1
```

Or, for a certificate in the Windows certificate store, set
`FAIRFAX_SIGN_THUMBPRINT` instead. `signtool` signs `fairfax.exe`, `frfx.exe`
and the installer with SHA-256 and a timestamp (`FAIRFAX_TIMESTAMP_URL`,
default `http://timestamp.digicert.com`).

## Running the build on GitHub (manual only)

`.github/workflows/build-macos-windows.yml` builds on GitHub's runners. It
has no automatic trigger: it only ever runs when you start it.

Start a run from the Actions tab: "Build macOS/Windows" > Run workflow, or

```sh
gh workflow run build-macos-windows.yml -f platforms=windows
```

Inputs:

- `platforms`: `windows` (the default, the cheapest), `macos` or `both`.
- `run_tests`: run the unit tests and the selftest (default on).
- `make_installers`: also make the DMG (macOS) and the Inno Setup installer
  (Windows). Off by default; without it you get `Fairfax-app.zip` and the
  Windows zip.

Roughly what a run costs: Windows about 15 to 25 minutes, macOS about 10 to 20
minutes with a warm Qt cache. Minutes on macOS runners count ten times on
private repositories, so a macOS run is billed like two to three hours of
Linux. Each job stops after 45 minutes, and starting a new run on the same ref
cancels the previous one.

Artifacts appear at the bottom of the run's page, as `fairfax-macos` and
`fairfax-windows`, and are deleted after 5 days. They include the build logs.

The workflow's actions are pinned to full commit SHAs, with the release in a
comment next to each. To update one, resolve the new release to its commit
(for example `gh api repos/actions/checkout/commits/v4.4.0 --jq .sha`) and
change both.

Signing in the workflow happens only when these repository secrets exist
(Settings > Secrets and variables > Actions); without them it builds unsigned:

| Secret | Content |
|---|---|
| `MACOS_CERT_P12` | base64 of the Developer ID Application certificate exported as .p12 (`base64 -i cert.p12`) |
| `MACOS_CERT_PASSWORD` | the .p12 password |
| `MACOS_SIGN_IDENTITY` | the identity name, as in `FAIRFAX_SIGN_IDENTITY` |
| `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD` | notarisation (with `make_installers`) |
| `WINDOWS_CERT_PFX` | base64 of the .pfx (`[Convert]::ToBase64String([IO.File]::ReadAllBytes("codesign.pfx"))`) |
| `WINDOWS_CERT_PASSWORD` | the .pfx password |

## Known gaps

- No global hotkey for Quick Entry. On macOS, bind `fairfax --quick` (or
  `open -a Fairfax --args --quick`) in Shortcuts.app or Raycast. On Windows,
  give a shortcut to `fairfax.exe --quick` a hotkey in its properties; the
  installer can create one.
- Alarm notifications are tray notifications only: no Complete or Snooze
  buttons.
- No audio-note recording outside Linux.
- No Waybar or Omarchy integration (`fairfax setup omarchy`, `--waybar` and
  "fit into a free third" are Linux only).
- The Dock badge and the login item (SMAppService) are not done on macOS.
- Windows on arm64 has not been tried.
- Importing a zipped task-database backup needs zlib. macOS has it; on Windows the
  build leaves it out unless the ZLIB_DIR environment variable names a prefix
  with include/ and lib/, and the importer then asks for the backup to be unzipped
  first.
- No PowerShell completions for `frfx`.
- Secrets (account passwords and tokens) are kept in the Windows Credential
  Manager, as generic credentials named `fairfax:<account>`; a secret saved by
  an older build as a file moves there the first time it is read. On macOS they
  go in the Keychain through the `security` tool, which takes the secret on its
  command line, so another local user could glimpse it in the process list
  while it is being saved; a later version should use the Security framework.

## When something fails

The script stops at the first failure and prints `Log: <path>`
(`build-macos/build.log` or `build-windows\build.log`). Send that file back,
along with `tests.log` or `selftest.log` from the same folder if they exist,
and the output of `qmake -query QT_VERSION`.

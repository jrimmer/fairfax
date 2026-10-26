<#
.SYNOPSIS
Builds Fairfax for Windows 10/11 x64: a deployed folder, a zip, and an installer when Inno Setup is present.
Best effort; see docs/building-macos-windows.md.

.DESCRIPTION
Output goes to build-windows\ . Re-runnable; needs no administrator rights.

Optional environment variables:
  QTDIR                      Qt kit for MSVC 2022 64-bit, e.g. C:\Qt\6.8.0\msvc2022_64 (or have qmake6 on PATH)
  FAIRFAX_SIGN_CERT          path to a .pfx file, with FAIRFAX_SIGN_PASSWORD
  FAIRFAX_SIGN_THUMBPRINT    thumbprint of a certificate in the certificate store (instead of the .pfx)
  FAIRFAX_TIMESTAMP_URL      timestamp server (default http://timestamp.digicert.com)
  FAIRFAX_SKIP_TESTS=1       skip the unit tests and the selftest
  FAIRFAX_SKIP_INSTALLER=1   leave the zip only
  FAIRFAX_JOBS               parallel compile jobs when jom is available (default: all cores)
#>
[CmdletBinding()]
param(
    [switch]$SkipTests,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Root 'build-windows'
$Log = Join-Path $Out 'build.log'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
Set-Content -Path $Log -Value ''

if ($env:FAIRFAX_SKIP_TESTS -eq '1') { $SkipTests = $true }
if ($env:FAIRFAX_SKIP_INSTALLER -eq '1') { $SkipInstaller = $true }

function Say($text) { Write-Host "==> $text" }

function Fail($text) {
    Write-Host ''
    Write-Host "FAILED: $text" -ForegroundColor Red
    Write-Host "Log: $Log"
    Write-Host 'Send that file back if you need help.'
    exit 1
}

# Runs a program with stdout and stderr going to a file; returns its exit code.
function Invoke-Logged($exe, [string[]]$arguments, $file) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $exe @arguments 2>&1 | ForEach-Object { "$_" } | Add-Content -Path $file
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
}

# Runs a program with its output in the log; shows the tail of the log on failure.
function Step($what, $exe, [string[]]$arguments) {
    Say $what
    if ((Invoke-Logged $exe $arguments $Log) -ne 0) {
        Get-Content $Log -Tail 25 | Write-Host
        Fail $what
    }
}

# Prerequisites
$missing = @()

# MSVC environment: find vcvars64.bat through vswhere and import its variables.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = $null
if (Test-Path $vswhere) {
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($install) {
        $candidate = Join-Path $install.Trim() 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) { $vcvars = $candidate }
    }
}
if (-not $vcvars) {
    $missing += 'Visual Studio 2022 Build Tools with the "Desktop development with C++" workload:'
    $missing += '    winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"'
}

# Qt: QTDIR, else qmake6/qmake on PATH.
$qmake = $null
if ($env:QTDIR -and (Test-Path (Join-Path $env:QTDIR 'bin\qmake.exe'))) {
    $qmake = Join-Path $env:QTDIR 'bin\qmake.exe'
} else {
    foreach ($name in 'qmake6', 'qmake') {
        $found = Get-Command $name -ErrorAction SilentlyContinue
        if ($found) { $qmake = $found.Source; break }
    }
}
if (-not $qmake) {
    $missing += 'Qt 6.8 or later for MSVC 2022 64-bit (with Qt Quick Controls 2 and Qt Shader Tools), either'
    $missing += '  from the Qt online installer (https://www.qt.io/download-qt-installer), then:  $env:QTDIR = "C:\Qt\6.8.0\msvc2022_64"'
    $missing += '  or with aqtinstall:  pip install aqtinstall; aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt'
}
if ($missing.Count -gt 0) {
    Write-Host 'Missing prerequisites. Install them and run this script again:' -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  $_" }
    exit 1
}

# Import the MSVC environment into this process.
Say "Setting up MSVC from $vcvars"
$envDump = cmd /c "`"$vcvars`" >nul && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

$QtBin = Split-Path -Parent $qmake
$env:PATH = "$QtBin;$env:PATH"
$windeploy = Join-Path $QtBin 'windeployqt.exe'
if (-not (Test-Path $windeploy)) { Fail "windeployqt.exe not found next to $qmake" }
$qtVersion = (& $qmake -query QT_VERSION).Trim()
if ([version]$qtVersion -lt [version]'6.8.0') { Fail "Qt $qtVersion found; Qt 6.8 or later is needed" }
Say "Qt $qtVersion at $QtBin"

$proText = Get-Content (Join-Path $Root 'fairfax.pro') -Raw
if ($proText -notmatch '(?m)^\s*VERSION\s*=\s*(\S+)') { Fail 'no VERSION in fairfax.pro' }
$Version = $Matches[1]

# Build tool: jom if present, else nmake.
$make = Get-Command jom -ErrorAction SilentlyContinue
if ($make) {
    $jobs = if ($env:FAIRFAX_JOBS) { $env:FAIRFAX_JOBS } else { [Environment]::ProcessorCount }
    $makeArgs = @('/J', "$jobs")
} else {
    $make = Get-Command nmake -ErrorAction SilentlyContinue
    if (-not $make) { Fail 'neither jom nor nmake was found; install the Visual Studio C++ build tools' }
    $makeArgs = @()
    Say 'jom not found; using nmake (slower, one job at a time). jom comes with Qt Creator.'
}

# The first signing mechanism that is configured.
$signing = 'none'
if ($env:FAIRFAX_SIGN_CERT -and $env:FAIRFAX_SIGN_PASSWORD) { $signing = 'pfx' }
elseif ($env:FAIRFAX_SIGN_THUMBPRINT) { $signing = 'thumbprint' }
$timestamp = if ($env:FAIRFAX_TIMESTAMP_URL) { $env:FAIRFAX_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
$signtool = $null
if ($signing -ne 'none') {
    $signtool = Get-Command signtool -ErrorAction SilentlyContinue
    if (-not $signtool) {
        $kit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
        if ($kit) { $signtool = $kit.FullName }
    } else { $signtool = $signtool.Source }
    if (-not $signtool) { Fail 'signing was requested but signtool.exe was not found (it comes with the Windows SDK)' }
}
function Sign($file) {
    if ($signing -eq 'none') { return }
    $signArgs = @('sign', '/fd', 'SHA256', '/tr', $timestamp, '/td', 'SHA256')
    if ($signing -eq 'pfx') { $signArgs += @('/f', $env:FAIRFAX_SIGN_CERT, '/p', $env:FAIRFAX_SIGN_PASSWORD) }
    else { $signArgs += @('/sha1', $env:FAIRFAX_SIGN_THUMBPRINT) }
    $signArgs += $file
    Step "Signing $(Split-Path -Leaf $file)" $signtool $signArgs
}

# Build the program.
$BuildDir = Join-Path $Out 'obj'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Push-Location $BuildDir
try {
    # The icon is made from the SVG by a first, icon-less build of the app itself.
    $ico = Join-Path $Root 'pkgbuild\windows\fairfax.ico'
    if (-not (Test-Path $ico)) {
        $magick = Get-Command magick -ErrorAction SilentlyContinue
        if ($magick) {
            Step 'Making fairfax.ico with ImageMagick' $magick.Source @('-background', 'none', (Join-Path $Root 'pkgbuild\fairfax.svg'), '-define', 'icon:auto-resize=256,128,64,48,32,16', $ico)
        } else {
            Say 'No fairfax.ico yet: the first build is icon-less, then the app draws the icon and the build is repeated.'
        }
    }
    Step 'Running qmake' $qmake @((Join-Path $Root 'fairfax.pro'), '-spec', 'win32-msvc', 'CONFIG+=release')
    Step 'Compiling' $make.Source $makeArgs
    $exe = Join-Path $BuildDir 'release\fairfax.exe'
    if (-not (Test-Path $exe)) { $exe = Join-Path $BuildDir 'fairfax.exe' }
    if (-not (Test-Path $exe)) { Fail 'the build did not produce fairfax.exe' }
    if (-not (Test-Path $ico)) {
        Step 'Drawing fairfax.ico' $exe @('--render-icon', $ico)
        if (-not (Test-Path $ico)) { Fail 'the app did not write fairfax.ico' }
        Step 'Running qmake again, with the icon' $qmake @((Join-Path $Root 'fairfax.pro'), '-spec', 'win32-msvc', 'CONFIG+=release')
        Step 'Compiling again' $make.Source $makeArgs
    }
} finally { Pop-Location }

# Tests
$Result = 'tests skipped'
if (-not $SkipTests) {
    $TestDir = Join-Path $Out 'tests'
    New-Item -ItemType Directory -Force -Path $TestDir | Out-Null
    Push-Location $TestDir
    try {
        Step 'Running qmake for the unit tests' $qmake @((Join-Path $Root 'tests\fairfax_tests.pro'), '-spec', 'win32-msvc', 'CONFIG+=release')
        Step 'Compiling the unit tests' $make.Source $makeArgs
        $testExe = Get-ChildItem -Path $TestDir -Recurse -Filter fairfax_tests.exe | Select-Object -First 1
        if (-not $testExe) { Fail 'the build did not produce fairfax_tests.exe' }
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:FAIRFAX_KEYRING = 'memory'
        Say 'Running the unit tests'
        $testLog = Join-Path $Out 'tests.log'
        Set-Content -Path $testLog -Value ''
        $testCode = Invoke-Logged $testExe.FullName @() $testLog
        Add-Content $Log (Get-Content $testLog)
        if ($testCode -ne 0) {
            Get-Content $testLog -Tail 30 | Write-Host
            # A Windows status code says why a run printed nothing: 0xC0000135 is a
            # missing DLL, 0xC0000005 a crash.
            Say ('unit tests exited with code {0} (0x{1:X8})' -f $testCode, $testCode)
            Fail "unit tests failed (full output in $testLog)"
        }
        $totals = Select-String -Path $testLog -Pattern '^Totals:' | Select-Object -Last 1
        $Result = 'unit tests passed' + $(if ($totals) { " ($($totals.Line))" } else { '' })
    } finally {
        Pop-Location
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        Remove-Item Env:FAIRFAX_KEYRING -ErrorAction SilentlyContinue
    }
}

# Deploy: copy the exe, then let windeployqt add Qt, QML modules and plugins.
$Deploy = Join-Path $Out 'Fairfax'
if (Test-Path $Deploy) { Remove-Item -Recurse -Force $Deploy }
New-Item -ItemType Directory -Force -Path $Deploy | Out-Null
Copy-Item $exe (Join-Path $Deploy 'fairfax.exe')
# frfx.exe is a copy of the same program: it acts as the terminal shorthand under that name.
Copy-Item $exe (Join-Path $Deploy 'frfx.exe')
Copy-Item (Join-Path $Root 'LICENSE') (Join-Path $Deploy 'LICENSE.txt')
Step 'Running windeployqt' $windeploy @('--release', '--qmldir', (Join-Path $Root 'src\qml'), '--no-translations', (Join-Path $Deploy 'fairfax.exe'))

if (-not $SkipTests) {
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:FAIRFAX_KEYRING = 'memory'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    try {
        Say 'Running --demo --selftest'
        $selfLog = Join-Path $Out 'selftest.log'
        Set-Content -Path $selfLog -Value ''
        $selfCode = Invoke-Logged (Join-Path $Deploy 'fairfax.exe') @('--demo', '--selftest') $selfLog
        Add-Content $Log (Get-Content $selfLog)
        if ($selfCode -ne 0) {
            Get-Content $selfLog -Tail 30 | Write-Host
            Fail "selftest failed (full output in $selfLog)"
        }
        $warn = Select-String -Path $selfLog -Pattern 'QML warning' | Select-Object -Last 1
        $Result += '; selftest: ' + $(if ($warn) { $warn.Line.Trim() } else { 'ok' })
    } finally {
        Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        Remove-Item Env:FAIRFAX_KEYRING -ErrorAction SilentlyContinue
        Remove-Item Env:QT_FORCE_STDERR_LOGGING -ErrorAction SilentlyContinue
    }
}

Sign (Join-Path $Deploy 'fairfax.exe')
Sign (Join-Path $Deploy 'frfx.exe')

# Zip of the deployed folder, always.
$Zip = Join-Path $Out "Fairfax-$Version-windows-x64.zip"
if (Test-Path $Zip) { Remove-Item -Force $Zip }
Say 'Making the zip'
Compress-Archive -Path (Join-Path $Deploy '*') -DestinationPath $Zip

# Installer, when Inno Setup is available.
$Installer = $null
if (-not $SkipInstaller) {
    $iscc = Get-Command iscc -ErrorAction SilentlyContinue
    if (-not $iscc) {
        $guess = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($guess) { $iscc = Get-Command $guess }
    }
    if ($iscc) {
        $issArgs = @("/DAppVersion=$Version", "/DSourceDir=$Deploy", "/DOutputDir=$Out")
        $ico = Join-Path $Root 'pkgbuild\windows\fairfax.ico'
        if (Test-Path $ico) { $issArgs += "/DSetupIcon=$ico" }
        $issArgs += (Join-Path $Root 'pkgbuild\windows\fairfax.iss')
        Step 'Building the installer' $iscc.Source $issArgs
        $Installer = Join-Path $Out "Fairfax-$Version-setup.exe"
        if (-not (Test-Path $Installer)) { Fail "Inno Setup did not produce $Installer" }
        Sign $Installer
    } else {
        Say 'Inno Setup not found; only the zip is made. For an installer: winget install JRSoftware.InnoSetup'
    }
}

Write-Host ''
Write-Host "Folder:    $Deploy"
Write-Host "Zip:       $Zip"
if ($Installer) { Write-Host "Installer: $Installer" }
Write-Host "Log:       $Log"
Write-Host "Result:    $Result; signing: $signing"

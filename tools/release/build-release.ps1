<#
Builds a release of Chatterino by Mist1X: the installer, a portable zip and
SHA256SUMS.txt.

  .\build-release.ps1 -Version 1.0.0 -Exe <built Leafyrino.exe> -Runtime <folder with the Qt/OpenSSL/VC runtime> -Out <folder>

The runtime folder is a working installation (or a fresh deployment); only the
files on the allowlist below are taken from it, never a profile (Settings,
Cache, Logs, ...). The result is checked for profile files before packing.
#>
param(
    [Parameter(Mandatory)] [string] $Version,
    [Parameter(Mandatory)] [string] $Exe,
    [Parameter(Mandatory)] [string] $Runtime,
    [Parameter(Mandatory)] [string] $Out,
    [string] $Iscc = 'D:\devtools\InnoSetup6\ISCC.exe'
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Resolve-Path (Join-Path $here '..\..')
$staging = Join-Path $Out 'staging'
$dist = Join-Path $Out 'dist'

if (Test-Path $Out) { throw "Output folder already exists: $Out" }
# The program compares itself with the releases by this number.
$versionHeader = Join-Path $PSScriptRoot '..\..\src\common\Mist1XVersion.hpp'
$inSource = [regex]::Match((Get-Content $versionHeader -Raw), 'MIST1X_VERSION "([^"]+)"').Groups[1].Value
if ($inSource -ne $Version) { throw "MIST1X_VERSION in src/common/Mist1XVersion.hpp is '$inSource', not '$Version'. Change it and rebuild first." }
New-Item -ItemType Directory -Force $staging, $dist | Out-Null

# 1. Program: the built exe and the runtime it needs (allowlist).
Copy-Item $Exe (Join-Path $staging 'Leafyrino.exe')
$runtimeFiles = @(
    'Qt6Charts.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'Qt6OpenGL.dll',
    'Qt6OpenGLWidgets.dll', 'Qt6Svg.dll', 'Qt6Widgets.dll',
    'D3Dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll',
    'libcrypto-3-x64.dll', 'libssl-3-x64.dll',
    'concrt140.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
    'msvcp140_atomic_wait.dll', 'msvcp140_codecvt_ids.dll', 'vccorlib140.dll',
    'vcruntime140.dll', 'vcruntime140_1.dll', 'vcruntime140_threads.dll',
    'modes'
)
foreach ($file in $runtimeFiles) { Copy-Item (Join-Path $Runtime $file) $staging }
$runtimeDirs = @('generic', 'iconengines', 'imageformats', 'networkinformation',
                 'platforms', 'styles', 'tls', 'crashpad')
foreach ($dir in $runtimeDirs) { Copy-Item (Join-Path $Runtime $dir) $staging -Recurse }

# 2. Icon, licences, the guide, release notes, the extra theme.
Copy-Item (Join-Path $repo 'resources\icon.ico') (Join-Path $staging 'Chatterino-by-Mist1X.ico')
Copy-Item (Join-Path $repo 'LICENSE') (Join-Path $staging 'LICENSE.txt')
Copy-Item (Join-Path $repo 'LICENSES') $staging -Recurse
# licenses of the bundled libraries, fonts and data (the same texts as in About)
Copy-Item (Join-Path $repo 'resources\licenses') (Join-Path $staging 'LICENSES\third-party') -Recurse
Copy-Item (Join-Path $repo 'CREDITS.md') (Join-Path $staging 'Авторы и лицензии.txt')
Copy-Item (Join-Path $here 'Руководство.html') $staging
Copy-Item (Join-Path $here 'Что нового.txt') $staging
New-Item -ItemType Directory (Join-Path $staging 'Themes') | Out-Null
Copy-Item (Join-Path $here 'Mist-Dark.json') (Join-Path $staging 'Themes')

# 3. Nothing from a profile may be in the package.
$forbidden = @('Settings', 'Cache', 'Logs', 'Misc', 'Plugins', 'ProfileAvatars', 'IPC', 'Crashes')
foreach ($name in $forbidden) {
    if (Test-Path (Join-Path $staging $name)) { throw "Profile folder in the package: $name" }
}
$bad = Get-ChildItem $staging -Recurse -File | Where-Object {
    $_.Extension -in '.json', '.txt', '.ini', '.log' -and
    (Select-String -Path $_.FullName -Pattern 'oauth:|"oauthToken"|access_token' -Quiet)
}
if ($bad) { throw "Token-like text in: $($bad.FullName -join ', ')" }

# 4. Installer.
& $Iscc "/DAppVersion=$Version" "/DStagingDir=$staging" "/DOutputDir=$dist" (Join-Path $here 'ChatterinoByMist1X.iss') | Out-Host
if ($LASTEXITCODE -ne 0) { throw "ISCC failed: $LASTEXITCODE" }

# 5. Portable zip: the same files in one folder.
$portableName = "ChatterinoByMist1X-$Version-portable"
$portable = Join-Path $Out $portableName
Copy-Item $staging $portable -Recurse
Compress-Archive -Path $portable -DestinationPath (Join-Path $dist "$portableName.zip") -CompressionLevel Optimal
Remove-Item $portable -Recurse -Force

# 6. Checksums.
$sums = Get-ChildItem $dist -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
}
[IO.File]::WriteAllLines((Join-Path $dist 'SHA256SUMS.txt'), $sums)
Get-ChildItem $dist | Select-Object Name, Length | Format-Table -AutoSize | Out-Host

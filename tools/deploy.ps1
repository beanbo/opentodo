#requires -Version 5.0
<#
    Builds a portable distribution of OpenTodo for sharing with another
    Windows user.

    Steps:
      1. Copy OpenTodo.exe from the shadow build directory.
      2. Run windeployqt to bring in Qt DLLs and QML plugins.
      3. Copy MinGW runtime DLLs (libgcc_s_seh-1, libstdc++-6, libwinpthread-1).
      4. Zip the payload as deploy/OpenTodo-windows-x64.zip.

    Usage:
        pwsh tools/deploy.ps1
        pwsh tools/deploy.ps1 -Out C:\Some\Path
#>

[CmdletBinding()]
param(
    [string] $Out = "deploy"
)

$ErrorActionPreference = "Stop"

$root      = Split-Path $PSScriptRoot -Parent
# Try the cmake build first, then fall back to Qt Creator's shadow build dir.
$exe       = Join-Path $root "build\release\OpenTodo.exe"
if (-not (Test-Path $exe)) {
    $qtCreatorBuild = Get-ChildItem -Path (Join-Path $root "build") -Directory -Filter "Desktop_Qt*" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($qtCreatorBuild) {
        $exe = Join-Path $qtCreatorBuild.FullName "OpenTodo.exe"
    }
}
$qtBin     = "C:\Qt\6.11.0\mingw_64\bin"
$mingwBin  = "C:\Qt\Tools\mingw1310_64\bin"

if (-not (Test-Path $exe))      { throw "exe not found: $exe -- build first via 'cmake --build build/release'" }
if (-not (Test-Path $qtBin))    { throw "Qt not found at $qtBin" }
if (-not (Test-Path $mingwBin)) { throw "MinGW not found at $mingwBin" }

$absOut    = [System.IO.Path]::GetFullPath((Join-Path $root $Out))
$payload   = Join-Path $absOut "OpenTodo"

if (Test-Path $payload) { Remove-Item $payload -Recurse -Force }
New-Item -ItemType Directory -Path $payload -Force | Out-Null

Copy-Item $exe -Destination $payload

Write-Output "[1/4] windeployqt - Qt DLLs and QML plugins"
$env:PATH = "$qtBin;$mingwBin;$env:PATH"
# windeployqt prints harmless Direct3D warnings to stderr; suspend Stop mode
# while it runs so they don't get treated as errors.
$prev = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& "$qtBin\windeployqt.exe" `
    --qmldir "$root\qml" `
    --no-translations `
    --no-system-d3d-compiler `
    --no-opengl-sw `
    "$payload\OpenTodo.exe" *>$null
$ErrorActionPreference = $prev

Write-Output "[2/4] MinGW runtime"
foreach ($dll in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    Copy-Item "$mingwBin\$dll" -Destination $payload
}

Write-Output "[3/4] README"
$readme = "OpenTodo - portable Windows x64 build.`r`n`r`n" +
          "Run: double-click OpenTodo.exe.`r`n`r`n" +
          "Data location: %APPDATA%\OpenTodo\OpenTodo\OpenTodo\state.json`r`n" +
          "(holds your tasks, projects, theme choice, karma).`r`n`r`n" +
          "Requires Windows 10 1809+ or Windows 11."
Set-Content -Path "$payload\README.txt" -Value $readme -Encoding UTF8

Write-Output "[4/4] zip"
$zip = Join-Path $absOut "OpenTodo-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$payload\*" -DestinationPath $zip -CompressionLevel Optimal

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Output ""
Write-Output "Done."
Write-Output "  Folder: $payload"
Write-Output "  ZIP:    $zip ($size MB)"

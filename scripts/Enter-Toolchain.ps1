<#
.SYNOPSIS
Initializes an x64 MSVC/Windows SDK shell without opening the Visual Studio IDE.
.DESCRIPTION
Dot-source this script before invoking the Windows CMake presets. Installed
CMake/Ninja/LLVM tools are added when they are available in the selected instance.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $installer)) { throw 'Install Visual Studio Build Tools with C++ and the Windows SDK.' }
$installation = & $installer -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'An x64 MSVC toolchain was not found.' }
& (Join-Path $installation 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not $?) { throw 'Could not initialize the developer shell.' }
foreach ($relative in @('Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin', 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja', 'VC/Tools/Llvm/x64/bin')) {
    $toolDirectory = Join-Path $installation $relative
    if (Test-Path -LiteralPath $toolDirectory) { $env:PATH = "$toolDirectory;$env:PATH" }
}

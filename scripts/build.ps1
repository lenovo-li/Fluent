<#
.SYNOPSIS
    Cross-machine compatible MSBuild wrapper script that automatically finds VS installation path
#>
param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [Parameter(Mandatory=$false)]
    [ValidateSet("Static", "Dynamic")]
    [string]$CRTLinkage = "Static"
)

$ErrorActionPreference = "Stop"

# Find the latest VS installation that includes MSBuild. vswhere.exe always
# installs to the x86 Program Files folder, whatever VS version or edition is
# present, so this works on any machine without a hard-coded VS path.
#
# Keep this file ASCII-only. Windows PowerShell 5.1 reads a UTF-8 file without
# a BOM in the system code page (GB2312 on a Chinese-locale machine); a Chinese
# comment then corrupts the bytes that follow it and the next statement parses
# wrong. An earlier version of this script lost its vswhere path exactly that way.
$programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Is Visual Studio installed?"
}

$vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) {
    throw "Visual Studio with MSBuild not found"
}

$msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    throw "MSBuild.exe not found at: $msbuild"
}

Write-Host "Using MSBuild from: $msbuild"

# Find solution file (.slnx preferred, fallback to .sln)
$solution = Get-ChildItem -LiteralPath $PSScriptRoot\.. -Filter *.slnx | Select-Object -First 1
if (-not $solution) {
    $solution = Get-ChildItem -LiteralPath $PSScriptRoot\.. -Filter *.sln | Select-Object -First 1
}
if (-not $solution) {
    throw "No solution file found in repo root."
}

# Execute build
& $msbuild $solution.FullName -p:Configuration=$Configuration -p:Platform=x64 -p:FluentCRTLinkage=$CRTLinkage -verbosity:minimal
exit $LASTEXITCODE

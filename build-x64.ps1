param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Clean
)

$preset = "x64-$($Configuration.ToLower())"

if ($Clean -and (Test-Path build)) {
    Remove-Item -Recurse -Force build
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

$vsWhereArguments = @(
    '-latest'
    '-products'
    '*'
    '-requires'
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'
    '-property'
    'installationPath'
)

$vsInstallPath = & $vswhere @vsWhereArguments

if (-not $vsInstallPath) {
    throw "No Visual Studio installation with the MSVC C++ toolchain was found."
}

Write-Host "VS: $vsInstallPath"

Import-Module "$vsInstallPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"

Enter-VsDevShell `
    -VsInstallPath $vsInstallPath `
    -SkipAutomaticLocation `
    -DevCmdArguments "-arch=x64 -host_arch=x64"

cmake --preset $preset
cmake --build --preset $preset
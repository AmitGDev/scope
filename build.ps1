#Requires -Version 7.4

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Clean
)

# Fail fast like `set -euo pipefail` in build.sh. Native commands (cmake,
# python) ignore $ErrorActionPreference unless the second setting is also on;
# without it a failed configure would still fall through to the build step.
# The #Requires line makes older hosts fail loudly instead of silently
# ignoring the unknown variable.
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true

$preset = "windows-$($Configuration.ToLower())"

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

Write-Host "Configuring project: $preset"

# Read the binary directory from CMake's own report rather than re-deriving it,
# so binaryDir stays defined only in CMakePresets.json (the directory name no
# longer matches the preset name).
cmake --preset $preset | Tee-Object -Variable configureLog

$match = $configureLog |
    Select-String -Pattern '^-- Build files have been written to: (.+)$' |
    Select-Object -Last 1

if (-not $match) {
    throw 'Could not find the binary directory in the CMake output.'
}

$buildDir = $match.Matches[0].Groups[1].Value.Trim()

# ---- compile_commands.json for clangd ----
# Editor support, not a build concern. cmake --preset already generated the
# compilation database in the preset's build directory. clangd only reads a
# single CompilationDatabase directory (see .clangd: CompilationDatabase:
# build), so update the clangd database there. If the repository provides a
# post-processing script, it can modify the database before clangd uses it;
# otherwise, copy it unchanged.
Write-Host "Updating clangd compile_commands.json..."

$postProcessScript = ".github-ext/post_process_compile_commands.py"
$sourceCompileCommands = "$buildDir/compile_commands.json"

if (Test-Path $postProcessScript) {
    python $postProcessScript $sourceCompileCommands "build"
} else {
    cmake -E copy_if_different `
        $sourceCompileCommands `
        "build/compile_commands.json"
}

# TEMPORARY: CMake 4.5.0 emits -std:c++23 for MSVC C++23 targets
# (CMake 4.4.0 emits -std:c++latest for them). clangd falls back
# to an old language mode when given -std:c++23. The clangd copy gets
# -std:c++latest, which worked before; the real build database is not
# touched.
# Remove when clangd handles -std:c++23.
$clangdDatabase = "build/compile_commands.json"
$databaseText = Get-Content -LiteralPath $clangdDatabase -Raw
$fixedText = $databaseText -replace '([-/])std:c\+\+23\b', '$1std:c++latest'

if ($fixedText -ne $databaseText) {
    Set-Content -LiteralPath $clangdDatabase -Value $fixedText `
        -Encoding utf8NoBOM -NoNewline
    $rewritten = ([regex]::Matches($databaseText, '([-/])std:c\+\+23\b')).Count
    Write-Host "Rewrote -std:c++23 to -std:c++latest in $rewritten entries (clang-cl workaround)"
} else {
    Write-Host "No -std:c++23 switch found, nothing to rewrite"
}

Write-Host "Building project..."
cmake --build --preset $preset
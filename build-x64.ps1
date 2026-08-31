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

Write-Host "Configuring project: $preset"
cmake --preset $preset

# ---- compile_commands.json for clangd ----
# Editor support, not a build concern. cmake --preset already generated the
# compilation database in the per-preset build directory. clangd only reads a
# single CompilationDatabase directory (see .clangd: CompilationDatabase:
# build), so update the clangd database there. If the repository provides a
# post-processing script, it can modify the database before clangd uses it;
# otherwise, copy it unchanged.
Write-Host "Updating clangd compile_commands.json..."

$postProcessScript = ".github-ext/post_process_compile_commands.py"
$sourceCompileCommands = "build/$preset/compile_commands.json"

if (Test-Path $postProcessScript) {
    python $postProcessScript $sourceCompileCommands "build"
} else {
    cmake -E copy_if_different `
        $sourceCompileCommands `
        "build/compile_commands.json"
}

Write-Host "Building project..."
cmake --build --preset $preset
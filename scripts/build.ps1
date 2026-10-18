param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) {
    $cmakePath = $cmakeCommand.Source
} else {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswherePath)) { throw 'Install CMake and a C11 compiler or Visual Studio 2022 with C++ tools.' }
    $vsRoot = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
& $cmakePath -S $repoRoot -B (Join-Path $repoRoot 'build') -DAV_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& $cmakePath --build (Join-Path $repoRoot 'build') --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& $ctestPath --test-dir (Join-Path $repoRoot 'build') -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }

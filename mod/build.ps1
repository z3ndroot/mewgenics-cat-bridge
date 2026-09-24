# Builds cat_bridge.dll and copies it to ..\bin\cat_bridge.dll.
#
#   powershell -ExecutionPolicy Bypass -File mod\build.ps1            # release build
#   powershell -ExecutionPolicy Bypass -File mod\build.ps1 -Console   # + debug console window in the game
#
# Needs Visual Studio 2022 or newer (or its Build Tools) with the
# "Desktop development with C++" workload, which also ships CMake.

param([switch]$Console)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
    foreach ($root in $roots) {
        $found = Get-ChildItem "$root\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    throw "CMake not found. Install Visual Studio (Build Tools) with 'Desktop development with C++'."
}

$cmake = Find-CMake
Write-Host "Using $cmake"
$consoleFlag = if ($Console) { "ON" } else { "OFF" }

& $cmake -S $here -B "$here\build" -A x64 "-DCAT_BRIDGE_CONSOLE=$consoleFlag"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $cmake --build "$here\build" --config Release --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }

$bin = Join-Path (Split-Path $here -Parent) "bin"
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item "$here\build\cat_bridge\Release\cat_bridge.dll" $bin -Force
Write-Host "Built $bin\cat_bridge.dll"

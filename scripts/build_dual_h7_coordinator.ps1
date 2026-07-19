param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "firmware\sdk"
$build = Join-Path $source "build-coordinator-ninja"
$toolchain = Join-Path $source "cmake\arm-none-eabi-gcc.cmake"
$toolchainArg = "-DCMAKE_TOOLCHAIN_FILE=$($toolchain.Replace('\', '/'))"
$configurationArg = "-DCMAKE_BUILD_TYPE=$Configuration"

cmake -S $source -B $build -G Ninja $toolchainArg $configurationArg
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }
cmake --build $build --target aginti_dual_h7_coordinator --parallel
if ($LASTEXITCODE -ne 0) { throw "Coordinator build failed: $LASTEXITCODE" }

Write-Host "Compiled only. Nothing was erased or flashed."
Write-Host (Join-Path $build "aginti_dual_h7_coordinator.elf")

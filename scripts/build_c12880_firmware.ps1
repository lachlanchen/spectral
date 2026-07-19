[CmdletBinding()]
param(
    [ValidateSet('DMA','DIRECT')][string]$Engine = 'DMA',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$ArmGccBin = 'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin'
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $repo 'firmware\sdk'
$build = Join-Path $source ("build-" + $Engine.ToLowerInvariant())
if (Test-Path $ArmGccBin) { $env:PATH = "$ArmGccBin;$env:PATH" }
$toolchain = Join-Path $source 'cmake\arm-none-eabi-gcc.cmake'
$compiler = Join-Path $ArmGccBin 'arm-none-eabi-gcc.exe'
if (-not (Test-Path $compiler)) { throw "ARM compiler not found: $compiler" }

$cache = Join-Path $build 'CMakeCache.txt'
if (Test-Path $cache) {
    $usesArm = Select-String -LiteralPath $cache -SimpleMatch 'arm-none-eabi-gcc' -Quiet
    if (-not $usesArm) {
        $fullBuild = [IO.Path]::GetFullPath($build)
        $fullSource = [IO.Path]::GetFullPath($source) + [IO.Path]::DirectorySeparatorChar
        if (-not $fullBuild.StartsWith($fullSource, [StringComparison]::OrdinalIgnoreCase) -or
            -not ([IO.Path]::GetFileName($fullBuild)).StartsWith('build-', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected build directory: $fullBuild"
        }
        Remove-Item -LiteralPath $fullBuild -Recurse -Force
    }
}

$configureArgs = @(
    '-S', $source, '-B', $build, '-G', 'Ninja',
    "-DCMAKE_C_COMPILER=$compiler",
    "-DCMAKE_ASM_COMPILER=$compiler",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DAGINTI_CAPTURE_ENGINE=$Engine"
)
if (-not (Test-Path $cache)) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
}
cmake @configureArgs
cmake --build $build --config $Configuration

$artifacts = 'elf','bin','hex','map' | ForEach-Object {
    Join-Path $build "aginti_c12880_h743.$_"
}
foreach ($artifact in $artifacts) {
    if (-not (Test-Path $artifact)) { throw "Missing build artifact: $artifact" }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifact).Hash
    Write-Host ("{0}  {1}" -f $hash, $artifact)
}
Write-Host 'Build only: no device was erased, programmed, reset, or otherwise modified.'

[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$ArmGccBin = 'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin'
)

$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Join-Path $repo 'firmware\sdk'
$build = Join-Path $source 'build-suite'
$toolchain = Join-Path $source 'cmake\arm-none-eabi-gcc.cmake'
$compiler = Join-Path $ArmGccBin 'arm-none-eabi-gcc.exe'
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "ARM compiler not found: $compiler"
}
$env:PATH = "$ArmGccBin;$env:PATH"

cmake -S $source -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$($toolchain.Replace('\', '/'))" `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    '-DAGINTI_CAPTURE_ENGINE=DMA'
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $LASTEXITCODE" }

$targets = @(
    'aginti_c12880_reconstruction_h743',
    'aginti_c12880_h743',
    'aginti_dual_h7_coordinator'
)
cmake --build $build --target $targets --parallel
if ($LASTEXITCODE -ne 0) { throw "Firmware suite build failed: $LASTEXITCODE" }

function Get-ArtifactSet([string]$Target) {
    @('elf','bin','hex','map') | ForEach-Object {
        $path = Join-Path $build "$Target.$_"
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Missing build artifact: $path"
        }
        $fullPath = [IO.Path]::GetFullPath($path)
        $repoPrefix = $repo.TrimEnd('\') + '\'
        if (-not $fullPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Artifact escaped repository root: $fullPath"
        }
        [ordered]@{
            path = $fullPath.Substring($repoPrefix.Length).Replace('\', '/')
            bytes = (Get-Item -LiteralPath $path).Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        }
    }
}

function Get-DependencyRevision([string]$Name) {
    $path = Join-Path $repo "firmware\private\official-sources\$Name"
    if (-not (Test-Path -LiteralPath $path)) { return 'MISSING' }
    $revision = (& git -C $path rev-parse HEAD 2>$null)
    if ($LASTEXITCODE -ne 0) { return 'UNKNOWN' }
    return ($revision | Select-Object -First 1).Trim()
}

$manifest = [ordered]@{
    schema = 'aginti.c12880.firmware-suite.v2'
    generated_utc = [DateTime]::UtcNow.ToString('o')
    state = 'COMPILED_NOT_FLASHED'
    target = 'STM32H743xx'
    compiler = ((& $compiler --version | Select-Object -First 1).Trim())
    components = [ordered]@{
        reconstruction = [ordered]@{
            version = '0.1.0'
            compatibility = 'clean-room behavioral reconstruction; not byte-identical'
            target = 'aginti_c12880_reconstruction_h743'
            artifacts = @(Get-ArtifactSet 'aginti_c12880_reconstruction_h743')
        }
        performance = [ordered]@{
            version = '0.3.0'
            profile = 'TIM2 GPIO DMA + ADC DMA + USB CDC + legacy/V2 protocols'
            target = 'aginti_c12880_h743'
            artifacts = @(Get-ArtifactSet 'aginti_c12880_h743')
        }
        coordinator = [ordered]@{
            version = '0.1.0'
            profile = 'dual-lamp PWM, telemetry, LUT, cooling, and trigger coordinator'
            target = 'aginti_dual_h7_coordinator'
            artifacts = @(Get-ArtifactSet 'aginti_dual_h7_coordinator')
        }
    }
    dependencies = [ordered]@{
        stm32h7xx_hal_driver = Get-DependencyRevision 'stm32h7xx-hal-driver'
        cmsis_device_h7 = Get-DependencyRevision 'cmsis-device-h7'
        cmsis_5 = Get-DependencyRevision 'CMSIS_5'
        stm32_mw_usb_device = Get-DependencyRevision 'stm32-mw-usb-device'
    }
    original_internal_flash_backup = [ordered]@{
        bytes = 2097152
        sha256 = '67F1F6C421D56C2077D5A3F7417AA6F5213A2791D0C63AE5DAFBDBDF461764B4'
        published = $false
    }
    safety = [ordered]@{
        flashed = $false
        erased = $false
        option_bytes_modified = $false
        eeprom_modified = $false
    }
}

$manifestPath = Join-Path $source 'BUILD-MANIFEST.json'
$json = $manifest | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText(
    $manifestPath,
    $json + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false)
)
Write-Host "Build manifest: $manifestPath"
Write-Host 'Build only: no device was erased, programmed, reset, or otherwise modified.'

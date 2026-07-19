[CmdletBinding()]
param(
    [string]$OutputRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) "firmware\private"),
    [ValidateRange(100, 4000)]
    [int]$AdapterKHz = 950
)

$ErrorActionPreference = "Stop"
$FlashBase = 0x08000000
$FlashBytes = 0x00200000

function Find-OpenOcd {
    $WinGetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
    if (Test-Path -LiteralPath $WinGetRoot) {
        $Candidate = Get-ChildItem -LiteralPath $WinGetRoot -Recurse -File `
            -Filter openocd.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match "xpack-openocd" } |
            Select-Object -First 1
        if ($Candidate) {
            return $Candidate.FullName
        }
    }
    $Command = Get-Command openocd.exe -ErrorAction Stop
    return $Command.Source
}

$OpenOcd = Find-OpenOcd
$Stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$OutputDirectory = Join-Path $OutputRoot "c12880_stm32h7_$Stamp"
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$FirmwarePath = Join-Path $OutputDirectory "stm32h74x75x_internal_flash_0x08000000_2MiB.bin"
$BackupLog = Join-Path $OutputDirectory "openocd-backup.log"
$ResumeLog = Join-Path $OutputDirectory "openocd-resume.log"
$TclFirmwarePath = $FirmwarePath.Replace("\", "/")

# Safety invariant: this command list contains reads only. Never add program,
# write, erase, unlock, option-write, or readout-unprotect operations here.
$ReadCommand = @(
    "transport select swd",
    "adapter speed $AdapterKHz",
    "init",
    "reset halt",
    "flash probe 0",
    "flash info 0",
    "dump_image {$TclFirmwarePath} 0x08000000 0x00200000",
    "shutdown"
) -join "; "
$ResumeCommand = "transport select swd; adapter speed 100; init; reset run; shutdown"

$PreviousPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    $BackupOutput = & $OpenOcd -f interface/stlink.cfg -f target/stm32h7x.cfg `
        -c $ReadCommand 2>&1
    $BackupExitCode = $LASTEXITCODE
    $BackupOutput | Set-Content -Encoding UTF8 $BackupLog
}
finally {
    $ResumeOutput = & $OpenOcd -f interface/stlink.cfg -f target/stm32h7x.cfg `
        -c $ResumeCommand 2>&1
    $ResumeOutput | Set-Content -Encoding UTF8 $ResumeLog
    $ErrorActionPreference = $PreviousPreference
}

if ($BackupExitCode -ne 0) {
    throw "Read-only flash dump failed with exit code $BackupExitCode. No write was attempted."
}
$Firmware = Get-Item -LiteralPath $FirmwarePath
if ($Firmware.Length -ne $FlashBytes) {
    throw "Unexpected backup size: $($Firmware.Length) bytes; expected $FlashBytes."
}
$Hash = Get-FileHash -Algorithm SHA256 -LiteralPath $FirmwarePath
$Manifest = [ordered]@{
    created_utc = (Get-Date).ToUniversalTime().ToString("o")
    target = "STM32H74x/75x"
    flash_base = "0x08000000"
    flash_bytes = $Firmware.Length
    sha256 = $Hash.Hash
    adapter_khz = $AdapterKHz
    openocd = $OpenOcd
    write_operations = 0
    firmware = $FirmwarePath
}
$Manifest | ConvertTo-Json | Set-Content -Encoding UTF8 `
    (Join-Path $OutputDirectory "backup-manifest.json")
$Manifest | ConvertTo-Json

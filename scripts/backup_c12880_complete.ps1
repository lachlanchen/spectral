[CmdletBinding()]
param(
    [string]$OutputRoot,
    [string]$MirrorRoot,
    [string]$HlaSerial,
    [ValidateRange(100, 4000)]
    [int]$AdapterKHz = 450,
    [ValidateRange(2, 3)]
    [int]$FlashReadCount = 3,
    [string]$ExpectedFlashSha256,
    [switch]$CaptureVolatileRam,
    [switch]$CaptureMappedSystemMemory,
    [string[]]$HostAssetRoots = @(),
    [switch]$CopyHostAssets,
    [string]$ExternalEepromImage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-FullPath {
    param([Parameter(Mandatory)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path)
}

function Convert-ToTclPath {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FullPath $Path).Replace('\', '/')
}

function Get-Sha256 {
    param([Parameter(Mandatory)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Find-OpenOcd {
    $exeCandidates = [System.Collections.Generic.List[string]]::new()
    $command = Get-Command openocd.exe -ErrorAction SilentlyContinue
    if ($command) {
        $exeCandidates.Add($command.Source)
    }

    $searchRoots = @(
        (Join-Path $env:LOCALAPPDATA 'xPacks\@xpack-dev-tools\openocd'),
        'C:\ProgramData\chocolatey\lib\openocd'
    )
    foreach ($root in $searchRoots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Recurse -Filter openocd.exe -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                ForEach-Object { $exeCandidates.Add($_.FullName) }
        }
    }

    $scriptCandidates = @(
        'C:\ProgramData\chocolatey\lib\openocd\tools\install\share\openocd\scripts'
    )
    foreach ($root in $searchRoots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Recurse -Filter stm32h7x.cfg -File -ErrorAction SilentlyContinue |
                ForEach-Object {
                    $scriptCandidates += Split-Path (Split-Path $_.FullName -Parent) -Parent
                }
        }
    }

    $exe = $exeCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $scripts = $scriptCandidates |
        Where-Object { Test-Path -LiteralPath (Join-Path $_ 'target\stm32h7x.cfg') } |
        Select-Object -First 1

    if (-not $exe -or -not $scripts) {
        throw 'OpenOCD or its stm32h7x target scripts were not found.'
    }

    [pscustomobject]@{
        Exe = (Get-FullPath $exe)
        Scripts = (Get-FullPath $scripts)
    }
}

function Invoke-OpenOcd {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [Parameter(Mandatory)][string]$Scripts,
        [Parameter(Mandatory)][string]$CommandFile,
        [Parameter(Mandatory)][string]$LogFile,
        [string]$Serial
    )

    $argumentLine = "-s `"$Scripts`" -f interface/stlink.cfg"
    if ($Serial) {
        $argumentLine += " -c `"hla_serial $Serial`""
    }
    $argumentLine += " -f target/stm32h7x.cfg -f `"$CommandFile`""

    $stdoutLog = $LogFile + '.stdout.log'
    $stderrLog = $LogFile + '.stderr.log'
    $process = Start-Process -FilePath $Exe -ArgumentList $argumentLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
    $exitCode = $process.ExitCode
    $lines = @(
        Get-Content -LiteralPath $stdoutLog -ErrorAction SilentlyContinue
        Get-Content -LiteralPath $stderrLog -ErrorAction SilentlyContinue
    )
    $lines | Set-Content -LiteralPath $LogFile -Encoding utf8

    [pscustomobject]@{
        ExitCode = $exitCode
        Lines = @($lines | ForEach-Object { "$_" })
    }
}

function Invoke-ResumeCleanup {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [Parameter(Mandatory)][string]$Scripts,
        [Parameter(Mandatory)][string]$LogFile,
        [string]$Serial
    )

    $argumentLine = "-s `"$Scripts`" -f interface/stlink.cfg"
    if ($Serial) {
        $argumentLine += " -c `"hla_serial $Serial`""
    }
    $argumentLine += ' -f target/stm32h7x.cfg -c "adapter speed 450" -c "init; halt; resume; shutdown"'

    $stdoutLog = $LogFile + '.stdout.log'
    $stderrLog = $LogFile + '.stderr.log'
    $process = Start-Process -FilePath $Exe -ArgumentList $argumentLine -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
    $exitCode = $process.ExitCode
    $lines = @(
        Get-Content -LiteralPath $stdoutLog -ErrorAction SilentlyContinue
        Get-Content -LiteralPath $stderrLog -ErrorAction SilentlyContinue
    )
    $lines | Set-Content -LiteralPath $LogFile -Encoding utf8
    return $exitCode
}

function Read-RegisterValues {
    param([Parameter(Mandatory)][AllowEmptyString()][string[]]$Lines)

    $result = [ordered]@{}
    foreach ($line in $Lines) {
        if ($line -match '(?i)AGINTI_REGISTER_VALUE_([A-Z0-9_]+)=(0x[0-9a-f]{8})') {
            $name = $matches[1].ToUpperInvariant()
            $value = $matches[2].ToUpperInvariant()
            if (-not $result.Contains($name)) {
                $result[$name] = $value
            }
        }
    }
    return $result
}

function Get-ArtifactRecords {
    param([Parameter(Mandatory)][string]$Root)

    $rootPath = Get-FullPath $Root
    $records = foreach ($file in Get-ChildItem -LiteralPath $rootPath -Recurse -File) {
        if ($file.Name -in @('backup-manifest.json', 'SHA256SUMS.txt')) {
            continue
        }
        [pscustomobject]@{
            relative_path = $file.FullName.Substring($rootPath.Length).TrimStart('\').Replace('\', '/')
            bytes = $file.Length
            sha256 = Get-Sha256 $file.FullName
        }
    }
    return @($records | Sort-Object relative_path)
}

function Copy-HostAssets {
    param(
        [Parameter(Mandatory)][string[]]$Roots,
        [Parameter(Mandatory)][string]$Destination,
        [switch]$CopyFiles
    )

    $inventory = [System.Collections.Generic.List[object]]::new()
    foreach ($root in $Roots) {
        if (-not (Test-Path -LiteralPath $root)) {
            $inventory.Add([pscustomobject]@{
                source_root = $root
                relative_path = $null
                status = 'missing'
                bytes = 0
                sha256 = $null
            })
            continue
        }

        $resolvedRoot = (Resolve-Path -LiteralPath $root).Path
        $label = Split-Path $resolvedRoot -Leaf
        $destinationRoot = Join-Path $Destination $label
        if ($CopyFiles) {
            New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
        }

        foreach ($file in Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File) {
            $relative = $file.FullName.Substring($resolvedRoot.Length).TrimStart('\')
            $inventory.Add([pscustomobject]@{
                source_root = $resolvedRoot
                relative_path = $relative.Replace('\', '/')
                status = $(if ($CopyFiles) { 'copied' } else { 'inventoried' })
                bytes = $file.Length
                sha256 = Get-Sha256 $file.FullName
            })
            if ($CopyFiles) {
                $destinationFile = Join-Path $destinationRoot $relative
                New-Item -ItemType Directory -Force -Path (Split-Path $destinationFile -Parent) | Out-Null
                Copy-Item -LiteralPath $file.FullName -Destination $destinationFile
            }
        }
    }
    return @($inventory)
}

$repoRoot = Get-FullPath (Join-Path $PSScriptRoot '..')
if (-not $OutputRoot) {
    $OutputRoot = Join-Path $repoRoot 'firmware\private'
}
$OutputRoot = Get-FullPath $OutputRoot
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$backupName = "c12880_complete_$stamp"
$backupDir = Join-Path $OutputRoot $backupName
New-Item -ItemType Directory -Path $backupDir | Out-Null

$runningOpenOcd = @(Get-Process openocd -ErrorAction SilentlyContinue)
if ($runningOpenOcd.Count -gt 0) {
    throw 'An OpenOCD process is already running. Close it before making a coherent backup.'
}

$presentStLinks = @(
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VID_0483&PID_3748' }
)
if (-not $HlaSerial -and $presentStLinks.Count -ne 1) {
    throw "Expected exactly one external ST-Link/V2 (VID 0483 PID 3748), found $($presentStLinks.Count). Use -HlaSerial when multiple probes are connected."
}

$openOcd = Find-OpenOcd
$openOcdVersion = (Get-Item -LiteralPath $openOcd.Exe).VersionInfo.FileVersion
if (-not $openOcdVersion) {
    $openOcdVersion = 'reported-in-openocd-session-log'
}

$registers = [ordered]@{
    CPUID = '0xE000ED00'
    DBGMCU_IDCODE = '0x5C001000'
    UID_WORD0 = '0x1FF1E800'
    UID_WORD1 = '0x1FF1E804'
    UID_WORD2 = '0x1FF1E808'
    FLASH_SIZE = '0x1FF1E880'
    FLASH_ACR = '0x52002000'
    FLASH_CR1 = '0x5200200C'
    FLASH_SR1 = '0x52002010'
    FLASH_OPTCR = '0x52002018'
    FLASH_OPTSR_CUR = '0x5200201C'
    FLASH_OPTSR_PRG = '0x52002020'
    FLASH_PRAR_CUR1 = '0x52002028'
    FLASH_PRAR_PRG1 = '0x5200202C'
    FLASH_SCAR_CUR1 = '0x52002030'
    FLASH_SCAR_PRG1 = '0x52002034'
    FLASH_WPSN_CUR1R = '0x52002038'
    FLASH_WPSN_PRG1R = '0x5200203C'
    FLASH_BOOT_CURR = '0x52002040'
    FLASH_BOOT_PRGR = '0x52002044'
    FLASH_CR2 = '0x5200210C'
    FLASH_SR2 = '0x52002110'
    FLASH_PRAR_CUR2 = '0x52002128'
    FLASH_PRAR_PRG2 = '0x5200212C'
    FLASH_SCAR_CUR2 = '0x52002130'
    FLASH_SCAR_PRG2 = '0x52002134'
    FLASH_WPSN_CUR2R = '0x52002138'
    FLASH_WPSN_PRG2R = '0x5200213C'
}

$commands = [System.Collections.Generic.List[string]]::new()
$commands.Add('adapter speed ' + $AdapterKHz)
$commands.Add('init')
$commands.Add('echo "AGINTI_BACKUP_MODE=READ_ONLY"')
$commands.Add('targets')
$commands.Add('halt')
$commands.Add('echo "AGINTI_TARGET_HALTED=1"')
$commands.Add('flash banks')
$commands.Add('flash probe 0')
$commands.Add('flash info 0')
foreach ($entry in $registers.GetEnumerator()) {
    $commands.Add('echo "AGINTI_REGISTER_' + $entry.Key + '"')
    $commands.Add('set aginti_register_value [lindex [read_memory ' + $entry.Value + ' 32 1] 0]')
    $commands.Add('echo [format "AGINTI_REGISTER_VALUE_' + $entry.Key + '=0x%08X" $aginti_register_value]')
}
$commands.Add('echo "AGINTI_CPU_REGISTERS_BEGIN"')
$commands.Add('reg')
$commands.Add('echo "AGINTI_CPU_REGISTERS_END"')

for ($index = 1; $index -le $FlashReadCount; $index++) {
    $path = Convert-ToTclPath (Join-Path $backupDir "internal_flash_read$index.bin")
    $commands.Add("dump_image {$path} 0x08000000 0x00200000")
    $commands.Add('echo "AGINTI_INTERNAL_FLASH_READ_' + $index + '_DONE=1"')
}

if ($CaptureMappedSystemMemory) {
    $path = Convert-ToTclPath (Join-Path $backupDir 'mapped_system_memory_0x1FF00000_128KiB.bin')
    $commands.Add("set aginti_sysmem_rc [catch {dump_image {$path} 0x1FF00000 0x00020000} aginti_sysmem_msg]")
    $commands.Add('echo "AGINTI_SYSTEM_MEMORY_RC=$aginti_sysmem_rc"')
    $commands.Add('echo "AGINTI_SYSTEM_MEMORY_RESULT=$aginti_sysmem_msg"')
}

if ($CaptureVolatileRam) {
    $ramRegions = [ordered]@{
        dtcm_0x20000000_128KiB = @('0x20000000', '0x00020000')
        axi_sram_0x24000000_512KiB = @('0x24000000', '0x00080000')
        d2_sram_0x30000000_288KiB = @('0x30000000', '0x00048000')
        d3_sram_0x38000000_64KiB = @('0x38000000', '0x00010000')
        backup_sram_0x38800000_4KiB = @('0x38800000', '0x00001000')
    }
    foreach ($entry in $ramRegions.GetEnumerator()) {
        $path = Convert-ToTclPath (Join-Path $backupDir ($entry.Key + '.bin'))
        $commands.Add("set aginti_ram_rc [catch {dump_image {$path} $($entry.Value[0]) $($entry.Value[1])} aginti_ram_msg]")
        $commands.Add('echo "AGINTI_RAM_' + $entry.Key.ToUpperInvariant() + '_RC=$aginti_ram_rc"')
        $commands.Add('echo "AGINTI_RAM_RESULT=$aginti_ram_msg"')
    }
}

$commands.Add('resume')
$commands.Add('echo "AGINTI_TARGET_RESUMED=1"')
$commands.Add('shutdown')

$commandText = $commands -join [Environment]::NewLine
$forbiddenPatterns = @(
    '(?i)\bprogram\b',
    '(?i)\bwrite_image\b',
    '(?i)\bflash\s+write',
    '(?i)\berase\b',
    '(?i)\bunlock\b',
    '(?i)\boption_write\b',
    '(?i)\bmass_erase\b',
    '(?i)\breadout_protect\b'
)
foreach ($pattern in $forbiddenPatterns) {
    if ($commandText -match $pattern) {
        throw "Generated OpenOCD command file failed the read-only policy: $pattern"
    }
}

$commandFile = Join-Path $backupDir 'openocd-read-only-backup.tcl'
$commandText | Set-Content -LiteralPath $commandFile -Encoding ascii
$sessionLog = Join-Path $backupDir 'openocd-session.log'
$cleanupLog = Join-Path $backupDir 'openocd-resume-cleanup.log'

$session = Invoke-OpenOcd -Exe $openOcd.Exe -Scripts $openOcd.Scripts -CommandFile $commandFile -LogFile $sessionLog -Serial $HlaSerial
$cleanupExit = Invoke-ResumeCleanup -Exe $openOcd.Exe -Scripts $openOcd.Scripts -LogFile $cleanupLog -Serial $HlaSerial

$registerValues = Read-RegisterValues -Lines $session.Lines
$namedRegisterValues = [ordered]@{}
foreach ($entry in $registers.GetEnumerator()) {
    $name = $entry.Key.ToUpperInvariant()
    $namedRegisterValues[$entry.Key] = if ($registerValues.Contains($name)) { $registerValues[$name] } else { $null }
}
$namedRegisterValues | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $backupDir 'registers-and-option-state.json') -Encoding utf8

$flashFiles = @()
for ($index = 1; $index -le $FlashReadCount; $index++) {
    $candidate = Join-Path $backupDir "internal_flash_read$index.bin"
    if (Test-Path -LiteralPath $candidate) {
        $flashFiles += Get-Item -LiteralPath $candidate
    }
}

$flashReadRecords = @($flashFiles | ForEach-Object {
    [pscustomobject]@{
        file = $_.FullName
        sha256 = Get-Sha256 $_.FullName
    }
})
$flashReadHashes = @($flashReadRecords | ForEach-Object sha256)
$flashSizesValid = $flashFiles.Count -eq $FlashReadCount -and
    @($flashFiles | Where-Object Length -ne 0x00200000).Count -eq 0
$flashReadsMatch = $flashSizesValid -and
    (@($flashReadHashes | Select-Object -Unique).Count -eq 1)
$hashGroups = @($flashReadHashes | Group-Object | Sort-Object Count -Descending)
$requiredConsensusCount = [Math]::Floor($FlashReadCount / 2) + 1
$flashConsensusHash = if ($hashGroups.Count -gt 0 -and $hashGroups[0].Count -ge $requiredConsensusCount) {
    $hashGroups[0].Name
} else {
    $null
}
$flashConsensusValid = $flashSizesValid -and $null -ne $flashConsensusHash
$expectedHashMatches = $null
if ($ExpectedFlashSha256 -and $flashReadHashes.Count -gt 0) {
    $expectedHashMatches = $ExpectedFlashSha256.ToUpperInvariant() -in $flashReadHashes
}

$canonicalFlashRecord = $null
if ($ExpectedFlashSha256) {
    $canonicalFlashRecord = $flashReadRecords |
        Where-Object sha256 -eq $ExpectedFlashSha256.ToUpperInvariant() |
        Select-Object -First 1
}
if (-not $canonicalFlashRecord -and $flashConsensusHash) {
    $canonicalFlashRecord = $flashReadRecords |
        Where-Object sha256 -eq $flashConsensusHash |
        Select-Object -First 1
}
$canonicalFlashFile = if ($canonicalFlashRecord) { Get-Item -LiteralPath $canonicalFlashRecord.file } else { $null }

$vector = $null
if ($canonicalFlashFile -and $canonicalFlashFile.Length -ge 8) {
    $stream = [System.IO.File]::OpenRead($canonicalFlashFile.FullName)
    try {
        $buffer = New-Object byte[] 8
        [void]$stream.Read($buffer, 0, 8)
    }
    finally {
        $stream.Dispose()
    }
    $initialSp = [BitConverter]::ToUInt32($buffer, 0)
    $resetVector = [BitConverter]::ToUInt32($buffer, 4)
    $vector = [ordered]@{
        initial_stack_pointer = ('0x{0:X8}' -f $initialSp)
        reset_vector = ('0x{0:X8}' -f $resetVector)
        initial_sp_plausible = (($initialSp -ge 0x20000000L -and $initialSp -lt 0x20020000L) -or
            ($initialSp -ge 0x24000000L -and $initialSp -lt 0x24080000L) -or
            ($initialSp -ge 0x30000000L -and $initialSp -lt 0x30048000L) -or
            ($initialSp -ge 0x38000000L -and $initialSp -lt 0x38010000L))
        reset_vector_plausible = (($resetVector -band 1) -eq 1 -and
            (($resetVector -band 0xFFFFFFFEL) -ge 0x08000000L) -and
            (($resetVector -band 0xFFFFFFFEL) -lt 0x08200000L))
    }
}

if ($canonicalFlashFile -and $canonicalFlashFile.Length -eq 0x00200000) {
    $bytes = [System.IO.File]::ReadAllBytes($canonicalFlashFile.FullName)
    [System.IO.File]::WriteAllBytes(
        (Join-Path $backupDir 'internal_flash_bank1_0x08000000_1MiB.bin'),
        $bytes[0..0x0FFFFF]
    )
    [System.IO.File]::WriteAllBytes(
        (Join-Path $backupDir 'internal_flash_bank2_0x08100000_1MiB.bin'),
        $bytes[0x100000..0x1FFFFF]
    )
}

$idCode = $namedRegisterValues.DBGMCU_IDCODE
$deviceId = $null
if ($idCode) {
    $deviceId = ([Convert]::ToUInt32($idCode.Substring(2), 16) -band 0xFFF)
}
$flashSizeKiB = $null
if ($namedRegisterValues.FLASH_SIZE) {
    $flashSizeKiB = ([Convert]::ToUInt32($namedRegisterValues.FLASH_SIZE.Substring(2), 16) -band 0xFFFF)
}
$rdpByte = $null
$rdpLevel = 'unknown'
if ($namedRegisterValues.FLASH_OPTSR_CUR) {
    $optsr = [Convert]::ToUInt32($namedRegisterValues.FLASH_OPTSR_CUR.Substring(2), 16)
    $rdpByte = (($optsr -shr 8) -band 0xFF)
    $rdpLevel = switch ($rdpByte) {
        0xAA { 'level-0' }
        0xCC { 'level-2' }
        default { 'level-1' }
    }
}

$identity = [ordered]@{
    uid_words = @(
        $namedRegisterValues.UID_WORD0,
        $namedRegisterValues.UID_WORD1,
        $namedRegisterValues.UID_WORD2
    )
    dbgmcu_idcode = $idCode
    stm32_device_id = $(if ($null -ne $deviceId) { '0x{0:X3}' -f $deviceId } else { $null })
    cpuid = $namedRegisterValues.CPUID
    flash_size_kib = $flashSizeKiB
    rdp_byte = $(if ($null -ne $rdpByte) { '0x{0:X2}' -f $rdpByte } else { $null })
    rdp_level = $rdpLevel
}
$identity | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $backupDir 'device-identity-private.json') -Encoding utf8

$externalEepromStatus = 'not-captured'
if ($ExternalEepromImage) {
    if (-not (Test-Path -LiteralPath $ExternalEepromImage)) {
        throw "External EEPROM image does not exist: $ExternalEepromImage"
    }
    $eepromDestination = Join-Path $backupDir 'external_eeprom_supplied_image.bin'
    Copy-Item -LiteralPath $ExternalEepromImage -Destination $eepromDestination
    $externalEepromStatus = 'supplied-image-copied'
}

$hostAssetInventory = @()
if ($HostAssetRoots.Count -gt 0) {
    $hostAssetDestination = Join-Path $backupDir 'host-assets'
    New-Item -ItemType Directory -Force -Path $hostAssetDestination | Out-Null
    $hostAssetInventory = Copy-HostAssets -Roots $HostAssetRoots -Destination $hostAssetDestination -CopyFiles:$CopyHostAssets
    $hostAssetInventory | Export-Csv -LiteralPath (Join-Path $backupDir 'host-assets-inventory.csv') -NoTypeInformation -Encoding utf8
}

$restoreMap = [ordered]@{
    internal_flash_full = [ordered]@{
        file = 'internal_flash_read1.bin'
        address = '0x08000000'
        bytes = 0x00200000
    }
    internal_flash_bank1 = [ordered]@{
        file = 'internal_flash_bank1_0x08000000_1MiB.bin'
        address = '0x08000000'
        bytes = 0x00100000
    }
    internal_flash_bank2 = [ordered]@{
        file = 'internal_flash_bank2_0x08100000_1MiB.bin'
        address = '0x08100000'
        bytes = 0x00100000
    }
    option_state = [ordered]@{
        file = 'registers-and-option-state.json'
        automatic_restore = $false
        reason = 'Option bytes require field-by-field audit and are intentionally never written by this workflow.'
    }
    external_eeprom = [ordered]@{
        file = $(if ($externalEepromStatus -eq 'supplied-image-copied') { 'external_eeprom_supplied_image.bin' } else { $null })
        automatic_restore = $false
    }
}
$restoreMap | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $backupDir 'restore-map.json') -Encoding utf8

$systemMemoryFile = Join-Path $backupDir 'mapped_system_memory_0x1FF00000_128KiB.bin'
$ramFiles = @(
    Get-ChildItem -LiteralPath $backupDir -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^(dtcm|.*sram).*\.bin$' }
)
$targetVoltage = $null
foreach ($line in $session.Lines) {
    if ($line -match '(?i)target voltage:\s*([0-9.]+)') {
        $targetVoltage = [double]$matches[1]
        break
    }
}

$completeness = [ordered]@{
    generated_at = (Get-Date).ToString('o')
    target_expected = 'STM32H74x/H75x device ID 0x450 with 2048 KiB internal flash'
    openocd_exit_code = $session.ExitCode
    resume_cleanup_exit_code = $cleanupExit
    target_voltage_v = $targetVoltage
    device_id_matches = ($deviceId -eq 0x450)
    flash_size_matches = ($flashSizeKiB -eq 2048)
    internal_flash_read_count = $flashFiles.Count
    internal_flash_size_valid = $flashSizesValid
    internal_flash_reads_match = $flashReadsMatch
    internal_flash_consensus_count = $(if ($hashGroups.Count -gt 0) { $hashGroups[0].Count } else { 0 })
    internal_flash_consensus_required = $requiredConsensusCount
    internal_flash_consensus_sha256 = $flashConsensusHash
    internal_flash_consensus_valid = $flashConsensusValid
    expected_previous_hash_matches = $expectedHashMatches
    vector_table = $vector
    option_registers_captured = ($null -ne $namedRegisterValues.FLASH_OPTSR_CUR -and
        $null -ne $namedRegisterValues.FLASH_BOOT_CURR -and
        $null -ne $namedRegisterValues.FLASH_WPSN_CUR2R)
    rdp_level = $rdpLevel
    mapped_system_memory = $(if (Test-Path -LiteralPath $systemMemoryFile) { 'captured-reference-only' } else { 'not-requested-or-unreadable' })
    user_otp = 'not-applicable-on-stm32h743'
    volatile_ram = $(if ($CaptureVolatileRam) { "best-effort-$($ramFiles.Count)-regions" } else { 'not-requested-not-needed-for-firmware-restore' })
    external_eeprom = $externalEepromStatus
    host_assets = $(if ($HostAssetRoots.Count -gt 0) { "$($hostAssetInventory.Count)-files" } else { 'not-requested' })
    complete_mcu_user_flash = ($flashConsensusValid -and $deviceId -eq 0x450 -and $flashSizeKiB -eq 2048)
    complete_board_nonvolatile_state = ($flashConsensusValid -and
        $externalEepromStatus -eq 'supplied-image-copied')
    notes = @(
        'Factory system memory and electronic signatures are not writable restoration targets.',
        'Option-byte register views are captured, but no option bytes are modified.',
        'A board-level backup is incomplete until any external EEPROM is identified and imaged.',
        'RAM snapshots are diagnostic only and are not restored after reset.'
    )
}
$completeness | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $backupDir 'completeness-report.json') -Encoding utf8

$repoCommit = $null
try {
    $repoCommit = (git -C $repoRoot rev-parse HEAD 2>$null).Trim()
}
catch {
    $repoCommit = $null
}

$artifacts = Get-ArtifactRecords -Root $backupDir
$manifest = [ordered]@{
    schema = 'aginti.c12880.backup.v1'
    created_at = (Get-Date).ToString('o')
    backup_name = $backupName
    host = $env:COMPUTERNAME
    user = $env:USERNAME
    repository_commit = $repoCommit
    openocd = [ordered]@{
        executable = $openOcd.Exe
        scripts = $openOcd.Scripts
        version = $openOcdVersion
        adapter_khz = $AdapterKHz
    }
    stlink_pnp = @($presentStLinks | ForEach-Object {
        [ordered]@{
            friendly_name = $_.FriendlyName
            instance_id = $_.InstanceId
            status = $_.Status
        }
    })
    target = $identity
    flash_read_hashes = $flashReadHashes
    flash_consensus_sha256 = $flashConsensusHash
    artifacts = $artifacts
}
$manifestPath = Join-Path $backupDir 'backup-manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$sumLines = foreach ($file in Get-ChildItem -LiteralPath $backupDir -Recurse -File | Sort-Object FullName) {
    if ($file.Name -eq 'SHA256SUMS.txt') {
        continue
    }
    $relative = $file.FullName.Substring($backupDir.Length).TrimStart('\').Replace('\', '/')
    "$(Get-Sha256 $file.FullName) *$relative"
}
$sumLines | Set-Content -LiteralPath (Join-Path $backupDir 'SHA256SUMS.txt') -Encoding ascii

$archivePath = Join-Path $OutputRoot ($backupName + '.zip')
Compress-Archive -Path (Join-Path $backupDir '*') -DestinationPath $archivePath -CompressionLevel Optimal
$archiveHash = Get-Sha256 $archivePath
"$archiveHash *$(Split-Path $archivePath -Leaf)" |
    Set-Content -LiteralPath ($archivePath + '.sha256') -Encoding ascii

$mirrorDirectory = $null
if ($MirrorRoot) {
    $MirrorRoot = Get-FullPath $MirrorRoot
    New-Item -ItemType Directory -Force -Path $MirrorRoot | Out-Null
    $mirrorDirectory = Join-Path $MirrorRoot $backupName
    Copy-Item -LiteralPath $backupDir -Destination $mirrorDirectory -Recurse
    Copy-Item -LiteralPath $archivePath -Destination (Join-Path $MirrorRoot (Split-Path $archivePath -Leaf))
    Copy-Item -LiteralPath ($archivePath + '.sha256') -Destination (Join-Path $MirrorRoot (Split-Path ($archivePath + '.sha256') -Leaf))
}

$hardFailures = [System.Collections.Generic.List[string]]::new()
if ($session.ExitCode -ne 0) { $hardFailures.Add("OpenOCD exited with code $($session.ExitCode).") }
if ($cleanupExit -ne 0) { $hardFailures.Add("Resume cleanup exited with code $cleanupExit.") }
if ($deviceId -ne 0x450) { $hardFailures.Add('Target device ID is not STM32H74x/H75x 0x450.') }
if ($flashSizeKiB -ne 2048) { $hardFailures.Add('Target flash-size signature is not 2048 KiB.') }
if (-not $flashConsensusValid) { $hardFailures.Add('No two-of-three consensus exists among the repeated 2 MiB flash reads.') }
if ($false -eq $expectedHashMatches) { $hardFailures.Add('Current flash differs from the explicitly supplied previous backup hash.') }
if ($vector -and (-not $vector.initial_sp_plausible -or -not $vector.reset_vector_plausible)) {
    $hardFailures.Add('The vector table is not plausible for this STM32H743 image.')
}

[pscustomobject]@{
    backup_directory = $backupDir
    archive = $archivePath
    archive_sha256 = $archiveHash
    mirror_directory = $mirrorDirectory
    internal_flash_sha256 = $(if ($canonicalFlashRecord) { $canonicalFlashRecord.sha256 } else { $null })
    repeated_reads_match = $flashReadsMatch
    consensus_sha256 = $flashConsensusHash
    consensus_valid = $flashConsensusValid
    rdp_level = $rdpLevel
    external_eeprom = $externalEepromStatus
    complete_board_nonvolatile_state = $completeness.complete_board_nonvolatile_state
    hard_failures = @($hardFailures)
} | ConvertTo-Json -Depth 6

if ($hardFailures.Count -gt 0) {
    exit 2
}

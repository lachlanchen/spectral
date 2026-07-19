[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$BackupDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $BackupDirectory).Path
$manifestPath = Join-Path $root 'backup-manifest.json'
$completenessPath = Join-Path $root 'completeness-report.json'

if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "Missing manifest: $manifestPath"
}
if (-not (Test-Path -LiteralPath $completenessPath)) {
    throw "Missing completeness report: $completenessPath"
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$completeness = Get-Content -LiteralPath $completenessPath -Raw | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()

foreach ($artifact in $manifest.artifacts) {
    $path = Join-Path $root ($artifact.relative_path.Replace('/', '\'))
    if (-not (Test-Path -LiteralPath $path)) {
        $failures.Add("Missing: $($artifact.relative_path)")
        continue
    }
    $file = Get-Item -LiteralPath $path
    if ($file.Length -ne [long]$artifact.bytes) {
        $failures.Add("Size mismatch: $($artifact.relative_path)")
        continue
    }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actual -ne $artifact.sha256.ToUpperInvariant()) {
        $failures.Add("SHA256 mismatch: $($artifact.relative_path)")
    }
}

$flashReads = @(Get-ChildItem -LiteralPath $root -Filter 'internal_flash_read*.bin' -File)
if ($flashReads.Count -lt 2) {
    $failures.Add('At least two repeated internal-flash images are required.')
}
else {
    $hashes = @($flashReads | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash })
    $groups = @($hashes | Group-Object | Sort-Object Count -Descending)
    $required = [Math]::Floor($flashReads.Count / 2) + 1
    if ($groups.Count -eq 0 -or $groups[0].Count -lt $required) {
        $failures.Add('Repeated internal-flash images have no strict-majority consensus.')
    }
    elseif ($manifest.flash_consensus_sha256 -and $groups[0].Name -ne $manifest.flash_consensus_sha256) {
        $failures.Add('Computed flash consensus differs from the manifest.')
    }
}

$result = [ordered]@{
    schema = $manifest.schema
    backup_directory = $root
    artifact_count = @($manifest.artifacts).Count
    internal_flash_complete = [bool]$completeness.complete_mcu_user_flash
    board_nonvolatile_complete = [bool]$completeness.complete_board_nonvolatile_state
    external_eeprom = $completeness.external_eeprom
    verification_passed = ($failures.Count -eq 0)
    failures = @($failures)
    verified_at = (Get-Date).ToString('o')
}

$result | ConvertTo-Json -Depth 6
if ($failures.Count -gt 0) {
    exit 2
}

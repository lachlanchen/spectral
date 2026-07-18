param(
    [string]$ExtractRoot = "C:\Users\Administrator\Projects\.c12880-spectral-work-20260718\wave_main.exe_extracted",
    [string]$OutputDirectory = "C:\Users\Administrator\Projects\spectral\references\vendor\raw\c12880-wave-main-recovered\python"
)

$ErrorActionPreference = "Continue"
$env:PATH = "C:\Users\Administrator\.local\bin;$env:PATH"
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
$env:NO_COLOR = "1"

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$log = Join-Path $OutputDirectory "recovery.log"
$pyz = Join-Path $ExtractRoot "PYZ.pyz_extracted"
$modules = @(
    "abuat",
    "Auto_exposure",
    "com_config",
    "comread_data",
    "config_read",
    "data_processing",
    "DateDialog",
    "fd_color",
    "File_ConfigParser",
    "find_com",
    "global_vlaue",
    "mainplay",
    "save_data"
)

foreach ($module in $modules) {
    $source = Join-Path $pyz "$module.pyc"
    $destination = Join-Path $OutputDirectory "decompiled_$module.py"
    if (Test-Path -LiteralPath $destination) {
        "$(Get-Date -Format o) SKIP $module" | Add-Content -LiteralPath $log
        continue
    }
    "$(Get-Date -Format o) START $module" | Add-Content -LiteralPath $log
    & pylingual $source --out-dir $OutputDirectory --version 3.11 --quiet *>> $log
    "$(Get-Date -Format o) EXIT $module code=$LASTEXITCODE" | Add-Content -LiteralPath $log
}

$entry = Join-Path $ExtractRoot "wave_main.pyc"
$entryDestination = Join-Path $OutputDirectory "decompiled_wave_main.py"
if (-not (Test-Path -LiteralPath $entryDestination)) {
    "$(Get-Date -Format o) START wave_main" | Add-Content -LiteralPath $log
    & pylingual $entry --out-dir $OutputDirectory --version 3.11 --quiet *>> $log
    "$(Get-Date -Format o) EXIT wave_main code=$LASTEXITCODE" | Add-Content -LiteralPath $log
}

"$(Get-Date -Format o) COMPLETE" | Add-Content -LiteralPath $log

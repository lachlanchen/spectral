param(
    [string]$Port = "COM3"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Pythonw = Join-Path $Root ".venv\Scripts\pythonw.exe"
$Icon = Join-Path $Root "src\spectral\resources\icon.ico"

if (-not (Test-Path -LiteralPath $Pythonw)) {
    $Pythonw = (Get-Command pythonw.exe -ErrorAction Stop).Source
}
if (-not (Test-Path -LiteralPath $Icon)) {
    throw "Windows icon not found: $Icon"
}

$Shell = New-Object -ComObject WScript.Shell
$Locations = @(
    [Environment]::GetFolderPath("Desktop"),
    (Join-Path ([Environment]::GetFolderPath("StartMenu")) "Programs")
)

foreach ($Location in $Locations) {
    $ShortcutPath = Join-Path $Location "AgInTi Spectrum Studio.lnk"
    $Shortcut = $Shell.CreateShortcut($ShortcutPath)
    $Shortcut.TargetPath = $Pythonw
    $Shortcut.Arguments = "-m spectral --port $Port"
    $Shortcut.WorkingDirectory = $Root
    $Shortcut.IconLocation = "$Icon,0"
    $Shortcut.Description = "AgInTi Spectrum Studio C12880MA workbench"
    $Shortcut.Save()
    Write-Output $ShortcutPath
}

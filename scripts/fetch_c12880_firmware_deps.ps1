[CmdletBinding()]
param(
    [string]$Destination = (Join-Path $PSScriptRoot '..\firmware\private\official-sources')
)

$ErrorActionPreference = 'Stop'
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$dependencies = @(
    @{ Name='stm32h7xx-hal-driver'; Url='https://github.com/STMicroelectronics/stm32h7xx-hal-driver.git'; Commit='c5e70527126710a6415929ff10c1fd1f40394b1e' },
    @{ Name='cmsis-device-h7'; Url='https://github.com/STMicroelectronics/cmsis-device-h7.git'; Commit='de8243d2c15f87936f28a49fcd9e6f5ba10fc233' },
    @{ Name='CMSIS_5'; Url='https://github.com/ARM-software/CMSIS_5.git'; Commit='55b19837f5703e418ca37894d5745b1dc05e4c91' },
    @{ Name='stm32-mw-usb-device'; Url='https://github.com/STMicroelectronics/stm32-mw-usb-device.git'; Commit='2df324bd60d4b0bb27404fd70b1c089b467f0e09' }
)

foreach ($dependency in $dependencies) {
    $path = Join-Path $Destination $dependency.Name
    if (-not (Test-Path (Join-Path $path '.git'))) {
        git clone --filter=blob:none $dependency.Url $path
    }
    git -C $path fetch --depth 1 origin $dependency.Commit
    git -C $path checkout --detach $dependency.Commit
}

Write-Host "Pinned firmware dependencies are ready under $Destination"


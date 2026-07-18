$ErrorActionPreference = 'Stop'
$Project = Split-Path -Parent $PSScriptRoot
Set-Location $Project
uv sync --extra vendor
uv run spectrum-studio @args


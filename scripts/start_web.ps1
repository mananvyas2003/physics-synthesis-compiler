# Start the chat UI (browser). Loads .env from repo root.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root
if (-not (Test-Path ".\synth.exe") -and -not (Test-Path ".\build\synth.exe")) {
  Write-Host "Build synth.exe first."
  exit 1
}
Write-Host "Starting http://127.0.0.1:8765/  (Ctrl+C to stop)"
python web\server.py

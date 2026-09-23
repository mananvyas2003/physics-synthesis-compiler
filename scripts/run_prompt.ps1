# Interactive prompt → schematic via the offline C NLP frontend.
# Usage:  .\scripts\run_prompt.ps1
# Or:     .\scripts\run_prompt.ps1 -OutDir out_live

param(
  [string]$OutDir = "out_live",
  [string]$Synth = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root
$env:SYNTH_FIXTURE_ROOT = $root
# Seed/fixture IR embeds DEMO parts[]; keep inserting them if a catalogue is set.
if (-not $env:SYNTH_ALLOW_IR_PARTS) { $env:SYNTH_ALLOW_IR_PARTS = "1" }

if (-not $Synth) {
  if (Test-Path ".\synth.exe") { $Synth = ".\synth.exe" }
  elseif (Test-Path ".\build\synth.exe") { $Synth = ".\build\synth.exe" }
  elseif (Test-Path ".\build\synth") { $Synth = ".\build\synth" }
  else { throw "synth binary not found; build first" }
}

Write-Host ""
Write-Host "Frontend: offline C NLP"
Write-Host "Output: $OutDir"
Write-Host "Enter a prompt (empty line to quit)."
Write-Host ""

$n = 0
while ($true) {
  $prompt = Read-Host "prompt"
  if ([string]::IsNullOrWhiteSpace($prompt)) { break }
  $n++
  $dest = Join-Path $OutDir ("run_{0:d3}" -f $n)
  New-Item -ItemType Directory -Force -Path $dest | Out-Null

  & $Synth generate --prompt-text $prompt -o $dest

  if ($LASTEXITCODE -eq 0) {
    Write-Host "OK → $dest\design.kicad_sch"
  } else {
    Write-Host "FAILED (exit $LASTEXITCODE)"
  }
  Write-Host ""
}

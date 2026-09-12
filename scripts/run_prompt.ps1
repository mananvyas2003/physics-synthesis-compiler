# Interactive prompt → schematic (live Gemini if GEMINI_API_KEY is set).
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

if (-not $Synth) {
  if (Test-Path ".\synth.exe") { $Synth = ".\synth.exe" }
  elseif (Test-Path ".\build\synth.exe") { $Synth = ".\build\synth.exe" }
  elseif (Test-Path ".\build\synth") { $Synth = ".\build\synth" }
  else { throw "synth binary not found; build first" }
}

if (-not $env:GEMINI_API_KEY -and -not $env:SYNTH_LLM_API_KEY) {
  Write-Host ""
  Write-Host "GEMINI_API_KEY is not set in this shell."
  Write-Host "Paste your key (input hidden), or press Enter to use offline corpus mode:"
  $secure = Read-Host -AsSecureString
  if ($secure.Length -gt 0) {
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure)
    try {
      $env:GEMINI_API_KEY = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    } finally {
      [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
  }
}

$live = ($env:GEMINI_API_KEY -or $env:SYNTH_LLM_API_KEY)
Write-Host ""
Write-Host "Mode: $(if ($live) { 'LIVE Gemini' } else { 'OFFLINE corpus (--offline-prompt)' })"
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

  if ($live) {
    & $Synth generate --prompt-text $prompt -o $dest
  } else {
    Write-Host "(offline) mapping fixtures/prompts/001.txt — set GEMINI_API_KEY for free-text"
    & $Synth generate --prompt fixtures/prompts/001.txt --offline-prompt -o $dest
  }

  if ($LASTEXITCODE -eq 0) {
    Write-Host "OK → $dest\design.kicad_sch"
  } else {
    Write-Host "FAILED (exit $LASTEXITCODE)"
  }
  Write-Host ""
}

[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Name = 'MoveBeatTrackAssistant'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Venv = Join-Path $ProjectRoot '.venv-assistant'
$Python = Join-Path $Venv 'Scripts\python.exe'
$Output = Join-Path $ProjectRoot 'out\assistant'
$Work = Join-Path $ProjectRoot 'build-assistant'

if (-not (Test-Path -LiteralPath $Python)) {
    python -m venv $Venv
}
& $Python -m pip install --disable-pip-version-check -r (Join-Path $ProjectRoot 'assistant\requirements-build.txt')
if ($LASTEXITCODE -ne 0) {
    throw "Installing assistant build dependencies failed with exit code $LASTEXITCODE"
}
& $Python -m PyInstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --name $Name `
    --paths $ProjectRoot `
    --hidden-import tools.beatsaver_import `
    --hidden-import tools.ftp_upload `
    --hidden-import tools.validate_bundle `
    --distpath $Output `
    --workpath (Join-Path $Work 'work') `
    --specpath $Work `
    (Join-Path $ProjectRoot 'assistant\movebeat_assistant.py')
if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller failed with exit code $LASTEXITCODE"
}

$Executable = Join-Path $Output "$Name.exe"
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "PyInstaller did not produce $Executable"
}
$HashLine = (Get-FileHash -Algorithm SHA256 -LiteralPath $Executable |
    ForEach-Object { "$($_.Hash.ToLower())  $Name.exe" })
$HashLine | Set-Content -Encoding ascii -LiteralPath (Join-Path $Output "$Name.sha256.txt")
if ($Name -eq 'MoveBeatTrackAssistant') {
    $HashLine | Set-Content -Encoding ascii -LiteralPath (Join-Path $Output 'SHA256SUMS.txt')
}
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'LICENSE') -Destination $Output -Force
Copy-Item -LiteralPath (Join-Path $ProjectRoot 'assistant\THIRD_PARTY_NOTICES.md') -Destination $Output -Force
Write-Host "Assistant: $Executable"

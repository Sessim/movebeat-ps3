param(
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$LevelId = '132ce'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$app = (Resolve-Path -LiteralPath (Join-Path $project 'out/rpcs3-19675/app')).Path
$usrdir = Join-Path $app 'dev_hdd0/game/MOVEBEAT1/USRDIR'
$boot = Join-Path $usrdir 'EBOOT.BIN'
$demo = Join-Path $usrdir 'demo_mode.ini'
$self = Join-Path $project 'out/ps3/movebeat.self'
$exe = Join-Path $app 'rpcs3.exe'

foreach ($path in @($boot, $self, $exe)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
$level = Join-Path (Join-Path $usrdir 'levels') $LevelId
if (-not (Test-Path -LiteralPath (Join-Path $level 'level.ini') -PathType Leaf)) {
    throw "Level is not installed in this RPCS3 profile: $LevelId"
}
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close RPCS3 before updating the preview profile.'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $project ("out/rpcs3-preview-backups/$stamp-$([guid]::NewGuid().ToString('N').Substring(0, 8))")
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $boot -Destination (Join-Path $backup 'EBOOT.BIN')
if (Test-Path -LiteralPath $demo -PathType Leaf) {
    Copy-Item -LiteralPath $demo -Destination (Join-Path $backup 'demo_mode.ini')
}

Copy-Item -LiteralPath $self -Destination $boot -Force
$sourceHash = (Get-FileHash -LiteralPath $self -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $boot -Algorithm SHA256).Hash
if ($installedHash -ne $sourceHash) {
    throw "Preview EBOOT hash mismatch; backup is at $backup"
}
Set-Content -LiteralPath $demo -Encoding Ascii -Value @('enabled=1', "level_id=$LevelId")

Write-Output "Preview EBOOT updated: $boot"
Write-Output "SHA-256: $installedHash"
Write-Output "Synthetic Move demo level: $LevelId"
Write-Output "Previous files backed up: $backup"
Write-Output "Launch RPCS3 from: $exe"
Write-Output 'This updates only the local emulator preview, not the PS3 package.'

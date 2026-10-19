param(
    [string]$PackagePath = 'out/ps3/movebeat.pkg',
    [string[]]$LevelIds = @('132ce'),
    [ValidateRange(0, 63)][int]$ChartIndex = 0,
    [switch]$AllCharts,
    [switch]$TrackingRecovery,
    [ValidateRange(0, 600000)][int]$CaptureAtMs = 0,
    [ValidateRange(5, 600)][int]$DurationSeconds = 12,
    # Emulate a TV that takes 1080p, so the game picks its 1080p output.
    [switch]$FullHd
)

$ErrorActionPreference = 'Stop'
$LevelIds = @($LevelIds | ForEach-Object { $_ -split ',' } |
    ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($LevelIds.Count -eq 0) { throw 'At least one level ID is required.' }
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$runtime = (Resolve-Path -LiteralPath (Join-Path $project 'out/rpcs3-19675/app')).Path
$package = (Resolve-Path -LiteralPath $PackagePath).Path
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close all RPCS3 instances before testing package installation.'
}

$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' +
    [guid]::NewGuid().ToString('N').Substring(0, 8)
$app = Join-Path $project "out/rpcs3-pkg-smoke-runs/$runId/app"
New-Item -ItemType Directory -Path $app -Force | Out-Null

# Copy the emulator and firmware, but not the installed game or old caches.
Copy-Item -Path (Join-Path $runtime '*.dll') -Destination $app
foreach ($name in @('rpcs3.exe', 'RPCS3.buf')) {
    Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination $app
}
foreach ($name in @('qt6', 'config', 'dev_flash', 'dev_flash2',
                    'dev_flash3', 'GuiConfigs', 'Icons', 'sounds', 'patches')) {
    $source = Join-Path $runtime $name
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $app -Recurse
    }
}

# The game draws on the RSX. RPCS3 keeps GPU output in host textures unless
# "Write Color Buffers" copies it back, so PPU frame captures need it on.
$isolatedConfig = Join-Path $app 'config/config.yml'
if (Test-Path -LiteralPath $isolatedConfig -PathType Leaf) {
    $configText = Get-Content -LiteralPath $isolatedConfig -Raw
    $configText = $configText -replace '(?m)^(\s*Write Color Buffers:\s*)false',
        '${1}true'
    if ($FullHd) {
        $configText = $configText -replace '(?m)^(\s*Resolution:\s*)\S+', '${1}1920x1080'
    }
    Set-Content -LiteralPath $isolatedConfig -Value $configText -NoNewline `
        -Encoding UTF8
}

$installer = Start-Process -FilePath (Join-Path $app 'rpcs3.exe') `
    -ArgumentList @('--headless', '--installpkg', "`"$package`"") `
    -WorkingDirectory $app -WindowStyle Hidden -PassThru -Wait
$installerExitPath = Join-Path $app 'installpkg-exit-code.txt'
Set-Content -LiteralPath $installerExitPath -Encoding Ascii `
    -Value ([string]$installer.ExitCode)
$installerLog = Join-Path $app 'log/RPCS3.log'
if (Test-Path -LiteralPath $installerLog -PathType Leaf) {
    Copy-Item -LiteralPath $installerLog `
        -Destination (Join-Path $app 'RPCS3-installpkg.log') -Force
}
if ($installer.ExitCode -ne 0) {
    Write-Warning "RPCS3 installer exited with $($installer.ExitCode); validating installed files and boot next."
}

$usrdir = Join-Path $app 'dev_hdd0/game/MOVEBEAT1/USRDIR'
$boot = Join-Path $usrdir 'EBOOT.BIN'
if (-not (Test-Path -LiteralPath $boot -PathType Leaf) -or
    (Get-Item -LiteralPath $boot).Length -eq 0) {
    throw "Installed package has no EBOOT: $boot"
}
foreach ($name in @('PARAM.SFO', 'ICON0.PNG', 'PIC1.PNG')) {
    $asset = Join-Path (Split-Path -Parent $usrdir) $name
    if (-not (Test-Path -LiteralPath $asset -PathType Leaf) -or
        (Get-Item -LiteralPath $asset).Length -eq 0) {
        throw "Installed package is missing $name"
    }
}
foreach ($id in $LevelIds) {
    if ($id -notmatch '^[A-Za-z0-9_-]+$') { throw "Unsafe level ID: $id" }
    if (-not (Test-Path -LiteralPath (Join-Path $usrdir "levels/$id/level.ini"))) {
        throw "Installed package is missing level $id"
    }
}

Write-Output "Installed PKG SHA-256: $((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash)"
Write-Output "Isolated RPCS3 profile: $app"
$smokeArguments = @(
    '-AppPath', $app,
    '-UseInstalledEboot',
    '-LevelIds', ($LevelIds -join ','),
    '-DurationSeconds', [string]$DurationSeconds
)
if ($AllCharts) {
    $smokeArguments += '-AllCharts'
} else {
    $smokeArguments += @('-ChartIndex', [string]$ChartIndex)
}
if ($TrackingRecovery) { $smokeArguments += '-TrackingRecovery' }
if ($CaptureAtMs -gt 0) { $smokeArguments += @('-CaptureAtMs', [string]$CaptureAtMs) }
& powershell -NoProfile -ExecutionPolicy Bypass -File `
    (Join-Path $PSScriptRoot 'rpcs3_smoke.ps1') @smokeArguments
if ($LASTEXITCODE -ne 0) { throw "Installed package smoke failed ($LASTEXITCODE)" }

param(
    # An installed RPCS3 profile, e.g. out/rpcs3-pkg-smoke-runs/<run>/app.
    [Parameter(Mandatory = $true)]
    [string]$AppPath,
    [ValidateRange(10, 600)]
    [int]$TimeoutSeconds = 180
)

# Runs the game's scripted library navigation (demo_mode.ini menu_test=1)
# and reports the startup scan time and the cost of each navigation step.
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$app = if ([IO.Path]::IsPathRooted($AppPath)) {
    (Resolve-Path -LiteralPath $AppPath).Path
} else {
    (Resolve-Path -LiteralPath (Join-Path $project $AppPath)).Path
}
$exe = Join-Path $app 'rpcs3.exe'
$usrdir = Join-Path $app 'dev_hdd0/game/MOVEBEAT1/USRDIR'
$boot = Join-Path $usrdir 'EBOOT.BIN'
$demo = Join-Path $usrdir 'demo_mode.ini'
$demoBackup = Join-Path $usrdir 'demo_mode.menu-smoke-backup'
$statusPath = Join-Path $usrdir 'run_status.ini'

foreach ($path in @($exe, $boot)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required menu-smoke file is missing: $path"
    }
}
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close all RPCS3 instances before the menu smoke.'
}
if (Test-Path -LiteralPath $demoBackup) {
    throw "Stale demo config backup exists: $demoBackup"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportDir = Join-Path $project "out/rpcs3-menu-smoke/$stamp"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$baseline = if (Test-Path -LiteralPath $statusPath) {
    (Get-Item -LiteralPath $statusPath).LastWriteTimeUtc
} else { [datetime]::MinValue }
$hadDemo = Test-Path -LiteralPath $demo -PathType Leaf
if ($hadDemo) { Move-Item -LiteralPath $demo -Destination $demoBackup }
Set-Content -LiteralPath $demo -Encoding Ascii -Value @('enabled=1', 'menu_test=1')

$process = $null
$status = @{}
$outcome = 'timeout'
try {
    $process = Start-Process -FilePath $exe -ArgumentList @("`"$boot`"") `
        -WorkingDirectory $app -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $lines = $null
        try {
            if ((Test-Path -LiteralPath $statusPath -PathType Leaf) -and
                (Get-Item -LiteralPath $statusPath -ErrorAction Stop).LastWriteTimeUtc -gt $baseline) {
                $lines = Get-Content -LiteralPath $statusPath -ErrorAction Stop
            }
        } catch [System.IO.IOException] {
        } catch [System.Management.Automation.ItemNotFoundException] {
        }
        if ($lines) {
            $status = @{}
            foreach ($line in $lines) {
                $separator = $line.IndexOf('=')
                if ($separator -gt 0) {
                    $status[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
                }
            }
            if ($status['phase'] -eq 'menu_test_done') { $outcome = 'done'; break }
            if ($status['state'] -eq 'fatal') { $outcome = 'fatal'; break }
        }
        $process.Refresh()
        if ($process.HasExited) { $outcome = 'exited'; break }
        Start-Sleep -Milliseconds 250
    }
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            if (-not $process.WaitForExit(5000)) {
                Stop-Process -Id $process.Id
                $process.WaitForExit()
            }
        }
    }
    if (Test-Path -LiteralPath $demo) { Remove-Item -LiteralPath $demo }
    if ($hadDemo -and (Test-Path -LiteralPath $demoBackup)) {
        Move-Item -LiteralPath $demoBackup -Destination $demo
    }
}

if (Test-Path -LiteralPath $statusPath) {
    Copy-Item -LiteralPath $statusPath -Destination (Join-Path $reportDir 'run_status.ini')
}
$report = [pscustomobject]@{
    passed = $outcome -eq 'done'
    outcome = $outcome
    renderer = $status['renderer']
    level_count = [int]$status['level_count']
    library_scan_ms = [int]$status['library_scan_ms']
    library_cards_loaded = [int]$status['library_cards_loaded']
    menu_steps = [int]$status['menu_steps']
    menu_step_avg_us = [int]$status['menu_step_avg_us']
    menu_step_max_us = [int]$status['menu_step_max_us']
    menu_idle_max_us = [int]$status['menu_idle_max_us']
    batch_max_vertices = [int]$status['batch_max_vertices']
}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reportDir 'report.json') -Encoding Ascii
$report | Format-List
Write-Output "Menu-smoke evidence: $reportDir"
if (-not $report.passed) { throw "Menu smoke did not finish ($outcome)." }

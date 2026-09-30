param(
    [Parameter(Mandatory = $true)]
    [string]$AppPath,
    [ValidateRange(5, 180)]
    [int]$TimeoutSeconds = 30
)

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
$demoBackup = Join-Path $usrdir 'demo_mode.startup-smoke-backup'
$statusPath = Join-Path $usrdir 'run_status.ini'
$startupErrorPath = Join-Path $usrdir 'startup_error.txt'
$firstFramePath = Join-Path $usrdir 'first_frame.ppm'

foreach ($path in @($exe, $boot, $statusPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required startup-smoke file is missing: $path"
    }
}
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close all RPCS3 instances before the isolated startup smoke.'
}
if (Test-Path -LiteralPath $demoBackup) {
    throw "Stale demo config backup exists: $demoBackup"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportDir = Join-Path $project "out/rpcs3-startup-smoke/$stamp"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$baseline = (Get-Item -LiteralPath $statusPath).LastWriteTimeUtc
$frameBaseline = if (Test-Path -LiteralPath $firstFramePath) {
    (Get-Item -LiteralPath $firstFramePath).LastWriteTimeUtc
} else { [datetime]::MinValue }
$startupErrorBaseline = if (Test-Path -LiteralPath $startupErrorPath) {
    (Get-Item -LiteralPath $startupErrorPath).LastWriteTimeUtc
} else { [datetime]::MinValue }
$hadDemo = Test-Path -LiteralPath $demo -PathType Leaf
if ($hadDemo) { Move-Item -LiteralPath $demo -Destination $demoBackup }

$process = $null
$status = @{}
$outcome = 'timeout'
$firstFrameCaptured = $false
$startupErrorCaptured = $false
try {
    $process = Start-Process -FilePath $exe -ArgumentList @("`"$boot`"") `
        -WorkingDirectory $app -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $statusItem = $null
        $statusLines = $null
        if (Test-Path -LiteralPath $statusPath -PathType Leaf) {
            try {
                # writeRunStatus replaces this file through a temporary path;
                # on filesystems where rename cannot replace, there is a brief
                # remove/rename gap. Treat that as a poll miss, not a test error.
                $statusItem = Get-Item -LiteralPath $statusPath -ErrorAction Stop
                if ($statusItem.LastWriteTimeUtc -gt $baseline) {
                    $statusLines = Get-Content -LiteralPath $statusPath -ErrorAction Stop
                }
            } catch [System.IO.IOException] {
                Start-Sleep -Milliseconds 50
                continue
            } catch [System.Management.Automation.ItemNotFoundException] {
                Start-Sleep -Milliseconds 50
                continue
            }
            if ($statusItem -and $statusLines -and
                $statusItem.LastWriteTimeUtc -gt $baseline) {
                $status = @{}
                foreach ($line in $statusLines) {
                    $separator = $line.IndexOf('=')
                    if ($separator -gt 0) {
                        $status[$line.Substring(0, $separator)] =
                            $line.Substring($separator + 1)
                    }
                }
                if (Test-Path -LiteralPath $firstFramePath -PathType Leaf) {
                    $firstFrameCaptured =
                        (Get-Item -LiteralPath $firstFramePath).LastWriteTimeUtc -gt $frameBaseline
                }
                if (Test-Path -LiteralPath $startupErrorPath -PathType Leaf) {
                    $startupErrorCaptured =
                        (Get-Item -LiteralPath $startupErrorPath).LastWriteTimeUtc -gt $startupErrorBaseline
                }
                if ($status['phase'] -eq 'select' -and $firstFrameCaptured) {
                    $outcome = 'library'
                    break
                }
                if ($status['phase'] -eq 'gem_initialization' -and
                    $status['state'] -eq 'fatal' -and
                    $firstFrameCaptured -and
                    $startupErrorCaptured) {
                    $outcome = 'visible_gem_error'
                    break
                }
            }
        }
        $process.Refresh()
        if ($process.HasExited) { break }
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
    if ($hadDemo -and (Test-Path -LiteralPath $demoBackup)) {
        Move-Item -LiteralPath $demoBackup -Destination $demo
    }
}

if ($firstFrameCaptured) {
    Copy-Item -LiteralPath $firstFramePath -Destination `
        (Join-Path $reportDir 'first_frame.ppm')
}
if ($startupErrorCaptured) {
    Copy-Item -LiteralPath $startupErrorPath -Destination `
        (Join-Path $reportDir 'startup_error.txt')
}
$report = [pscustomobject]@{
    passed = $outcome -in @('library', 'visible_gem_error')
    outcome = $outcome
    app_path = $app
    state = $status['state']
    phase = $status['phase']
    error_stage = $status['error_stage']
    error_code = $status['error_code']
    startup_error_captured = $startupErrorCaptured
    camera_read_count = $status['camera_read_count']
    gem_connected_count = $status['gem_connected_count']
    left_tracked = $status['left_tracked']
    right_tracked = $status['right_tracked']
    first_frame_captured = $firstFrameCaptured
}
$reportPath = Join-Path $reportDir 'report.json'
$report | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding Ascii
$report | Format-List
Write-Output "Startup-smoke evidence: $reportDir"
if (-not $report.passed) { throw "Normal startup failed to reach a visible state ($outcome)." }

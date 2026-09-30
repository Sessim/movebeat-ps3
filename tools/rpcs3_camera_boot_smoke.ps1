param(
    [ValidateRange(5, 60)][int]$WaitSeconds = 20
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$app = (Resolve-Path -LiteralPath (Join-Path $project 'out/rpcs3-19675/app')).Path
$usrdir = Join-Path $app 'dev_hdd0/game/MOVEBEAT1/USRDIR'
$exe = Join-Path $app 'rpcs3.exe'
$boot = Join-Path $usrdir 'EBOOT.BIN'
$demo = Join-Path $usrdir 'demo_mode.ini'
$self = Join-Path $project 'out/ps3/movebeat.self'
$status = Join-Path $usrdir 'run_status.ini'
$startup = Join-Path $usrdir 'startup_error.txt'
$firstFrame = Join-Path $usrdir 'first_frame.ppm'
foreach ($path in @($exe, $boot, $self)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close RPCS3 before this isolated boot test.'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$report = Join-Path $project "out/rpcs3-camera-boot/$stamp"
New-Item -ItemType Directory -Path $report -Force | Out-Null
$bootBackup = Join-Path $report 'EBOOT.BIN.before-test'
$demoBackup = Join-Path $report 'demo_mode.ini.before-test'
$hadDemo = Test-Path -LiteralPath $demo -PathType Leaf
Copy-Item -LiteralPath $boot -Destination $bootBackup
if ($hadDemo) { Copy-Item -LiteralPath $demo -Destination $demoBackup }
$process = $null
try {
    Copy-Item -LiteralPath $self -Destination $boot -Force
    if ($hadDemo) { Remove-Item -LiteralPath $demo }
    $launched = Get-Date
    $process = Start-Process -FilePath $exe -ArgumentList @( "`"$boot`"" ) `
        -WorkingDirectory $app -WindowStyle Hidden -PassThru
    $deadline = $launched.AddSeconds($WaitSeconds)
    do {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($process.HasExited) { break }
        if (Test-Path -LiteralPath $status -PathType Leaf) {
            $statusItem = Get-Item -LiteralPath $status
            if ($statusItem.LastWriteTime -ge $launched) {
                $lines = Get-Content -LiteralPath $status
                if ($lines -contains 'state=fatal' -or
                    $lines -contains 'state=running') { break }
            }
        }
    } while ((Get-Date) -lt $deadline)
    foreach ($path in @($status, $startup, $firstFrame)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $item = Get-Item -LiteralPath $path
            if ($item.LastWriteTime -ge $launched) {
                Copy-Item -LiteralPath $path -Destination $report
            }
        }
    }
    $log = Join-Path $app 'log/RPCS3.log'
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        Copy-Item -LiteralPath $log -Destination $report
    }
} finally {
    if ($null -ne $process) {
        $process.Refresh()
        if (-not $process.HasExited) {
            $process.CloseMainWindow() | Out-Null
            Start-Sleep -Seconds 2
            $process.Refresh()
            if (-not $process.HasExited) { Stop-Process -Id $process.Id }
        }
    }
    Copy-Item -LiteralPath $bootBackup -Destination $boot -Force
    if ($hadDemo) {
        Copy-Item -LiteralPath $demoBackup -Destination $demo -Force
    } elseif (Test-Path -LiteralPath $demo) {
        Remove-Item -LiteralPath $demo
    }
}
Write-Output "Camera boot evidence: $report"
if (Test-Path -LiteralPath (Join-Path $report 'run_status.ini')) {
    Get-Content -LiteralPath (Join-Path $report 'run_status.ini') |
        Select-String '^(state|phase|error_stage|error_code)=' |
        ForEach-Object { $_.Line }
}

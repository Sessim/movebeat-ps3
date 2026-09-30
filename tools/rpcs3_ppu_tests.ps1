param(
    [string]$PackagePath = 'out/ps3/movebeat-tests.pkg',
    [ValidateRange(30, 600)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$runtime = (Resolve-Path -LiteralPath (Join-Path $project 'out/rpcs3-19675/app')).Path
$package = (Resolve-Path -LiteralPath $PackagePath).Path
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close all RPCS3 instances before running the isolated PPU test package.'
}

$runId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' +
    [guid]::NewGuid().ToString('N').Substring(0, 8)
$app = Join-Path $project "out/rpcs3-ppu-test-runs/$runId/app"
New-Item -ItemType Directory -Path $app -Force | Out-Null
Copy-Item -Path (Join-Path $runtime '*.dll') -Destination $app
foreach ($name in @('rpcs3.exe', 'RPCS3.buf')) {
    Copy-Item -LiteralPath (Join-Path $runtime $name) -Destination $app
}
foreach ($name in @('qt6', 'config', 'dev_flash', 'dev_flash2', 'dev_flash3',
                    'GuiConfigs', 'Icons', 'sounds', 'patches')) {
    $source = Join-Path $runtime $name
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $app -Recurse
    }
}

$installer = Start-Process -FilePath (Join-Path $app 'rpcs3.exe') `
    -ArgumentList @('--headless', '--installpkg', "`"$package`"") `
    -WorkingDirectory $app -WindowStyle Hidden -PassThru -Wait
Set-Content -LiteralPath (Join-Path $app 'installpkg-exit-code.txt') `
    -Encoding Ascii -Value ([string]$installer.ExitCode)
$boot = Join-Path $app 'dev_hdd0/game/MOVEBTST1/USRDIR/EBOOT.BIN'
if (-not (Test-Path -LiteralPath $boot -PathType Leaf) -or
    (Get-Item -LiteralPath $boot).Length -eq 0) {
    throw "Test PKG installation did not produce an EBOOT (exit $($installer.ExitCode))."
}
if ($installer.ExitCode -ne 0) {
    Write-Warning "RPCS3 installer exited with $($installer.ExitCode); installed EBOOT exists, running it."
}

$ttyLog = Join-Path $app 'log/TTY.log'
$emulatorLog = Join-Path $app 'log/RPCS3.log'
$process = Start-Process -FilePath (Join-Path $app 'rpcs3.exe') `
    -ArgumentList @('--headless', '--stdout', '--stderr', "`"$boot`"") `
    -WorkingDirectory $app -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$testOutput = ''
$testResult = ''
while ([DateTime]::UtcNow -lt $deadline) {
    foreach ($path in @($ttyLog, $emulatorLog)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        try {
            $stream = [System.IO.File]::Open(
                $path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
            try {
                $reader = [System.IO.StreamReader]::new($stream)
                $content = $reader.ReadToEnd()
                $reader.Dispose()
            } finally {
                $stream.Dispose()
            }
            if ($content) { $testOutput = $content }
            if ($testOutput -match 'All MoveBeat core tests passed\.') {
                $testResult = 'passed'
                break
            }
            if ($testOutput -match '\d+ test\(s\) failed\.') {
                $testResult = 'failed'
                break
            }
        } catch {
            # RPCS3 may briefly hold its log while flushing a PPU message.
        }
    }
    if ($testResult) { break }
    if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        $testResult = 'process-exited-without-result'
        break
    }
    Start-Sleep -Milliseconds 250
}

# The PPU test calls sys_process_exit, but headless RPCS3 can keep its outer
# process alive. Stop only the emulator process launched by this script after
# observing the test result; never terminate another RPCS3 instance.
$owned = Get-CimInstance Win32_Process -Filter "ProcessId = $($process.Id)" `
    -ErrorAction SilentlyContinue
$expectedExe = (Resolve-Path -LiteralPath (Join-Path $app 'rpcs3.exe')).Path
if ($owned -and [IO.Path]::GetFullPath($owned.ExecutablePath) -eq $expectedExe) {
    Stop-Process -Id $process.Id
    Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
}

Write-Output "Isolated PPU test profile: $app"
Write-Output "Test PKG SHA-256: $((Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash)"
Write-Output "Test result: $testResult"
if ($testOutput) { Write-Output $testOutput }
if ($testResult -ne 'passed') {
    throw "PPU test package did not report success. Logs: $ttyLog, $emulatorLog"
}

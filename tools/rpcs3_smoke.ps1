param(
    [string[]]$LevelIds = @('132ce'),
    [ValidateRange(0, 63)][int]$ChartIndex = 0,
    [switch]$AllCharts,
    [ValidateRange(5, 600)][int]$DurationSeconds = 35,
    [ValidateRange(30, 600)][int]$StartupTimeoutSeconds = 120,
    [switch]$TrackingRecovery,
    [ValidateRange(0, 600000)][int]$CaptureAtMs = 0,
    [string]$AppPath = '',
    [switch]$UseInstalledEboot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'run_status_reader.ps1')
$LevelIds = @($LevelIds | ForEach-Object { $_ -split ',' } |
    ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($LevelIds.Count -eq 0) { throw 'At least one level ID is required.' }
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$app = if ($AppPath) { (Resolve-Path -LiteralPath $AppPath).Path } else {
    (Resolve-Path -LiteralPath (Join-Path $project 'out/rpcs3-19675/app')).Path
}
$exe = Join-Path $app 'rpcs3.exe'
$usrdir = Join-Path $app 'dev_hdd0/game/MOVEBEAT1/USRDIR'
$boot = Join-Path $usrdir 'EBOOT.BIN'
$demo = Join-Path $usrdir 'demo_mode.ini'
$status = Join-Path $usrdir 'run_status.ini'
$self = Join-Path $project 'out/ps3/movebeat.self'
$ppmConverter = Join-Path $project 'tools/ppm_to_bmp.py'
$pythonCommand = Get-Command python.exe -ErrorAction SilentlyContinue
$cases = @()
if ($AllCharts -and $ChartIndex -ne 0) {
    throw 'Use either -ChartIndex or -AllCharts, not both.'
}

$requiredPaths = @($exe, $boot)
if (-not $UseInstalledEboot) { $requiredPaths += $self }
foreach ($path in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required file is missing: $path"
    }
}
if (Get-Process -Name rpcs3 -ErrorAction SilentlyContinue) {
    throw 'Close all existing RPCS3 instances before running this isolated smoke test.'
}
foreach ($id in $LevelIds) {
    if ($id -notmatch '^[A-Za-z0-9_-]+$') { throw "Unsafe level ID: $id" }
    $manifest = Join-Path $usrdir "levels/$id/level.ini"
    if (-not (Test-Path -LiteralPath $manifest)) {
        throw "Level is not installed in the RPCS3 library: $id"
    }
    $manifestLines = Get-Content -LiteralPath $manifest
    if ($AllCharts) {
        foreach ($line in $manifestLines) {
            if ($line -match '^chart([0-9]+)_difficulty=(.*)$') {
                $cases += [pscustomobject]@{
                    level_id = $id
                    chart_index = [int]$Matches[1]
                    expected_difficulty = $Matches[2]
                }
            }
        }
    } else {
        $chartPrefix = "chart${ChartIndex}_difficulty="
        $chartLine = $manifestLines |
            Where-Object { $_.StartsWith($chartPrefix, [StringComparison]::Ordinal) } |
            Select-Object -First 1
        if (-not $chartLine) { throw "Level $id has no chart at index $ChartIndex" }
        $cases += [pscustomobject]@{
            level_id = $id
            chart_index = $ChartIndex
            expected_difficulty = $chartLine.Substring($chartPrefix.Length)
        }
    }
}
if ($cases.Count -eq 0) { throw 'No playable charts found in the selected levels.' }

# Keep short diagnostic runs meaningful: a chart with a long intro cannot
# produce a hit before its first note arrives, so honor at least that much
# gameplay time plus a small scoring margin.
foreach ($case in $cases) {
    $manifest = Join-Path $usrdir "levels/$($case.level_id)/level.ini"
    $chartFilePrefix = "chart$($case.chart_index)_file="
    $chartFileLine = Get-Content -LiteralPath $manifest |
        Where-Object { $_.StartsWith($chartFilePrefix, [StringComparison]::Ordinal) } |
        Select-Object -First 1
    if (-not $chartFileLine) {
        throw "Level $($case.level_id) is missing chart file metadata for index $($case.chart_index)"
    }
    $chartFileName = $chartFileLine.Substring($chartFilePrefix.Length)
    if ([IO.Path]::GetFileName($chartFileName) -ne $chartFileName) {
        throw "Unsafe chart filename in level $($case.level_id): $chartFileName"
    }
    $chartPath = Join-Path (Split-Path -Parent $manifest) $chartFileName
    if (-not (Test-Path -LiteralPath $chartPath -PathType Leaf)) {
        throw "Chart file is missing: $chartPath"
    }
    $firstNoteLine = Get-Content -LiteralPath $chartPath |
        Where-Object { $_ -match '^[0-9]+,[0-3],[0-2],[LR],' } |
        Select-Object -First 1
    $firstNoteMs = 0
    if ($firstNoteLine -and $firstNoteLine -match '^([0-9]+),') {
        $firstNoteMs = [int]$Matches[1]
    }
    Add-Member -InputObject $case -NotePropertyName first_note_ms `
        -NotePropertyValue $firstNoteMs -Force
}

function Read-RunStatus([string]$path) {
    $values = @{}
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $values }
    try { $lines = Read-SharedLines -Path $path } catch { return $values }
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
    }
    return $values
}

function Test-StatusLevelId([string]$RequestedId, [string]$ReportedId) {
    if ($ReportedId -eq $RequestedId) { return $true }
    # The built-in bundle uses a versioned runtime key while its directory and
    # manifest retain the stable user-facing ID.
    return $RequestedId -eq 'movebeat_first_steps' -and
        $ReportedId -eq 'movebeat_first_steps_v1'
}

function New-FrameTimingSnapshot([hashtable]$Values) {
    return [pscustomobject]@{
        heartbeat = [int]$Values['heartbeat']
        uptime_ms = [int]$Values['uptime_ms']
        frame = [int]$Values['frame']
        audio_time_ms = [int]$Values['audio_time_ms']
        samples = [int]$Values['frame_work_samples']
        work_avg_us = [int]$Values['frame_work_avg_us']
        work_max_us = [int]$Values['frame_work_max_us']
        work_over_60hz = [int]$Values['frame_work_over_60hz']
        active_avg_us = [int]$Values['frame_active_avg_us']
        active_max_us = [int]$Values['frame_active_max_us']
        active_over_60hz = [int]$Values['frame_active_over_60hz']
        render_avg_us = [int]$Values['frame_render_avg_us']
        render_max_us = [int]$Values['frame_render_max_us']
        clear_avg_us = [int]$Values['frame_clear_avg_us']
        scene_avg_us = [int]$Values['frame_scene_avg_us']
        notes_avg_us = [int]$Values['frame_gpu_notes_avg_us']
    }
}

function Save-Rpcs3FailureEvidence([string]$ReportDir, [string]$App,
                                   [string]$StatusPath, [string]$LevelId,
                                   [int]$ChartIndex, [string]$Difficulty,
                                   [string]$Stage, [object]$ExitCode,
                                   [bool]$ProcessAlive, [datetime]$Baseline) {
    $failureDir = Join-Path $ReportDir "$LevelId-chart$ChartIndex-failure"
    New-Item -ItemType Directory -Path $failureDir -Force | Out-Null
    $log = Join-Path $App 'log/RPCS3.log'
    $tty = Join-Path $App 'log/TTY.log'
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        Copy-Item -LiteralPath $log -Destination (Join-Path $failureDir 'RPCS3.log') -Force
    }
    if (Test-Path -LiteralPath $tty -PathType Leaf) {
        Copy-Item -LiteralPath $tty -Destination (Join-Path $failureDir 'TTY.log') -Force
    }
    $relevantLog = @()
    if (Test-Path -LiteralPath $log -PathType Leaf) {
        $relevantLog = @(Select-String -LiteralPath $log `
            -Pattern 'sys_rsx_context_allocate|PPU.*thread context|SYS:|Assertion|Fatal|Unhandled' `
            -ErrorAction SilentlyContinue | Select-Object -Last 8 |
            ForEach-Object { $_.Line })
    }
    $renderer = 'unknown'
    $config = Join-Path $App 'config/config.yml'
    if (Test-Path -LiteralPath $config -PathType Leaf) {
        $rendererLine = Select-String -LiteralPath $config `
            -Pattern '^  Renderer:\s*(.+)$' | Select-Object -First 1
        if ($rendererLine -and $rendererLine.Matches[0].Groups.Count -gt 1) {
            $renderer = $rendererLine.Matches[0].Groups[1].Value.Trim()
        }
    }
    $freshStatus = $false
    $statusItem = $null
    try { $statusItem = Get-Item -LiteralPath $StatusPath -ErrorAction Stop }
    catch { }
    if ($statusItem -and $statusItem.LastWriteTimeUtc -gt $Baseline) {
        $freshStatus = $true
        Copy-Item -LiteralPath $StatusPath `
            -Destination (Join-Path $failureDir 'run_status.ini') -Force
    }
    $failureLines = @(
        "stage=$Stage",
        "rpcs3_exit_code=$ExitCode",
        "rpcs3_process_alive=$ProcessAlive",
        "renderer=$renderer",
        "fresh_run_status=$freshStatus"
    )
    if ($relevantLog.Count) {
        $failureLines += 'Relevant RPCS3 log excerpts:'
        $failureLines += $relevantLog
    }
    $failureDetails = Join-Path $failureDir 'failure.txt'
    Set-Content -LiteralPath $failureDetails -Encoding UTF8 -Value $failureLines
    return [pscustomobject]@{
        level_id = $LevelId
        chart_index = $ChartIndex
        chart_difficulty = $Difficulty
        passed = $false
        failure_stage = $Stage
        rpcs3_exit_code = $ExitCode
        rpcs3_process_alive = $ProcessAlive
        renderer = $renderer
        fresh_run_status = $freshStatus
        failure_details = $failureDetails
    }
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportDir = Join-Path $project "out/rpcs3-smoke/$stamp"
New-Item -ItemType Directory -Path $reportDir -Force | Out-Null
$bootBackup = Join-Path $reportDir 'EBOOT.BIN.before-smoke'
$demoBackup = Join-Path $reportDir 'demo_mode.ini.before-smoke'
$hadDemo = Test-Path -LiteralPath $demo -PathType Leaf
Copy-Item -LiteralPath $boot -Destination $bootBackup
if ($hadDemo) { Copy-Item -LiteralPath $demo -Destination $demoBackup }

$reports = @()
try {
    if (-not $UseInstalledEboot) {
        Copy-Item -LiteralPath $self -Destination $boot -Force
    }
    foreach ($case in $cases) {
        $id = $case.level_id
        $chartIndex = $case.chart_index
        $expectedDifficulty = $case.expected_difficulty
        $caseDurationSeconds = [Math]::Max($DurationSeconds,
            [int][Math]::Ceiling($case.first_note_ms / 1000.0) + 3)
        Write-Output "$id chart $chartIndex first note: $($case.first_note_ms) ms; gameplay window: ${caseDurationSeconds}s"
        $demoLines = @('enabled=1', "level_id=$id", "chart_index=$chartIndex")
        if ($TrackingRecovery) { $demoLines += 'tracking_loss_test=1' }
        # Optional extra frame at a chosen song time, e.g. when a wall passes.
        $timed = Join-Path $usrdir 'demo_timed_frame.ppm'
        if (Test-Path -LiteralPath $timed) { Remove-Item -LiteralPath $timed }
        if ($CaptureAtMs -gt 0) { $demoLines += "capture_at_ms=$CaptureAtMs" }
        Set-Content -LiteralPath $demo -Encoding Ascii -Value $demoLines
        $baseline = if (Test-Path -LiteralPath $status) {
            (Get-Item -LiteralPath $status).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $scored = Join-Path $usrdir 'demo_scored_frame.ppm'
        $perspective = Join-Path $usrdir 'demo_frame.ppm'
        $perspectiveBaseline = if (Test-Path -LiteralPath $perspective) {
            (Get-Item -LiteralPath $perspective).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $scoredBaseline = if (Test-Path -LiteralPath $scored) {
            (Get-Item -LiteralPath $scored).LastWriteTimeUtc
        } else { [datetime]::MinValue }
        $process = Start-Process -FilePath $exe -ArgumentList @("`"$boot`"") `
            -WorkingDirectory $app -WindowStyle Hidden -PassThru
        $started = $null
        $deadline = (Get-Date).AddSeconds(
            $caseDurationSeconds + $StartupTimeoutSeconds)
        $highestWorkUs = 0
        $overBudget = 0
        $highestActiveUs = 0
        $activeOverBudget = 0
        $frameSamples = [System.Collections.Generic.List[object]]::new()
        $highestHits = 0
        $audioSeen = $false
        $lastHeartbeat = ''
        try {
            while ((Get-Date) -lt $deadline) {
                $process.Refresh()
                if ($process.HasExited) {
                    $exitCode = $process.ExitCode
                    $failure = Save-Rpcs3FailureEvidence $reportDir $app $status `
                        $id $chartIndex $expectedDifficulty `
                        'rpcs3_exited_before_demo_play' $exitCode $false $baseline
                    $reports += $failure
                    throw "RPCS3 exited early for $id chart $chartIndex (code $exitCode). See $($failure.failure_details)"
                }
                $statusItem = $null
                try { $statusItem = Get-Item -LiteralPath $status -ErrorAction Stop }
                catch { }
                if ($null -ne $statusItem -and
                    $statusItem.LastWriteTimeUtc -gt $baseline) {
                    $current = Read-RunStatus $status
                    if (Test-StatusLevelId $id $current['level_id']) {
                        if ($current['phase'] -eq 'audio_error' -or
                            $current['state'] -eq 'fatal') {
                            throw "Game reported $($current['phase']) for $id; audio=$($current['audio_error_code'])"
                        }
                        if ($current['phase'] -eq 'demo_play' -and
                            $current['state'] -eq 'running') {
                            if ($null -eq $started) { $started = Get-Date }
                            if ($current['heartbeat'] -ne $lastHeartbeat) {
                                $lastHeartbeat = $current['heartbeat']
                                $frameSamples.Add((New-FrameTimingSnapshot $current))
                                $highestWorkUs = [Math]::Max($highestWorkUs,
                                    [int]$current['frame_work_max_us'])
                                $overBudget += [int]$current['frame_work_over_60hz']
                                $highestActiveUs = [Math]::Max($highestActiveUs,
                                    [int]$current['frame_active_max_us'])
                                $activeOverBudget += [int]$current['frame_active_over_60hz']
                                $highestHits = [Math]::Max($highestHits, [int]$current['hits'])
                                if ($current['audio_active'] -eq '1') { $audioSeen = $true }
                            }
                            if (((Get-Date) - $started).TotalSeconds -ge $caseDurationSeconds) {
                                break
                            }
                        }
                    }
                }
                Start-Sleep -Seconds 2
            }
            if ($null -eq $started) {
                $process.Refresh()
                $stillAlive = -not $process.HasExited
                $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
                $failure = Save-Rpcs3FailureEvidence $reportDir $app $status `
                    $id $chartIndex $expectedDifficulty 'demo_play_not_reached' `
                    $exitCode $stillAlive $baseline
                $reports += $failure
                throw "Game did not enter demo_play for $id chart $chartIndex; see $($failure.failure_details)"
            }
            if ((Get-Date) -ge $deadline) {
                $process.Refresh()
                $stillAlive = -not $process.HasExited
                $exitCode = if ($process.HasExited) { $process.ExitCode } else { $null }
                $failure = Save-Rpcs3FailureEvidence $reportDir $app $status `
                    $id $chartIndex $expectedDifficulty 'gameplay_timeout' `
                    $exitCode $stillAlive $baseline
                $reports += $failure
                throw "Timed out on $id chart $chartIndex; see $($failure.failure_details)"
            }
        } finally {
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.CloseMainWindow() | Out-Null
                Start-Sleep -Seconds 3
                $process.Refresh()
                if (-not $process.HasExited) { Stop-Process -Id $process.Id }
            }
        }
        $final = Read-RunStatus $status
        if ($final['chart_difficulty'] -ne $expectedDifficulty) {
            throw "Expected chart $expectedDifficulty at index $chartIndex for $id, got $($final['chart_difficulty'])"
        }
        if ($final['heartbeat'] -ne $lastHeartbeat) {
            $frameSamples.Add((New-FrameTimingSnapshot $final))
            $highestWorkUs = [Math]::Max($highestWorkUs,
                [int]$final['frame_work_max_us'])
            $overBudget += [int]$final['frame_work_over_60hz']
            $highestActiveUs = [Math]::Max($highestActiveUs,
                [int]$final['frame_active_max_us'])
            $activeOverBudget += [int]$final['frame_active_over_60hz']
            $highestHits = [Math]::Max($highestHits, [int]$final['hits'])
        }
        $reportName = if ($AllCharts) { "$id-chart$chartIndex" } else { $id }
        $levelReportDir = Join-Path $reportDir $reportName
        New-Item -ItemType Directory -Path $levelReportDir -Force | Out-Null
        Copy-Item -LiteralPath $status -Destination (Join-Path $levelReportDir 'run_status.ini')
        $scoredCaptured = (Test-Path -LiteralPath $scored) -and
            (Get-Item -LiteralPath $scored).LastWriteTimeUtc -gt $scoredBaseline -and
            (Get-Item -LiteralPath $scored).Length -gt (1280 * 720 * 3)
        $perspectiveCaptured = (Test-Path -LiteralPath $perspective) -and
            (Get-Item -LiteralPath $perspective).LastWriteTimeUtc -gt $perspectiveBaseline -and
            (Get-Item -LiteralPath $perspective).Length -gt (1280 * 720 * 3)
        $perspectiveBmpCaptured = $false
        $scoredBmpCaptured = $false
        if ($perspectiveCaptured) {
            $perspectivePpm = Join-Path $levelReportDir 'demo_frame.ppm'
            Copy-Item -LiteralPath $perspective -Destination $perspectivePpm
            if ($pythonCommand) {
                $perspectiveBmp = Join-Path $levelReportDir 'demo_frame_preview.bmp'
                & $pythonCommand.Source $ppmConverter $perspectivePpm $perspectiveBmp
                $perspectiveBmpCaptured = $LASTEXITCODE -eq 0 -and
                    (Test-Path -LiteralPath $perspectiveBmp -PathType Leaf)
                if (-not $perspectiveBmpCaptured) {
                    Write-Warning "Could not convert perspective frame to BMP: $perspectivePpm"
                }
            }
        }
        if ($CaptureAtMs -gt 0 -and (Test-Path -LiteralPath $timed -PathType Leaf)) {
            $timedPpm = Join-Path $levelReportDir 'demo_timed_frame.ppm'
            Copy-Item -LiteralPath $timed -Destination $timedPpm
            if ($pythonCommand) {
                & $pythonCommand.Source $ppmConverter $timedPpm `
                    (Join-Path $levelReportDir 'demo_timed_frame.bmp')
            }
        }
        if ($scoredCaptured) {
            $scoredPpm = Join-Path $levelReportDir 'demo_scored_frame.ppm'
            Copy-Item -LiteralPath $scored -Destination $scoredPpm
            if ($pythonCommand) {
                $scoredBmp = Join-Path $levelReportDir 'demo_scored_frame.bmp'
                & $pythonCommand.Source $ppmConverter $scoredPpm $scoredBmp
                $scoredBmpCaptured = $LASTEXITCODE -eq 0 -and
                    (Test-Path -LiteralPath $scoredBmp -PathType Leaf)
                if (-not $scoredBmpCaptured) {
                    Write-Warning "Could not convert scored frame to BMP: $scoredPpm"
                }
            }
        }
        $trackingPauses = [int]$final['tracking_pause_count']
        $trackingRecoveries = [int]$final['tracking_recovery_count']
        $audioPauseDelta = [Math]::Abs(
            [int]$final['tracking_resume_audio_ms'] -
            [int]$final['tracking_pause_audio_ms'])
        $scoreStable = [int]$final['tracking_pause_score'] -eq
            [int]$final['tracking_resume_score'] -and
            [int]$final['tracking_pause_hits'] -eq
            [int]$final['tracking_resume_hits'] -and
            [int]$final['tracking_pause_misses'] -eq
            [int]$final['tracking_resume_misses']
        $trackingPassed = -not $TrackingRecovery -or (
            $trackingPauses -eq 1 -and $trackingRecoveries -eq 1 -and
            $audioPauseDelta -le 25 -and $scoreStable)
        $gpuDrawCalls = [int]$final['gpu_note_draw_calls']
        $gpuMaxCubes = [int]$final['gpu_note_max_cubes']
        $gpuFallbackFrames = [int]$final['gpu_note_fallback_frames']
        $softwareDrawCalls = [int]$final['software_note_draw_calls']
        $softwareMaxCubes = [int]$final['software_note_max_cubes']
        $passed = $final['state'] -eq 'clean_exit' -and $audioSeen -and
            $highestHits -gt 0 -and $trackingPassed -and
            $scoredCaptured -and
            (($gpuDrawCalls -gt 0 -and $gpuMaxCubes -gt 0) -or
             ($softwareDrawCalls -gt 0 -and $softwareMaxCubes -gt 0)) -and
            $gpuFallbackFrames -eq 0
        $reports += [pscustomobject]@{
            level_id = $id
            chart_index = $chartIndex
            chart_difficulty = $final['chart_difficulty']
            first_note_ms = $case.first_note_ms
            duration_seconds = $caseDurationSeconds
            passed = $passed
            clean_exit = $final['state'] -eq 'clean_exit'
            audio_active_seen = $audioSeen
            hits = $highestHits
            highest_frame_work_us = $highestWorkUs
            observed_over_60hz = $overBudget
            highest_active_us = $highestActiveUs
            active_over_60hz = $activeOverBudget
            frame_timing = @($frameSamples.ToArray())
            tracking_pauses = $trackingPauses
            tracking_recoveries = $trackingRecoveries
            tracking_audio_pause_delta_ms = $audioPauseDelta
            tracking_score_stable = $scoreStable
            scored_frame_captured = $scoredCaptured
            perspective_frame_captured = $perspectiveCaptured
            scored_bmp_captured = $scoredBmpCaptured
            perspective_bmp_captured = $perspectiveBmpCaptured
            gpu_note_draw_calls = $gpuDrawCalls
            gpu_note_max_cubes = $gpuMaxCubes
            gpu_note_fallback_frames = $gpuFallbackFrames
            software_note_draw_calls = $softwareDrawCalls
            software_note_max_cubes = $softwareMaxCubes
        }
        if (-not $passed) { throw "Smoke test failed for $id; see $levelReportDir" }
    }
} finally {
    Copy-Item -LiteralPath $bootBackup -Destination $boot -Force
    if ($hadDemo) {
        Copy-Item -LiteralPath $demoBackup -Destination $demo -Force
    } elseif (Test-Path -LiteralPath $demo) {
        Remove-Item -LiteralPath $demo
    }
    ConvertTo-Json -InputObject @($reports) -Depth 3 |
        Set-Content -LiteralPath (Join-Path $reportDir 'report.json') -Encoding UTF8
}
$reports | Format-Table -AutoSize
Write-Output "Smoke-test evidence: $reportDir"

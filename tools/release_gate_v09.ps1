param(
    [string]$BuildDir = "build",
    [string]$BenchmarkOutput = "",
    [string]$TicketOutput = "文件解析目录/v09_release_ticket.json",
    [string]$FailureReport = "文件解析目录/v09_release_failures.txt",
    [switch]$EnableSTM32F429Check = $false,
    [string]$STM32BuildDir = "build-stm32f429",
    [string]$ReleaseVersion = "v1.0.0-rc1"
)

$ErrorActionPreference = "Stop"

function Resolve-Owner {
    param([string]$Reason)

    if ($Reason -like "required_tests_failed*") { return "qa/interconnect" }
    if ($Reason -like "benchmark_*") { return "perf/benchmark" }
    if ($Reason -like "soak_*") { return "perf/reliability" }
    if ($Reason -like "fault_assertion_*") { return "interconnect/fault-injection" }
    if ($Reason -like "stm32f429_*") { return "platform/stm32" }
    if ($Reason -like "f429_diag_*") { return "platform/stm32/diagnostics" }
    return "release/triage"
}

function Resolve-Action {
    param([string]$Reason)

    switch -Wildcard ($Reason) {
        "required_tests_failed*" { return "Run failing ctest targets with --output-on-failure and fix regressions." }
        "benchmark_*" { return "Re-run benchmark driver and compare throughput against baseline." }
        "soak_*" { return "Increase soak samples and investigate latency/error spikes." }
        "fault_assertion_*" { return "Validate fault injection path and expected assertion fields." }
        "stm32f429_*" { return "Build stm32f429_min_fw and verify ELF/HEX/BIN artifacts exist." }
        "f429_diag_*" { return "Run f429_bridge_diag_adapter_test and validate snapshot fields." }
        default { return "Inspect gate logs and route to responsible domain." }
    }
}

$failures = @()
$gateReasons = @()
$releaseGate = "pass"

if (-not (Test-Path $BuildDir)) {
    $failures += "build_dir_missing"
}

# ---------------------------------------------------------------------------
# Hard assertions for gate truthfulness (task T04).
# This script used to only forward the exit code of run_quality_gate.ps1, so a
# run that executed zero tests could still be reported as pass. Two independent
# checks are added here: the declared-versus-registered test count evaluated
# against CMake's manifest, and the executed count read back from the quality
# gate output.
# ---------------------------------------------------------------------------
if (Test-Path $BuildDir) {
    $manifestPath = Join-Path $BuildDir "vvos_declared_tests.txt"
    if (-not (Test-Path $manifestPath)) {
        $failures += "declared_test_manifest_missing"
    } else {
        $declaredNames = @(Get-Content $manifestPath | Where-Object { $_.Trim() -ne "" })
        $prevListEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $listRaw = (& ctest --test-dir $BuildDir -N 2>&1 | Out-String)
        $ErrorActionPreference = $prevListEap

        $registeredNames = @()
        foreach ($m in [regex]::Matches($listRaw, "(?m)^\s*Test\s+#\d+:\s*(\S+)\s*$")) {
            $registeredNames += $m.Groups[1].Value
        }

        Write-Host "[release-v09][assert] declared=$($declaredNames.Count) registered=$($registeredNames.Count)"
        if ($declaredNames.Count -eq 0) {
            $failures += "declared_tests_zero"
        }
        if ($registeredNames.Count -eq 0) {
            $failures += "registered_tests_zero"
        }
        if ($declaredNames.Count -ne $registeredNames.Count) {
            $failures += "test_count_mismatch_declared_$($declaredNames.Count)_registered_$($registeredNames.Count)"
        }
    }
}

if ($failures.Count -eq 0) {
    # run_quality_gate.ps1 writes a machine-readable report here. Parsing the
    # child's console text is not reliable across nested powershell.exe calls on
    # PowerShell 5.1, and a failed read must not be able to turn into a pass.
    $reportFile = [System.IO.Path]::GetTempFileName()

    $qualityCmd = @(
        "-ExecutionPolicy", "Bypass",
        "-File", "tools/run_quality_gate.ps1",
        "-BuildDir", $BuildDir,
        "-ReportPath", $reportFile
    )

    # Only append -BenchmarkOutput when it has a value: an empty string in a
    # native argument array is dropped on Windows, which makes the following
    # switch be consumed as its value (this silently swallowed -ReportPath).
    if ($BenchmarkOutput -ne "") {
        $qualityCmd += @("-BenchmarkOutput", $BenchmarkOutput)
    }

    if ($EnableSTM32F429Check) {
        $qualityCmd += @("-EnableSTM32F429Check", "-STM32BuildDir", $STM32BuildDir)
    }

    $prevGateEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & powershell @qualityCmd | Out-Host
    $qualityExit = $LASTEXITCODE
    $ErrorActionPreference = $prevGateEap

    if ($qualityExit -ne 0) {
        $releaseGate = "fail"
        $failures += "quality_gate_failed"
    }

    $qualityReport = $null
    if (Test-Path $reportFile) {
        $reportText = Get-Content $reportFile -Raw
        Remove-Item $reportFile -Force
        if ($null -ne $reportText -and $reportText.Trim() -ne "") {
            try {
                $qualityReport = $reportText | ConvertFrom-Json
            } catch {
                $qualityReport = $null
            }
        }
    }

    if ($null -eq $qualityReport) {
        $failures += "quality_gate_report_missing"
    } else {
        $executedValue = [int]$qualityReport.tests_executed
        $registeredValue = [int]$qualityReport.tests_registered
        $declaredValue = [int]$qualityReport.tests_declared

        Write-Host "[release-v09][assert] declared=$declaredValue registered=$registeredValue executed=$executedValue"

        if ($executedValue -eq 0) {
            $failures += "tests_executed_zero"
        }
        if (($registeredValue -gt 0) -and ($executedValue -ne $registeredValue)) {
            $failures += "tests_executed_mismatch_expected_${registeredValue}_actual_${executedValue}"
        }
        if (($declaredValue -gt 0) -and ($declaredValue -ne $registeredValue)) {
            $failures += "test_count_mismatch_declared_${declaredValue}_registered_${registeredValue}"
        }
        if ([string]$qualityReport.release_gate -ne "pass") {
            $releaseGate = "fail"
        }
        if ([string]$qualityReport.block_reasons -ne "") {
            $blockReasonsFromGate = ([string]$qualityReport.block_reasons) -split "\|" |
                ForEach-Object { $_.Trim() } |
                Where-Object { $_ -ne "" }
            foreach ($reason in $blockReasonsFromGate) {
                $gateReasons += $reason
            }
        }
    }
}

$blockReasons = @()
if (Test-Path $FailureReport) {
    Remove-Item $FailureReport -Force
}

# Reuse the block reasons reported by the single quality-gate invocation above.
# Re-running the gate here (as the previous version did) doubled the runtime and
# could observe a different result than the one whose exit code was checked.
foreach ($reason in $gateReasons) {
    $blockReasons += $reason
}

# Any pre-flight failure (missing build dir, missing manifest, count mismatch)
# must also fail the release gate; the old flow left $releaseGate at "pass".
if ($failures.Count -gt 0) {
    $releaseGate = "fail"
}

if ($blockReasons.Count -gt 0) {
    $releaseGate = "fail"
}

$items = @()
foreach ($reason in $blockReasons) {
    $items += [PSCustomObject]@{
        reason = $reason
        owner = Resolve-Owner -Reason $reason
        suggested_action = Resolve-Action -Reason $reason
    }
}

if ($releaseGate -eq "pass") {
    Set-Content -Path $FailureReport -Value "none"
} else {
    if ($blockReasons.Count -gt 0) {
        Set-Content -Path $FailureReport -Value ($blockReasons -join "`n")
    } else {
        Set-Content -Path $FailureReport -Value ($failures -join "`n")
    }
}

$ticket = [PSCustomObject]@{
    gate = "release_v09"
    release_version = $ReleaseVersion
    ci_archive_key = ("release_ticket_" + $ReleaseVersion + "_" + (Get-Date).ToString("yyyyMMdd_HHmmss"))
    status = $releaseGate
    generated_at = (Get-Date).ToString("o")
    build_dir = $BuildDir
    benchmark_output = $BenchmarkOutput
    stm32_check_enabled = [bool]$EnableSTM32F429Check
    failures = $failures
    block_reasons = $blockReasons
    work_items = $items
}

$ticket | ConvertTo-Json -Depth 6 | Set-Content -Path $TicketOutput -Encoding UTF8

Write-Host "[release-v09] release_version=$ReleaseVersion"
Write-Host "[release-v09] release_gate=$releaseGate"
Write-Host "[release-v09] ticket=$TicketOutput"
Write-Host "[release-v09] failure_report=$FailureReport"

if ($releaseGate -eq "pass") {
    exit 0
}
exit 1

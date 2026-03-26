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
$releaseGate = "pass"

if (-not (Test-Path $BuildDir)) {
    $failures += "build_dir_missing"
}

if ($failures.Count -eq 0) {
    $qualityCmd = @(
        "-ExecutionPolicy", "Bypass",
        "-File", "tools/run_quality_gate.ps1",
        "-BuildDir", $BuildDir,
        "-BenchmarkOutput", $BenchmarkOutput
    )

    if ($EnableSTM32F429Check) {
        $qualityCmd += @("-EnableSTM32F429Check", "-STM32BuildDir", $STM32BuildDir)
    }

    & powershell @qualityCmd
    if ($LASTEXITCODE -ne 0) {
        $releaseGate = "fail"
        $failures += "quality_gate_failed"
    }
}

$blockReasons = @()
if (Test-Path $FailureReport) {
    Remove-Item $FailureReport -Force
}

# best-effort parse run_quality_gate output summary by re-running in quiet capture mode
$qualityRaw = ""
if ((Test-Path $BuildDir) -and $failures.Count -eq 0) {
    $captureCmd = "powershell -ExecutionPolicy Bypass -File tools/run_quality_gate.ps1 -BuildDir `"$BuildDir`" -BenchmarkOutput `"$BenchmarkOutput`""
    if ($EnableSTM32F429Check) {
        $captureCmd += " -EnableSTM32F429Check -STM32BuildDir `"$STM32BuildDir`""
    }
    $qualityRaw = Invoke-Expression $captureCmd | Out-String
    if ($qualityRaw -match "block_reasons=([^\r\n]+)") {
        $blockReasons = ($matches[1] -split ",") | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
    }
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

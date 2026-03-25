param(
    [string]$BuildDir = "build",
    [string]$BenchmarkOutput = "",
    [string]$BaselineFile = "tools/benchmark_baseline.json",
    [double]$MinThroughput = 500.0,
    [double]$MaxErrorRate = 0.05,
    [double]$MaxP99LatencyMs = 200.0,
    [switch]$RunCtest = $true
)

$ErrorActionPreference = "Stop"

# 执行一组测试清单：required 失败会阻断发布，advisory 失败仅告警。
function Run-TestList {
    param(
        [string]$Label,
        [string[]]$Tests,
        [string]$BuildDir,
        [bool]$HardFail
    )

    $failed = @()

Write-Host "[quality-gate] export consistency self-check..."
$metricsTest = "interconnect_metrics_delta_test"
ctest --test-dir $BuildDir -R "^$metricsTest$" --output-on-failure
if ($LASTEXITCODE -ne 0) {
    $failed += $metricsTest
}
    foreach ($name in $Tests) {
        Write-Host "[quality-gate][$Label] running $name"
        ctest --test-dir $BuildDir -R "^$name$" --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            $failed += $name
            Write-Host "[quality-gate][$Label] fail: $name"
            if ($HardFail) {
                continue
            }
        }
    }

    return ,$failed
}

function Parse-Throughput {
    param([string]$Content)

    if ($Content -match '"throughput_msg_per_sec"\s*:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    if ($Content -match 'throughput_msg_per_sec:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    return 0.0
}

function Parse-ErrorRate {
    param([string]$Content)

    if ($Content -match '"error_rate"\s*:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    if ($Content -match 'error_rate:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    return 0.0
}

function Parse-P99 {
    param([string]$Content)

    if ($Content -match '"latency_p99_ms"\s*:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    if ($Content -match 'latency_p99_ms:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    return 0.0
}

function Parse-FaultAssertion {
    param([string]$Content)

    if ($Content -match '"fault_assertion_pass"\s*:\s*(true|false)') {
        return $matches[1] -eq 'true'
    }
    if ($Content -match 'fault_assertion_pass:\s*(true|false)') {
        return $matches[1] -eq 'true'
    }
    return $false
}

function Parse-ErrorRate {
    param([string]$Content)

    if ($Content -match '"error_rate"\s*:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    if ($Content -match 'error_rate:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    return 0.0
}

function Parse-P99 {
    param([string]$Content)

    if ($Content -match '"latency_p99_ms"\s*:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    if ($Content -match 'latency_p99_ms:\s*([0-9]+\.?[0-9]*)') {
        return [double]$matches[1]
    }
    return 0.0
}

function Load-Baseline {
    param([string]$Path)

    if (-not (Test-Path $Path)) {
        return @{ throughput = 0.0 }
    }

    try {
        return (Get-Content $Path -Raw | ConvertFrom-Json)
    } catch {
        return @{ throughput = 0.0 }
    }
}

Write-Host "[quality-gate] start"

if ($RunCtest -and -not (Test-Path $BuildDir)) {
    Write-Host "[quality-gate] build dir not found: $BuildDir"
    exit 1
}

$requiredTests = @(
    "interconnect_fault_injection_test",
    "interconnect_config_reload_test",
    "interconnect_protocol_adapter_test"
)

$advisoryTests = @(
    "interconnect_tcp_loopback_test",
    "interconnect_metrics_delta_test"
)

$requiredFailed = @()
$advisoryFailed = @()

if ($RunCtest) {
    $requiredFailed = Run-TestList -Label "required" -Tests $requiredTests -BuildDir $BuildDir -HardFail $true
    $advisoryFailed = Run-TestList -Label "advisory" -Tests $advisoryTests -BuildDir $BuildDir -HardFail $false
}

$throughput = 0.0
$errorRate = 0.0
$p99 = 0.0
$faultAssertPass = $true
$benchPass = $true
$soakPass = $true
$regressWarn = $false
$blockReasons = @()
$baseline = Load-Baseline -Path $BaselineFile
$baselineThroughput = [double]$baseline.throughput

if ($BenchmarkOutput -and (Test-Path $BenchmarkOutput)) {
    $content = Get-Content $BenchmarkOutput -Raw
    $throughput = Parse-Throughput -Content $content
    $errorRate = Parse-ErrorRate -Content $content
    $p99 = Parse-P99 -Content $content

    $benchPass = $throughput -ge $MinThroughput
    $soakPass = ($errorRate -le $MaxErrorRate) -and ($p99 -le $MaxP99LatencyMs)
    $faultAssertPass = Parse-FaultAssertion -Content $content

    if ($baselineThroughput -gt 0 -and $throughput -lt ($baselineThroughput * 0.95)) {
        $regressWarn = $true
    }

    Write-Host "[quality-gate][benchmark] throughput=$throughput min=$MinThroughput baseline=$baselineThroughput"
    Write-Host "[quality-gate][soak] error_rate=$errorRate max=$MaxErrorRate p99=$p99 max_p99=$MaxP99LatencyMs"
} else {
    Write-Host "[quality-gate][benchmark] output not provided, skip hard gate"
}

if ($requiredFailed.Count -gt 0) {
    $blockReasons += "required_tests_failed"
}
if (-not $benchPass) {
    $blockReasons += "benchmark_throughput_below_threshold"
}
if (-not $soakPass) {
    $blockReasons += "soak_or_fault_threshold_violation"
}
if (-not $faultAssertPass) {
    $blockReasons += "fault_assertion_failed"
}

if ($blockReasons.Count -eq 0) {
    Write-Host "[quality-gate] release_gate=pass"
} else {
    Write-Host "[quality-gate] release_gate=fail"
}

if ($advisoryFailed.Count -gt 0) {
    Write-Host "[quality-gate] advisory_failed=$($advisoryFailed -join ',')"
}
if ($regressWarn) {
    Write-Host "[quality-gate] performance_regression_warning=true"
}
if ($blockReasons.Count -gt 0) {
    Write-Host "[quality-gate] block_reasons=$($blockReasons -join ',')"
}

Write-Host "[quality-gate] ci_report={\"throughput\":$throughput,\"error_rate\":$errorRate,\"p99_ms\":$p99,\"fault_assertion\":$($faultAssertPass.ToString().ToLower()),\"release_gate\":\"$([string]::Join('', @($(if ($blockReasons.Count -eq 0) {'pass'} else {'fail'}))))\",\"block_reasons\":\"$($blockReasons -join '|')\"}"

if ($blockReasons.Count -eq 0) {
    exit 0
}
exit 1

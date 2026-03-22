param(
    [string]$BuildDir = "build",
    [string]$BenchmarkOutput = "",
    [string]$BaselineFile = "tools/benchmark_baseline.json",
    [double]$MinThroughput = 500.0,
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
$benchPass = $true
$regressWarn = $false
$baseline = Load-Baseline -Path $BaselineFile
$baselineThroughput = [double]$baseline.throughput

if ($BenchmarkOutput -and (Test-Path $BenchmarkOutput)) {
    $content = Get-Content $BenchmarkOutput -Raw
    $throughput = Parse-Throughput -Content $content

    $benchPass = $throughput -ge $MinThroughput
    if ($baselineThroughput -gt 0 -and $throughput -lt ($baselineThroughput * 0.95)) {
        $regressWarn = $true
    }

    Write-Host "[quality-gate][benchmark] throughput=$throughput min=$MinThroughput baseline=$baselineThroughput"
} else {
    Write-Host "[quality-gate][benchmark] output not provided, skip hard gate"
}

if ($requiredFailed.Count -eq 0 -and $benchPass) {
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

if ($requiredFailed.Count -eq 0 -and $benchPass) {
    exit 0
}
exit 1

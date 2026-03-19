param(
    [string]$BuildDir = "build",
    [string]$BenchmarkOutput = "",
    [double]$MinThroughput = 500.0,
    [switch]$RunCtest = $true
)

Write-Host "[quality-gate] start"

$failed = @()

if ($RunCtest) {
    if (-not (Test-Path $BuildDir)) {
        Write-Host "[quality-gate] build dir not found: $BuildDir"
        exit 1
    }

    Write-Host "[quality-gate] running ctest regression list..."
    $ctestNames = @(
        "interconnect_fault_injection_test",
        "interconnect_tcp_loopback_test",
        "interconnect_metrics_delta_test"
    )

    foreach ($name in $ctestNames) {
        ctest --test-dir $BuildDir -R "^$name$" --output-on-failure
        if ($LASTEXITCODE -ne 0) {
            $failed += $name
        }
    }
}

$throughput = 0.0
$benchPass = $true

if ($BenchmarkOutput -and (Test-Path $BenchmarkOutput)) {
    $content = Get-Content $BenchmarkOutput -Raw

    if ($content -match '"throughput_msg_per_sec"\s*:\s*([0-9]+\.?[0-9]*)') {
        $throughput = [double]$matches[1]
    } elseif ($content -match 'throughput_msg_per_sec:\s*([0-9]+\.?[0-9]*)') {
        $throughput = [double]$matches[1]
    }

    $benchPass = $throughput -ge $MinThroughput
    Write-Host "[quality-gate] throughput=$throughput, min=$MinThroughput, pass=$benchPass"
} else {
    Write-Host "[quality-gate] benchmark output not provided, throughput gate skipped"
}

if ($failed.Count -gt 0) {
    Write-Host "[quality-gate] ctest failed: $($failed -join ', ')"
}

if ($benchPass -and $failed.Count -eq 0) {
    Write-Host "[quality-gate] release_gate=pass"
    exit 0
}

Write-Host "[quality-gate] release_gate=fail"
exit 1

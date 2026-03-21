param(
    [string]$BuildDir = "build",
    [string]$BenchmarkOutput = "",
    [string]$FailureReport = "文件解析目录/v07_release_failures.txt"
)

$failures = @()

Write-Host "[release-v07] step1 build check"
if (-not (Test-Path $BuildDir)) {
    $failures += "build_dir_missing:$BuildDir"
    Set-Content -Path $FailureReport -Value ($failures -join "`n")
    Write-Host "[release-v07] build dir missing: $BuildDir"
    exit 1
}

Write-Host "[release-v07] step2 quality gate"
powershell -ExecutionPolicy Bypass -File "tools/run_quality_gate.ps1" -BuildDir $BuildDir -BenchmarkOutput $BenchmarkOutput
if ($LASTEXITCODE -ne 0) {
    $failures += "quality_gate_failed"
}

Write-Host "[release-v07] step3 audit report"
powershell -ExecutionPolicy Bypass -File "tools/generate_v07_audit_report.ps1"
if ($LASTEXITCODE -ne 0) {
    $failures += "audit_generation_failed"
}

if ($failures.Count -gt 0) {
    Set-Content -Path $FailureReport -Value ($failures -join "`n")
    Write-Host "[release-v07] release_gate=fail"
    Write-Host "[release-v07] failure report: $FailureReport"
    exit 1
}

Set-Content -Path $FailureReport -Value "none"
Write-Host "[release-v07] release_gate=pass"
exit 0

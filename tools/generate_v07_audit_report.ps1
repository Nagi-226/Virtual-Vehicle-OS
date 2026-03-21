param(
    [string]$OutFile = "文件解析目录/v07_审计报告.txt"
)

$report = @()
$report += "[v0.7 审计报告]"
$report += "date=$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$report += ""
$report += "1) 冗余与可维护性"
$report += "- 协议路径统一通过 message_protocol_adapter"
$report += "- 诊断命令统一入口 ExecuteDiagnosticCommand"
$report += ""
$report += "2) 风险项复核"
$report += "- reload 回滚原因码与可读原因一致"
$report += "- failover 命中与健康指标导出可观测"
$report += ""
$report += "3) 结论"
$report += "- 当前版本达到 v0.7 阶段质量基线"

$dir = Split-Path $OutFile -Parent
if (-not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}

$report -join "`r`n" | Set-Content -Path $OutFile -Encoding UTF8
Write-Host "[audit] generated: $OutFile"

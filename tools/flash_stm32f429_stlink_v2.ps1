param(
    [string]$BuildDir = "build-stm32f429",
    [string]$Elf = "stm32f429_min_fw",
    [string]$Programmer = "STM32_Programmer_CLI"
)

$ErrorActionPreference = "Stop"

$elfPath = Join-Path $BuildDir "$Elf"
if (-not (Test-Path $elfPath)) {
    Write-Host "[stm32-flash] firmware not found: $elfPath"
    exit 1
}

Write-Host "[stm32-flash] flashing $elfPath via STLINK-V2"
& $Programmer -c port=SWD -d $elfPath 0x08000000 -v -rst
if ($LASTEXITCODE -ne 0) {
    Write-Host "[stm32-flash] flash failed"
    exit 1
}

Write-Host "[stm32-flash] flash ok"

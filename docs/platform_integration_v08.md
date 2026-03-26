# v0.8 Platform Integration Guide

## Scope
This guide defines minimal integration steps for RTOS/STM32 and ROS2 sample paths in v0.8.

## 1) RTOS/STM32 Minimal Path
- Sample file: `demo/rtos_adapter_sample.cpp`
- Expected behavior: queue init/send/receive roundtrip success.
- Validation:
  1. Build sample target
  2. Run sample
  3. Verify output contains `rtos adapter sample ok`

## 2) ROS2 Minimal Path
- Sample file: `demo/ros2_adapter_sample.cpp`
- Expected behavior: topic and qos setup + topic mapping to envelope namespace.
- Validation:
  1. Build sample target
  2. Run sample
  3. Verify output contains `ros2 adapter sample ok`

## 3) Error/Code Mapping Baseline
- Keep transport and adapter errors mapped to existing `vr::core::ErrorCode`
- For platform-specific failures, map first to nearest common code, then add extension later.

## 4) STM32F429ZGT6 + STLINK-V2 Minimal Firmware Flow (Command Level)
- Configure:
  - `cmake -S . -B build-stm32f429 -G Ninja -DENABLE_STM32F429_TARGET=ON -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/stm32f429.cmake`
- Build:
  - `cmake --build build-stm32f429 --target stm32f429_min_fw`
- Expected artifacts:
  - `build-stm32f429/stm32f429_min_fw`
  - `build-stm32f429/stm32f429_min_fw.hex`
  - `build-stm32f429/stm32f429_min_fw.bin`
- Flash via STLINK-V2:
  - `powershell -ExecutionPolicy Bypass -File tools/flash_stm32f429_stlink_v2.ps1 -BuildDir build-stm32f429 -Elf stm32f429_min_fw`

### 实机下载验证 checklist（可执行）
1. 连接与识别
   - 连接 STLINK-V2 与目标板（SWDIO/SWCLK/NRST/GND/3V3）。
   - 运行 `STM32_Programmer_CLI -l usb`，确认探测到设备。
2. 构建产物检查
   - 确认 `stm32f429_min_fw`, `.hex`, `.bin` 均存在。
3. 擦写与下载
   - 执行 flash 脚本，确认输出包含 `Download verified successfully`。
4. 运行验证
   - 复位板卡，确认无 HardFault（可通过调试器查看 PC 不停留在 Fault handler）。
5. 回读验证（可选）
   - 使用 Programmer CLI 回读首段 Flash 与下载文件比对。
6. 问题定位
   - 若下载失败：优先检查供电/接线/读保护；
   - 若运行异常：优先检查 SystemInit/链接脚本/向量表首地址。

## 5) Quality Gate Integration for F429 Artifact Presence
- Enable check:
  - `powershell -ExecutionPolicy Bypass -File tools/run_quality_gate.ps1 -EnableSTM32F429Check`
- Blocking rule:
  - if `stm32f429_min_fw` / `.hex` / `.bin` any missing => `release_gate=fail` with `block_reasons=stm32f429_artifacts_missing`

## 6) FMC SRAM Bring-up Self Test (Production Phase-2)
- Boot flow now runs `FMC_SRAM_BringupSelfTest(128)` in `main_min.cpp`

## 6.1) Production Phase-3 Key Enhancements
- PLL template is now upgraded to Cube-style HSE=8MHz -> PLL -> SYSCLK=168MHz skeleton
- FMC SRAM init upgraded from pure stub to parameterized register setup via macros:
  - `F429_FMC_BANK1_BCR1_VALUE`
  - `F429_FMC_BANK1_BTR1_VALUE`
- UART BRR made configurable for board-level calibration:
  - `F429_UART_BAUD_BRR`
- Added minimal UART logs (startup + SRAM selftest result):
  - `[boot] stm32f429_min start`
  - `[diag] fmc_sram code=0x...`
  - `[diag] fmc_sram selftest OK/FAIL`

## 6.2) Recommended Template Values (8MHz HSE + 115200 UART)
- PLL (HSE=8MHz -> SYSCLK=168MHz):
  - `PLLM=8`, `PLLN=336`, `PLLP=2`, `PLLQ=7`
- FMC SRAM template (needs board-level tuning):
  - `F429_FMC_BANK1_BCR1_VALUE = 0x00001011`
  - `F429_FMC_BANK1_BTR1_VALUE = 0x00110413`
- UART (USART1, APB2 ~84MHz):
  - `F429_UART_BAUD_BRR = 0x2D9` (115200 template)
- Self test behavior:
  - write/read loopback at `0x68000000`
  - returns error code (`FMC_SRAM_TEST_*`)
  - updates diagnostic snapshot code (`HAL_GetDiagSnapshotCode`)
  - updates audit summary text (`HAL_GetAuditSummary`)
- Failure behavior:
  - `g_heartbeat = 0xEE000000 | diag_code`
  - loop with slow delay for debugger observation

## 7) Acceptance Checklist
- [ ] RTOS sample builds and runs
- [ ] ROS2 sample builds and runs
- [ ] STM32F429 minimal firmware target builds successfully
- [ ] STM32F429 ELF/HEX/BIN artifacts are generated
- [ ] STLINK-V2 flash script can run with generated ELF
- [ ] FMC SRAM self test returns OK on target board
- [ ] Failure path exposes diagnostic code and audit summary
- [ ] quality gate remains pass

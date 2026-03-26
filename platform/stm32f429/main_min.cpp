#include <cstdint>

extern "C" {
#include "stm32f429_hal_min.h"
}

#include "interconnect/diagnostics_manager.hpp"
#include "platform/stm32f429/f429_bridge_diag_adapter.hpp"

extern "C" {
#include "uart_log_min.h"
}

volatile std::uint32_t g_heartbeat __attribute__((section(".ext_sram"))) = 0U;

int main(void) {
    HAL_Init();
    UART_LogInit();
    UART_LogWrite("[boot] stm32f429_min start\r\n");

    const FmcSramSelfTestCode sram_test = FMC_SRAM_BringupSelfTest(128U);
    const uint32_t diag_code = HAL_GetDiagSnapshotCode();
    const char* audit = HAL_GetAuditSummary();

    vr::interconnect::DiagnosticsManager diagnostics;
    vr::platform::F429SramDiagEvent event;
    event.code = diag_code;
    event.audit_summary = (audit == nullptr) ? "" : audit;
    (void)vr::platform::F429BridgeDiagAdapter::EmitSramBringupEvent(
        diagnostics,
        "build/diagnostics_snapshots.jsonl",
        100U,
        event);

    UART_LogWrite("[diag] fmc_sram code=");
    UART_LogWriteHex(diag_code);
    UART_LogWrite("\r\n");

    if (sram_test != FMC_SRAM_TEST_OK || diag_code != 0U) {
        UART_LogWrite("[diag] fmc_sram selftest FAIL\r\n");
        // 失败路径：心跳停在错误码，便于调试器快速观察。
        g_heartbeat = 0xEE000000UL | diag_code;
        while (1) {
            HAL_Delay(100U);
        }
    }

    UART_LogWrite("[diag] fmc_sram selftest OK\r\n");

    while (1) {
        ++g_heartbeat;
        HAL_Delay(10U);
    }

    return 0;
}

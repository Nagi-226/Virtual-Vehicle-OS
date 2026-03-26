#include "stm32f429_hal_min.h"

#include <stddef.h>

#define FMC_EXT_SRAM_BASE  (0x68000000UL)
#define FMC_EXT_SRAM_WORDS_MAX (256U)

static volatile uint32_t g_diag_snapshot_code = 0U;
static const char* g_audit_summary = "fmc_sram_audit:{status=init,code=0}";

void HAL_Init(void) {
    // 最小 HAL 初始化占位。
    g_diag_snapshot_code = 0U;
    g_audit_summary = "fmc_sram_audit:{status=hal_init,code=0}";
}

void HAL_Delay(uint32_t ms) {
    volatile uint32_t cycles = ms * 8000U;
    while (cycles-- > 0U) {
        __asm__ volatile ("nop");
    }
}

FmcSramSelfTestCode FMC_SRAM_BringupSelfTest(uint32_t words_to_test) {
    if (words_to_test == 0U) {
        g_diag_snapshot_code = FMC_SRAM_TEST_ERR_PARAM;
        g_audit_summary = "fmc_sram_audit:{status=fail,reason=invalid_param,code=1}";
        return FMC_SRAM_TEST_ERR_PARAM;
    }

    if (words_to_test > FMC_EXT_SRAM_WORDS_MAX) {
        words_to_test = FMC_EXT_SRAM_WORDS_MAX;
    }

    volatile uint32_t* const sram = (volatile uint32_t*)FMC_EXT_SRAM_BASE;
    for (uint32_t i = 0U; i < words_to_test; ++i) {
        const uint32_t pattern = (0xA5A50000UL | (i & 0xFFFFUL));
        sram[i] = pattern;
    }

    for (uint32_t i = 0U; i < words_to_test; ++i) {
        const uint32_t expected = (0xA5A50000UL | (i & 0xFFFFUL));
        const uint32_t actual = sram[i];
        if (actual != expected) {
            g_diag_snapshot_code = FMC_SRAM_TEST_ERR_MISMATCH;
            g_audit_summary = "fmc_sram_audit:{status=fail,reason=readback_mismatch,code=2}";
            return FMC_SRAM_TEST_ERR_MISMATCH;
        }
    }

    g_diag_snapshot_code = FMC_SRAM_TEST_OK;
    g_audit_summary = "fmc_sram_audit:{status=ok,code=0}";
    return FMC_SRAM_TEST_OK;
}

uint32_t HAL_GetDiagSnapshotCode(void) {
    return g_diag_snapshot_code;
}

const char* HAL_GetAuditSummary(void) {
    return g_audit_summary;
}

#ifndef STM32F429_HAL_MIN_H
#define STM32F429_HAL_MIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FMC_SRAM_TEST_OK = 0,
    FMC_SRAM_TEST_ERR_PARAM = 1,
    FMC_SRAM_TEST_ERR_MISMATCH = 2
} FmcSramSelfTestCode;

void HAL_Init(void);
void HAL_Delay(uint32_t ms);

FmcSramSelfTestCode FMC_SRAM_BringupSelfTest(uint32_t words_to_test);
uint32_t HAL_GetDiagSnapshotCode(void);
const char* HAL_GetAuditSummary(void);

#ifdef __cplusplus
}
#endif

#endif

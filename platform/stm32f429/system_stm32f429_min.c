#include <stdint.h>

// =========================
// STM32F429 最小寄存器定义
// =========================
#define SCB_CPACR_ADDR         (0xE000ED88UL)
#define FLASH_BASE_ADDR        (0x40023C00UL)
#define FLASH_ACR              (*(volatile uint32_t*)(FLASH_BASE_ADDR + 0x00UL))

#define RCC_BASE               (0x40023800UL)
#define RCC_CR                 (*(volatile uint32_t*)(RCC_BASE + 0x00UL))
#define RCC_PLLCFGR            (*(volatile uint32_t*)(RCC_BASE + 0x04UL))
#define RCC_CFGR               (*(volatile uint32_t*)(RCC_BASE + 0x08UL))
#define RCC_AHB1ENR            (*(volatile uint32_t*)(RCC_BASE + 0x30UL))
#define RCC_AHB3ENR            (*(volatile uint32_t*)(RCC_BASE + 0x38UL))
#define RCC_AHB3RSTR           (*(volatile uint32_t*)(RCC_BASE + 0x18UL))

#define RCC_CR_HSION           (1UL << 0)
#define RCC_CR_HSIRDY          (1UL << 1)
#define RCC_CR_HSEON           (1UL << 16)
#define RCC_CR_HSERDY          (1UL << 17)
#define RCC_CR_PLLON           (1UL << 24)
#define RCC_CR_PLLRDY          (1UL << 25)

#define RCC_CFGR_SW_HSI        (0x0UL)
#define RCC_CFGR_SW_PLL        (0x2UL)
#define RCC_CFGR_SWS_MASK      (0xCUL)
#define RCC_CFGR_HPRE_DIV1     (0x0UL << 4)
#define RCC_CFGR_PPRE1_DIV4    (0x5UL << 10)
#define RCC_CFGR_PPRE2_DIV2    (0x4UL << 13)

#define FLASH_ACR_ICEN         (1UL << 9)
#define FLASH_ACR_DCEN         (1UL << 10)
#define FLASH_ACR_LATENCY_5WS  (5UL << 0)

#define RCC_AHB1ENR_GPIOAEN    (1UL << 0)
#define RCC_AHB1ENR_GPIOBEN    (1UL << 1)
#define RCC_AHB1ENR_GPIOCEN    (1UL << 2)
#define RCC_AHB1ENR_GPIODEN    (1UL << 3)
#define RCC_AHB1ENR_GPIOEEN    (1UL << 4)
#define RCC_AHB1ENR_GPIOFEN    (1UL << 5)
#define RCC_AHB1ENR_GPIOGEN    (1UL << 6)

#define RCC_AHB3ENR_FMCEN      (1UL << 0)
#define RCC_AHB3RSTR_FMCRST    (1UL << 0)

// =========================
// FMC 参数化配置宏（按板级 SRAM 型号校准）
// =========================
#ifndef F429_FMC_BANK1_BCR1_VALUE
#define F429_FMC_BANK1_BCR1_VALUE (0x00001011UL)
#endif

#ifndef F429_FMC_BANK1_BTR1_VALUE
#define F429_FMC_BANK1_BTR1_VALUE (0x00110413UL)
#endif

#define FMC_Bank1_BASE         (0xA0000000UL)
#define FMC_BCR1               (*(volatile uint32_t*)(FMC_Bank1_BASE + 0x00UL))
#define FMC_BTR1               (*(volatile uint32_t*)(FMC_Bank1_BASE + 0x04UL))

// =========================
// PLL 模板（8MHz HSE -> 168MHz SYSCLK）
// PLLM=8, PLLN=336, PLLP=2, PLLQ=7
// =========================
static void SystemClock_Config_FromHSE8_PLL168(void) {
    RCC_CR |= RCC_CR_HSION;
    while ((RCC_CR & RCC_CR_HSIRDY) == 0U) {
        // wait HSI ready
    }

    RCC_CFGR &= ~0x3UL;
    RCC_CFGR |= RCC_CFGR_SW_HSI;

    RCC_CR |= RCC_CR_HSEON;
    while ((RCC_CR & RCC_CR_HSERDY) == 0U) {
        // wait HSE ready
    }

    RCC_CR &= ~RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) != 0U) {
        // wait PLL unlock
    }

    const uint32_t pll_m = 8U;
    const uint32_t pll_n = 336U;
    const uint32_t pll_p = 0U;
    const uint32_t pll_q = 7U;
    const uint32_t pll_src_hse = (1UL << 22);

    RCC_PLLCFGR = pll_src_hse | (pll_m << 0U) | (pll_n << 6U) | (pll_p << 16U) | (pll_q << 24U);

    FLASH_ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_5WS;

    RCC_CFGR &= ~((0xFUL << 4) | (0x7UL << 10) | (0x7UL << 13));
    RCC_CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

    RCC_CR |= RCC_CR_PLLON;
    while ((RCC_CR & RCC_CR_PLLRDY) == 0U) {
        // wait PLL lock
    }

    RCC_CFGR &= ~0x3UL;
    RCC_CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC_CFGR & RCC_CFGR_SWS_MASK) != (RCC_CFGR_SW_PLL << 2U)) {
        // wait SYSCLK switch to PLL
    }
}

static void FPU_Enable(void) {
    volatile uint32_t* const cpacr = (volatile uint32_t*)SCB_CPACR_ADDR;
    *cpacr |= (0xFUL << 20U);
}

static void FMC_SRAM_Init_Param(void) {
    // 1) GPIO clock enable（FMC 相关端口，后续可按实际使用裁剪）
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
                   RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOFEN |
                   RCC_AHB1ENR_GPIOGEN;

    // 2) FMC clock & reset pulse
    RCC_AHB3ENR |= RCC_AHB3ENR_FMCEN;
    RCC_AHB3RSTR |= RCC_AHB3RSTR_FMCRST;
    RCC_AHB3RSTR &= ~RCC_AHB3RSTR_FMCRST;

    // 3) 参数化寄存器写入（生产级需按 SRAM 器件手册校准）
    FMC_BCR1 = F429_FMC_BANK1_BCR1_VALUE;
    FMC_BTR1 = F429_FMC_BANK1_BTR1_VALUE;
}

void SystemInit(void) {
    FPU_Enable();
    SystemClock_Config_FromHSE8_PLL168();
    FMC_SRAM_Init_Param();
}

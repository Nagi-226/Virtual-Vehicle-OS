#include "uart_log_min.h"

#define USART1_BASE           (0x40011000UL)
#define USART1_SR             (*(volatile uint32_t*)(USART1_BASE + 0x00UL))
#define USART1_DR             (*(volatile uint32_t*)(USART1_BASE + 0x04UL))
#define USART1_BRR            (*(volatile uint32_t*)(USART1_BASE + 0x08UL))
#define USART1_CR1            (*(volatile uint32_t*)(USART1_BASE + 0x0CUL))

#define USART_SR_TXE          (1UL << 7)
#define USART_CR1_UE          (1UL << 13)
#define USART_CR1_TE          (1UL << 3)

static void UART_LogWriteChar(const char ch) {
    while ((USART1_SR & USART_SR_TXE) == 0U) {
        // wait tx empty
    }
    USART1_DR = (uint32_t)ch;
}

void UART_LogInit(void) {
    // 最小占位：BRR 设默认值（需按真实 APB2 时钟校准）
    USART1_BRR = 0x2D9U; // 约 115200 @ 84MHz APB2（模板值）
    USART1_CR1 = USART_CR1_TE;
    USART1_CR1 |= USART_CR1_UE;
}

void UART_LogWrite(const char* text) {
    if (text == 0) {
        return;
    }
    while (*text != '\0') {
        UART_LogWriteChar(*text++);
    }
}

void UART_LogWriteHex(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    UART_LogWrite("0x");
    for (int i = 7; i >= 0; --i) {
        const uint32_t nibble = (value >> (i * 4)) & 0xFU;
        UART_LogWriteChar(hex[nibble]);
    }
}

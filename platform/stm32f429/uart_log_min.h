#ifndef STM32F429_UART_LOG_MIN_H
#define STM32F429_UART_LOG_MIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void UART_LogInit(void);
void UART_LogWrite(const char* text);
void UART_LogWriteHex(uint32_t value);

#ifdef __cplusplus
}
#endif

#endif

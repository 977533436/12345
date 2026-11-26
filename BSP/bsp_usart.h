/*
 * 文件名：bsp_usart.h
 * 描述  ：USART 串口驱动头文件（USART1 中断接收 + 阻塞发送）
 * 备注  ：基于 STM32F103 标准外设库
 *          开发板为"双 micro F103RCT6"，USART1 的 PA9/PA10 板载直连 CH340C，
 *          插 J6（Micro-USB）即可与 PC 串口助手通信，无需电平转换
 *          接收采用 RXNE + IDLE 双中断：协议层用"总线空闲"断帧，
 *          上位机没有"发送新行"功能也能正常识别一条指令
 */

#ifndef __BSP_USART_H
#define __BSP_USART_H

#include "stm32f10x.h"

/* ======================== 串口与引脚宏定义 ======================== */
#define USART_DEBUG               USART1                          /* 调试/通信串口 */
#define USART1_GPIO_PORT          GPIOA
#define USART1_TX_GPIO_PIN        GPIO_Pin_9                      /* PA9  -> CH340C RXD */
#define USART1_RX_GPIO_PIN        GPIO_Pin_10                     /* PA10 <- CH340C TXD */
#define USART1_GPIO_CLK           RCC_APB2Periph_GPIOA
#define USART1_CLK                RCC_APB2Periph_USART1

/* ======================== 参数宏定义 ======================== */
#define USART_DEFAULT_BAUD        115200                          /* 115200-8-N-1 */
#define USART_RX_BUF_SIZE         64                              /* 行缓冲大小，最大有效指令 63 字符 */

/* ======================== 函数声明 ======================== */

/*
 * 函数名：Usart1_Init
 * 描述  ：初始化 USART1（115200-8-N-1、无流控、收发使能）并开启 RXNE + IDLE 中断
 * 输入  ：baud-波特率
 * 输出  ：无
 */
void Usart1_Init(unsigned int baud);

/*
 * 函数名：Usart_Init
 * 描述  ：以默认波特率（115200）初始化 USART1
 * 输入  ：无
 * 输出  ：无
 */
void Usart_Init(void);

/*
 * 函数名：Usart_SendByte
 * 描述  ：发送一个字节，并等待发送完成
 * 输入  ：USARTx-串口组, ch-待发送字节
 * 输出  ：无
 */
void Usart_SendByte(USART_TypeDef *USARTx, uint8_t ch);

/*
 * 函数名：Usart_SendString
 * 描述  ：发送以 '\0' 结尾的字符串
 * 输入  ：USARTx-串口组, str-待发送字符串
 * 输出  ：无
 */
void Usart_SendString(USART_TypeDef *USARTx, const char *str);

/*
 * 函数名：UsartPrintf
 * 描述  ：格式化发送（不依赖 printf 重定向）
 * 输入  ：USARTx-串口组, fmt-格式串, ...-不定参数
 * 输出  ：无
 */
void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...);

/*
 * 函数名：Usart_RxLineReady
 * 描述  ：查询是否收到完整一帧（总线空闲断帧，或收到 '\r'/'\n'）
 * 输入  ：无
 * 输出  ：1-有新帧待取走, 0-无
 */
uint8_t Usart_RxLineReady(void);

/*
 * 函数名：Usart_GetRxLine
 * 描述  ：取走一帧数据（含 '\0'），取走后清除"收到一帧"标志
 * 输入  ：buf-输出缓冲, size-缓冲大小（含 '\0'）
 * 输出  ：本帧字符数；无数据或参数非法返回 0
 */
uint8_t Usart_GetRxLine(char *buf, uint8_t size);

/*
 * 函数名：Usart_RxOverflow
 * 描述  ：查询是否发生过"帧超长溢出"，读取后自动清零
 * 输入  ：无
 * 输出  ：1-发生过溢出, 0-未发生
 */
uint8_t Usart_RxOverflow(void);

/*
 * 函数名：Usart_RxReset
 * 描述  ：清空接收行缓冲与所有接收标志
 * 输入  ：无
 * 输出  ：无
 */
void Usart_RxReset(void);

#endif /* __BSP_USART_H */

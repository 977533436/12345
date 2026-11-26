/*
 * 文件名：bsp_led.h
 * 描述  ：板载 LED2 驱动头文件
 * 备注  ：LED2 阳极经 R11(1K) 接 3V3，阴极接 PA8，低电平点亮。
 */

#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "stm32f10x.h"

/* ======================== 引脚宏定义 ======================== */
#define LED_GPIO_PORT             GPIOA
#define LED_GPIO_PIN              GPIO_Pin_8
#define LED_GPIO_CLK              RCC_APB2Periph_GPIOA

/* ======================== 电平/状态宏定义 ======================== */
#define LED_ON_LEVEL              Bit_RESET      /* 点亮：低电平 */
#define LED_OFF_LEVEL             Bit_SET        /* 熄灭：高电平 */

#define LED_STATE_OFF             0U
#define LED_STATE_ON              1U

/* ======================== 函数声明 ======================== */
void LED_Init(void);
void LED_On(void);
void LED_Off(void);
void LED_Toggle(void);
void LED_Set(uint8_t on);
uint8_t LED_GetState(void);

#endif /* __BSP_LED_H */

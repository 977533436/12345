/*
 * 文件名：bsp_led.c
 * 描述  ：板载 LED2 驱动源文件
 * 备注  ：3V3 -> R11(1K) -> LED2 -> PA8，输出低电平点亮。
 */

#include "bsp_led.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/*
 * 函数名：LED_Init
 * 描述  ：初始化 LED2 控制引脚，上电默认熄灭
 */
void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(LED_GPIO_CLK, ENABLE);

    /* 先预置输出寄存器为高电平，再切换为推挽输出，避免初始化瞬间闪亮 */
    GPIO_SetBits(LED_GPIO_PORT, LED_GPIO_PIN);

    GPIO_InitStructure.GPIO_Pin   = LED_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_GPIO_PORT, &GPIO_InitStructure);

    LED_Off();
}

/*
 * 函数名：LED_On
 * 描述  ：点亮 LED2
 */
void LED_On(void)
{
    GPIO_ResetBits(LED_GPIO_PORT, LED_GPIO_PIN);
}

/*
 * 函数名：LED_Off
 * 描述  ：熄灭 LED2
 */
void LED_Off(void)
{
    GPIO_SetBits(LED_GPIO_PORT, LED_GPIO_PIN);
}

/*
 * 函数名：LED_Toggle
 * 描述  ：翻转 LED2 当前状态
 */
void LED_Toggle(void)
{
    if (GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_GPIO_PIN) == (uint8_t)LED_ON_LEVEL)
    {
        LED_Off();
    }
    else
    {
        LED_On();
    }
}

/*
 * 函数名：LED_Set
 * 描述  ：按逻辑状态设置 LED2
 * 输入  ：on-1 点亮，0 熄灭
 */
void LED_Set(uint8_t on)
{
    if (on != 0U)
    {
        LED_On();
    }
    else
    {
        LED_Off();
    }
}

/*
 * 函数名：LED_GetState
 * 描述  ：读取 LED2 当前逻辑状态
 * 输出  ：LED_STATE_ON-点亮，LED_STATE_OFF-熄灭
 */
uint8_t LED_GetState(void)
{
    if (GPIO_ReadOutputDataBit(LED_GPIO_PORT, LED_GPIO_PIN) == (uint8_t)LED_ON_LEVEL)
    {
        return LED_STATE_ON;
    }

    return LED_STATE_OFF;
}

/*
 * 文件名：bsp_key.h
 * 描述  ：板载独立按键驱动头文件（轮询式 20ms 消抖，按下沿触发）
 * 备注  ：基于 STM32F103 标准外设库，按键编号与开发板丝印一致：
 *            KEY1 = 复位键（NRST），硬件直接复位，不归本驱动管理
 *            KEY2 = PA0，按下接 3V3，高电平有效，内部下拉（原理图 SW2）
 *            KEY3 = PC8，按下接 GND，低电平有效，内部上拉（原理图 SW4）
 *            KEY4 = PC9，按下接 GND，低电平有效，内部上拉（原理图 SW3）
 *          KEY2 与 KEY3/KEY4 极性相反，驱动内部按 active level 分别判断；
 *          不使用 EXTI 中断，主循环轮询即可，消抖计时依赖 Timer_GetTick()
 */

#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include "stm32f10x.h"

/* ======================== 引脚宏定义 ======================== */
#define KEY_GPIO_CLK                  (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC)

/* KEY2（原理图 SW2）：PA0，高电平有效，内部下拉 */
#define KEY2_GPIO_PORT                GPIOA
#define KEY2_GPIO_PIN                 GPIO_Pin_0
#define KEY2_GPIO_MODE                GPIO_Mode_IPD
#define KEY2_ACTIVE_LEVEL             1U

/* KEY3（原理图 SW4）：PC8，低电平有效，内部上拉 */
#define KEY3_GPIO_PORT                GPIOC
#define KEY3_GPIO_PIN                 GPIO_Pin_8
#define KEY3_GPIO_MODE                GPIO_Mode_IPU
#define KEY3_ACTIVE_LEVEL             0U

/* KEY4（原理图 SW3）：PC9，低电平有效，内部上拉 */
#define KEY4_GPIO_PORT                GPIOC
#define KEY4_GPIO_PIN                 GPIO_Pin_9
#define KEY4_GPIO_MODE                GPIO_Mode_IPU
#define KEY4_ACTIVE_LEVEL             0U

/* ======================== 参数宏定义 ======================== */
#define KEY_DEBOUNCE_MS               20U      /* 消抖时间：电平稳定 20ms 才认账 */
#define KEY_COUNT                     3U       /* 本驱动管理的按键数量（KEY2~KEY4） */

/* ======================== 类型定义 ======================== */

/*
 * 类型名：Key_Status
 * 描述  ：按键事件（一次按下只上报一次，长按不重复）
 * 备注  ：枚举值与板上丝印编号一一对应，便于对照接线图排查
 */
typedef enum
{
    KEY_NONE = 0,       /* 无新事件 */
    KEY2_PRESS,         /* KEY2 / PA0 按下 */
    KEY3_PRESS,         /* KEY3 / PC8 按下 */
    KEY4_PRESS          /* KEY4 / PC9 按下 */
} Key_Status;

/* ======================== 函数声明 ======================== */

/*
 * 函数名：Key_Init
 * 描述  ：配置 KEY2/KEY3/KEY4 引脚为输入并复位消抖状态
 * 输入  ：无
 * 输出  ：无
 * 备注  ：必须在 Timer_Init() 之后调用，消抖要用 Timer_GetTick()
 */
void Key_Init(void);

/*
 * 函数名：Key_Scan
 * 描述  ：非阻塞扫描，完成 20ms 消抖并返回"按下沿"事件
 * 输入  ：无
 * 输出  ：KEY_NONE 或 KEY2_PRESS / KEY3_PRESS / KEY4_PRESS
 * 备注  ：需在主循环中反复调用；每次最多返回一个事件；
 *          长按不会重复上报，必须松开再按下才会产生下一个事件；
 *          同时按下多个键时优先级 KEY2 > KEY3 > KEY4
 */
Key_Status Key_Scan(void);

/*
 * 函数名：Key_IsDown
 * 描述  ：查询某个按键当前是否处于按下状态（用于长按连调等扩展功能）
 * 输入  ：key-按键事件编号（KEY2_PRESS / KEY3_PRESS / KEY4_PRESS）
 * 输出  ：1-按下, 0-松开或参数非法
 */
uint8_t Key_IsDown(Key_Status key);

#endif /* __BSP_KEY_H */

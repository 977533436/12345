/*
 * 文件名：bsp_timer.h
 * 描述  ：TIM2 时基驱动头文件（1ms 中断 + 时/分/秒进位）
 * 备注  ：基于 STM32F103 标准外设库
 *          TIM2 时钟 72MHz，预分频 71 → 1MHz 计数，自动重装 999 → 1ms 更新中断；
 *          中断内累加毫秒计数，满 1000 次产生 1 秒进位，秒→分→时逐级进位
 *          时/分/秒 采用 24 小时制：23:59:59 → 00:00:00
 */

#ifndef __BSP_TIMER_H
#define __BSP_TIMER_H

#include "stm32f10x.h"

/* ======================== 参数宏定义 ======================== */
#define TIMER_MS_PER_SEC          1000U       /* 1000 个 1ms 中断 = 1 秒 */
#define TIMER_SEC_PER_MIN         60U
#define TIMER_MIN_PER_HOUR        60U
#define TIMER_HOUR_PER_DAY        24U

/* ======================== 类型定义 ======================== */

/*
 * 类型名：Timer_TimeTypeDef
 * 描述  ：时间数据结构（24 小时制）
 */
typedef struct
{
    uint8_t hour;      /* 0~23 */
    uint8_t minute;    /* 0~59 */
    uint8_t second;    /* 0~59 */
} Timer_TimeTypeDef;

/* ======================== 函数声明 ======================== */

/*
 * 函数名：Timer_Init
 * 描述  ：初始化 TIM2 为 1ms 更新中断并启动计数，时间清零为 00:00:00
 * 输入  ：无
 * 输出  ：无
 */
void Timer_Init(void);

/*
 * 函数名：Timer_GetTick
 * 描述  ：读取系统毫秒计数（上电后累加，约 49.7 天回绕）
 * 输入  ：无
 * 输出  ：毫秒计数
 */
uint32_t Timer_GetTick(void);

/*
 * 函数名：Timer_GetTime
 * 描述  ：读取当前时间（临界区保护，读到的一定是同一时刻的完整值）
 * 输入  ：time-时间结构体指针
 * 输出  ：无
 */
void Timer_GetTime(Timer_TimeTypeDef *time);

/*
 * 函数名：Timer_SetTime
 * 描述  ：设置当前时间（按键校时、上电对时用）
 * 输入  ：hour-0~23, minute-0~59, second-0~59；超范围自动取模
 * 输出  ：无
 */
void Timer_SetTime(uint8_t hour, uint8_t minute, uint8_t second);

/*
 * 函数名：Timer_SecondChanged
 * 描述  ：查询并清除"秒已变化"标志，主循环据此每秒刷新一次显示
 * 输入  ：无
 * 输出  ：1-这一秒刚过去, 0-没有变化
 */
uint8_t Timer_SecondChanged(void);

#endif /* __BSP_TIMER_H */

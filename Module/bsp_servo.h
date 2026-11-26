/*
 * 文件名：bsp_servo.h
 * 描述  ：SG90 舵机驱动头文件（TIM4_CH3 硬件 PWM，50Hz）
 * 备注  ：基于 STM32F103 标准外设库
 *          信号线 PB8（JP2.7，TIM4_CH3 默认复用，无需重映射），复用推挽输出
 *          SG90 自带位置控制电路，只需给周期性脉宽即可，无需外部驱动
 *          时基：72MHz/(71+1)=1MHz（1us 计数），ARR=19999 → 20ms 周期 = 50Hz
 *          脉宽 500us~2500us 对应 0°~180°
 *
 *          位置定义：上锁 0°（0.5ms），开锁 180°（2.5ms）
 *          开锁流程：Servo_Unlock() 转到 180°，保持 3s，由 Servo_Task() 自动回 0°
 */

#ifndef __BSP_SERVO_H
#define __BSP_SERVO_H

#include "stm32f10x.h"

/* ======================== 外设与引脚宏定义 ======================== */
#define SERVO_GPIO_PORT           GPIOB
#define SERVO_GPIO_PIN            GPIO_Pin_8      /* PB8，JP2.7（JP3.7 同网络） */
#define SERVO_GPIO_CLK            RCC_APB2Periph_GPIOB

#define SERVO_TIM                 TIM4            /* 用 TIM4_CH3；TIM2 已被 1ms 时基占用 */
#define SERVO_TIM_CLK             RCC_APB1Periph_TIM4

/* ======================== 参数宏定义 ======================== */
#define SERVO_TIM_PSC             71U             /* 72MHz/(71+1) = 1MHz，1 个计数 = 1us */
#define SERVO_TIM_ARR             19999U          /* 20000us = 20ms → 50Hz */

#define SERVO_MIN_US              500U            /* 0° 对应脉宽 */
#define SERVO_MAX_US              2500U           /* 180° 对应脉宽 */
#define SERVO_ANGLE_MIN           0U
#define SERVO_ANGLE_MAX           180U

#define SERVO_LOCK_ANGLE          0U              /* 上锁位 */
#define SERVO_UNLOCK_ANGLE        180U            /* 开锁位 */
#define SERVO_HOLD_MS             3000U           /* 开锁后保持时间，到点自动回上锁位 */

/* ======================== 函数声明 ======================== */

/*
 * 函数名：Servo_Init
 * 描述  ：初始化 PB8 为复用推挽、TIM4 为 50Hz PWM（CH3），并立即输出上锁位 0°
 * 输入  ：无
 * 输出  ：无
 * 备注  ：必须在 Timer_Init() 之后调用（自动回位要用毫秒时基）；
 *          上电即输出 0.5ms，保证接舵机时不会乱转
 */
void Servo_Init(void);

/*
 * 函数名：Servo_SetAngle
 * 描述  ：直接设置角度（0~180），立即生效
 * 输入  ：angle-目标角度，超范围自动钳位到 0/180
 * 输出  ：无
 * 备注  ：只改比较值，不参与自动回位计时；扫掠测试时使用
 */
void Servo_SetAngle(uint8_t angle);

/*
 * 函数名：Servo_GetAngle
 * 描述  ：读取当前设定的角度
 * 输入  ：无
 * 输出  ：0~180（度）
 */
uint8_t Servo_GetAngle(void);

/*
 * 函数名：Servo_Lock
 * 描述  ：上锁：转到 0° 并取消自动回位计时
 * 输入  ：无
 * 输出  ：无
 */
void Servo_Lock(void);

/*
 * 函数名：Servo_Unlock
 * 描述  ：开锁：转到 180°，开始 3s 保持计时，到点由 Servo_Task() 自动回 0°
 * 输入  ：无
 * 输出  ：无
 */
void Servo_Unlock(void);

/*
 * 函数名：Servo_Task
 * 描述  ：开锁保持计时任务，需在主循环中反复调用（非阻塞）
 * 输入  ：无
 * 输出  ：无
 * 备注  ：未处于开锁计时段时直接返回，开销可以忽略
 */
void Servo_Task(void);

/*
 * 函数名：Servo_GetRemainMs
 * 描述  ：查询距自动回位还剩多少毫秒（用于界面倒计时显示）
 * 输入  ：无
 * 输出  ：剩余毫秒数；0-未在计时或已回位
 */
uint16_t Servo_GetRemainMs(void);

#endif /* __BSP_SERVO_H */

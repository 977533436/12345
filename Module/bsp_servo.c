/*
 * 文件名：bsp_servo.c
 * 描述  ：SG90 舵机驱动源文件（TIM4_CH3 硬件 PWM，50Hz）
 * 备注  ：PB8 复用推挽输出，TIM4 计数 1us、周期 20ms
 *          CCR = 500~2500 对应 0°~180°
 *          开锁后 3s 自动回上锁位，由 Servo_Task() 用毫秒时基非阻塞推进
 */

#include "bsp_servo.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "bsp_timer.h"

/* ======================== 私有变量 ======================== */
static uint8_t  s_Angle      = SERVO_LOCK_ANGLE;   /* 当前角度 0~180 */
static uint8_t  s_AutoReturn = 0U;                 /* 1-开锁保持计时中 */
static uint32_t s_UnlockTick = 0U;                 /* 开锁时刻（ms） */

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：Servo_Init
 * 描述  ：PB8 复用推挽 + TIM4_CH3 输出 50Hz PWM，初始为上锁位 0°
 * 输入  ：无
 * 输出  ：无
 */
void Servo_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    /* ① 时钟：GPIOB（APB2）+ TIM4（APB1） */
    RCC_APB2PeriphClockCmd(SERVO_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(SERVO_TIM_CLK, ENABLE);

    /* ② PB8 交给 TIM4_CH3：复用推挽，不能用普通推挽 */
    GPIO_InitStructure.GPIO_Pin   = SERVO_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SERVO_GPIO_PORT, &GPIO_InitStructure);

    /* ③ 时基：72MHz → 1MHz 计数（1us），ARR=19999 → 20ms/50Hz */
    TIM_TimeBaseStructure.TIM_Period        = SERVO_TIM_ARR;
    TIM_TimeBaseStructure.TIM_Prescaler     = SERVO_TIM_PSC;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(SERVO_TIM, &TIM_TimeBaseStructure);

    /* ④ 通道 3 配置为 PWM1，初值为上锁位脉宽 */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = SERVO_MIN_US;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC3Init(SERVO_TIM, &TIM_OCInitStructure);

    /* ⑤ 预装载：改写 CCR 时在下一个周期生效，避免出现半周期毛刺 */
    TIM_OC3PreloadConfig(SERVO_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(SERVO_TIM, ENABLE);

    /* ⑥ 启动定时器 */
    TIM_Cmd(SERVO_TIM, ENABLE);

    /* ⑦ 状态复位并输出上锁位 */
    s_AutoReturn = 0U;
    s_Angle      = SERVO_LOCK_ANGLE;
    TIM_SetCompare3(SERVO_TIM, SERVO_MIN_US);
}

/* ======================== 角度控制 ======================== */

/*
 * 函数名：Servo_SetAngle
 * 描述  ：设置角度（0~180），换算成脉宽写入 CCR3
 * 输入  ：angle-目标角度
 * 输出  ：无
 */
void Servo_SetAngle(uint8_t angle)
{
    uint32_t pulse;

    if (angle > SERVO_ANGLE_MAX)          /* 越界钳位，防止脉宽超出舵机行程 */
    {
        angle = SERVO_ANGLE_MAX;
    }

    /* pulse = 500 + 2000 * angle / 180  →  0°=500us, 90°=1500us, 180°=2500us */
    pulse = (uint32_t)SERVO_MIN_US +
            ((uint32_t)(SERVO_MAX_US - SERVO_MIN_US) * (uint32_t)angle) / SERVO_ANGLE_MAX;

    TIM_SetCompare3(SERVO_TIM, (uint16_t)pulse);

    s_Angle = angle;
}

/*
 * 函数名：Servo_GetAngle
 * 描述  ：读取当前设定角度
 * 输入  ：无
 * 输出  ：0~180（度）
 */
uint8_t Servo_GetAngle(void)
{
    return s_Angle;
}

/*
 * 函数名：Servo_Lock
 * 描述  ：上锁：转到 0° 并取消自动回位计时
 * 输入  ：无
 * 输出  ：无
 */
void Servo_Lock(void)
{
    s_AutoReturn = 0U;
    Servo_SetAngle(SERVO_LOCK_ANGLE);
}

/*
 * 函数名：Servo_Unlock
 * 描述  ：开锁：转到 180° 并启动 3s 保持计时
 * 输入  ：无
 * 输出  ：无
 */
void Servo_Unlock(void)
{
    Servo_SetAngle(SERVO_UNLOCK_ANGLE);

    s_UnlockTick = Timer_GetTick();
    s_AutoReturn = 1U;
}

/* ======================== 保持与自动回位 ======================== */

/*
 * 函数名：Servo_Task
 * 描述  ：开锁保持计时，到 3s 自动回上锁位（非阻塞）
 * 输入  ：无
 * 输出  ：无
 */
void Servo_Task(void)
{
    if (s_AutoReturn == 0U)
    {
        return;                            /* 没有在计时，直接返回 */
    }

    if ((Timer_GetTick() - s_UnlockTick) < SERVO_HOLD_MS)
    {
        return;                            /* 还没到 3s */
    }

    Servo_Lock();                          /* 保持结束，自动回位并清计时标志 */
}

/*
 * 函数名：Servo_GetRemainMs
 * 描述  ：查询距自动回位剩余时间
 * 输入  ：无
 * 输出  ：剩余毫秒数；0-未在计时或已回位
 */
uint16_t Servo_GetRemainMs(void)
{
    uint32_t elapsed;

    if (s_AutoReturn == 0U)
    {
        return 0U;
    }

    elapsed = Timer_GetTick() - s_UnlockTick;
    if (elapsed >= SERVO_HOLD_MS)
    {
        return 0U;
    }

    return (uint16_t)(SERVO_HOLD_MS - elapsed);
}

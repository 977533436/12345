/*
 * 文件名：bsp_timer.c
 * 描述  ：TIM2 时基驱动源文件（1ms 中断 + 时/分/秒进位）
 * 备注  ：TIM2 时钟 72MHz，预分频 71、自动重装 999，得到 1ms 更新中断；
 *          中断内维护毫秒计数与时/分/秒，秒进位时置"秒已变化"标志，
 *          主循环用 Timer_SecondChanged() 查询，避免在主循环里轮询时间
 */

#include "bsp_timer.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_tim.h"
#include "misc.h"

/* ======================== 参数宏定义 ======================== */
#define TIMER_PRESCALER           71U          /* 72MHz / (71+1) = 1MHz */
#define TIMER_PERIOD              999U         /* 1MHz / (999+1) = 1kHz → 1ms */

/* ======================== 私有变量 ======================== */

/* 与中断共享的变量必须使用 volatile */
static volatile uint32_t s_MsTick = 0U;        /* 上电以来的毫秒计数 */
static volatile uint32_t s_MsCnt  = 0U;        /* 1ms 累加计数，满 1000 为 1 秒 */
static volatile uint8_t  s_Hour   = 0U;        /* 时 0~23 */
static volatile uint8_t  s_Minute = 0U;        /* 分 0~59 */
static volatile uint8_t  s_Second = 0U;        /* 秒 0~59 */
static volatile uint8_t  s_SecFlag = 0U;       /* 1-刚刚过完一秒 */

/* ======================== 私有函数 ======================== */

/*
 * 函数名：Timer_EnterCritical
 * 描述  ：进入临界区，返回原中断屏蔽状态
 * 输入  ：无
 * 输出  ：进入前的 PRIMASK 值
 */
static uint32_t Timer_EnterCritical(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    return primask;
}

/*
 * 函数名：Timer_ExitCritical
 * 描述  ：恢复进入临界区前的中断屏蔽状态
 * 输入  ：primask-进入临界区时保存的值
 * 输出  ：无
 */
static void Timer_ExitCritical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/*
 * 函数名：Timer_NVIC_Config
 * 描述  ：配置 TIM2 更新中断优先级（2 位抢占 + 2 位子优先级）
 * 输入  ：无
 * 输出  ：无
 */
static void Timer_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1U;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0U;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/*
 * 函数名：Timer_Mode_Config
 * 描述  ：配置 TIM2 为 1ms 向上计数的更新中断
 * 输入  ：无
 * 输出  ：无
 */
static void Timer_Mode_Config(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructure.TIM_Period            = TIMER_PERIOD;
    TIM_TimeBaseStructure.TIM_Prescaler         = TIMER_PRESCALER;
    TIM_TimeBaseStructure.TIM_ClockDivision     = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0U;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
}

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：Timer_Init
 * 描述  ：初始化 TIM2 时基、中断与 NVIC，时间清零后立即开始走时
 * 输入  ：无
 * 输出  ：无
 */
void Timer_Init(void)
{
    /* 时间从 00:00:00 开始；如需验证进位，可在 main 里调 Timer_SetTime(23,59,50) */
    s_MsTick  = 0U;
    s_MsCnt   = 0U;
    s_Hour    = 0U;
    s_Minute  = 0U;
    s_Second  = 0U;
    s_SecFlag = 0U;

    Timer_Mode_Config();
    Timer_NVIC_Config();

    TIM_Cmd(TIM2, ENABLE);
}

/* ======================== 时间读写 ======================== */

/*
 * 函数名：Timer_GetTick
 * 描述  ：读取系统毫秒计数，供按键消抖、非阻塞延时等使用
 * 输入  ：无
 * 输出  ：毫秒计数
 */
uint32_t Timer_GetTick(void)
{
    return s_MsTick;
}

/*
 * 函数名：Timer_GetTime
 * 描述  ：读取当前时间（关中断拷贝，保证时/分/秒是同一时刻的值）
 * 输入  ：time-时间结构体指针
 * 输出  ：无
 */
void Timer_GetTime(Timer_TimeTypeDef *time)
{
    uint32_t primask;

    if (time == 0)
    {
        return;
    }

    primask = Timer_EnterCritical();

    time->hour   = s_Hour;
    time->minute = s_Minute;
    time->second = s_Second;

    Timer_ExitCritical(primask);
}

/*
 * 函数名：Timer_SetTime
 * 描述  ：设置当前时间，超范围自动取模；不改变毫秒计数
 * 输入  ：hour-0~23, minute-0~59, second-0~59
 * 输出  ：无
 */
void Timer_SetTime(uint8_t hour, uint8_t minute, uint8_t second)
{
    uint32_t primask;

    primask = Timer_EnterCritical();

    s_Hour   = (uint8_t)(hour   % TIMER_HOUR_PER_DAY);
    s_Minute = (uint8_t)(minute % TIMER_SEC_PER_MIN);
    s_Second = (uint8_t)(second % TIMER_SEC_PER_MIN);
    s_MsCnt  = 0U;             /* 从整秒重新计时，避免设置后立刻跳秒 */

    Timer_ExitCritical(primask);
}

/*
 * 函数名：Timer_SecondChanged
 * 描述  ：查询并清除"秒已变化"标志，属于读后即清除
 * 输入  ：无
 * 输出  ：1-这一秒刚过去, 0-没有变化
 */
uint8_t Timer_SecondChanged(void)
{
    uint8_t changed;
    uint32_t primask;

    primask = Timer_EnterCritical();

    changed   = s_SecFlag;
    s_SecFlag = 0U;

    Timer_ExitCritical(primask);

    return changed;
}

/* ======================== 中断服务函数 ======================== */

/*
 * 函数名：TIM2_IRQHandler
 * 描述  ：TIM2 更新中断，每 1ms 进入一次：累加毫秒，满 1000 进 1 秒并进位
 * 输入  ：无
 * 输出  ：无
 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        s_MsTick++;

        s_MsCnt++;
        if (s_MsCnt >= TIMER_MS_PER_SEC)
        {
            s_MsCnt   = 0U;
            s_SecFlag = 1U;           /* 通知主循环：可以刷新显示了 */

            s_Second++;
            if (s_Second >= TIMER_SEC_PER_MIN)          /* 秒满 60 进 1 分 */
            {
                s_Second = 0U;
                s_Minute++;
                if (s_Minute >= TIMER_MIN_PER_HOUR)     /* 分满 60 进 1 时 */
                {
                    s_Minute = 0U;
                    s_Hour++;
                    if (s_Hour >= TIMER_HOUR_PER_DAY)   /* 时满 24 归零 */
                    {
                        s_Hour = 0U;
                    }
                }
            }
        }
    }
}

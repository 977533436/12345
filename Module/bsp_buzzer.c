/*
 * 文件名：bsp_buzzer.c
 * 描述  ：有源蜂鸣器驱动源文件（非阻塞有限次数节奏）
 * 备注  ：PB13 推挽输出，低电平触发（输出低=响，高=停）
 *          节奏由 Timer_GetTick() 毫秒计数推进，Buzzer_Task() 在主循环轮询，
 *          不使用定时器中断、不使用 Delay，不会阻塞键盘扫描与显示刷新
 */

#include "bsp_buzzer.h"
#include "bsp_timer.h"
#include "bsp_delay.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* ======================== 私有变量 ======================== */
static uint8_t  s_Playing = 0U;                       /* 1-正在播放节奏 */
static uint8_t  s_SoundOn = 0U;                       /* 1-当前处于"响"的半周期 */
static uint8_t  s_Remain  = 0U;                       /* 还需要响的次数（不含当前这一声） */
static uint16_t s_OnMs    = BUZZER_DEFAULT_ON_MS;     /* 单声时长 */
static uint16_t s_OffMs   = BUZZER_DEFAULT_OFF_MS;    /* 间隔时长 */
static uint32_t s_Tick    = 0U;                       /* 半周期起始时刻 */

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：Buzzer_Init
 * 描述  ：配置 PB13 为推挽输出，并把蜂鸣器置为静音
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(BUZZER_GPIO_CLK, ENABLE);

    /* 先把输出寄存器预置成"静音"电平，再切成输出，避免上电瞬间误响 */
    Buzzer_Off();

    GPIO_InitStructure.GPIO_Pin   = BUZZER_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(BUZZER_GPIO_PORT, &GPIO_InitStructure);

    /* 复位节奏状态 */
    s_Playing = 0U;
    s_SoundOn = 0U;
    s_Remain  = 0U;

    Buzzer_Off();                 /* 引脚切换完成后再强制输出静音电平 */
}

/* ======================== 电平控制 ======================== */

/*
 * 函数名：Buzzer_On
 * 描述  ：输出有效电平，蜂鸣器响
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_On(void)
{
    if (BUZZER_ON_LEVEL != 0U)
    {
        GPIO_SetBits(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    }
    else
    {
        GPIO_ResetBits(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);   /* 本项目：低电平响 */
    }
}

/*
 * 函数名：Buzzer_Off
 * 描述  ：输出无效电平，蜂鸣器静音
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_Off(void)
{
    if (BUZZER_ON_LEVEL != 0U)
    {
        GPIO_ResetBits(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);
    }
    else
    {
        GPIO_SetBits(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN);     /* 本项目：高电平停 */
    }
}

/* ======================== 节奏控制 ======================== */

/*
 * 函数名：Buzzer_Play
 * 描述  ：启动一段非阻塞节奏，立即响第一声
 * 输入  ：on_ms-单声时长, off_ms-间隔时长, times-总声数
 * 输出  ：无
 */
void Buzzer_Play(uint16_t on_ms, uint16_t off_ms, uint8_t times)
{
    if (times == 0U)              /* 声数为 0 等于停止 */
    {
        Buzzer_Stop();
        return;
    }

    s_OnMs  = (on_ms  == 0U) ? BUZZER_DEFAULT_ON_MS  : on_ms;
    s_OffMs = (off_ms == 0U) ? BUZZER_DEFAULT_OFF_MS : off_ms;

    s_Remain  = (uint8_t)(times - 1U);   /* 第一声马上响，剩余次数先减掉 */
    s_Playing = 1U;
    s_SoundOn = 1U;
    s_Tick    = Timer_GetTick();

    Buzzer_On();
}

/*
 * 函数名：Buzzer_ErrorBeep
 * 描述  ：密码错误提示：短鸣 2 声（100ms 响 / 100ms 停）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_ErrorBeep(void)
{
    Buzzer_Play(BUZZER_SHORT_ON_MS, BUZZER_SHORT_OFF_MS, BUZZER_SHORT_TIMES);
}

/*
 * 函数名：Buzzer_LockBeep
 * 描述  ：连续错误锁定提示：长鸣 1 声（500ms）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_LockBeep(void)
{
    Buzzer_Play(BUZZER_LONG_ON_MS, 0U, 1U);
}

/*
 * 函数名：Buzzer_Stop
 * 描述  ：立即停止节奏并静音
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_Stop(void)
{
    s_Playing = 0U;
    s_SoundOn = 0U;
    s_Remain  = 0U;

    Buzzer_Off();
}

/*
 * 函数名：Buzzer_Task
 * 描述  ：节奏推进任务（非阻塞），需在主循环反复调用
 * 输入  ：无
 * 输出  ：无
 * 备注  ：用无符号相减算时间差，毫秒计数回绕时也不会出错
 */
void Buzzer_Task(void)
{
    uint32_t now;

    if (s_Playing == 0U)
    {
        return;
    }

    now = Timer_GetTick();

    if (s_SoundOn != 0U)                        /* 当前处于"响"的半周期 */
    {
        if ((now - s_Tick) < (uint32_t)s_OnMs)
        {
            return;                             /* 这一声还没响完 */
        }

        s_Tick    = now;
        s_SoundOn = 0U;
        Buzzer_Off();                           /* 响够了，先静音 */

        if (s_Remain == 0U)                     /* 后面没有声了 → 整段结束（停在静音） */
        {
            s_Playing = 0U;
        }
    }
    else                                        /* 当前处于"停"的半周期 */
    {
        if ((now - s_Tick) < (uint32_t)s_OffMs)
        {
            return;                             /* 间隔还没到 */
        }

        s_Tick    = now;
        s_SoundOn = 1U;
        s_Remain--;
        Buzzer_On();                            /* 响下一声 */
    }
}

/*
 * 函数名：Buzzer_IsBusy
 * 描述  ：查询是否正在播放节奏
 * 输入  ：无
 * 输出  ：1-正在播放, 0-空闲
 */
uint8_t Buzzer_IsBusy(void)
{
    return s_Playing;
}

/* ======================== 阻塞式单次鸣叫 ======================== */

/*
 * 函数名：Buzzer_Beep
 * 描述  ：阻塞式单次鸣叫（内部 DelayMs 等待）
 * 输入  ：on_ms-鸣叫时长(ms)
 * 输出  ：无
 * 备注  ：仅用于上电自检，不要在按键响应或密码验证流程里使用
 */
void Buzzer_Beep(uint16_t on_ms)
{
    Buzzer_On();
    DelayMs(on_ms);
    Buzzer_Off();
}

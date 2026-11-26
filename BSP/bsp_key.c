/*
 * 文件名：bsp_key.c
 * 描述  ：板载独立按键驱动源文件（轮询式 20ms 消抖）
 * 备注  ：按键编号与丝印一致：KEY2=PA0（高有效）、KEY3=PC8、KEY4=PC9（低有效）；
 *          KEY1 是复位键，由硬件直接拉低 NRST，软件不参与。
 *          每个按键各自维护 lastRaw/stable/changeTick/reported 四个状态，
 *          只有"电平稳定超过 20ms"才承认电平变化，稳定为按下时上报一次事件；
 *          松开后清 reported，允许下一次按下再上报，因此长按不会连续触发。
 *          不使用 EXTI，不会与 TIM2 的 NVIC 分组配置冲突。
 */

#include "bsp_key.h"
#include "bsp_timer.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* ======================== 私有宏定义 ======================== */
#define KEY2_IDX                      0U       /* 数组下标：KEY2 */
#define KEY3_IDX                      1U       /* 数组下标：KEY3 */
#define KEY4_IDX                      2U       /* 数组下标：KEY4 */

/* ======================== 私有类型定义 ======================== */

/*
 * 类型名：Key_State
 * 描述  ：单个按键的消抖状态
 */
typedef struct
{
    uint8_t  lastRaw;       /* 上一次采样到的原始电平：1-按下, 0-松开 */
    uint8_t  stable;        /* 消抖后确认的电平：1-按下, 0-松开 */
    uint8_t  reported;      /* 1-本次按下已上报，等待松开 */
    uint32_t changeTick;    /* 原始电平最后一次跳变的时刻（ms） */
} Key_State;

/* ======================== 私有变量 ======================== */
static Key_State s_Key[KEY_COUNT];      /* [0]=KEY2, [1]=KEY3, [2]=KEY4 */

/* ======================== 私有函数 ======================== */

/*
 * 函数名：Key_ReadRaw
 * 描述  ：读取某个按键的原始电平，换算成"按下=1 / 松开=0"
 * 输入  ：idx-按键下标（KEY2_IDX / KEY3_IDX / KEY4_IDX）
 * 输出  ：1-按下, 0-松开
 * 备注  ：KEY2 高有效，KEY3/KEY4 低有效，这里统一成逻辑值，上层不必关心极性
 */
static uint8_t Key_ReadRaw(uint8_t idx)
{
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       active;

    if (idx == KEY2_IDX)                    /* KEY2 -> PA0，高有效 */
    {
        port   = KEY2_GPIO_PORT;
        pin    = KEY2_GPIO_PIN;
        active = KEY2_ACTIVE_LEVEL;
    }
    else if (idx == KEY3_IDX)               /* KEY3 -> PC8，低有效 */
    {
        port   = KEY3_GPIO_PORT;
        pin    = KEY3_GPIO_PIN;
        active = KEY3_ACTIVE_LEVEL;
    }
    else                                    /* KEY4 -> PC9，低有效 */
    {
        port   = KEY4_GPIO_PORT;
        pin    = KEY4_GPIO_PIN;
        active = KEY4_ACTIVE_LEVEL;
    }

    if (GPIO_ReadInputDataBit(port, pin) == active)
    {
        return 1U;
    }
    return 0U;
}

/*
 * 函数名：Key_GPIO_Config
 * 描述  ：配置按键引脚为输入（KEY2 下拉，KEY3/KEY4 上拉）
 * 输入  ：无
 * 输出  ：无
 */
static void Key_GPIO_Config(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(KEY_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Mode = KEY2_GPIO_MODE;      /* PA0 下拉输入 */
    GPIO_InitStructure.GPIO_Pin  = KEY2_GPIO_PIN;
    GPIO_Init(KEY2_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = KEY3_GPIO_MODE;      /* PC8 上拉输入 */
    GPIO_InitStructure.GPIO_Pin  = KEY3_GPIO_PIN;
    GPIO_Init(KEY3_GPIO_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Mode = KEY4_GPIO_MODE;      /* PC9 上拉输入 */
    GPIO_InitStructure.GPIO_Pin  = KEY4_GPIO_PIN;
    GPIO_Init(KEY4_GPIO_PORT, &GPIO_InitStructure);
}

/*
 * 函数名：Key_UpdateOne
 * 描述  ：单个按键的消抖状态机，稳定为按下时返回 1
 * 输入  ：idx-按键下标, now-当前毫秒计数
 * 输出  ：1-检测到一次新的按下沿, 0-无
 */
static uint8_t Key_UpdateOne(uint8_t idx, uint32_t now)
{
    uint8_t raw;

    raw = Key_ReadRaw(idx);

    if (raw != s_Key[idx].lastRaw)
    {
        /* 电平跳变：更新采样值，并重新开始 20ms 计时，不立即认账 */
        s_Key[idx].lastRaw    = raw;
        s_Key[idx].changeTick = now;
    }
    else if ((now - s_Key[idx].changeTick) >= KEY_DEBOUNCE_MS)
    {
        /* 电平已稳定超过 20ms，才承认这次变化 */
        if (raw != s_Key[idx].stable)
        {
            s_Key[idx].stable = raw;

            if (raw != 0U)                  /* 稳定为"按下"：产生一次事件 */
            {
                if (s_Key[idx].reported == 0U)
                {
                    s_Key[idx].reported = 1U;
                    return 1U;
                }
            }
            else                            /* 稳定为"松开"：允许下次再上报 */
            {
                s_Key[idx].reported = 0U;
            }
        }
    }

    return 0U;
}

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：Key_Init
 * 描述  ：配置 KEY2/KEY3/KEY4 引脚并复位消抖状态
 * 输入  ：无
 * 输出  ：无
 * 备注  ：必须在 Timer_Init() 之后调用；初始化时把当前电平当作稳定值，
 *          所以上电瞬间按住按键不会误触发
 */
void Key_Init(void)
{
    uint32_t now;
    uint8_t  i;

    Key_GPIO_Config();

    now = Timer_GetTick();

    for (i = 0U; i < KEY_COUNT; i++)
    {
        s_Key[i].lastRaw    = Key_ReadRaw(i);
        s_Key[i].stable     = s_Key[i].lastRaw;
        s_Key[i].changeTick = now;
        s_Key[i].reported   = 0U;
    }
}

/* ======================== 扫描函数 ======================== */

/*
 * 函数名：Key_Scan
 * 描述  ：扫描三个按键，返回一个按下沿事件（优先级 KEY2 > KEY3 > KEY4）
 * 输入  ：无
 * 输出  ：KEY_NONE / KEY2_PRESS / KEY3_PRESS / KEY4_PRESS
 */
Key_Status Key_Scan(void)
{
    uint32_t now;

    now = Timer_GetTick();

    if (Key_UpdateOne(KEY2_IDX, now) != 0U)
    {
        return KEY2_PRESS;
    }
    if (Key_UpdateOne(KEY3_IDX, now) != 0U)
    {
        return KEY3_PRESS;
    }
    if (Key_UpdateOne(KEY4_IDX, now) != 0U)
    {
        return KEY4_PRESS;
    }

    return KEY_NONE;
}

/*
 * 函数名：Key_IsDown
 * 描述  ：查询按键当前是否按下（稳定电平，已消抖）
 * 输入  ：key-按键事件编号
 * 输出  ：1-按下, 0-松开或参数非法
 */
uint8_t Key_IsDown(Key_Status key)
{
    uint8_t idx;

    if (key == KEY2_PRESS)
    {
        idx = KEY2_IDX;
    }
    else if (key == KEY3_PRESS)
    {
        idx = KEY3_IDX;
    }
    else if (key == KEY4_PRESS)
    {
        idx = KEY4_IDX;
    }
    else
    {
        return 0U;
    }

    return s_Key[idx].stable;
}

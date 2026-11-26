/*
 * 文件名：main.c
 * 描述  ：智能电子密码锁系统 —— 功能联调主程序（模块1~5 整合）
 * 备注  ：基于 STM32F103 标准外设库
 *          模块1 OLED     0.96 寸 SSD1306，I2C1，PB6(SCL)/PB7(SDA)
 *          模块2 键盘     HW-834，R1~R4=PA1~PA4，C1~C4=PA5/PA6/PA7/PC4（0~F 布局）
 *          模块3 蜂鸣器   有源、低电平触发，PB13
 *          模块4 舵机     SG90，PB8（TIM4_CH3，50Hz），0°上锁 / 180°开锁
 *          模块5 密码存储 片内 Flash 最后一页 0x0803F800，默认密码 1234
 *          调试口         USART1（板载 CH340），115200-8-N-1
 *
 *          【按键定义】
 *            0~9  ：输入数字
 *            A    ：删除（退格）
 *            B    ：确认（提交 4 位密码）
 *            C    ：进入/退出改密模式
 *            D    ：恢复出厂 —— 擦除 Flash 中的密码，密码回到默认 1234（仅待机状态有效）
 *
 *          【界面】（4 行 × 16 字符）
 *            第 1 行：标题 PASSWORD LOCK / CHANGE PASSWORD
 *            第 2 行：提示语（输入密码 / 错误提示 / 改密步骤）
 *            第 3 行：输入框 [ * * _ _ ] 或 开锁/锁定倒计时
 *            第 4 行：按键提示
 *
 *          【业务流程】
 *            ① 待机：输入 4 位密码，B 确认
 *            ② 正确：清错误计数 → 舵机转 180° 保持 3s → 自动回 0°，屏幕显示 UNLOCKED
 *            ③ 错误：短鸣 2 声 + 提示；连续 3 次错误 → 长鸣 1 声 + 锁定 30s（键盘无效）
 *            ④ 改密：C 进入 → 验原密码 → 输入新密码 → 再次确认 → 写 Flash（断电保留）
 *            ⑤ 改密过程 30s 无按键自动退出，避免卡在改密界面
 */

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include "bsp_delay.h"
#include "bsp_timer.h"
#include "bsp_usart.h"
#include "bsp_oled.h"
#include "bsp_matrixkey.h"
#include "bsp_buzzer.h"
#include "bsp_servo.h"
#include "bsp_flashpwd.h"

/* ======================== 参数宏定义 ======================== */
#define PWD_LEN                   4U          /* 密码位数 */
#define ERR_LOCK_MAX              3U          /* 连错几次进入锁定 */
#define LOCKOUT_MS                30000U      /* 锁定时长 30s */
#define CHG_IDLE_MS               30000U      /* 改密界面无操作退出时间 */
#define UI_REFRESH_MS             200U        /* 界面刷新周期 */
#define BOOT_REDRAW_MS            300U        /* 上电兜底重绘延时 */

/* 功能键（0~F 布局，避开数字键） */
#define KEY_DEL                   'A'         /* 删除 */
#define KEY_OK                    'B'         /* 确认 */
#define KEY_SET                   'C'         /* 改密 */
#define KEY_RST                   'D'         /* 恢复出厂：擦除 Flash 密码，回到 1234 */

/* ======================== 类型定义 ======================== */

/*
 * 类型名：LockState
 * 描述  ：系统状态（待机输入 / 开锁 / 锁定 / 改密三步）
 */
typedef enum
{
    ST_IDLE = 0,        /* 待机：输入密码 */
    ST_OPEN,            /* 开锁成功，舵机保持 180°，3s 后自动回位 */
    ST_LOCKOUT,         /* 连错 3 次，锁定倒计时中 */
    ST_CHG_OLD,         /* 改密：输入原密码 */
    ST_CHG_NEW,         /* 改密：输入新密码 */
    ST_CHG_AGAIN        /* 改密：再次输入新密码确认 */
} LockState;

/* ======================== 全局变量（工程约定：定义在 main 外部） ======================== */
unsigned char g_OledOk = 0;                     /* 1-OLED 有应答 */
unsigned char g_OledBuf[24];                    /* 显示/格式化缓冲 */

char          g_UiLines[4][17];                 /* 4 行显示内容快照（内容没变就不重画） */

char          g_KeyChar = 0;                    /* 本次按键字符 */
uint8_t       g_KeyRaw  = 0;                    /* 本次按键物理键号 */

LockState     g_State      = ST_IDLE;           /* 当前状态 */
uint8_t       g_Input[PWD_LEN] = { 0U, 0U, 0U, 0U };   /* 输入缓冲 */
uint8_t       g_InputLen   = 0U;                /* 已输入位数 */
uint8_t       g_Pwd[PWD_LEN]    = { 1U, 2U, 3U, 4U };  /* 当前有效密码 */
uint8_t       g_NewPwd[PWD_LEN] = { 0U, 0U, 0U, 0U };  /* 改密时的新密码暂存 */
uint8_t       g_ErrCount   = 0U;                /* 连续错误次数 */

char         *g_Hint       = 0;                 /* 临时提示文字（0 表示无） */
uint32_t      g_HintUntil  = 0U;                /* 提示到期时刻 */
uint32_t      g_LockTick   = 0U;                /* 锁定开始时刻 */
uint32_t      g_StateTick  = 0U;                /* 进入当前状态的时刻 */
uint32_t      g_LastKeyTick = 0U;               /* 最近按键时刻（改密超时用） */
uint32_t      g_LastUiTick = 0U;                /* 最近界面刷新时刻 */
uint32_t      g_BootTick   = 0U;                /* 上电时刻 */
uint8_t       g_RedrawDone = 0U;                /* 上电兜底重绘标志 */

/* ======================== 私有函数：输入缓冲 ======================== */

/*
 * 函数名：Input_Clear
 * 描述  ：清空输入缓冲
 */
static void Input_Clear(void)
{
    g_InputLen = 0U;
}

/*
 * 函数名：Input_Append
 * 描述  ：追加一位数字（超过 4 位忽略）
 * 输入  ：digit-0~9
 */
static void Input_Append(uint8_t digit)
{
    if (g_InputLen < PWD_LEN)
    {
        g_Input[g_InputLen] = digit;
        g_InputLen++;
    }
}

/*
 * 函数名：Input_Delete
 * 描述  ：退格删除一位
 */
static void Input_Delete(void)
{
    if (g_InputLen > 0U)
    {
        g_InputLen--;
    }
}

/*
 * 函数名：Pwd_Equal
 * 描述  ：比较两组 4 位密码
 * 输入  ：a-密码1, b-密码2
 * 输出  ：1-相同, 0-不同
 */
static uint8_t Pwd_Equal(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;

    for (i = 0U; i < PWD_LEN; i++)
    {
        if (a[i] != b[i])
        {
            return 0U;
        }
    }
    return 1U;
}

/*
 * 函数名：Hint_Set
 * 描述  ：设置临时提示（在提示有效期内覆盖第 2 行文字）
 * 输入  ：s-提示文字, ms-持续毫秒
 */
static void Hint_Set(char *s, uint32_t ms)
{
    g_Hint      = s;
    g_HintUntil = Timer_GetTick() + ms;
}

/* ======================== 私有函数：界面 ======================== */

/*
 * 函数名：Ui_Line
 * 描述  ：按行刷新（内容与上次相同时跳过，减少 I2C 传输）
 * 输入  ：line-行号 0~3, str-内容（≤16 字符）
 */
static void Ui_Line(uint8_t line, char *str)
{
    if (strcmp(g_UiLines[line], str) == 0)
    {
        return;                          /* 内容没变，不重画 */
    }

    strncpy(g_UiLines[line], str, OLED_LINE_CHARS);
    g_UiLines[line][OLED_LINE_CHARS] = '\0';

    OLED_ShowLine(line, str);
}

/*
 * 函数名：Ui_BuildBox
 * 描述  ：生成密码输入框 "[ * * _ _ ]"
 * 输入  ：filled-已输入位数
 * 输出  ：无（结果写 g_OledBuf）
 */
static void Ui_BuildBox(uint8_t filled)
{
    uint8_t i;

    g_OledBuf[0] = '[';
    g_OledBuf[1] = ' ';

    for (i = 0U; i < PWD_LEN; i++)
    {
        g_OledBuf[2U + i * 2U] = (i < filled) ? '*' : '_';
        g_OledBuf[3U + i * 2U] = ' ';
    }

    g_OledBuf[2U + PWD_LEN * 2U] = ']';
    g_OledBuf[3U + PWD_LEN * 2U] = '\0';
}

/*
 * 函数名：Ui_Update
 * 描述  ：按当前状态刷新整屏（每行只在内容变化时才真正写屏）
 */
static void Ui_Update(void)
{
    uint32_t now   = Timer_GetTick();
    uint16_t remain;
    uint16_t sec;

    if (g_OledOk == 0)
    {
        return;
    }

    switch (g_State)
    {
    case ST_OPEN:                        /* 开锁成功：显示倒计时 */
        Ui_Line(0, "  PASSWORD LOCK");
        Ui_Line(1, "*** UNLOCKED ***");

        remain = Servo_GetRemainMs();
        sec    = (uint16_t)((remain + 999U) / 1000U);
        sprintf((char *)g_OledBuf, "AUTO LOCK IN %us", (unsigned int)sec);
        Ui_Line(2, (char *)g_OledBuf);

        Ui_Line(3, "PLEASE WAIT");
        break;

    case ST_LOCKOUT:                     /* 连错锁定：显示剩余秒 */
        Ui_Line(0, "  PASSWORD LOCK");
        Ui_Line(1, "TOO MANY ERRORS");

        remain = (uint16_t)(LOCKOUT_MS - (now - g_LockTick));
        sec    = (uint16_t)((remain + 999U) / 1000U);
        sprintf((char *)g_OledBuf, "WAIT %2us", (unsigned int)sec);
        Ui_Line(2, (char *)g_OledBuf);

        Ui_Line(3, "KEYBOARD LOCKED");
        break;

    default:                             /* 待机 / 改密三步 */
        if (g_State == ST_IDLE)
        {
            Ui_Line(0, "  PASSWORD LOCK");

            if ((g_Hint != 0) && ((int32_t)(now - g_HintUntil) < 0))
            {
                Ui_Line(1, g_Hint);
            }
            else
            {
                Ui_Line(1, "ENTER PASSWORD");
            }

            Ui_BuildBox(g_InputLen);
            Ui_Line(2, (char *)g_OledBuf);

            /* 第 4 行：16 字符放不下 4 个功能键说明，两组提示每 3s 轮换一次 */
            if (((now / 3000U) & 1U) == 0U)
            {
                Ui_Line(3, "A:DEL B:OK C:SET");
            }
            else
            {
                Ui_Line(3, "D:RESET FLASH");
            }
        }
        else                             /* 改密流程 */
        {
            Ui_Line(0, " CHANGE PASSWORD");

            if ((g_Hint != 0) && ((int32_t)(now - g_HintUntil) < 0))
            {
                Ui_Line(1, g_Hint);
            }
            else if (g_State == ST_CHG_OLD)
            {
                Ui_Line(1, "ENTER OLD PWD");
            }
            else if (g_State == ST_CHG_NEW)
            {
                Ui_Line(1, "ENTER NEW PWD");
            }
            else
            {
                Ui_Line(1, "CONFIRM NEW PWD");
            }

            Ui_BuildBox(g_InputLen);
            Ui_Line(2, (char *)g_OledBuf);

            Ui_Line(3, "A:DEL B:OK C:ESC");
        }
        break;
    }
}

/* ======================== 私有函数：业务逻辑 ======================== */

/*
 * 函数名：App_HandleKey
 * 描述  ：按键分发（按当前状态处理数字输入与功能键）
 * 输入  ：ch-按键字符
 */
static void App_HandleKey(char ch)
{
    uint8_t digit   = 0U;
    uint8_t isDigit = 0U;
    uint8_t i;

    if ((ch >= '0') && (ch <= '9'))
    {
        digit   = (uint8_t)(ch - '0');
        isDigit = 1U;
    }

    switch (g_State)
    {
    case ST_IDLE:
        if (isDigit != 0U)
        {
            Input_Append(digit);
        }
        else if (ch == KEY_DEL)
        {
            Input_Delete();
        }
        else if (ch == KEY_OK)
        {
            if (g_InputLen != PWD_LEN)
            {
                Hint_Set("NEED 4 DIGITS", 1200U);
                break;
            }

            if (Pwd_Equal(g_Input, g_Pwd) != 0U)     /* 密码正确 */
            {
                g_ErrCount = 0U;
                Input_Clear();
                Servo_Unlock();                       /* 开锁：180°，保持 3s */
                g_State     = ST_OPEN;
                g_StateTick = Timer_GetTick();
            }
            else                                      /* 密码错误 */
            {
                g_ErrCount++;
                Input_Clear();
                Buzzer_ErrorBeep();                   /* 短鸣 2 声 */

                if (g_ErrCount >= ERR_LOCK_MAX)       /* 连错 3 次 → 锁定 */
                {
                    g_ErrCount  = 0U;
                    g_LockTick  = Timer_GetTick();
                    g_State     = ST_LOCKOUT;
                    Buzzer_LockBeep();                /* 长鸣 1 声 */
                }
                else
                {
                    Hint_Set("WRONG PASSWORD", 1200U);
                }
            }
        }
        else if (ch == KEY_SET)                       /* 进入改密 */
        {
            Input_Clear();
            g_State     = ST_CHG_OLD;
            g_StateTick = Timer_GetTick();
        }
        else if (ch == KEY_RST)                       /* 恢复出厂：擦除 Flash 密码 */
        {
            Input_Clear();
            g_ErrCount = 0U;

            if (FlashPwd_Erase() == FLASH_PWD_OK)
            {
                FlashPwd_Get(g_Pwd);                  /* 密码回到默认 1234 */
                Hint_Set("PWD RESET 1234", 2000U);
                Buzzer_Play(80U, 0U, 1U);             /* 短"滴"一声表示已重置 */
            }
            else
            {
                Hint_Set("FLASH ERROR", 2000U);
                Buzzer_ErrorBeep();
            }
        }
        else
        {
            /* 其余键忽略 */
        }
        break;

    case ST_CHG_OLD:                                  /* ① 验原密码 */
        if (isDigit != 0U)
        {
            Input_Append(digit);
        }
        else if (ch == KEY_DEL)
        {
            Input_Delete();
        }
        else if (ch == KEY_OK)
        {
            if (g_InputLen != PWD_LEN)
            {
                Hint_Set("NEED 4 DIGITS", 1200U);
                break;
            }

            if (Pwd_Equal(g_Input, g_Pwd) != 0U)
            {
                Input_Clear();
                g_State     = ST_CHG_NEW;
                g_StateTick = Timer_GetTick();
            }
            else
            {
                Input_Clear();
                g_State = ST_IDLE;
                Hint_Set("OLD PWD WRONG", 1500U);
            }
        }
        else if (ch == KEY_SET)                       /* 取消 */
        {
            Input_Clear();
            g_State = ST_IDLE;
        }
        else
        {
            /* 其余键忽略 */
        }
        break;

    case ST_CHG_NEW:                                  /* ② 输入新密码 */
        if (isDigit != 0U)
        {
            Input_Append(digit);
        }
        else if (ch == KEY_DEL)
        {
            Input_Delete();
        }
        else if (ch == KEY_OK)
        {
            if (g_InputLen != PWD_LEN)
            {
                Hint_Set("NEED 4 DIGITS", 1200U);
                break;
            }

            for (i = 0U; i < PWD_LEN; i++)            /* 暂存新密码 */
            {
                g_NewPwd[i] = g_Input[i];
            }

            Input_Clear();
            g_State     = ST_CHG_AGAIN;
            g_StateTick = Timer_GetTick();
        }
        else if (ch == KEY_SET)                       /* 取消 */
        {
            Input_Clear();
            g_State = ST_IDLE;
        }
        else
        {
            /* 其余键忽略 */
        }
        break;

    case ST_CHG_AGAIN:                                /* ③ 再次确认 */
        if (isDigit != 0U)
        {
            Input_Append(digit);
        }
        else if (ch == KEY_DEL)
        {
            Input_Delete();
        }
        else if (ch == KEY_OK)
        {
            if (g_InputLen != PWD_LEN)
            {
                Hint_Set("NEED 4 DIGITS", 1200U);
                break;
            }

            if (Pwd_Equal(g_Input, g_NewPwd) != 0U)   /* 两次一致 → 写 Flash */
            {
                Input_Clear();

                if (FlashPwd_Set(g_NewPwd) == FLASH_PWD_OK)
                {
                    FlashPwd_Get(g_Pwd);              /* 新密码立即生效 */
                    g_State = ST_IDLE;
                    Hint_Set("PASSWORD CHANGED", 2000U);
                }
                else
                {
                    g_State = ST_IDLE;
                    Hint_Set("FLASH ERROR", 2000U);
                }
            }
            else                                      /* 不一致：重输确认 */
            {
                Input_Clear();
                Hint_Set("NOT MATCH, RETRY", 1500U);
            }
        }
        else if (ch == KEY_SET)                       /* 取消 */
        {
            Input_Clear();
            g_State = ST_IDLE;
        }
        else
        {
            /* 其余键忽略 */
        }
        break;

    case ST_OPEN:                                     /* 开锁中不响应按键 */
    case ST_LOCKOUT:                                  /* 锁定中不响应按键 */
    default:
        break;
    }
}

/*
 * 函数名：App_Task
 * 描述  ：状态超时处理（开锁结束 / 锁定结束 / 改密超时）
 */
static void App_Task(void)
{
    uint32_t now = Timer_GetTick();

    /* ① 开锁保持结束（舵机已自动回位）→ 回待机 */
    if ((g_State == ST_OPEN) && (Servo_GetRemainMs() == 0U) &&
        ((now - g_StateTick) >= 100U))
    {
        Input_Clear();
        g_State     = ST_IDLE;
        g_StateTick = now;
    }

    /* ② 锁定时间到 → 解锁并清零错误计数 */
    if ((g_State == ST_LOCKOUT) && ((now - g_LockTick) >= LOCKOUT_MS))
    {
        g_ErrCount  = 0U;
        Input_Clear();
        g_State     = ST_IDLE;
        g_StateTick = now;
    }

    /* ③ 改密界面长时间无操作 → 自动退出 */
    if (((g_State == ST_CHG_OLD) || (g_State == ST_CHG_NEW) ||
         (g_State == ST_CHG_AGAIN)) &&
        ((now - g_LastKeyTick) >= CHG_IDLE_MS))
    {
        Input_Clear();
        g_State = ST_IDLE;
        Hint_Set("CHANGE TIMEOUT", 1500U);
        g_StateTick = now;
    }
}

/* ======================== 主函数 ======================== */

int main(void)
{
    /* ① 底层初始化 */
    Delay_Init();
    Timer_Init();
    Usart_Init();

    /* ② 模块初始化：键盘 → 蜂鸣器 → 舵机（上电即上锁位） → Flash 密码 → OLED */
    MatrixKey_Init();
    Buzzer_Init();
    Servo_Init();
    FlashPwd_Init();
    FlashPwd_Get(g_Pwd);

    OLED_Init();
    if (OLED_Check() == 0)
    {
        g_OledOk = 1;
    }

    /* ③ 初始状态与界面 */
    Input_Clear();
    memset(g_UiLines, 0, sizeof(g_UiLines));
    Ui_Update();

    g_BootTick  = Timer_GetTick();
    g_LastKeyTick = g_BootTick;
    g_LastUiTick  = g_BootTick;

    UsartPrintf(USART_DEBUG, "\r\n[Lock] System ready, pwd stored=%u\r\n",
                (unsigned int)FlashPwd_IsStored());

    /* ④ 主循环：全部查询式，无阻塞等待 */
    while (1)
    {
        /* 各模块任务推进 */
        Buzzer_Task();                  /* 蜂鸣器节奏 */
        Servo_Task();                   /* 开锁 3s 自动回位 */
        App_Task();                     /* 状态超时处理 */

        /* 上电兜底重绘：断电重启瞬间总线最不稳，300ms 后强制整屏重画一次 */
        if ((g_RedrawDone == 0U) && ((Timer_GetTick() - g_BootTick) >= BOOT_REDRAW_MS))
        {
            g_RedrawDone = 1U;
            memset(g_UiLines, 0, sizeof(g_UiLines));   /* 清快照 → 强制 4 行都重画 */
            Ui_Update();
        }

        /* 按键处理 */
        g_KeyChar = MatrixKey_GetKey();
        if (g_KeyChar != 0)
        {
            g_KeyRaw     = MatrixKey_GetRawKey();
            g_LastKeyTick = Timer_GetTick();

            App_HandleKey(g_KeyChar);
            Ui_Update();

            UsartPrintf(USART_DEBUG, "KEY=%c(S%02u) STATE=%u ERR=%u PWD=%u%u%u%u\r\n",
                        g_KeyChar, (unsigned int)g_KeyRaw,
                        (unsigned int)g_State, (unsigned int)g_ErrCount,
                        (unsigned int)g_Pwd[0], (unsigned int)g_Pwd[1],
                        (unsigned int)g_Pwd[2], (unsigned int)g_Pwd[3]);
        }

        /* 定时刷新（倒计时、提示超时） */
        if ((Timer_GetTick() - g_LastUiTick) >= UI_REFRESH_MS)
        {
            g_LastUiTick = Timer_GetTick();
            Ui_Update();

            if (g_OledOk != 0)          /* OLED 掉线保护 */
            {
                if (OLED_IsAlive() == 0U)
                {
                    g_OledOk = 0;
                }
            }
        }
    }
}

/*
 * 文件名：bsp_buzzer.h
 * 描述  ：有源蜂鸣器驱动头文件（非阻塞节奏输出）
 * 备注  ：基于 STM32F103 标准外设库
 *          开发板无蜂鸣器，外接"有源蜂鸣器模块"（模块自带振荡与三极管驱动），
 *          控制端接 PB13（JP1.23 引出），推挽输出。
 *          **低电平触发**：输出低电平响、高电平停，上电初始化为高电平（静音）。
 *          有源蜂鸣器不需要 PWM，也不需要定时器通道，只占一个普通 GPIO。
 *
 *          密码锁用到的两种节奏：
 *            · 密码错误   → 短鸣 2 声（响 100ms / 停 100ms，共 2 次）
 *            · 连错 3 次锁定 → 长鸣 1 声（500ms）
 *          两种节奏都由 Buzzer_Task() 用毫秒计数推进，主循环反复调用即可，
 *          不阻塞键盘扫描、串口接收与 OLED 刷新。
 */

#ifndef __BSP_BUZZER_H
#define __BSP_BUZZER_H

#include "stm32f10x.h"

/* ======================== 引脚宏定义 ======================== */
#define BUZZER_GPIO_PORT          GPIOB
#define BUZZER_GPIO_PIN           GPIO_Pin_13     /* PB13，JP1.23 */
#define BUZZER_GPIO_CLK           RCC_APB2Periph_GPIOB

/* ======================== 触发电平宏定义 ======================== */
/*
 * 0-低电平响（本项目模块：模块上带 PNP 三极管，控制端拉低导通）
 * 1-高电平响（模块带 NPN 三极管或裸蜂鸣器直驱）
 * 判断方法：只接 VCC/GND、信号线悬空，若一直响就是低电平触发。
 */
#define BUZZER_ON_LEVEL           0U

/* ======================== 节奏参数宏定义 ======================== */
#define BUZZER_SHORT_ON_MS        100U            /* 错误短鸣：单声时长 */
#define BUZZER_SHORT_OFF_MS       100U            /* 错误短鸣：间隔时长 */
#define BUZZER_SHORT_TIMES        2U              /* 错误短鸣：响几声 */

#define BUZZER_LONG_ON_MS         500U            /* 锁定长鸣：单声时长 */

#define BUZZER_DEFAULT_ON_MS      100U            /* Buzzer_Play 传 0 时的默认值 */
#define BUZZER_DEFAULT_OFF_MS     100U

/* ======================== 函数声明 ======================== */

/*
 * 函数名：Buzzer_Init
 * 描述  ：配置 PB13 为推挽输出，并先置为静音电平（上电不响）
 * 输入  ：无
 * 输出  ：无
 * 备注  ：必须在 Timer_Init() 之后调用（节奏依赖毫秒时基）
 */
void Buzzer_Init(void);

/*
 * 函数名：Buzzer_On
 * 描述  ：立刻发声（输出有效电平）
 * 输入  ：无
 * 输出  ：无
 * 备注  ：只做电平操作，不管理节奏；一般用 Buzzer_Play 系列
 */
void Buzzer_On(void);

/*
 * 函数名：Buzzer_Off
 * 描述  ：立刻静音（输出无效电平）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_Off(void);

/*
 * 函数名：Buzzer_Play
 * 描述  ：启动一段非阻塞节奏：响 times 次，每次响 on_ms、间隔 off_ms
 * 输入  ：on_ms-单声时长(ms), off_ms-间隔时长(ms), times-响几声(≥1)
 * 输出  ：无
 * 备注  ：立即响第一声；之后必须在主循环持续调用 Buzzer_Task() 才会继续；
 *          再次调用会打断上一段节奏（以新参数为准）
 */
void Buzzer_Play(uint16_t on_ms, uint16_t off_ms, uint8_t times);

/*
 * 函数名：Buzzer_ErrorBeep
 * 描述  ：密码错误提示音：短鸣 2 声（100ms 响 / 100ms 停）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_ErrorBeep(void);

/*
 * 函数名：Buzzer_LockBeep
 * 描述  ：连续错误进入锁定提示音：长鸣 1 声（500ms）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_LockBeep(void);

/*
 * 函数名：Buzzer_Stop
 * 描述  ：立即停止当前节奏并静音（打断正在播放的提示音）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_Stop(void);

/*
 * 函数名：Buzzer_Task
 * 描述  ：节奏推进任务，需在主循环里反复调用（非阻塞，约 1ms 调用一次即可）
 * 输入  ：无
 * 输出  ：无
 */
void Buzzer_Task(void);

/*
 * 函数名：Buzzer_IsBusy
 * 描述  ：查询是否正在播放（用于界面显示 / 避免重复触发）
 * 输入  ：无
 * 输出  ：1-正在响或等待间隔, 0-空闲
 */
uint8_t Buzzer_IsBusy(void);

/*
 * 函数名：Buzzer_Beep
 * 描述  ：阻塞式单次鸣叫（内部 DelayMs 等待）
 * 输入  ：on_ms-鸣叫时长(ms)
 * 输出  ：无
 * 备注  ：仅用于上电自检等允许阻塞的场合，不要在按键处理里使用
 */
void Buzzer_Beep(uint16_t on_ms);

#endif /* __BSP_BUZZER_H */

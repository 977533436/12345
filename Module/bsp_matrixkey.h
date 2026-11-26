/*
 * 文件名：bsp_matrixkey.h
 * 描述  ：4×4 矩阵键盘驱动头文件（HW-834，行扫描）
 * 备注  ：基于 STM32F103 标准外设库
 *          行线 R1~R4 = PA1~PA4（推挽输出，逐行拉低）
 *          列线 C1~C4 = PA5/PA6/PA7/PC4（上拉输入，读到低电平即该列被按下）
 *          模块为无源开关矩阵，无 VCC/GND，不接电源
 *          扫描周期 10ms，2 次采样一致才确认（约 20ms 消抖），只在按下沿上报一次（长按不重复）
 */

#ifndef __BSP_MATRIXKEY_H
#define __BSP_MATRIXKEY_H

#include "stm32f10x.h"

/* ======================== 行线引脚（GPIOA 推挽输出） ======================== */
#define KEY_ROW_GPIO_PORT         GPIOA
#define KEY_ROW_GPIO_CLK          RCC_APB2Periph_GPIOA

#define KEY_ROW1_PIN              GPIO_Pin_1     /* R1 → PA1，JP1.9 （最上面一行） */
#define KEY_ROW2_PIN              GPIO_Pin_2     /* R2 → PA2，JP1.12 */
#define KEY_ROW3_PIN              GPIO_Pin_3     /* R3 → PA3，JP1.11 */
#define KEY_ROW4_PIN              GPIO_Pin_4     /* R4 → PA4，JP1.14（最下面一行） */

/* ======================== 列线引脚（上拉输入） ======================== */
#define KEY_COL_GPIO_CLK          (RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC)

#define KEY_COL1_GPIO_PORT        GPIOA
#define KEY_COL1_PIN              GPIO_Pin_5     /* C1 → PA5，JP1.13（实测安装方向下在最左列） */
#define KEY_COL2_GPIO_PORT        GPIOA
#define KEY_COL2_PIN              GPIO_Pin_6     /* C2 → PA6，JP1.16 */
#define KEY_COL3_GPIO_PORT        GPIOA
#define KEY_COL3_PIN              GPIO_Pin_7     /* C3 → PA7，JP1.15 */
#define KEY_COL4_GPIO_PORT        GPIOC
#define KEY_COL4_PIN              GPIO_Pin_4     /* C4 → PC4，JP1.18（实测安装方向下在最右列） */

/* ======================== 参数宏定义 ======================== */
#define KEY_SCAN_PERIOD_MS        10U            /* 扫描周期 10ms（4 行 × 50us，占用 CPU < 3%） */
#define KEY_DEBOUNCE_CNT          2U             /* 连续 2 次一致才认账，即约 20ms 消抖 */
#define KEY_ROW_NUM               4U
#define KEY_COL_NUM               4U
#define KEY_RAW_NONE              0U             /* 无键 / 无效（多键同时按下） */

/*
 * 键值映射（实测：C1 在安装方向的最左列，R1 在最上行）：
 *         C1   C2   C3   C4
 *   R1     0    1    2    3
 *   R2     4    5    6    7
 *   R3     8    9    A    B
 *   R4     C    D    E    F
 *
 * 物理键号 S1~S16 与行列的对应关系是固定的：
 *   S1=R1C1  S2=R2C1  S3=R3C1  S4=R4C1   S5=R1C2 ... S13=R1C4  S16=R4C4
 * 按当前安装方向即：S1 左上、S4 左下、S13 右上、S16 右下
 */

/* ======================== 函数声明 ======================== */

/*
 * 函数名：MatrixKey_Init
 * 描述  ：初始化 4 行推挽输出（默认全高）、4 列上拉输入，并复位扫描状态
 * 输入  ：无
 * 输出  ：无
 * 备注  ：必须在 Delay_Init() 与 Timer_Init() 之后调用（消抖依赖毫秒时基）
 */
void MatrixKey_Init(void);

/*
 * 函数名：MatrixKey_GetKey
 * 描述  ：非阻塞获取按键事件（边沿触发，长按只上报一次）
 * 输入  ：无（需在主循环中反复调用）
 * 输出  ：0-无新事件；'0'~'9' 'A'~'D' '*' '#'-本次按下的键
 * 备注  ：内部自带 20ms 扫描周期与消抖，可直接放进主循环
 */
char MatrixKey_GetKey(void);

/*
 * 函数名：MatrixKey_GetRawKey
 * 描述  ：读取最近一次确认的物理键号，便于界面显示与接线核对
 * 输入  ：无
 * 输出  ：0-无键；1~16-S1~S16（模块丝印编号）
 */
uint8_t MatrixKey_GetRawKey(void);

/*
 * 函数名：MatrixKey_RawToChar
 * 描述  ：把物理键号转换成逻辑字符（按标准 4×4 布局）
 * 输入  ：raw-物理键号 1~16
 * 输出  ：对应的字符；输入非法返回 0
 */
char MatrixKey_RawToChar(uint8_t raw);

#endif /* __BSP_MATRIXKEY_H */

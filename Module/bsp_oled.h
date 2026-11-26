/*
 * 文件名：bsp_oled.h
 * 描述  ：0.96 寸 SSD1306 OLED 驱动头文件（128×64，硬件 I2C1）
 * 备注  ：基于 STM32F103 标准外设库
 *          开发板为"双 micro F103RCT6"，SCL 接 PB6（JP2.5）、SDA 接 PB7（JP2.6），
 *          两者是 I2C1 的默认引脚，无需重映射，配置为复用开漏（AF_OD）
 *          模块自带上拉电阻，SCL/SDA 无需外接；总线速率 400kHz（Fast Mode）
 *          驱动直接把字模写进 OLED 显存，不需要额外的显存缓冲和整屏刷新函数
 */

#ifndef __BSP_OLED_H
#define __BSP_OLED_H

#include "stm32f10x.h"

/* ======================== 外设与引脚宏定义 ======================== */
#define OLED_I2C                  I2C1
#define OLED_I2C_CLK              RCC_APB1Periph_I2C1
#define OLED_I2C_SPEED            400000      /* 400kHz，SSD1306 支持到 400kHz */

#define OLED_I2C_GPIO_PORT        GPIOB
#define OLED_I2C_GPIO_CLK         RCC_APB2Periph_GPIOB
#define OLED_SCL_GPIO_PIN         GPIO_Pin_6  /* PB6 = I2C1_SCL，JP2.5 */
#define OLED_SDA_GPIO_PIN         GPIO_Pin_7  /* PB7 = I2C1_SDA，JP2.6 */

/* 以下电平宏只在"总线恢复"时把引脚临时当普通 GPIO 用，正常通信由硬件 I2C 产生 */
#define OLED_SCL_H()              GPIO_SetBits(OLED_I2C_GPIO_PORT, OLED_SCL_GPIO_PIN)
#define OLED_SCL_L()              GPIO_ResetBits(OLED_I2C_GPIO_PORT, OLED_SCL_GPIO_PIN)
#define OLED_SDA_H()              GPIO_SetBits(OLED_I2C_GPIO_PORT, OLED_SDA_GPIO_PIN)
#define OLED_SDA_L()              GPIO_ResetBits(OLED_I2C_GPIO_PORT, OLED_SDA_GPIO_PIN)
#define OLED_SDA_READ()           GPIO_ReadInputDataBit(OLED_I2C_GPIO_PORT, OLED_SDA_GPIO_PIN)

/* ======================== 参数宏定义 ======================== */
#define OLED_I2C_ADDR             0x78        /* 7 位地址 0x3C 左移一位；SPL 的发送函数要的就是已左移地址 */
#define OLED_I2C_TIMEOUT          1500        /* 事件等待循环次数，防止从机掉线时死等（约 0.6ms/帧） */
#define OLED_ERR_LIMIT            20U         /* 连续 20 帧失败即判定离线，之后写操作直接返回，不再拖慢主循环 */
#define OLED_WIDTH                128
#define OLED_HEIGHT               64
#define OLED_PAGE_NUM             8           /* 64 行 = 8 页，每页 8 点高 */
#define OLED_COL_OFFSET           0           /* 图像整体偏移时改这里：偏右 2 列改为 2 */

#define OLED_LINE_NUM             4           /* 8×16 字号可显示 4 行 */
#define OLED_LINE_CHARS           16          /* 每行 16 个 ASCII 字符（16×8 = 128 像素） */

/* ======================== 函数声明 ======================== */

/*
 * 函数名：OLED_Init
 * 描述  ：初始化 OLED（I2C1 硬件外设 + 总线恢复 + SSD1306 寄存器序列 + 清屏）
 * 输入  ：无
 * 输出  ：无
 */
void OLED_Init(void);

/*
 * 函数名：OLED_Check
 * 描述  ：探测 OLED 是否在线（发地址后看是否有应答），用于排查接线问题
 * 输入  ：无（需先调用 OLED_Init 初始化引脚）
 * 输出  ：0-在线, 1-无应答（检查 PB6/PB7 接线与 3.3V 供电）
 */
uint8_t OLED_Check(void);

/*
 * 函数名：OLED_IsAlive
 * 描述  ：查询显示是否仍然可用（连续多帧无应答后判为离线）
 * 输入  ：无
 * 输出  ：1-可用, 0-已判离线（调用方可停止刷屏，避免被超时拖慢主循环）
 */
uint8_t OLED_IsAlive(void);

/*
 * 函数名：OLED_Clear
 * 描述  ：全屏清空（所有像素熄灭）
 * 输入  ：无
 * 输出  ：无
 */
void OLED_Clear(void);

/*
 * 函数名：OLED_ClearLine
 * 描述  ：清空整行（按 8×16 字号，一行占上、下两个页）
 * 输入  ：line-行号 0~3（行0 → 页0/1，行1 → 页2/3，行2 → 页4/5，行3 → 页6/7）
 * 输出  ：无
 * 备注  ：改界面文字前先清整行，可避免新旧文字长短不一留下残影
 */
void OLED_ClearLine(uint8_t line);

/*
 * 函数名：OLED_Fill
 * 描述  ：全屏填充，用于点屏自检
 * 输入  ：dat-0x00 全灭, 0xFF 全亮
 * 输出  ：无
 */
void OLED_Fill(uint8_t dat);

/*
 * 函数名：OLED_DisplayOn
 * 描述  ：打开显示（休眠唤醒）
 * 输入  ：无
 * 输出  ：无
 */
void OLED_DisplayOn(void);

/*
 * 函数名：OLED_DisplayOff
 * 描述  ：关闭显示（进入休眠，显存内容保留）
 * 输入  ：无
 * 输出  ：无
 */
void OLED_DisplayOff(void);

/*
 * 函数名：OLED_SetPos
 * 描述  ：设置光标位置（页地址 + 列地址）
 * 输入  ：x-列坐标 0~127, y-页坐标 0~7
 * 输出  ：无
 */
void OLED_SetPos(uint8_t x, uint8_t y);

/*
 * 函数名：OLED_ShowChar
 * 描述  ：显示一个 ASCII 字符（8×16 点阵，占用上下两页）
 * 输入  ：x-列坐标(0~120), y-页坐标(0~6), ch-待显示字符
 * 输出  ：无
 */
void OLED_ShowChar(uint8_t x, uint8_t y, char ch);

/*
 * 函数名：OLED_ShowString
 * 描述  ：显示英文字符串（8×16 点阵，超出右边界自动截断）
 * 输入  ：x-列坐标(0~120), y-页坐标(0~6), str-待显示字符串
 * 输出  ：无
 */
void OLED_ShowString(uint8_t x, uint8_t y, char *str);

/*
 * 函数名：OLED_ShowLine
 * 描述  ：按"行"显示英文字符串（先把该行清空，再从行首显示）
 * 输入  ：line-行号 0~3, str-待显示字符串（超出 16 字符自动截断）
 * 输出  ：无
 * 备注  ：密码锁界面共 4 行，用本函数可省去手动换算 x/y
 */
void OLED_ShowLine(uint8_t line, char *str);

/*
 * 函数名：OLED_ShowNum
 * 描述  ：显示无符号十进制数（不足 len 位前补 0）
 * 输入  ：x-列坐标, y-页坐标, num-待显示数字, len-显示位数(1~10)
 * 输出  ：无
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len);

/*
 * 函数名：OLED_ShowChinese
 * 描述  ：按索引显示字库中的汉字（16×16 点阵，占用上下两页）
 * 输入  ：x-列坐标(0~112), y-页坐标(0~6), index-汉字在 hz_index 中的序号
 * 输出  ：无
 */
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t index);

/*
 * 函数名：OLED_ShowCH
 * 描述  ：中英文混合显示，按字节流自动区分 ASCII(8宽) 与汉字(16宽)
 * 输入  ：x-列坐标, y-页坐标, chs-以 '\0' 结尾的字节流（u8 数组）
 * 输出  ：无
 */
void OLED_ShowCH(uint8_t x, uint8_t y, uint8_t *chs);

#endif /* __BSP_OLED_H */

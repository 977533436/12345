/*
 * 文件名：bsp_oled.c
 * 描述  ：0.96 寸 SSD1306 OLED 驱动源文件（128×64，硬件 I2C1）
 * 备注  ：基于 STM32F103 标准外设库
 *          SCL = PB6（I2C1_SCL，JP2.5），SDA = PB7（I2C1_SDA，JP2.6），复用开漏 AF_OD
 *          I2C 时序由 I2C1 外设硬件产生，CPU 只负责填数据，速率 400kHz
 *          字库位于 bsp_oledfont.h：zf[] 为 8×16 ASCII，hz[] 为 16×16 汉字
 *          每次写屏都是"起始 + 地址 + 控制字节 + 数据…… + 停止"的一帧，
 *          页寻址模式下列地址会自增，所以一页的多个字节可以一次传完
 */

#include "bsp_oled.h"
#include <stdio.h>
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_i2c.h"
#include "bsp_delay.h"
#include "bsp_oledfont.h"

/* ======================== 私有变量 ======================== */

static uint16_t s_ErrCnt = 0U;      /* 连续失败帧数，成功一帧就清零 */
static uint8_t  s_Dead   = 0U;      /* 1-已判定离线，后续写操作直接返回，不再等超时 */

/* SSD1306 初始化命令序列：关显示→时钟→多路复用→偏移→起始行→电荷泵→页寻址→扫描方向→对比度→开显示 */
static const uint8_t OLED_InitCmd[] =
{
    0xAE,                 /* 关显示 */
    0xD5, 0x80,           /* 时钟分频 */
    0xA8, 0x3F,           /* 多路复用比 = 64 */
    0xD3, 0x00,           /* 显示偏移 0 */
    0x40,                 /* 起始行 0 */
    0x8D, 0x14,           /* 电荷泵使能（不使能则整屏不亮） */
    0x20, 0x02,           /* 页寻址模式 */
    0xA1,                 /* 左右反置：列 127 映射到 SEG0 */
    0xC8,                 /* 上下反置：COM 扫描方向 */
    0xDA, 0x12,           /* COM 引脚硬件配置 */
    0x81, 0xCF,           /* 对比度 */
    0xD9, 0xF1,           /* 预充电周期 */
    0xDB, 0x40,           /* VCOMH 电压 */
    0xA4,                 /* 显示跟随显存内容 */
    0xA6,                 /* 正常显示（非反白） */
    0xAF                  /* 开显示 */
};

/* ======================== 硬件 I2C 底层 ======================== */

/*
 * 函数名：OLED_I2C_BusRecovery
 * 描述  ：总线恢复：复位 I2C1 并把引脚当 GPIO 手动补 9 个时钟 + 停止信号
 *          用于 MCU 复位打断了上一次传输、从机把 SDA 一直拉低的场合
 * 输入  ：无
 * 输出  ：无
 */
static void OLED_I2C_BusRecovery(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t i;

    /* 复位 I2C1 外设，清掉可能卡在 BUSY 的状态 */
    RCC_APB1PeriphResetCmd(OLED_I2C_CLK, ENABLE);
    RCC_APB1PeriphResetCmd(OLED_I2C_CLK, DISABLE);

    /* 引脚临时改为普通开漏输出 */
    GPIO_InitStructure.GPIO_Pin   = OLED_SCL_GPIO_PIN | OLED_SDA_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_I2C_GPIO_PORT, &GPIO_InitStructure);

    OLED_SDA_H();                   /* 释放数据线 */
    for (i = 0; i < 9; i++)         /* 9 个时钟足以让从机移出残留的一个字节 */
    {
        OLED_SCL_L();
        DelayUs(5);
        OLED_SCL_H();
        DelayUs(5);
    }

    /* 补一个停止信号，让从机释放总线 */
    OLED_SDA_L();
    DelayUs(5);
    OLED_SCL_H();
    DelayUs(5);
    OLED_SDA_H();
    DelayUs(5);
}

/*
 * 函数名：OLED_IIC_Init
 * 描述  ：初始化硬件 I2C1（引脚复用开漏、400kHz、7 位地址、应答使能）
 * 输入  ：无
 * 输出  ：无
 */
static void OLED_IIC_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef  I2C_InitStructure;

    /* 使能 GPIOB（APB2）与 I2C1（APB1）时钟 */
    RCC_APB2PeriphClockCmd(OLED_I2C_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(OLED_I2C_CLK, ENABLE);

    /* 先做一次总线恢复，防止上次复位留下 SDA 被拉死的状态 */
    OLED_I2C_BusRecovery();

    /* 引脚交给 I2C1：复用开漏，必须用 AF_OD，不能用推挽或普通开漏 */
    GPIO_InitStructure.GPIO_Pin   = OLED_SCL_GPIO_PIN | OLED_SDA_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_I2C_GPIO_PORT, &GPIO_InitStructure);

    /* I2C1 参数：标准 I2C 模式、7 位地址、主机不应答自身地址、速率 400kHz */
    I2C_InitStructure.I2C_Mode                = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle           = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1         = 0x00;
    I2C_InitStructure.I2C_Ack                 = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed          = OLED_I2C_SPEED;
    I2C_Init(OLED_I2C, &I2C_InitStructure);

    I2C_Cmd(OLED_I2C, ENABLE);
}

/*
 * 函数名：OLED_I2C_WaitEvent
 * 描述  ：带超时的事件等待，替代常见的 while(!I2C_CheckEvent(...)) 死等
 * 输入  ：event-待等待的 I2C 事件宏
 * 输出  ：0-事件到达, 1-超时
 */
static uint8_t OLED_I2C_WaitEvent(uint32_t event)
{
    uint32_t timeout = OLED_I2C_TIMEOUT;

    while (I2C_CheckEvent(OLED_I2C, event) != SUCCESS)
    {
        if (timeout-- == 0)
        {
            return 1;               /* 从机没应答/地址不对/总线被拉死 */
        }
    }
    return 0;
}

/*
 * 函数名：OLED_I2C_WaitBusFree
 * 描述  ：等待总线真正空闲（上一帧的 STOP 结束、BUSY 清零）再发新的 START
 *          上电瞬间/连续刷屏时若不等 BUSY，START 可能被硬件忽略，
 *          该帧数据整帧丢失 → 表现为"某一行显示不全/缺半截"
 * 输入  ：无
 * 输出  ：0-总线空闲, 1-超时（总线被从机拉死）
 */
static uint8_t OLED_I2C_WaitBusFree(void)
{
    uint32_t timeout = OLED_I2C_TIMEOUT;

    while (I2C_GetFlagStatus(OLED_I2C, I2C_FLAG_BUSY) != RESET)
    {
        if (timeout-- == 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

/*
 * 函数名：OLED_I2C_SendFrame
 * 描述  ：发送一帧（单次尝试）：起始 + 从机地址 + 控制字节 + 数据 + 停止
 * 输入  ：ctrl-控制字节（0x00 命令, 0x40 显存数据）, buf-数据缓冲, len-字节数
 * 输出  ：0-成功, 1-失败（超时）
 */
static uint8_t OLED_I2C_SendFrame(uint8_t ctrl, const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    /* ① 起始信号 */
    I2C_GenerateSTART(OLED_I2C, ENABLE);
    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) != 0)
    {
        goto i2c_error;
    }

    /* ② 发送从机地址 + 写方向 */
    I2C_Send7bitAddress(OLED_I2C, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != 0)
    {
        goto i2c_error;
    }

    /* ③ 控制字节：0x00 后续是命令，0x40 后续是显存数据 */
    I2C_SendData(OLED_I2C, ctrl);
    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING) != 0)
    {
        goto i2c_error;
    }

    /* ④ 连续发送数据，页寻址模式下列地址硬件自增 */
    for (i = 0; i < len; i++)
    {
        I2C_SendData(OLED_I2C, buf[i]);
        if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING) != 0)
        {
            goto i2c_error;
        }
    }

    /* ⑤ 等最后一个字节真正移位完成（BTF）再发停止，否则会截断最后一个字节 */
    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED) != 0)
    {
        goto i2c_error;
    }

    I2C_GenerateSTOP(OLED_I2C, ENABLE);
    return 0U;

i2c_error:
    I2C_GenerateSTOP(OLED_I2C, ENABLE);
    I2C_ClearFlag(OLED_I2C, I2C_FLAG_AF);   /* 清应答失败标志，否则下一帧发不出去 */
    return 1U;
}

/*
 * 函数名：OLED_I2C_WriteBytes
 * 描述  ：可靠发送一帧：先等总线空闲，失败自动重试一次
 * 输入  ：ctrl-控制字节, buf-数据缓冲, len-字节数
 * 输出  ：0-成功, 1-失败（重试后仍失败）
 */
static uint8_t OLED_I2C_WriteBytes(uint8_t ctrl, const uint8_t *buf, uint16_t len)
{
    uint8_t retry;

    /* 已判定离线：直接返回，避免每帧都跑满超时把主循环拖死 */
    if (s_Dead != 0U)
    {
        return 1U;
    }

    for (retry = 0U; retry < 2U; retry++)
    {
        if (OLED_I2C_WaitBusFree() != 0U)
        {
            /* 总线被从机拉死：复位 I2C 外设 + 补 9 个时钟释放总线，然后重试 */
            OLED_IIC_Init();
            continue;
        }

        if (OLED_I2C_SendFrame(ctrl, buf, len) == 0U)
        {
            s_ErrCnt = 0U;                  /* 成功一帧，失败计数清零 */
            return 0U;
        }
    }

    if (s_ErrCnt < 0xFFFFU)
    {
        s_ErrCnt++;
    }
    if (s_ErrCnt >= OLED_ERR_LIMIT)
    {
        s_Dead = 1U;                        /* 连续失败过多：判定掉线，停止后续写操作 */
    }
    return 1U;
}

/*
 * 函数名：OLED_WriteCmd
 * 描述  ：写一条命令
 * 输入  ：cmd-命令字
 * 输出  ：无
 */
static void OLED_WriteCmd(uint8_t cmd)
{
    OLED_I2C_WriteBytes(0x00, &cmd, 1);
}

/* ======================== 私有函数 ======================== */

/*
 * 函数名：OLED_FindChineseIndex
 * 描述  ：在字库汉字索引表中查找汉字序号
 * 输入  ：chs-汉字的两字节编码（GB2312）
 * 输出  ：序号 0~(HZ-1)；0xFF-字库中没有该字
 */
static uint8_t OLED_FindChineseIndex(const uint8_t *chs)
{
    uint8_t i;

    for (i = 0; hz_index[i] != 0 && hz_index[i + 1] != 0; i += 2)
    {
        if (hz_index[i] == chs[0] && hz_index[i + 1] == chs[1])
        {
            return (uint8_t)(i / 2);
        }
    }
    return 0xFF;
}

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：OLED_Init
 * 描述  ：初始化 OLED（I2C1 + SSD1306 命令序列 + 清屏）
 * 输入  ：无
 * 输出  ：无
 */
void OLED_Init(void)
{
    s_ErrCnt = 0U;                /* 重新初始化时清掉离线判定 */
    s_Dead   = 0U;

    OLED_IIC_Init();
    DelayMs(200);                 /* 上电后等待模块电源稳定 */

    /* 整条初始化序列一次发完，比逐条发送快得多 */
    OLED_I2C_WriteBytes(0x00, OLED_InitCmd, (uint16_t)sizeof(OLED_InitCmd));

    OLED_Clear();
}

/*
 * 函数名：OLED_Check
 * 描述  ：探测 OLED 是否在线（只发地址、看是否应答）
 * 输入  ：无
 * 输出  ：0-在线, 1-无应答
 */
uint8_t OLED_Check(void)
{
    if (s_Dead != 0U)             /* 已判离线，不再浪费时间探测 */
    {
        return 1;
    }

    I2C_GenerateSTART(OLED_I2C, ENABLE);
    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT) != 0)
    {
        I2C_GenerateSTOP(OLED_I2C, ENABLE);
        I2C_ClearFlag(OLED_I2C, I2C_FLAG_AF);
        return 1;
    }

    I2C_Send7bitAddress(OLED_I2C, OLED_I2C_ADDR, I2C_Direction_Transmitter);
    if (OLED_I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != 0)
    {
        I2C_GenerateSTOP(OLED_I2C, ENABLE);
        I2C_ClearFlag(OLED_I2C, I2C_FLAG_AF);
        return 1;
    }

    I2C_GenerateSTOP(OLED_I2C, ENABLE);
    return 0;
}

/*
 * 函数名：OLED_IsAlive
 * 描述  ：查询显示是否仍可用（连续多帧失败后被判为离线）
 * 输入  ：无
 * 输出  ：1-可用, 0-已判离线
 */
uint8_t OLED_IsAlive(void)
{
    return (s_Dead == 0U) ? 1U : 0U;
}

/* ======================== 显示控制 ======================== */

/*
 * 函数名：OLED_Fill
 * 描述  ：全屏填充
 * 输入  ：dat-0x00 全灭, 0xFF 全亮
 * 输出  ：无
 */
void OLED_Fill(uint8_t dat)
{
    uint8_t x, y;
    uint8_t page[OLED_WIDTH];     /* 一页 128 字节，凑齐后一次传完 */

    for (x = 0; x < OLED_WIDTH; x++)
    {
        page[x] = dat;
    }

    for (y = 0; y < OLED_PAGE_NUM; y++)
    {
        OLED_SetPos(0, y);
        OLED_I2C_WriteBytes(0x40, page, OLED_WIDTH);
    }
}

/*
 * 函数名：OLED_Clear
 * 描述  ：全屏清空
 * 输入  ：无
 * 输出  ：无
 */
void OLED_Clear(void)
{
    OLED_Fill(0x00);
}

/*
 * 函数名：OLED_ClearLine
 * 描述  ：清空整行（8×16 字号下一行占两个页，横向写满 128 列）
 * 输入  ：line-行号 0~3
 * 输出  ：无
 * 备注  ：按 0x00 写显存（不是写空格），行内所有像素都能清干净
 */
void OLED_ClearLine(uint8_t line)
{
    uint8_t page[OLED_WIDTH];
    uint8_t x;
    uint8_t y;

    if (line >= OLED_LINE_NUM)                /* 行号越界直接返回，防止写到别的行 */
    {
        return;
    }

    for (x = 0; x < OLED_WIDTH; x++)
    {
        page[x] = 0x00;
    }

    y = (uint8_t)(line * 2);                  /* 行号 → 起始页：行0=页0/1, 行1=页2/3 … */
    OLED_SetPos(0, y);
    OLED_I2C_WriteBytes(0x40, page, OLED_WIDTH);
    OLED_SetPos(0, (uint8_t)(y + 1));
    OLED_I2C_WriteBytes(0x40, page, OLED_WIDTH);
}

/*
 * 函数名：OLED_DisplayOn
 * 描述  ：打开显示
 * 输入  ：无
 * 输出  ：无
 */
void OLED_DisplayOn(void)
{
    OLED_WriteCmd(0xAF);
}

/*
 * 函数名：OLED_DisplayOff
 * 描述  ：关闭显示（省电，显存内容保留）
 * 输入  ：无
 * 输出  ：无
 */
void OLED_DisplayOff(void)
{
    OLED_WriteCmd(0xAE);
}

/*
 * 函数名：OLED_SetPos
 * 描述  ：设置光标位置（页地址 + 列地址），三条命令合并成一帧发送
 * 输入  ：x-列坐标 0~127, y-页坐标 0~7
 * 输出  ：无
 */
void OLED_SetPos(uint8_t x, uint8_t y)
{
    uint8_t col = (uint8_t)(x + OLED_COL_OFFSET);
    uint8_t cmd[3];

    cmd[0] = (uint8_t)(0xB0 + y);                        /* 页地址 */
    cmd[1] = (uint8_t)(0x00 | (col & 0x0F));             /* 列地址低 4 位 */
    cmd[2] = (uint8_t)(0x10 | ((col >> 4) & 0x0F));      /* 列地址高 4 位 */

    OLED_I2C_WriteBytes(0x00, cmd, 3);
}

/* ======================== 字符显示 ======================== */

/*
 * 函数名：OLED_ShowChar
 * 描述  ：显示一个 ASCII 字符（8×16 点阵，上下各占一页）
 * 输入  ：x-列坐标(0~120), y-页坐标(0~6), ch-待显示字符
 * 输出  ：无
 */
void OLED_ShowChar(uint8_t x, uint8_t y, char ch)
{
    uint8_t c = (uint8_t)ch;

    /* 字库只覆盖 0x20~0x7E，其余字符按空格处理，避免越界读 */
    if (c < ' ' || c > '~')
    {
        c = ' ';
    }
    c = (uint8_t)(c - ' ');

    OLED_SetPos(x, y);                                   /* 上半部分 8 行 */
    OLED_I2C_WriteBytes(0x40, &zf[c * 16], 8);

    OLED_SetPos(x, (uint8_t)(y + 1));                    /* 下半部分 8 行 */
    OLED_I2C_WriteBytes(0x40, &zf[c * 16 + 8], 8);
}

/*
 * 函数名：OLED_ShowString
 * 描述  ：显示英文字符串（8×16 点阵）
 * 输入  ：x-列坐标(0~120), y-页坐标(0~6), str-待显示字符串
 * 输出  ：无
 */
void OLED_ShowString(uint8_t x, uint8_t y, char *str)
{
    while (*str != '\0')
    {
        if ((uint8_t)(x + 8) > OLED_WIDTH)               /* 写不下就截断 */
        {
            break;
        }
        OLED_ShowChar(x, y, *str);
        x = (uint8_t)(x + 8);
        str++;
    }
}

/*
 * 函数名：OLED_ShowLine
 * 描述  ：按行显示英文字符串（先清整行，再从行首显示，天然避免残影）
 * 输入  ：line-行号 0~3, str-待显示字符串（超过 16 字符自动截断）
 * 输出  ：无
 */
void OLED_ShowLine(uint8_t line, char *str)
{
    if (line >= OLED_LINE_NUM)
    {
        return;
    }

    OLED_ClearLine(line);                                 /* 先清行，长字符串换成短字符串也不留残影 */
    OLED_ShowString(0, (uint8_t)(line * 2), str);
}

/*
 * 函数名：OLED_ShowNum
 * 描述  ：显示无符号十进制数（不足 len 位前补 0）
 * 输入  ：x-列坐标, y-页坐标, num-待显示数字, len-显示位数(1~10)
 * 输出  ：无
 */
void OLED_ShowNum(uint8_t x, uint8_t y, uint32_t num, uint8_t len)
{
    char buf[12];
    char fmt[8];

    if (len == 0 || len > 10)
    {
        return;
    }

    /* 先拼出 "%0Nu" 格式串，再 sprintf 成数字字符串 */
    sprintf(fmt, "%%0%du", (int)len);
    sprintf(buf, fmt, (unsigned int)num);
    OLED_ShowString(x, y, buf);
}

/* ======================== 汉字显示 ======================== */

/*
 * 函数名：OLED_ShowChinese
 * 描述  ：按索引显示字库中的汉字（16×16 点阵）
 * 输入  ：x-列坐标(0~112), y-页坐标(0~6), index-汉字序号（见 hz_index[]）
 * 输出  ：无
 */
void OLED_ShowChinese(uint8_t x, uint8_t y, uint8_t index)
{
    /* 序号超出字库范围则直接返回，防止读越界 */
    if ((uint32_t)index * 32 + 31 >= sizeof(hz))
    {
        return;
    }

    OLED_SetPos(x, y);                                   /* 上半部分 */
    OLED_I2C_WriteBytes(0x40, &hz[index * 32], 16);

    OLED_SetPos(x, (uint8_t)(y + 1));                    /* 下半部分 */
    OLED_I2C_WriteBytes(0x40, &hz[index * 32 + 16], 16);
}

/*
 * 函数名：OLED_ShowCH
 * 描述  ：中英文混合显示（ASCII 占 8 列，汉字占 16 列）
 * 输入  ：x-列坐标, y-页坐标, chs-以 '\0' 结尾的字节流
 * 输出  ：无
 */
void OLED_ShowCH(uint8_t x, uint8_t y, uint8_t *chs)
{
    uint8_t i = 0;
    uint8_t idx;

    while (chs[i] != 0)
    {
        if (chs[i] < 0x80)                               /* ASCII 字符 */
        {
            if ((uint8_t)(x + 8) > OLED_WIDTH)
            {
                break;
            }
            OLED_ShowChar(x, y, (char)chs[i]);
            x = (uint8_t)(x + 8);
            i++;
        }
        else                                             /* 汉字（两字节） */
        {
            if ((uint8_t)(x + 16) > OLED_WIDTH)
            {
                break;
            }
            idx = OLED_FindChineseIndex(&chs[i]);
            if (idx != 0xFF)                             /* 字库中有该字才画 */
            {
                OLED_ShowChinese(x, y, idx);
            }
            /* 字库中没有的汉字留空，但仍占 16 列，避免与右侧字符重叠 */
            x = (uint8_t)(x + 16);
            i = (uint8_t)(i + 2);
        }
    }
}

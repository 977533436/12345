/*
 * 文件名：bsp_matrixkey.c
 * 描述  ：4×4 矩阵键盘驱动源文件（HW-834，行扫描）
 * 备注  ：基于 STM32F103 标准外设库
 *          行线 R1~R4 = PA1~PA4（推挽输出），列线 C1~C4 = PA5/PA6/PA7/PC4（上拉输入）
 *          扫描方式：逐行拉低、其余行拉高，再读 4 根列线；
 *                    某列为低说明"该列 × 当前行"的按键闭合
 *          防错处理：① 10ms 扫描周期 + 2 次一致消抖（约 20ms）
 *                    ② 边沿触发，长按只上报一次
 *                    ③ 整轮扫描闭合点 ≥2 个直接丢弃（防鬼键/多键误判）
 */

#include "bsp_matrixkey.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "bsp_delay.h"
#include "bsp_timer.h"

/* ======================== 私有表 ======================== */

/* 行线引脚（都在 GPIOA，顺序对应 R1~R4） */
static const uint16_t s_RowPin[KEY_ROW_NUM] =
{
    KEY_ROW1_PIN, KEY_ROW2_PIN, KEY_ROW3_PIN, KEY_ROW4_PIN
};

/* 物理键号表：[行][列]，列序 C1~C4（C1 是最右列） */
static const uint8_t s_RawMap[KEY_ROW_NUM][KEY_COL_NUM] =
{
    /*  C1   C2   C3   C4  */
    {   1,   5,   9,  13 },     /* R1（最上行，对应丝印 S1 / S5 / S9 / S13） */
    {   2,   6,  10,  14 },     /* R2 */
    {   3,   7,  11,  15 },     /* R3 */
    {   4,   8,  12,  16 }      /* R4（最下行） */
};

/* 逻辑字符表：[行][列]，列序 C1~C4（实测安装方向下 C1 在最左列） */
static const char s_CharMap[KEY_ROW_NUM][KEY_COL_NUM] =
{
    /*  C1   C2   C3   C4  */
    {  '0', '1', '2', '3' },    /* R1（最上行） */
    {  '4', '5', '6', '7' },    /* R2 */
    {  '8', '9', 'A', 'B' },    /* R3 */
    {  'C', 'D', 'E', 'F' }     /* R4（最下行） */
};

/* ======================== 私有变量 ======================== */

static uint32_t s_LastScanTick = 0;              /* 上次扫描时刻（ms） */
static uint8_t  s_StableRaw    = KEY_RAW_NONE;   /* 已确认的稳定键值 */
static uint8_t  s_LastRaw      = KEY_RAW_NONE;   /* 最近一次确认的物理键号（对外查询） */
static uint8_t  s_SampleCnt    = 0;             /* 与稳定值不同的连续采样次数 */

/* ======================== 私有函数 ======================== */

/*
 * 函数名：MatrixKey_SelectRow
 * 描述  ：选通一行：该行输出低，其余三行输出高
 * 输入  ：row-行号 0~3
 * 输出  ：无
 */
static void MatrixKey_SelectRow(uint8_t row)
{
    uint8_t i;

    for (i = 0; i < KEY_ROW_NUM; i++)
    {
        GPIO_WriteBit(KEY_ROW_GPIO_PORT, s_RowPin[i],
                      (i == row) ? Bit_RESET : Bit_SET);
    }
}

/*
 * 函数名：MatrixKey_AllRowsHigh
 * 描述  ：全部行线输出高（扫描结束后的空闲状态，省电且不会误读）
 * 输入  ：无
 * 输出  ：无
 */
static void MatrixKey_AllRowsHigh(void)
{
    uint8_t i;

    for (i = 0; i < KEY_ROW_NUM; i++)
    {
        GPIO_WriteBit(KEY_ROW_GPIO_PORT, s_RowPin[i], Bit_SET);
    }
}

/*
 * 函数名：MatrixKey_ReadCol
 * 描述  ：读一根列线的电平
 * 输入  ：col-列号 0~3（0=C1 最右列）
 * 输出  ：1-高（未按下）, 0-低（该列与当前拉低的行闭合）
 */
static uint8_t MatrixKey_ReadCol(uint8_t col)
{
    uint8_t level = 1;

    switch (col)
    {
    case 0:
        level = GPIO_ReadInputDataBit(KEY_COL1_GPIO_PORT, KEY_COL1_PIN);
        break;
    case 1:
        level = GPIO_ReadInputDataBit(KEY_COL2_GPIO_PORT, KEY_COL2_PIN);
        break;
    case 2:
        level = GPIO_ReadInputDataBit(KEY_COL3_GPIO_PORT, KEY_COL3_PIN);
        break;
    case 3:
        level = GPIO_ReadInputDataBit(KEY_COL4_GPIO_PORT, KEY_COL4_PIN);
        break;
    default:
        break;
    }

    return (level != 0U) ? 1U : 0U;
}

/*
 * 函数名：MatrixKey_ScanOnce
 * 描述  ：完整扫描一轮（4 行），返回唯一按下的物理键号
 * 输入  ：无
 * 输出  ：0-无键或同时按下多个键；1~16-物理键号
 */
static uint8_t MatrixKey_ScanOnce(void)
{
    uint8_t row;
    uint8_t col;
    uint8_t raw = KEY_RAW_NONE;
    uint8_t cnt = 0;

    for (row = 0; row < KEY_ROW_NUM; row++)
    {
        MatrixKey_SelectRow(row);       /* 当前行拉低，其余拉高 */
        DelayUs(50);                    /* 等内部上拉把列线拉高（τ≈4us，取 50us 稳妥） */

        for (col = 0; col < KEY_COL_NUM; col++)
        {
            if (MatrixKey_ReadCol(col) == 0U)   /* 列线被拉低 = 该交点闭合 */
            {
                cnt++;
                raw = s_RawMap[row][col];
            }
        }
    }

    MatrixKey_AllRowsHigh();            /* 扫描结束，全部行回到高电平 */

    /* 只认"唯一闭合点"，两个及以上一律丢弃，避免鬼键误判成有效密码 */
    return (cnt == 1U) ? raw : KEY_RAW_NONE;
}

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：MatrixKey_Init
 * 描述  ：4 行配置推挽输出（初始全高）、4 列配置上拉输入，并复位扫描状态
 * 输入  ：无
 * 输出  ：无
 */
void MatrixKey_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    /* ① 行线、列线所在端口的时钟（GPIOA + GPIOC） */
    RCC_APB2PeriphClockCmd(KEY_ROW_GPIO_CLK, ENABLE);
    RCC_APB2PeriphClockCmd(KEY_COL_GPIO_CLK, ENABLE);

    /* ② 先让所有行输出高，避免初始化瞬间出现"低电平行"被误当成按键 */
    MatrixKey_AllRowsHigh();

    /* ③ 行线：推挽输出，50MHz */
    GPIO_InitStructure.GPIO_Pin   = KEY_ROW1_PIN | KEY_ROW2_PIN |
                                    KEY_ROW3_PIN | KEY_ROW4_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY_ROW_GPIO_PORT, &GPIO_InitStructure);

    /* ④ 列线 PA5/PA6/PA7：上拉输入（模块无上拉电阻，必须用内部上拉） */
    GPIO_InitStructure.GPIO_Pin  = KEY_COL1_PIN | KEY_COL2_PIN | KEY_COL3_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_COL1_GPIO_PORT, &GPIO_InitStructure);

    /* ⑤ 列线 PC4 单独初始化（端口不同） */
    GPIO_InitStructure.GPIO_Pin  = KEY_COL4_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(KEY_COL4_GPIO_PORT, &GPIO_InitStructure);

    /* ⑥ 复位扫描状态 */
    s_LastScanTick = 0;
    s_StableRaw    = KEY_RAW_NONE;
    s_LastRaw      = KEY_RAW_NONE;
    s_SampleCnt    = 0;
}

/* ======================== 核心功能函数 ======================== */

/*
 * 函数名：MatrixKey_GetKey
 * 描述  ：非阻塞获取按键事件（20ms 扫描 + 消抖 + 边沿触发）
 * 输入  ：无
 * 输出  ：0-无新事件；其余-本次按下的逻辑字符
 */
char MatrixKey_GetKey(void)
{
    uint32_t now;
    uint8_t  raw;

    now = Timer_GetTick();
    if ((now - s_LastScanTick) < KEY_SCAN_PERIOD_MS)   /* 未到扫描周期，直接返回 */
    {
        return 0;
    }
    s_LastScanTick = now;

    raw = MatrixKey_ScanOnce();

    /* 采样值与稳定值一致：说明没有新的按下/松开，清计数 */
    if (raw == s_StableRaw)
    {
        s_SampleCnt = 0;
        return 0;
    }

    /* 采样值翻转：连续 2 次一致才认账（消抖） */
    s_SampleCnt++;
    if (s_SampleCnt < KEY_DEBOUNCE_CNT)
    {
        return 0;
    }

    s_SampleCnt = 0;
    s_StableRaw = raw;
    s_LastRaw   = raw;

    /* 只在"从无到有"时上报；松开（raw=0）不产生事件 */
    if (raw == KEY_RAW_NONE)
    {
        return 0;
    }

    return MatrixKey_RawToChar(raw);
}

/*
 * 函数名：MatrixKey_GetRawKey
 * 描述  ：读取最近一次确认的物理键号（S1~S16），用于界面核对接线
 * 输入  ：无
 * 输出  ：0-无键；1~16-物理键号
 */
uint8_t MatrixKey_GetRawKey(void)
{
    return s_LastRaw;
}

/*
 * 函数名：MatrixKey_RawToChar
 * 描述  ：物理键号 → 逻辑字符
 * 输入  ：raw-物理键号 1~16
 * 输出  ：对应的字符；非法输入返回 0
 * 备注  ：S1 → C1/R1，S2 → C1/R2，…，S13 → C4/R1，S16 → C4/R4
 */
char MatrixKey_RawToChar(uint8_t raw)
{
    uint8_t row;
    uint8_t col;

    if ((raw < 1U) || (raw > 16U))
    {
        return 0;
    }

    row = (uint8_t)((raw - 1U) % KEY_ROW_NUM);   /* S 编号按列递增：每 4 个换一行 */
    col = (uint8_t)((raw - 1U) / KEY_ROW_NUM);

    return s_CharMap[row][col];
}

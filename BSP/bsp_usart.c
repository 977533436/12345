/*
 * 文件名：bsp_usart.c
 * 描述  ：USART 串口驱动源文件（USART1 中断接收 + 阻塞发送）
 * 备注  ：基于 STM32F103 标准外设库
 *          接收采用 RXNE + IDLE 双中断：字节由 RXNE 存入缓冲，
 *          发送方停止发送后由 IDLE（总线空闲）判定一帧结束，
 *          因此上位机不需要"发送新行"功能；带 '\r'/'\n' 的上位机同样兼容
 */

#include "bsp_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "misc.h"
#include <stdarg.h>
#include <stdio.h>

/* ======================== 私有变量 ======================== */
static volatile uint8_t s_RxBuf[USART_RX_BUF_SIZE];   /* 行缓冲：存放当前正在收的一行 */
static volatile uint8_t s_RxLen       = 0;            /* 当前行已收字符数 */
static volatile uint8_t s_RxLineLen   = 0;            /* 完整行的字符数 */
static volatile uint8_t s_RxLineReady = 0;            /* 1 = 收到完整一行，等待主循环取走 */
static volatile uint8_t s_RxOverflow  = 0;            /* 1 = 发生过行超长溢出 */
static volatile uint8_t s_RxDiscard   = 0;            /* 1 = 当前行已溢出，丢弃到行结束符 */

/* ======================== 私有函数 ======================== */

/*
 * 函数名：Usart1_NVIC_Config
 * 描述  ：配置 USART1 中断优先级
 * 输入  ：无
 * 输出  ：无
 */
static void Usart1_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    /* 优先级分组：2 位抢占 + 2 位子优先级（若后续增加中断源，请在 main 中统一设置） */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/* ======================== 初始化函数 ======================== */

/*
 * 函数名：Usart1_Init
 * 描述  ：初始化 USART1（115200-8-N-1、无流控、收发使能）并开启 RXNE 中断
 * 输入  ：baud-波特率
 * 输出  ：无
 */
void Usart1_Init(unsigned int baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    /* 先清空接收缓冲，避免上电残留 */
    Usart_RxReset();

    /* 使能 GPIOA 与 USART1 时钟 */
    RCC_APB2PeriphClockCmd(USART1_GPIO_CLK | USART1_CLK, ENABLE);

    /* PA9 = USART1_TX，复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = USART1_TX_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(USART1_GPIO_PORT, &GPIO_InitStructure);

    /* PA10 = USART1_RX，浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = USART1_RX_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(USART1_GPIO_PORT, &GPIO_InitStructure);

    /* 115200-8-N-1，无硬件流控，收发均使能 */
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 使能接收非空中断 + 总线空闲中断，并配置 NVIC
     * RXNE：每收到 1 个字节触发一次，用于取数入缓冲
     * IDLE：发送方停发后触发一次，用于判定"一帧结束"（无需回车换行） */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
    Usart1_NVIC_Config();

    /* 最后启动串口 */
    USART_Cmd(USART1, ENABLE);
}

/*
 * 函数名：Usart_Init
 * 描述  ：以默认波特率（115200）初始化 USART1
 * 输入  ：无
 * 输出  ：无
 */
void Usart_Init(void)
{
    Usart1_Init(USART_DEFAULT_BAUD);
}

/* ======================== 发送函数 ======================== */

/*
 * 函数名：Usart_SendByte
 * 描述  ：发送一个字节，并等待发送完成
 * 输入  ：USARTx-串口组, ch-待发送字节
 * 输出  ：无
 */
void Usart_SendByte(USART_TypeDef *USARTx, uint8_t ch)
{
    USART_SendData(USARTx, ch);

    /* TC 由写 DR 清零，整帧发送完成后置位；等 TC 可保证字节不被覆盖 */
    while (USART_GetFlagStatus(USARTx, USART_FLAG_TC) == RESET)
    {
    }
}

/*
 * 函数名：Usart_SendString
 * 描述  ：发送以 '\0' 结尾的字符串
 * 输入  ：USARTx-串口组, str-待发送字符串
 * 输出  ：无
 */
void Usart_SendString(USART_TypeDef *USARTx, const char *str)
{
    if (str == 0)
    {
        return;
    }

    while (*str != '\0')
    {
        Usart_SendByte(USARTx, (uint8_t)(*str));
        str++;
    }
}

/*
 * 函数名：UsartPrintf
 * 描述  ：格式化发送（不依赖 printf 重定向）
 * 输入  ：USARTx-串口组, fmt-格式串, ...-不定参数
 * 输出  ：无
 */
void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...)
{
    char buf[128];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    Usart_SendString(USARTx, buf);
}

/* ======================== 中断接收与帧缓冲 ======================== */

/*
 * 函数名：Usart_FrameDone
 * 描述  ：结束当前帧：长度非 0 时置"收到一帧"标志，然后清空当前帧缓冲
 * 输入  ：无
 * 输出  ：无
 */
static void Usart_FrameDone(void)
{
    if (s_RxLen > 0 && s_RxLineReady == 0)
    {
        s_RxBuf[s_RxLen] = '\0';
        s_RxLineLen      = s_RxLen;
        s_RxLineReady    = 1;
    }

    s_RxLen = 0;
}

/*
 * 函数名：USART1_IRQHandler
 * 描述  ：USART1 中断服务程序，只负责取字节与断帧
 * 输入  ：无
 * 输出  ：无
 * 备注  ：中断内不做解析、不调用延时。
 *          断帧方式一：IDLE（总线空闲）中断——发送方停发即认为一帧结束，
 *                      上位机不需要"发送新行"功能；
 *          断帧方式二：兼容收到 '\r' 或 '\n' 的场合（上位机能自动追加换行时）。
 */
void USART1_IRQHandler(void)
{
    uint8_t ch;

    /* ---- 1、接收非空中断：取走一个字节 ---- */
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        /* 读 DR 取走数据，同时清除 RXNE 标志（只清标志不读 DR 会导致 ORE 丢数据） */
        ch = (uint8_t)USART_ReceiveData(USART1);

        if (s_RxLineReady)
        {
            /* 上一帧还没被主循环取走：丢弃新数据，避免覆盖正在被读取的缓冲 */
        }
        else if (s_RxDiscard)
        {
            /* 本帧已溢出：全部丢弃，等 IDLE 断帧后重新开始 */
        }
        else if (ch == '\r' || ch == '\n')
        {
            /* 上位机会自动追加换行时，直接结束本帧（该字节不入缓冲） */
            Usart_FrameDone();
        }
        else if (s_RxLen < (USART_RX_BUF_SIZE - 1))
        {
            s_RxBuf[s_RxLen] = ch;
            s_RxLen++;
        }
        else
        {
            /* 超过 63 字节仍未结束：丢弃本帧并置溢出标志 */
            s_RxLen      = 0;
            s_RxOverflow = 1;
            s_RxDiscard  = 1;
        }
    }

    /* ---- 2、总线空闲中断：发送方已停发，本帧接收结束 ---- */
    if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
    {
        /* 先读 SR（上面已读）再读 DR，才能清除 IDLE 标志 */
        (void)USART_ReceiveData(USART1);

        if (s_RxDiscard)
        {
            s_RxDiscard = 0;        /* 丢弃结束，后续数据重新接收 */
        }
        else
        {
            Usart_FrameDone();
        }
    }
}

/*
 * 函数名：Usart_RxLineReady
 * 描述  ：查询是否收到完整一帧（总线空闲断帧，或收到 '\r'/'\n'）
 * 输入  ：无
 * 输出  ：1-有新帧待取走, 0-无
 */
uint8_t Usart_RxLineReady(void)
{
    return s_RxLineReady;
}

/*
 * 函数名：Usart_GetRxLine
 * 描述  ：取走一帧数据（含 '\0'），取走后清除"收到一帧"标志
 * 输入  ：buf-输出缓冲, size-缓冲大小（含 '\0'）
 * 输出  ：本帧字符数；无数据或参数非法返回 0
 */
uint8_t Usart_GetRxLine(char *buf, uint8_t size)
{
    uint8_t i;
    uint8_t len;

    if (buf == 0 || size == 0)
    {
        return 0;
    }

    if (s_RxLineReady == 0)
    {
        return 0;
    }

    len = s_RxLineLen;
    if (len > (uint8_t)(size - 1))
    {
        len = (uint8_t)(size - 1);
    }

    for (i = 0; i < len; i++)
    {
        buf[i] = (char)s_RxBuf[i];
    }
    buf[len] = '\0';

    s_RxLineReady = 0;
    s_RxLineLen   = 0;

    return len;
}

/*
 * 函数名：Usart_RxOverflow
 * 描述  ：查询是否发生过"帧超长溢出"，读取后自动清零
 * 输入  ：无
 * 输出  ：1-发生过溢出, 0-未发生
 */
uint8_t Usart_RxOverflow(void)
{
    uint8_t flag = s_RxOverflow;

    s_RxOverflow = 0;

    return flag;
}

/*
 * 函数名：Usart_RxReset
 * 描述  ：清空接收行缓冲与所有接收标志
 * 输入  ：无
 * 输出  ：无
 */
void Usart_RxReset(void)
{
    s_RxLen       = 0;
    s_RxLineLen   = 0;
    s_RxLineReady = 0;
    s_RxOverflow  = 0;
    s_RxDiscard   = 0;
}

/*********************************************END OF FILE**********************/

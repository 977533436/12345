/*
 * 文件名：bsp_flashpwd.c
 * 描述  ：片内 Flash 密码存储驱动源文件
 * 备注  ：使用最后一页 0x0803F800（2KB/页），存 6 个半字：
 *          Magic(2) + 长度&pwd0 + pwd1&pwd2 + pwd3&0 + 校验和
 *          上电读到 RAM；写入按"解锁→擦页→写半字→回读校验→上锁"执行
 */

#include "bsp_flashpwd.h"
#include "stm32f10x_flash.h"

/* ======================== 私有宏 ======================== */
#define FLASH_PWD_HALF_NUM        6U            /* 占用的半字数（12 字节） */
#define FLASH_PWD_SUM_NUM         5U            /* 参与校验和的半字数 */

/* ======================== 私有变量 ======================== */
static const uint8_t s_DefaultPwd[FLASH_PWD_LEN] = { 1U, 2U, 3U, 4U };  /* 默认密码 1234 */
static uint8_t s_Pwd[FLASH_PWD_LEN] = { 1U, 2U, 3U, 4U };              /* RAM 缓存 */
static uint8_t s_Stored = 0U;                                          /* 1-Flash 中有有效密码 */

/* ======================== 私有函数 ======================== */

/*
 * 函数名：FlashPwd_ReadPage
 * 描述  ：读取密码页并校验，成功时把 4 字节密码拷到 pwd
 * 输入  ：pwd-输出缓冲（至少 4 字节）
 * 输出  ：1-数据有效, 0-无效（未写过 / 被擦除 / 校验失败）
 */
static uint8_t FlashPwd_ReadPage(uint8_t *pwd)
{
    const volatile uint16_t *p = (const volatile uint16_t *)FLASH_PWD_PAGE_ADDR;
    uint16_t sum = 0U;
    uint8_t i;

    /* ① Magic 校验：不是本系统写的就判无效 */
    if ((p[0] != (uint16_t)(FLASH_PWD_MAGIC & 0xFFFFU)) ||
        (p[1] != (uint16_t)(FLASH_PWD_MAGIC >> 16)))
    {
        return 0U;
    }

    /* ② 长度校验 */
    if ((uint8_t)(p[2] >> 8) != FLASH_PWD_LEN)
    {
        return 0U;
    }

    /* ③ 校验和校验 */
    for (i = 0U; i < FLASH_PWD_SUM_NUM; i++)
    {
        sum = (uint16_t)(sum + p[i]);
    }
    if (sum != p[FLASH_PWD_SUM_NUM])
    {
        return 0U;
    }

    /* ④ 重组 4 字节密码 */
    pwd[0] = (uint8_t)(p[2] & 0xFFU);
    pwd[1] = (uint8_t)(p[3] >> 8);
    pwd[2] = (uint8_t)(p[3] & 0xFFU);
    pwd[3] = (uint8_t)(p[4] >> 8);

    return 1U;
}

/* ======================== 对外接口 ======================== */

/*
 * 函数名：FlashPwd_Init
 * 描述  ：上电从 Flash 读取密码到 RAM；数据无效时使用默认密码 1234
 * 输入  ：无
 * 输出  ：无
 */
void FlashPwd_Init(void)
{
    uint8_t tmp[FLASH_PWD_LEN];
    uint8_t i;

    if (FlashPwd_ReadPage(tmp) != 0U)
    {
        for (i = 0U; i < FLASH_PWD_LEN; i++)
        {
            s_Pwd[i] = tmp[i];
        }
        s_Stored = 1U;
    }
    else
    {
        for (i = 0U; i < FLASH_PWD_LEN; i++)
        {
            s_Pwd[i] = s_DefaultPwd[i];      /* 空片 / 校验失败 → 默认 1234 */
        }
        s_Stored = 0U;
    }
}

/*
 * 函数名：FlashPwd_IsStored
 * 描述  ：查询 Flash 中是否有有效密码
 * 输入  ：无
 * 输出  ：1-有效, 0-无效
 */
uint8_t FlashPwd_IsStored(void)
{
    return s_Stored;
}

/*
 * 函数名：FlashPwd_Get
 * 描述  ：取当前密码（RAM 缓存）
 * 输入  ：pwd-输出缓冲
 * 输出  ：无
 */
void FlashPwd_Get(uint8_t *pwd)
{
    uint8_t i;

    if (pwd == 0)
    {
        return;
    }

    for (i = 0U; i < FLASH_PWD_LEN; i++)
    {
        pwd[i] = s_Pwd[i];
    }
}

/*
 * 函数名：FlashPwd_Set
 * 描述  ：写入新密码：擦除该页 → 逐半字写入 → 回读校验 → 更新 RAM 缓存
 * 输入  ：pwd-4 字节新密码
 * 输出  ：FLASH_PWD_OK / ERR_ERASE / ERR_WRITE / ERR_CHECK
 */
uint8_t FlashPwd_Set(const uint8_t *pwd)
{
    uint16_t buf[FLASH_PWD_HALF_NUM];
    uint16_t sum;
    uint32_t addr;
    uint8_t  tmp[FLASH_PWD_LEN];
    uint8_t  i;
    FLASH_Status status;

    if (pwd == 0)
    {
        return FLASH_PWD_ERR_WRITE;
    }

    /* ① 组装 6 个半字（半字 = 2 字节，STM32F1 只支持半字/字编程） */
    buf[0] = (uint16_t)(FLASH_PWD_MAGIC & 0xFFFFU);
    buf[1] = (uint16_t)(FLASH_PWD_MAGIC >> 16);
    buf[2] = (uint16_t)(((uint16_t)FLASH_PWD_LEN << 8) | (uint16_t)pwd[0]);
    buf[3] = (uint16_t)(((uint16_t)pwd[1] << 8) | (uint16_t)pwd[2]);
    buf[4] = (uint16_t)((uint16_t)pwd[3] << 8);

    /* ② 计算校验和（前 5 个半字累加） */
    sum = 0U;
    for (i = 0U; i < FLASH_PWD_SUM_NUM; i++)
    {
        sum = (uint16_t)(sum + buf[i]);
    }
    buf[5] = sum;

    /* ③ 擦除整页（写入前必须先擦，Flash 只能 1→0） */
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    status = FLASH_ErasePage(FLASH_PWD_PAGE_ADDR);
    if (status != FLASH_COMPLETE)
    {
        FLASH_Lock();
        return FLASH_PWD_ERR_ERASE;
    }

    /* ④ 逐半字写入 */
    for (i = 0U; i < FLASH_PWD_HALF_NUM; i++)
    {
        addr   = FLASH_PWD_PAGE_ADDR + (uint32_t)i * 2U;
        status = FLASH_ProgramHalfWord(addr, buf[i]);
        if (status != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return FLASH_PWD_ERR_WRITE;
        }
    }

    FLASH_Lock();

    /* ⑤ 回读校验：结构有效且 4 字节与写入一致才算成功 */
    if (FlashPwd_ReadPage(tmp) == 0U)
    {
        return FLASH_PWD_ERR_CHECK;
    }
    for (i = 0U; i < FLASH_PWD_LEN; i++)
    {
        if (tmp[i] != pwd[i])
        {
            return FLASH_PWD_ERR_CHECK;
        }
    }

    /* ⑥ 更新 RAM 缓存 */
    for (i = 0U; i < FLASH_PWD_LEN; i++)
    {
        s_Pwd[i] = tmp[i];
    }
    s_Stored = 1U;

    return FLASH_PWD_OK;
}

/*
 * 函数名：FlashPwd_Erase
 * 描述  ：擦除密码页，恢复默认密码 1234
 * 输入  ：无
 * 输出  ：FLASH_PWD_OK / FLASH_PWD_ERR_ERASE
 */
uint8_t FlashPwd_Erase(void)
{
    FLASH_Status status;
    uint8_t i;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    status = FLASH_ErasePage(FLASH_PWD_PAGE_ADDR);

    FLASH_Lock();

    if (status != FLASH_COMPLETE)
    {
        return FLASH_PWD_ERR_ERASE;
    }

    for (i = 0U; i < FLASH_PWD_LEN; i++)
    {
        s_Pwd[i] = s_DefaultPwd[i];          /* 擦除后回落到默认密码 */
    }
    s_Stored = 0U;

    return FLASH_PWD_OK;
}

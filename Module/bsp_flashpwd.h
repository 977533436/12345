/*
 * 文件名：bsp_flashpwd.h
 * 描述  ：片内 Flash 密码存储驱动头文件
 * 备注  ：基于 STM32F103 标准外设库
 *          STM32F103RCT6：256KB Flash（0x08000000~0x0803FFFF），大容量产品每页 2KB，
 *          使用最后一页 0x0803F800 存放 4 位密码，不占用程序区。
 *
 *          存储格式（6 个半字 = 12 字节）：
 *            半字0/1 : Magic 0x4C4F434B（"LOCK"）
 *            半字2   : 长度(高字节) | pwd[0](低字节)
 *            半字3   : pwd[1](高字节) | pwd[2](低字节)
 *            半字4   : pwd[3](高字节) | 0x00
 *            半字5   : 校验和（半字0~4 的 16 位累加）
 *
 *          上电调用 FlashPwd_Init() 从 Flash 读到 RAM 缓存：
 *          Magic/长度/校验和任一不匹配即判定"未存过密码"，回落到默认密码 1234。
 *          写入只在改密成功时发生一次，避免频繁擦写（寿命约 1 万次）。
 */

#ifndef __BSP_FLASHPWD_H
#define __BSP_FLASHPWD_H

#include "stm32f10x.h"

/* ======================== 参数宏定义 ======================== */
#define FLASH_PWD_LEN             4U                    /* 密码位数（固定 4 位） */
#define FLASH_PWD_PAGE_ADDR       0x0803F800U           /* 最后一页起始地址（2KB/页） */
#define FLASH_PWD_MAGIC           0x4C4F434BU           /* 标志字 "LOCK" */

/* 返回码 */
#define FLASH_PWD_OK              0U                    /* 成功 */
#define FLASH_PWD_ERR_ERASE       1U                    /* 擦除失败 */
#define FLASH_PWD_ERR_WRITE       2U                    /* 写入失败 */
#define FLASH_PWD_ERR_CHECK       3U                    /* 回读校验失败 */

/* ======================== 函数声明 ======================== */

/*
 * 函数名：FlashPwd_Init
 * 描述  ：从 Flash 最后一页读取密码到 RAM 缓存；数据无效时使用默认密码 1234
 * 输入  ：无
 * 输出  ：无
 * 备注  ：上电初始化调用一次即可，之后业务层直接用 FlashPwd_Get() 取缓存，不再读 Flash
 */
void FlashPwd_Init(void);

/*
 * 函数名：FlashPwd_IsStored
 * 描述  ：查询 Flash 中是否存有有效密码（区分"用户改过的密码"与"默认密码"）
 * 输入  ：无
 * 输出  ：1-Flash 中有效（用户设过）, 0-无效（使用默认 1234）
 */
uint8_t FlashPwd_IsStored(void);

/*
 * 函数名：FlashPwd_Get
 * 描述  ：取当前密码（RAM 缓存）
 * 输入  ：pwd-输出缓冲，长度至少 FLASH_PWD_LEN 字节
 * 输出  ：无
 */
void FlashPwd_Get(uint8_t *pwd);

/*
 * 函数名：FlashPwd_Set
 * 描述  ：把新密码写入 Flash（擦除该页 → 写入 → 回读校验），成功后更新 RAM 缓存
 * 输入  ：pwd-4 字节新密码
 * 输出  ：FLASH_PWD_OK-成功；1/2/3-擦除/写入/校验失败
 * 备注  ：单次写入耗时约 20~40ms（2KB 页擦除），期间不可断电；
 *          只在密码修改成功时调用一次
 */
uint8_t FlashPwd_Set(const uint8_t *pwd);

/*
 * 函数名：FlashPwd_Erase
 * 描述  ：擦除密码页（恢复出厂：之后读密码回落到默认 1234）
 * 输入  ：无
 * 输出  ：FLASH_PWD_OK-成功；FLASH_PWD_ERR_ERASE-失败
 */
uint8_t FlashPwd_Erase(void);

#endif /* __BSP_FLASHPWD_H */

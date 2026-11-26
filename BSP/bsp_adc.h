#ifndef _BSP_ADC_H
#define _BSP_ADC_H
#include "stm32f10x.h"


/* 说明：ADC 采集所用的 IO 口（PB1）没有被其它外设复用，采集电压不受影响 */

/****************** ADC1 采样通道及引脚定义 ******************/
#define    ADC_APBxClock_FUN             RCC_APB2PeriphClockCmd
#define    ADC_CLK                       RCC_APB2Periph_ADC1

#define    ADC_GPIO_APBxClock_FUN        RCC_APB2PeriphClockCmd
#define    ADC_GPIO_CLK                  RCC_APB2Periph_GPIOB
#define    ADC_PORT                      GPIOB

/* 采样引脚：PB1（STM32F103RCT6 的 PB1 复用功能为 ADC12_IN9）
 * 注意：通道号必须与引脚对应，换引脚时 ADC_CHANNEL1 要同步修改
 *       PB0 -> ADC_Channel_8    PB1 -> ADC_Channel_9
 */

/* 转换通道数量 */
#define    NOFCHANEL										 1


#define    ADC_PIN1                      GPIO_Pin_1
#define    ADC_CHANNEL1                  ADC_Channel_9



/* ADC1 对应 DMA1 通道1，ADC3 对应 DMA2 通道5，ADC2 没有 DMA 功能 */
#define    ADC_x                         ADC1
#define    ADC_DMA_CHANNEL               DMA1_Channel1
#define    ADC_DMA_CLK                   RCC_AHBPeriph_DMA1


/************************** 函数声明 ************************/
void               ADCx_Init                               (void);


#endif /* _BSP_ADC_H */

#include "bsp_delay.h"


unsigned char UsCount = 0;
unsigned short MsCount = 0;

/* 
 --------------------------------------------------------------------------------------------------------- 
 * 	 函 数 名: Delay_Init 
 * 	 功能说明: systick初始化 
 * 	 参    数：无 
 * 	 返 回 值: 无 
 --------------------------------------------------------------------------------------------------------- 
 */
void Delay_Init(void)
{

	SysTick->CTRL &= ~(1 << 2);
	
	UsCount = 9;
	
	MsCount = UsCount * 1000;

}

/* 
 --------------------------------------------------------------------------------------------------------- 
 * 	 函 数 名: DelayUs 
 * 	 功能说明: 微秒级延时 
 * 	 参    数：us：延时的时长 
 * 	 返 回 值: 无 
 --------------------------------------------------------------------------------------------------------- 
 */
void DelayUs(unsigned short us)
{

	unsigned int ctrlResult = 0;
	
	us &= 0x00FFFFFF;
	
	SysTick->LOAD = us * UsCount;
	SysTick->VAL = 0;
	SysTick->CTRL = 1;
	
	do
	{
		ctrlResult = SysTick->CTRL;
	}
	while((ctrlResult & 0x01) && !(ctrlResult & (1 << 16)));
	
	SysTick->CTRL = 0;
	SysTick->VAL = 0;

}

/* 
 --------------------------------------------------------------------------------------------------------- 
 * 	 函 数 名: DelayMs 
 * 	 功能说明: 毫秒级延时，支持任意时长延时
 * 	 参    数：ms：延时的时长 
 * 	 返 回 值: 无 
 --------------------------------------------------------------------------------------------------------- 
*/
void DelayMs(unsigned short ms)
{

	unsigned int ctrlResult = 0;
	unsigned short repeat = 0;
	unsigned short remain = 0;

	if(ms == 0)
		return;

	repeat = ms / 500;
	remain = ms % 500;

	while(repeat)
	{
		SysTick->LOAD = 500 * MsCount;
		SysTick->VAL = 0;
		SysTick->CTRL = 1;

		do
		{
			ctrlResult = SysTick->CTRL;
		}
		while((ctrlResult & 0x01) && !(ctrlResult & (1 << 16)));

		SysTick->CTRL = 0;
		SysTick->VAL = 0;

		repeat--;
	}

	if(remain)
	{
		SysTick->LOAD = remain * MsCount;
		SysTick->VAL = 0;
		SysTick->CTRL = 1;

		do
		{
			ctrlResult = SysTick->CTRL;
		}
		while((ctrlResult & 0x01) && !(ctrlResult & (1 << 16)));

		SysTick->CTRL = 0;
		SysTick->VAL = 0;
	}

}

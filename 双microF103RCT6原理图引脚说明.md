# 双 micro F103RCT6 开发板 原理图引脚说明

> 来源：`双microF103RCT6原理图.pdf`（A3，1 页，日期 2024-03-18）
> 主控：U1 = STM32F103RCT6（LQFP64，256KB Flash / 48KB RAM）
> 本文档按原理图实际连线整理，供写驱动、查接线用。

---

## 1. 电源与时钟框图

```
USB J6 / J7  ──5V──┬── AMS1117-3V3 (V1) ──3V3──┬── MCU VDD_1~4 / VDDA
                   │  C6 104 / C7 106 输入      │  C4 106 输出
                   ├── JP3.29 / JP3.31 (5V 输出) │  C13~C16 104 去耦
                   └── 排针 5V 供电              ├── CH340C Vcc
                                                 ├── 25Q16 VCC /WP /HOLD
                                                 └── 各上拉电阻、JP1/JP2/JP3/JP5
HSE：X1 8MHz   + C8/C9 20pF      → OSC_IN(5) / OSC_OUT(6)      → 系统 72MHz
LSE：X2 32.768kHz + C10/C11 20pF → PC14(3) / PC15(4)
```

* 电源指示：3V3 → R2 1K → LED1 → GND（常亮）。
* **注意：VBAT(1 脚) 只连到网络 `VB`，最终只引到 JP1 的第 2 脚，原理图上并未与 3V3 相连**。要用 RTC/备份域需外部把 JP1.2 接到 3V3 或电池。

---

## 2. STM32F103RCT6（U1）全部 64 脚连接

| 脚号 | 引脚 | 网络名 | 连接去向 / 说明 |
|---:|---|---|---|
| 1 | VBAT | VB | 仅到 JP1.2，**未接 3V3** |
| 2 | PC13 | PC13 | JP1.1（TAMPER-RTC，驱动能力弱） |
| 3 | PC14 | OSC32_IN | X2 32.768kHz + C11 20pF |
| 4 | PC15 | OSC32_OUT | X2 32.768kHz + C10 20pF |
| 5 | OSC_IN | OSC_IN | X1 8MHz + C8 20pF |
| 6 | OSC_OUT | OSC_OUT | X1 8MHz + C9 20pF |
| 7 | NRST | RESET | SW1 复位键对地、R12 1K 上拉、C12 10uF、D1 一键下载 |
| 8 | PC0 | PC0 | 25Q16 SCLK；JP1.6 |
| 9 | PC1 | PC1 | 25Q16 SI；JP1.5 |
| 10 | PC2 | PC2 | 25Q16 SO；JP1.8 |
| 11 | PC3 | PC3 | 25Q16 /CS；JP1.7 |
| 12 | VSSA | GND | 模拟地 |
| 13 | VDDA | 3V3 | 模拟电源 |
| 14 | PA0-WKUP | WK_UP | SW2 按键（按下接 3V3，高有效）；JP1.10 |
| 15 | PA1 | PA1 | JP1.9 |
| 16 | PA2 | PA2 | **USART2_TX**；JP1.12 |
| 17 | PA3 | PA3 | **USART2_RX**；JP1.11 |
| 18 | VSS_4 | GND | |
| 19 | VDD_4 | 3V3 | |
| 20 | PA4 | PA4 | JP1.14 |
| 21 | PA5 | PA5 | JP1.13 |
| 22 | PA6 | PA6 | JP1.16 |
| 23 | PA7 | PA7 | JP1.15 |
| 24 | PC4 | PC4 | JP1.18 |
| 25 | PC5 | PC5 | JP1.17 |
| 26 | PB0 | PB0 | JP1.20（ADC12_IN8） |
| 27 | PB1 | PB1 | JP1.19（**ADC12_IN9**，BSP 的 ADC 采样脚） |
| 28 | PB2/BOOT1 | BOOT1 | R14 10K 下拉；经 R3 1K 到 JP3.4 |
| 29 | PB10 | PB10 | **USART3_TX**；JP1.22 |
| 30 | PB11 | PB11 | **USART3_RX**；JP1.21 |
| 31 | VSS_1 | GND | |
| 32 | VDD_1 | 3V3 | |
| 33 | PB12 | PB12 | JP1.24 |
| 34 | PB13 | PB13 | JP1.23 |
| 35 | PB14 | PB14 | JP1.26 |
| 36 | PB15 | PB15 | JP1.25 |
| 37 | PC6 | PC6 | JP1.28 |
| 38 | PC7 | PC7 | JP1.27 |
| 39 | PC8 | PC8 | **SW4 按键**（按下接 GND）；JP3.27 |
| 40 | PC9 | PC9 | **SW3 按键**（按下接 GND）；JP3.25 |
| 41 | PA8 | PA8 | LED2：3V3 → R11 1K → LED2 → PA8（**低电平点亮**）；JP3.26 |
| 42 | PA9 | PA9 | **USART1_TX → CH340C 的 RXD(3 脚)** |
| 43 | PA10 | PA10 | **USART1_RX ← CH340C 的 TXD(2 脚)** |
| 44 | PA11 | USB_D- | 经 R10 22R → J7 的 D-；JP3.21 |
| 45 | PA12 | USB_D+ | 经 R9 22R → J7 的 D+（R8 1K 上拉 3V3）；JP3.22 |
| 46 | PA13 | SWDIO | JP5.3（SWD 下载口）；JP3.19 |
| 47 | VSS_2 | GND | |
| 48 | VDD_2 | 3V3 | |
| 49 | PA14 | SWCLK | JP5.4（SWD 下载口）；JP3.20 |
| 50 | PA15 | PA15 | JP3.17（JTDI，当 GPIO 需关闭 JTAG） |
| 51 | PC10 | PC10 | UART4_TX；JP3.18 |
| 52 | PC11 | PC11 | UART4_RX；JP3.15 |
| 53 | PC12 | PC12 | UART5_TX；JP3.16 |
| 54 | PD2 | PD2 | UART5_RX；JP3.13 |
| 55 | PB3 | PB3 | JP3.14（JTDO，当 GPIO 需关闭 JTAG） |
| 56 | PB4 | PB4 | JP2.3 LCD（NJTRST，当 GPIO 需关闭 JTAG） |
| 57 | PB5 | PB5 | JP2.4 LCD；JP3.12 |
| 58 | PB6 | PB6 | JP2.5 LCD；JP3.9 |
| 59 | PB7 | PB7 | JP2.6 LCD；JP3.10 |
| 60 | BOOT0 | BOOT0 | R13 100K 下拉；Q2 一键下载控制；经 R4 10K 到 JP3.3 |
| 61 | PB8 | PB8 | JP2.7 LCD；JP3.7 |
| 62 | PB9 | PB9 | JP2.8 LCD；JP3.8 |
| 63 | VSS_3 | GND | |
| 64 | VDD_3 | 3V3 | |

> 引脚表中"JPx.y"表示该网络引到扩展排针 JPx 的第 y 脚。

---

## 3. 扩展排针（重点）

### 3.1 JP1 —— 2×16（32P）

| 脚 | 网络 | 脚 | 网络 | 脚 | 网络 | 脚 | 网络 |
|---:|---|---:|---|---:|---|---:|---|
| 1 | PC13 | 2 | VB(VBAT) | 3 | OSC32_IN(PC14) | 4 | OSC32_OUT(PC15) |
| 5 | PC1 | 6 | PC0 | 7 | PC3 | 8 | PC2 |
| 9 | PA1 | 10 | WK_UP(PA0) | 11 | PA3 | 12 | PA2 |
| 13 | PA5 | 14 | PA4 | 15 | PA7 | 16 | PA6 |
| 17 | PC5 | 18 | PC4 | 19 | PB1 | 20 | PB0 |
| 21 | PB11 | 22 | PB10 | 23 | PB13 | 24 | PB12 |
| 25 | PB15 | 26 | PB14 | 27 | PC7 | 28 | PC6 |
| 29 | 3V3 | 30 | GND | 31 | 3V3 | 32 | GND |

### 3.2 JP3 —— 2×16（32P）

| 脚 | 网络 | 脚 | 网络 | 脚 | 网络 | 脚 | 网络 |
|---:|---|---:|---|---:|---|---:|---|
| 1 | 3V3 | 2 | 3V3 | 3 | BOOT0（经 R4 10K） | 4 | BOOT1（经 R3 1K） |
| 5 | GND | 6 | GND | 7 | PB8 | 8 | PB9 |
| 9 | PB6 | 10 | PB7 | 11 | PB4 | 12 | PB5 |
| 13 | PD2 | 14 | PB3 | 15 | PC11 | 16 | PC12 |
| 17 | PA15 | 18 | PC10 | 19 | SWDIO(PA13) | 20 | SWCLK(PA14) |
| 21 | USB_D-(PA11) | 22 | USB_D+(PA12) | 23 | PA9 | 24 | PA10 |
| 25 | PC9 | 26 | PA8 | 27 | PC8 | 28 | GND |
| 29 | 5V | 30 | GND | 31 | 5V | 32 | GND |

### 3.3 JP2 —— 1×8 "LCD" 接口

| 脚 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| 网络 | GND | 3V3 | PB4 | PB5 | PB6 | PB7 | PB8 | PB9 |

### 3.4 JP5 —— 1×4 SWD 下载口

| 脚 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| 网络 | 3V3 | GND | SWDIO(PA13) | SWCLK(PA14) |

---

## 4. 功能电路明细

### 4.1 复位

| 元件 | 连接 |
|---|---|
| R12 1K | 3V3 → RESET（上拉） |
| SW1 | RESET → GND（按下复位） |
| C12 106(10uF) | RESET → GND |
| D1 1N4148 | 一键下载电路 → RESET（只能把 RESET 拉低） |

### 4.2 BOOT / 一键下载（CH340C + Q1/Q2）

| 元件 | 连接 |
|---|---|
| R13 100K | BOOT0 → GND（默认从 Flash 启动） |
| R14 10K | BOOT1(PB2) → GND |
| CH340C RTS#(14) | → R5 1K → Q1(SS8050) 基极；Q1 集电极 → D1 → RESET |
| CH340C DTR#(13) | → R6 330 → Q2(SS8550, PNP) 基极；Q2 发射极 → 3V3 |
| Q2 集电极 | → R7 1K → BOOT0 |

> 即：串口工具勾选 DTR/RTS 即可自动进入 ISP 下载，无需手动拨 BOOT0。

### 4.3 USB 转串口 CH340C（IC1）

| 脚 | 名称 | 连接 |
|---:|---|---|
| 1 | GND | GND |
| 2 | TXD | **PA10（MCU 的 USART1_RX）** |
| 3 | RXD | **PA9（MCU 的 USART1_TX）** |
| 4 | V3 | C3 0.1uF → GND |
| 5 | UD+ | CH340_D+ → J6.D+ |
| 6 | UD- | CH340_D- → J6.D- |
| 7/8 | XI/XO | 悬空（CH340C 内部时钟） |
| 16 | Vcc | 3V3 |

### 4.4 USB 接口

| 接口 | 脚 | 连接 |
|---|---|---|
| J6（供电+串口） | 1 +5V → 5V；2 D- → CH340_D-；3 D+ → CH340_D+；4 ID 悬空；5 GND | 用于下载/串口/供电 |
| J7（MCU 原生 USB） | 1 +5V → 5V；2 D- → UD-；3 D+ → UD+；4 ID 悬空；5 GND | UD- → R10 22R → USB_D-(PA11)；UD+ → R9 22R → USB_D+(PA12)；UD+ 另有 R8 1K 上拉到 3V3 |

### 4.5 SPI Flash 25Q16（U3，软件 SPI）

| 25Q16 脚 | 功能 | 连接 |
|---:|---|---|
| 1 | /CS | PC3 |
| 2 | SO | PC2 |
| 3 | /WP | 3V3 |
| 4 | VSS | GND |
| 5 | SI | PC1 |
| 6 | SCLK | PC0 |
| 7 | /HOLD | 3V3 |
| 8 | VCC | 3V3 |

### 4.6 按键与 LED

| 器件 | 连接 | 有效电平 |
|---|---|---|
| SW1 | RESET → GND | 低有效（复位） |
| SW2 | 3V3 → WK_UP(PA0) | **高有效**（按下为高） |
| SW3 | GND → PC9 | 低有效（按下为低） |
| SW4 | GND → PC8 | 低有效（按下为低） |
| LED1 | 3V3 → R2 1K → LED1 → GND | 电源指示，常亮 |
| LED2 | 3V3 → R11 1K → LED2 → PA8 | **低电平点亮** |

> 板上**没有** PB12/PB13/PB14 按键，这三个脚只引到 JP1（24/23/26 脚）。

---

## 5. 与本工程（USART 实验）的对应关系

| BSP 代码 | 原理图实际 | 结论 |
|---|---|---|
| `bsp_usart.c` USART1：PA9/PA10 | 接 CH340C，即板载 USB 串口 | ✅ 可直接做 printf/调试口，115200-8-N-1 |
| `bsp_usart.c` USART2：PA2/PA3 | JP1.12 / JP1.11 | ✅ 可接外部模块 |
| `bsp_adc.h`：PB1 + ADC_Channel_9 | PB1 = ADC12_IN9（JP1.19） | ✅ 通道号正确 |
| `bsp_key.h`：PB12/PB13/PB14 | 板上按键是 PA0 / PC9 / PC8 | ⚠️ **引脚需改**，且三者电平极性不同（PA0 高有效，PC8/PC9 低有效） |
| `bsp_timer.h`：TIM2 | 无冲突 | ✅ |
| `bsp_delay`：SysTick，假设 HCLK=72MHz | X1=8MHz，SYSCLK=72MHz | ✅ |

### 使用前必看的 5 个坑

1. **USART1（PA9/PA10）已接 CH340**，是板载调试串口，不要把它再接外部设备。
2. **PB3 / PB4 / PA15 / PA13 / PA14 是 JTAG/SWD 复用脚**：JP2 的 LCD 口用了 PB4，JP3 引出 PB3、PB4、PA15。要把它们当普通 GPIO，必须先关 JTAG（保留 SWD）：
   `RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE); GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);`
   关闭后 PA13/PA14 仍可用于 SWD 下载。
3. **PC0~PC3 被 25Q16 Flash 占用**（PC0=SCLK, PC1=SI, PC2=SO, PC3=/CS），JP1 也把这 4 个脚引了出来，复用时注意冲突。
4. **VBAT 没接 3V3**，只引到 JP1.2，RTC/备份寄存器默认不保电。
5. **PA8 上的 LED2 是低电平点亮**（3V3→1K→LED→PA8），PC13 驱动能力弱，不适合点灯。

---

## 6. 元件清单（原理图内）

| 位号 | 型号/参数 | 作用 |
|---|---|---|
| U1 | STM32F103RCT6 | 主控 |
| IC1 | CH340C | USB 转串口 |
| U3 | 25Q16 | SPI Flash（PC0~PC3） |
| V1 | AMS1117-3V3 | 5V→3V3 |
| Q1 / Q2 | SS8050 / SS8550 | 一键下载电平转换 |
| D1 | 1N4148 | RESET 隔离 |
| X1 / X2 | 8MHz / 32.768kHz | 主时钟 / RTC 时钟 |
| R1/R2/R3/R4/R11/R12 | 10K·1K·1K·10K·1K·1K | 上拉、限流 |
| R5/R6/R7 | 1K / 330 / 1K | 三极管基极、BOOT0 限流 |
| R8/R9/R10 | 1K / 22R / 22R | USB 上拉与串阻 |
| R13/R14 | 100K / 10K | BOOT0 / BOOT1 下拉 |
| C3 | 0.1uF | CH340 V3 退耦 |
| C4/C7 | 106 | 3V3 输出滤波 |
| C6 | 104 | 5V 输入滤波 |
| C8/C9/C10/C11 | 20pF | 晶振负载电容 |
| C12 | 106 | 复位滤波 |
| C13~C16 | 104 | 3V3 去耦 |
| JP1/JP3 | 2×16 | 扩展排针 |
| JP2 | 1×8 | LCD 接口 |
| JP5 | 1×4 | SWD 下载口 |
| J6/J7 | USB-5P（Micro-USB） | 串口/供电、MCU USB |
| SW1~SW4 | 轻触按键 | 复位、WK_UP、PC9、PC8 |
| LED1/LED2 | 电源灯 / PA8 灯 | 指示 |

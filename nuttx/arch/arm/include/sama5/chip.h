/************************************************************************************
 * arch/arm/include/samad5/chip.h
 *
 *   Copyright (C) 2013 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ************************************************************************************/

#ifndef __ARCH_ARM_INCLUDE_SAMA5_CHIP_H
#define __ARCH_ARM_INCLUDE_SAMA5_CHIP_H

/************************************************************************************
 * Included Files
 ************************************************************************************/

#include <nuttx/config.h>

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/
/* SAMA5D3 Family
 * 
 *                           ATSAMA5D31    ATSAMA5D33    ATSAMA5D34    ATSAMA5D35
 * ------------------------- ------------- ------------- ------------- -------------
 * Pin Count                 324           324           324           324
 * Max. Operating Frequency  536           536           536           536
 * CPU                       Cortex-A5     Cortex-A5     Cortex-A5     Cortex-A5
 * Max I/O Pins              160           160           160           160
 * Ext Interrupts            160           160           160           160
 * USB Transceiver           3             3             3             3
 * USB Speed                 Hi-Speed      Hi-Speed      Hi-Speed      Hi-Speed
 * USB Interface             Host, Device  Host, Device  Host, Device  Host, Device
 * SPI                       6             6             6             6
 * TWI (I2C)                 3             3             3             3
 * UART                      7             5             5             7
 * CAN                       -             -             2             2
 * LIN                       4             4             4             4
 * SSC                       2             2             2             2
 * Ethernet                  1             1             1             2
 * SD / eMMC                 3             2             3             3
 * Graphic LCD               Yes           Yes           Yes           -
 * Camera Interface          Yes           Yes           Yes           Yes
 * ADC channels              12            12            12            12
 * ADC Resolution (bits)     12            12            12            12
 * ADC Speed (ksps)          440           440           440           440
 * Resistive Touch Screen    Yes           Yes           Yes           Yes
 * Crypto Engine             AES/DES/      AES/DES/      AES/DES/      AES/DES/
 *                           SHA/TRNG      SHA/TRNG      SHA/TRNG      SHA/TRNG
 * SRAM (Kbytes)             128           128           128           128
 * External Bus Interface    1             1             1             1
 * DRAM Memory               DDR2/LPDDR,   DDR2/LPDDR,   DDR2/LPDDR,   DDR2/LPDDR,
 *                           SDRAM/LPSDR   SDRAM/LPSDR   DDR2/LPDDR,   DDR2/LPDDR,
 * NAND Interface            Yes           Yes           Yes           Yes
 * Temp. Range (deg C)       -40 to 85     -40 to 85     -40 to 85     -40 to 85
 * I/O Supply Class          1.8/3.3       1.8/3.3       1.8/3.3       1.8/3.3
 * Operating Voltage (Vcc)   1.08 to 1.32  1.08 to 1.32  1.08 to 1.32  1.08 to 1.32
 * FPU                       Yes           Yes           Yes           Yes
 * MPU / MMU                 No/Yes        No/Yes        No/Yes        No/Yes
 * Timers                    5             5             5             6
 * Output Compare channels   6             6             6             6
 * Input Capture Channels    6             6             6             6
 * PWM Channels              4             4             4             4
 * 32kHz RTC                 Yes           Yes           Yes           Yes
 * Packages                  LFBGA324_A    LFBGA324_A    LFBGA324_A    LFBGA324_A
 */

#if defined(CONFIG_ARCH_CHIP_ATSAMA5D31)
#  define ATSAMA5D3        1         /* SAMA5D3 family */
#  define SAM_ISRAM0_SIZE (64*1024)  /* 128KB of SRAM in two banks */
#  define SAM_ISRAM1_SIZE (64*1024)
#  define SAM_NDMAC        2         /* (2) DMA controllers */
#  define SAM_NDMACHAN     8         /* (8) DMA channels per DMA controller */
#elif defined(CONFIG_ARCH_CHIP_ATSAMA5D33)
#  define ATSAMA5D3        1         /* SAMA5D3 family */
#  define SAM_ISRAM0_SIZE (64*1024)  /* 128KB of SRAM in two banks */
#  define SAM_ISRAM1_SIZE (64*1024)
#  define SAM_NDMAC        2         /* (2) DMA controllers */
#  define SAM_NDMACHAN     8         /* (8) DMA channels per DMA controller */
#elif defined(CONFIG_ARCH_CHIP_ATSAMA5D34)
#  define ATSAMA5D3        1         /* SAMA5D3 family */
#  define SAM_ISRAM0_SIZE (64*1024)  /* 128KB of SRAM in two banks */
#  define SAM_ISRAM1_SIZE (64*1024)
#  define SAM_NDMAC        2         /* (2) DMA controllers */
#  define SAM_NDMACHAN     8         /* (8) DMA channels per DMA controller */
#elif defined(CONFIG_ARCH_CHIP_ATSAMA5D35)
#  define ATSAMA5D3        1         /* SAMA5D3 family */
#  define SAM_ISRAM0_SIZE (64*1024)  /* 128KB of SRAM in two banks */
#  define SAM_ISRAM1_SIZE (64*1024)
#  define SAM_NDMAC        2         /* (2) DMA controllers */
#  define SAM_NDMACHAN     8         /* (8) DMA channels per DMA controller */
#else
#  error Unrecognized SAMAD5 chip
#endif

/************************************************************************************
 * Public Types
 ************************************************************************************/

/************************************************************************************
 * Public Data
 ************************************************************************************/

/************************************************************************************
 * Public Functions
 ************************************************************************************/

#endif /* __ARCH_ARM_INCLUDE_SAMA5_CHIP_H */

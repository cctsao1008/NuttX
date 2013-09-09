/************************************************************************************
 * arch/arm/src/sama5/chip/sam_sfr.h
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

#ifndef __ARCH_ARM_SRC_SAMA5_CHIP_SAM_SFR_H
#define __ARCH_ARM_SRC_SAMA5_CHIP_SAM_SFR_H

/************************************************************************************
 * Included Files
 ************************************************************************************/

#include <nuttx/config.h>
#include "chip/sam_memorymap.h"

/************************************************************************************
 * Pre-processor Definitions
 ************************************************************************************/
/* SFR Register Offsets *************************************************************/

                                           /* 0x0000-0x000c: Reserved */
#define SAM_SFR_DDRCFG_OFFSET       0x0004 /* DDR Configuration register (undocumented) */
                                           /* 0x0000-0x000c: Reserved */
#define SAM_SFR_OHCIICR_OFFSET      0x0010 /* OHCI Interrupt Configuration Register */
#define SAM_SFR_OHCIISR_OFFSET      0x0014 /* OHCI Interrupt Status Register */
                                           /* 0x0018-0x001c: Reserved */
#define SAM_SFR_SECURE_OFFSET       0x0028 /* Security Configuration Register */
                                           /* 0x002c: Reserved */
#define SAM_SFR_UTMICKTRIM_OFFSET   0x0030 /* UTMI Clock Trimming Register */
#define SAM_SFR_EBICFG_OFFSET       0x0040 /* EBI Configuration Register */
                                           /* 0x0044-0x3ffc: Reserved */

/* SFR Register Addresses ***********************************************************/

#define SAM_SFR_DDRCFG              (SAM_SFR_VBASE+SAM_SFR_DDRCFG_OFFSET) /* REVISIT */
#define SAM_SFR_OHCIICR             (SAM_SFR_VBASE+SAM_SFR_OHCIICR_OFFSET)
#define SAM_SFR_OHCIISR             (SAM_SFR_VBASE+SAM_SFR_OHCIISR_OFFSET)
#define SAM_SFR_SECURE              (SAM_SFR_VBASE+SAM_SFR_SECURE_OFFSET)
#define SAM_SFR_UTMICKTRIM          (SAM_SFR_VBASE+SAM_SFR_UTMICKTRIM_OFFSET)
#define SAM_SFR_EBICFG              (SAM_SFR_VBASE+SAM_SFR_EBICFG_OFFSET)

/* SFR Register Bit Definitions *****************************************************/

/* DDR Configuration register (undocumented, REVISIT) */

#define SFR_DDRCFG_DRQON            (3 << 16) /* Force DDR_DQ and DDR_DQS input buffer always on */

/* OHCI Interrupt Configuration Register */

#define SFR_OHCIICR_RES(n)          (1 << (n)) /* Bit 0:  USB port n reset, n=0..2 */
#  define SFR_OHCIICR_RES0          (1 << 0)  /* Bit 0:  USB port 0 reset */
#  define SFR_OHCIICR_RES1          (1 << 1)  /* Bit 1:  USB port 1 reset */
#  define SFR_OHCIICR_RES2          (1 << 2)  /* Bit 2:  USB port 2 reset */
#define SFR_OHCIICR_ARIE            (1 << 4)  /* Bit 4:  OHCI asynchronous resume interrupt enable */
#define SFR_OHCIICR_APPSTART        (0)       /* Bit 5:  Reserved, must write 0 */
#define SFR_OHCIICR_UDPPUDIS        (1 << 23) /* Bit 23: USB device pull-up disable */

/* OHCI Interrupt Status Register */

#define SFR_OHCIISR_RIS0            (1 << 0)  /* Bit 0:  USB port 0 resume detected */
#define SFR_OHCIISR_RIS1            (1 << 1)  /* Bit 1:  USB port 1 resume detected */
#define SFR_OHCIISR_RIS2            (1 << 2)  /* Bit 2:  USB port 2 resume detected */

/* Security Configuration Register */

#define SFR_SECURE_ROM              (1 << 0)  /* Bit 0:  Disable Access to ROM Code */
#define SFR_SECURE_FUSE             (1 << 8)  /* Bit 8:  Disable Access to Fuse Controller */

/* UTMI Clock Trimming Register */

#define SFR_UTMICKTRIM_FREQ_SHIFT   (0)       /* Bits 0-1: UTMI Reference Clock Frequency */
#define SFR_UTMICKTRIM_FREQ_MASK    (3 << SFR_UTMICKTRIM_FREQ_SHIFT)
#  define SFR_UTMICKTRIM_FREQ_12MHZ (0 << SFR_UTMICKTRIM_FREQ_SHIFT) /* 12 MHz reference clock */
#  define SFR_UTMICKTRIM_FREQ_16MHZ (1 << SFR_UTMICKTRIM_FREQ_SHIFT) /* 16 MHz reference clock */
#  define SFR_UTMICKTRIM_FREQ_24MHZ (2 << SFR_UTMICKTRIM_FREQ_SHIFT) /* 24 MHz reference clock */
#  define SFR_UTMICKTRIM_FREQ_48MHZ (3 << SFR_UTMICKTRIM_FREQ_SHIFT) /* 48 MHz reference clock */

/* EBI Configuration Register */

#define SFR_EBICFG_DRIVE_LOW        (0)       /* LOW Low drive level */
#define SFR_EBICFG_DRIVE_MEDIUM     (2)       /* MEDIUM Medium drive level */
#define SFR_EBICFG_DRIVE_HIGH       (3)       /* HIGH High drive level */

#define SFR_EBICFG_PULL_UP          (0)       /* Pull-up */
#define SFR_EBICFG_PULL_NONE        (1)       /* No Pull */
#define SFR_EBICFG_PULL_DOWN        (3)       /* Pull-down */

#define SFR_EBICFG_DRIVE0_SHIFT     (0)       /* Bits 0-1: EBI Pins Drive Level */
#define SFR_EBICFG_DRIVE0_MASK      (3 << SFR_EBICFG_DRIVE0_SHIFT)
#  define SFR_EBICFG_DRIVE0(n)      ((n) << SFR_EBICFG_DRIVE0_SHIFT)
#define SFR_EBICFG_PULL0_SHIFT      (2)       /* Bits 2-3: EBI Pins Pull Value */
#define SFR_EBICFG_PULL0_MASK       (3 << SFR_EBICFG_PULL0_SHIFT)
#  define SFR_EBICFG_PULL0(n)       ((n) << SFR_EBICFG_PULL0_SHIFT)
#define SFR_EBICFG_SCH0             (1 << 4)  /* Bit 4:  EBI Pins Schmitt Trigger */
#define SFR_EBICFG_DRIVE1_SHIFT     (8)       /* Bits 8-9: EBI Pins Drive Level */
#define SFR_EBICFG_DRIVE1_MASK      (3 << SFR_EBICFG_DRIVE1_SHIFT)
#  define SFR_EBICFG_DRIVE1(n)      ((n) << SFR_EBICFG_DRIVE1_SHIFT)
#define SFR_EBICFG_PULL1_SHIFT      (10)      /* Bits 10-11: EBI Pins Pull Value */
#define SFR_EBICFG_PULL1_MASK       (3 << SFR_EBICFG_PULL1_SHIFT)
#  define SFR_EBICFG_PULL1(n)       ((n) << SFR_EBICFG_PULL1_SHIFT)
#define SFR_EBICFG_SCH1             (1 << 12) /* Bit 12: EBI Pins Schmitt Trigger */
#define SFR_EBICFG_BMS              (1 << 16) /* Bit 16:  BMS Sampled Value (Read Only) */

#endif /* __ARCH_ARM_SRC_SAMA5_CHIP_SAM_SFR_H */

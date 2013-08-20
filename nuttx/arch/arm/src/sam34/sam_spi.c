/****************************************************************************
 * arch/arm/src/sam34/sam_spi.c
 *
 *   Copyright (C) 2011, 2013 Gregory Nutt. All rights reserved.
 *   Authors: Gregory Nutt <gnutt@nuttx.org>
 *            Diego Sanchez <dsanchez@nx-engineering.com>
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
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <semaphore.h>
#include <errno.h>
#include <debug.h>

#include <arch/board/board.h>
#include <nuttx/arch.h>
#include <nuttx/spi/spi.h>

#include "up_internal.h"
#include "up_arch.h"

#include "chip.h"
#include "sam_gpio.h"
#include "sam_spi.h"
#include "sam_periphclks.h"
#include "chip/sam3u_pmc.h"
#include "chip/sam_spi.h"
#include "chip/sam_pinmap.h"

#if defined(CONFIG_SAM34_SPI0) || defined(CONFIG_SAM34_SPI1)

/****************************************************************************
 * Definitions
 ****************************************************************************/
/* Configuration ************************************************************/
/* Select MCU-specific settings
 *
 * For the SAM3U, SAM3A, and SAM3X SPI is driven by the main clock.
 * For the SAM4L, SPI is driven by CLK_SPI which is the PBB clock.
 */

#if defined(CONFIG_ARCH_CHIP_SAM3U) || defined(CONFIG_ARCH_CHIP_SAM3A) || \
    defined(CONFIG_ARCH_CHIP_SAM3X)
#  define SAM_SPI_CLOCK  BOARD_MCK_FREQUENCY  /* Frequency of the main clock */
#elif defined(CONFIG_ARCH_CHIP_SAM4L)
#  define SAM_SPI_CLOCK  BOARD_PBB_FREQUENCY  /* PBB frequency */
#else
#  error Unrecognized SAM architecture
#endif

#ifdef CONFIG_SAM34_SPI1
  /* NOTE: See arch/arm/sama5/sam_spi.c.  That is the same SPI IP and that
   * version on the driver has been extended to support both SPI0 and SPI1
   */

#  error Support for SPI1 has not yet been implemented (see NOTE)
#endif

/* Debug *******************************************************************/
/* Check if SPI debut is enabled (non-standard.. no support in
 * include/debug.h
 */

#ifndef CONFIG_DEBUG
#  undef CONFIG_DEBUG_VERBOSE
#  undef CONFIG_DEBUG_SPI
#endif

#ifdef CONFIG_DEBUG_SPI
#  define spidbg lldbg
#  ifdef CONFIG_DEBUG_VERBOSE
#    define spivdbg lldbg
#  else
#    define spivdbg(x...)
#  endif
#else
#  define spidbg(x...)
#  define spivdbg(x...)
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* The state of the one chip select */

struct sam_spidev_s
{
  struct spi_dev_s spidev;     /* Externally visible part of the SPI interface */
#ifndef CONFIG_SPI_OWNBUS
  uint32_t         frequency;  /* Requested clock frequency */
  uint32_t         actual;     /* Actual clock frequency */
  uint8_t          nbits;      /* Width of word in bits (8 to 16) */
  uint8_t          mode;       /* Mode 0,1,2,3 */
#endif
  uint8_t          cs;         /* Chip select number */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Helpers */

#if defined(CONFIG_DEBUG_SPI) && defined(CONFIG_DEBUG_VERBOSE)
static void spi_dumpregs(FAR const char *msg);
#else
# define spi_dumpregs(msg)
#endif

static inline void spi_flush(void);
static inline uint32_t spi_cs2pcs(FAR struct sam_spidev_s *priv);

/* SPI methods */

#ifndef CONFIG_SPI_OWNBUS
static int      spi_lock(FAR struct spi_dev_s *dev, bool lock);
#endif
static void     spi_select(FAR struct spi_dev_s *dev, enum spi_dev_e devid,
                  bool selected);
static uint32_t spi_setfrequency(FAR struct spi_dev_s *dev,
                  uint32_t frequency);
static void     spi_setmode(FAR struct spi_dev_s *dev,
                  enum spi_mode_e mode);
static void     spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint16_t spi_send(FAR struct spi_dev_s *dev, uint16_t ch);
static void     spi_exchange(FAR struct spi_dev_s *dev,
                   FAR const void *txbuffer, FAR void *rxbuffer, size_t nwords);
#ifndef CONFIG_SPI_EXCHANGE
static void     spi_sndblock(FAR struct spi_dev_s *dev, FAR const void *buffer, size_t nwords);
static void     spi_recvblock(FAR struct spi_dev_s *dev, FAR void *buffer, size_t nwords);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* SPI driver operations */

static const struct spi_ops_s g_spiops =
{
#ifndef CONFIG_SPI_OWNBUS
  .lock              = spi_lock,
#endif
  .select            = spi_select,
  .setfrequency      = spi_setfrequency,
  .setmode           = spi_setmode,
  .setbits           = spi_setbits,
  .status            = sam_spistatus,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata           = sam_spicmddata,
#endif
  .send              = spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange          = spi_exchange,
#else
  .sndblock          = spi_sndblock,
  .recvblock         = spi_recvblock,
#endif
  .registercallback  = 0,                 /* Not implemented */
};

#ifdef CONFIG_SPI_OWNBUS
/* Single chip select device structure */

static struct sam_spidev_s g_spidev;

#else
/* Held while chip is selected for mutual exclusion */

static sem_t g_spisem;
static bool g_spinitialized = false;
#endif

/* This array maps chip select numbers (0-3) to CSR register addresses */

static const uint32_t g_csraddr[4] =
{
  SAM_SPI0_CSR0, SAM_SPI0_CSR1, SAM_SPI0_CSR2, SAM_SPI0_CSR3
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: spi_dumpregs
 *
 * Description:
 *   Dump the contents of all SPI registers
 *
 * Input Parameters:
 *   msg - Message to print before the register data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#if defined(CONFIG_DEBUG_SPI) && defined(CONFIG_DEBUG_VERBOSE)
static void spi_dumpregs(FAR const char *msg)
{
  spivdbg("%s:\n", msg);
  spivdbg("    MR:%08x   SR:%08x  IMR:%08x\n",
          getreg32(SAM_SPI0_MR), getreg32(SAM_SPI0_SR),
          getreg32(SAM_SPI0_IMR));
  spivdbg("  CSR0:%08x CSR1:%08x CSR2:%08x CSR3:%08x\n",
          getreg32(SAM_SPI0_CSR0), getreg32(SAM_SPI0_CSR1),
          getreg32(SAM_SPI0_CSR2), getreg32(SAM_SPI0_CSR3));
  spivdbg("  WPCR:%08x WPSR:%08x\n",
          getreg32(SAM_SPI0_WPCR), getreg32(SAM_SPI0_WPSR));
}
#endif

/****************************************************************************
 * Name: spi_flush
 *
 * Description:
 *   Make sure that there are now dangling SPI transfer in progress
 *
 * Input Parameters:
 *   priv - Device-specific state data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void spi_flush(void)
{
  /* Make sure the no TX activity is in progress... waiting if necessary */

  while ((getreg32(SAM_SPI0_SR) & SPI_INT_TXEMPTY) == 0);

  /* Then make sure that there is no pending RX data .. reading as
   * discarding as necessary.
   */

  while ((getreg32(SAM_SPI0_SR) & SPI_INT_RDRF) != 0)
    {
       (void)getreg32(SAM_SPI0_RDR);
    }
}

/****************************************************************************
 * Name: spi_cs2pcs
 *
 * Description:
 *   Map the chip select number to the bit-set PCS field used in the SPI
 *   registers.  A chip select number is used for indexing and identifying
 *   chip selects.  However, the chip select information is represented by
 *   a bit set in the SPI regsisters.  This function maps those chip select
 *   numbers to the correct bit set:
 *
 *    CS  Returned   Spec    Effective
 *    No.   PCS      Value    NPCS
 *   ---- --------  -------- --------
 *    0    0000      xxx0     1110
 *    1    0001      xx01     1101
 *    2    0011      x011     1011
 *    3    0111      0111     0111
 *
 * Input Parameters:
 *   priv - Device-specific state data
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline uint32_t spi_cs2pcs(FAR struct sam_spidev_s *priv)
{
  return ((uint32_t)1 << (priv->cs)) - 1;
}

/****************************************************************************
 * Name: spi_lock
 *
 * Description:
 *   On SPI busses where there are multiple devices, it will be necessary to
 *   lock SPI to have exclusive access to the busses for a sequence of
 *   transfers.  The bus should be locked before the chip is selected. After
 *   locking the SPI bus, the caller should then also call the setfrequency,
 *   setbits, and setmode methods to make sure that the SPI is properly
 *   configured for the device.  If the SPI buss is being shared, then it
 *   may have been left in an incompatible state.
 *
 * Input Parameters:
 *   dev  - Device-specific state data
 *   lock - true: Lock spi bus, false: unlock SPI bus
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifndef CONFIG_SPI_OWNBUS
static int spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  spivdbg("lock=%d\n", lock);
  if (lock)
    {
      /* Take the semaphore (perhaps waiting) */

      while (sem_wait(&g_spisem) != 0)
        {
          /* The only case that an error should occur here is if the wait was awakened
           * by a signal.
           */

          ASSERT(errno == EINTR);
        }
    }
  else
    {
      (void)sem_post(&g_spisem);
    }

  return OK;
}
#endif

/****************************************************************************
 * Name: spi_select
 *
 * Description:
 *   This function does not actually set the chip select line.  Rather, it
 *   simply maps the device ID into a chip select number and retains that
 *   chip select number for later use.
 *
 * Input Parameters:
 *   dev -       Device-specific state data
 *   frequency - The SPI frequency requested
 *
 * Returned Value:
 *   Returns the actual frequency selected
 *
 ****************************************************************************/

 static void spi_select(FAR struct spi_dev_s *dev, enum spi_dev_e devid,
                        bool selected)
 {
  FAR struct sam_spidev_s *priv = (FAR struct sam_spidev_s *)dev;
  uint32_t regval;

  /* Are we selecting or de-selecting the device? */

  spivdbg("selected=%d\n", selected);
  if (selected)
    {
      spivdbg("cs=%d\n", priv->cs);

      /* Before writing the TDR, the PCS field in the SPI_MR register must be set
       * in order to select a slave.
       */

      regval  = getreg32(SAM_SPI0_MR);
      regval &= ~SPI_MR_PCS_MASK;
      regval |= (spi_cs2pcs(priv) << SPI_MR_PCS_SHIFT);
      putreg32(regval, SAM_SPI0_MR);
    }

  /* Perform any board-specific chip select operations. PIO chip select
   * pins may be programmed by the board specific logic in one of two
   * different ways.  First, the pins may be programmed as SPI peripherals.
   * In that case, the pins are completely controlled by the SPI driver.
   * This sam_spiselect method still needs to be provided, but it may
   * be only a stub.
   *
   * An alternative way to program the PIO chip select pins is as normal
   * GPIO outputs.  In that case, the automatic control of the CS pins is
   * bypassed and this function must provide control of the chip select.
   * NOTE:  In this case, the GPIO output pin does *not* have to be the
   * same as the NPCS pin normal associated with the chip select number.
   */

  sam_spiselect(devid, selected);
}

/****************************************************************************
 * Name: spi_setfrequency
 *
 * Description:
 *   Set the SPI frequency.
 *
 * Input Parameters:
 *   dev -       Device-specific state data
 *   frequency - The SPI frequency requested
 *
 * Returned Value:
 *   Returns the actual frequency selected
 *
 ****************************************************************************/

static uint32_t spi_setfrequency(FAR struct spi_dev_s *dev, uint32_t frequency)
{
  FAR struct sam_spidev_s *priv = (FAR struct sam_spidev_s *)dev;
  uint32_t actual;
  uint32_t scbr;
  uint32_t dlybs;
  uint32_t dlybct;
  uint32_t regval;
  uint32_t regaddr;

  spivdbg("cs=%d frequency=%d\n", priv->cs, frequency);

  /* Check if the requested frequency is the same as the frequency selection */

#ifndef CONFIG_SPI_OWNBUS
  if (priv->frequency == frequency)
    {
      /* We are already at this frequency.  Return the actual. */

      return priv->actual;
    }
#endif

  /* Configure SPI to a frequency as close as possible to the requested frequency.
   *
   *   SPCK frequency = SPI_CLK / SCBR, or SCBR = SPI_CLK / frequency
   */

  scbr = SAM_SPI_CLOCK / frequency;

  if (scbr < 8)
    {
      scbr = 8;
    }
  else if (scbr > 254)
    {
      scbr = 254;
    }

  scbr = (scbr + 1) & ~1;

  /* Save the new scbr value */

  regaddr = g_csraddr[priv->cs];
  regval  = getreg32(regaddr);
  regval &= ~(SPI_CSR_SCBR_MASK | SPI_CSR_DLYBS_MASK | SPI_CSR_DLYBCT_MASK);
  regval |= scbr << SPI_CSR_SCBR_SHIFT;

  /* DLYBS: Delay Before SPCK.  This field defines the delay from NPCS valid to the
   * first valid SPCK transition. When DLYBS equals zero, the NPCS valid to SPCK
   * transition is 1/2 the SPCK clock period. Otherwise, the following equations
   * determine the delay:
   *
   *   Delay Before SPCK = DLYBS / SPI_CLK
   *
   * For a 2uS delay
   *
   *   DLYBS = SPI_CLK * 0.000002 = SPI_CLK / 500000
   */

  dlybs   = SAM_SPI_CLOCK / 500000;
  regval |= dlybs << SPI_CSR_DLYBS_SHIFT;

  /* DLYBCT: Delay Between Consecutive Transfers.  This field defines the delay
   * between two consecutive transfers with the same peripheral without removing
   * the chip select. The delay is always inserted after each transfer and
   * before removing the chip select if needed.
   *
   *  Delay Between Consecutive Transfers = (32 x DLYBCT) / SPI_CLK
   *
   * For a 5uS delay:
   *
   *  DLYBCT = SPI_CLK * 0.000005 / 32 = SPI_CLK / 200000 / 32
   */

  dlybct  = SAM_SPI_CLOCK / 200000 / 32;
  regval |= dlybct << SPI_CSR_DLYBCT_SHIFT;
  putreg32(regval, regaddr);

  /* Calculate the new actual frequency */

  actual = SAM_SPI_CLOCK / scbr;
  spivdbg("csr[%08x]=%08x actual=%d\n", regaddr, regval, actual);

  /* Save the frequency setting */

#ifndef CONFIG_SPI_OWNBUS
  priv->frequency = frequency;
  priv->actual    = actual;
#endif

  spidbg("Frequency %d->%d\n", frequency, actual);
  return actual;
}

/****************************************************************************
 * Name: spi_setmode
 *
 * Description:
 *   Set the SPI mode. Optional.  See enum spi_mode_e for mode definitions
 *
 * Input Parameters:
 *   dev -  Device-specific state data
 *   mode - The SPI mode requested
 *
 * Returned Value:
 *   none
 *
 ****************************************************************************/

static void spi_setmode(FAR struct spi_dev_s *dev, enum spi_mode_e mode)
{
  FAR struct sam_spidev_s *priv = (FAR struct sam_spidev_s *)dev;
  uint32_t regval;
  uint32_t regaddr;

  spivdbg("cs=%d mode=%d\n", priv->cs, mode);

  /* Has the mode changed? */

#ifndef CONFIG_SPI_OWNBUS
  if (mode != priv->mode)
    {
#endif
      /* Yes... Set the mode appropriately:
       *
       * SPI  CPOL NCPHA
       * MODE
       *  0    0    1
       *  1    0    0
       *  2    1    1
       *  3    1    0
       */

      regaddr = g_csraddr[priv->cs];
      regval  = getreg32(regaddr);
      regval &= ~(SPI_CSR_CPOL | SPI_CSR_NCPHA);

      switch (mode)
        {
        case SPIDEV_MODE0: /* CPOL=0; NCPHA=1 */
          regval |= SPI_CSR_NCPHA;
          break;

        case SPIDEV_MODE1: /* CPOL=0; NCPHA=0 */
          break;

        case SPIDEV_MODE2: /* CPOL=1; NCPHA=1 */
          regval |= (SPI_CSR_CPOL | SPI_CSR_NCPHA);
          break;

        case SPIDEV_MODE3: /* CPOL=1; NCPHA=0 */
          regval |= SPI_CSR_CPOL;
          break;

        default:
          DEBUGASSERT(FALSE);
          return;
        }

      putreg32(regval, regaddr);
      spivdbg("csr[%08x]=%08x\n", regaddr, regval);

      /* Save the mode so that subsequent re-configurations will be faster */

#ifndef CONFIG_SPI_OWNBUS
      priv->mode = mode;
    }
#endif
}

/****************************************************************************
 * Name: spi_setbits
 *
 * Description:
 *   Set the number if bits per word.
 *
 * Input Parameters:
 *   dev -  Device-specific state data
 *   nbits - The number of bits requests
 *
 * Returned Value:
 *   none
 *
 ****************************************************************************/

static void spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  FAR struct sam_spidev_s *priv = (FAR struct sam_spidev_s *)dev;
  uint32_t regaddr;
  uint32_t regval;

  spivdbg("cs=%d nbits=%d\n", priv->cs, nbits);
  DEBUGASSERT(priv && nbits > 7 && nbits < 17);

  /* NOTE:  The logic in spi_send and in spi_exchange only handles 8-bit
   * data at the present time.  So the following extra assertion is a
   * reminder that we have to fix that someday.
   */

  DEBUGASSERT(nbits == 8); /* Temporary -- FIX ME */

  /* Has the number of bits changed? */

#ifndef CONFIG_SPI_OWNBUS
  if (nbits != priv->nbits)
    {
#endif
      /* Yes... Set number of bits appropriately */

      regaddr = g_csraddr[priv->cs];
      regval  = getreg32(regaddr);
      regval &= ~SPI_CSR_BITS_MASK;
      regval |= SPI_CSR_BITS(nbits);
      putreg32(regval, regaddr);

      spivdbg("csr[%08x]=%08x\n", regaddr, regval);

      /* Save the selection so the subsequence re-configurations will be faster */

#ifndef CONFIG_SPI_OWNBUS
      priv->nbits = nbits;
    }
#endif
}

/****************************************************************************
 * Name: spi_send
 *
 * Description:
 *   Exchange one word on SPI
 *
 * Input Parameters:
 *   dev - Device-specific state data
 *   wd  - The word to send.  the size of the data is determined by the
 *         number of bits selected for the SPI interface.
 *
 * Returned Value:
 *   response
 *
 ****************************************************************************/

static uint16_t spi_send(FAR struct spi_dev_s *dev, uint16_t wd)
{
  uint8_t txbyte;
  uint8_t rxbyte;

  /* spi_exchange can do this. Note: right now, this only deals with 8-bit
   * words.  If the SPI interface were configured for words of other sizes,
   * this would fail.
   */

  txbyte = (uint8_t)wd;
  spi_exchange(dev, &txbyte, &rxbyte, 1);

  spivdbg("Sent %02x received %02x\n", txbyte, rxbyte);
  return (uint16_t)rxbyte;
}

/****************************************************************************
 * Name: spi_exchange
 *
 * Description:
 *   Exahange a block of data from SPI. Required.
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *   txbuffer - A pointer to the buffer of data to be sent
 *   rxbuffer - A pointer to the buffer in which to recieve data
 *   nwords   - the length of data that to be exchanged in units of words.
 *              The wordsize is determined by the number of bits-per-word
 *              selected for the SPI interface.  If nbits <= 8, the data is
 *              packed into uint8_t's; if nbits >8, the data is packed into
 *              uint16_t's
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void  spi_exchange(FAR struct spi_dev_s *dev,
                          FAR const void *txbuffer, FAR void *rxbuffer,
                          size_t nwords)
{
  FAR struct sam_spidev_s *priv = (FAR struct sam_spidev_s *)dev;
  FAR uint8_t *rxptr = (FAR uint8_t*)rxbuffer;
  FAR uint8_t *txptr = (FAR uint8_t*)txbuffer;
  uint32_t pcs;
  uint32_t data;

  spivdbg("txbuffer=%p rxbuffer=%p nwords=%d\n", txbuffer, rxbuffer, nwords);

  /* Set up PCS bits */

  pcs = spi_cs2pcs(priv) << SPI_TDR_PCS_SHIFT;

  /* Make sure that any previous transfer is flushed from the hardware */

  spi_flush();

  /* Loop, sending each word in the user-provied data buffer.
   *
   * Note 1: Right now, this only deals with 8-bit words.  If the SPI
   *         interface were configured for words of other sizes, this
   *         would fail.
   * Note 2: Good SPI performance would require that we implement DMA
   *         transfers!
   * Note 3: This loop might be made more efficient.  Would logic
   *         like the following improve the throughput?  Or would it
   *         just add the risk of overruns?
   *
   *   Get word 1;
   *   Send word 1;  Now word 1 is "in flight"
   *   nwords--;
   *   for ( ; nwords > 0; nwords--)
   *     {
   *       Get word N.
   *       Wait for TDRE meaning that word N-1 has moved to the shift
   *          register.
   *       Disable interrupts to keep the following atomic
   *       Send word N.  Now both work N-1 and N are "in flight"
   *       Wait for RDRF meaning that word N-1 is available
   *       Read word N-1.
   *       Re-enable interrupts.
   *       Save word N-1.
   *     }
   *   Wait for RDRF meaning that the final word is available
   *   Read the final word.
   *   Save the final word.
   */

  for ( ; nwords > 0; nwords--)
    {
      /* Get the data to send (0xff if there is no data source) */

      if (txptr)
        {
          data = (uint32_t)*txptr++;
        }
      else
        {
          data = 0xffff;
        }

      /* Set the PCS field in the value written to the TDR */

      data |= pcs;

      /* Do we need to set the LASTXFER bit in the TDR value too? */

#ifdef CONFIG_SPI_VARSELECT
      if (nwords == 1)
        {
          data |= SPI_TDR_LASTXFER;
        }
#endif

      /* Wait for any previous data written to the TDR to be transferred
       * to the serializer.
       */

      while ((getreg32(SAM_SPI0_SR) & SPI_INT_TDRE) == 0);

      /* Write the data to transmitted to the Transmit Data Register (TDR) */

      putreg32(data, SAM_SPI0_TDR);

      /* Wait for the read data to be available in the RDR.
       * TODO:  Data transfer rates would be improved using the RX FIFO
       *        (and also DMA)
       */

      while ((getreg32(SAM_SPI0_SR) & SPI_INT_RDRF) == 0);

      /* Read the received data from the SPI Data Register..
       * TODO: The following only works if nbits <= 8.
       */

      data = getreg32(SAM_SPI0_RDR);
      if (rxptr)
        {
          *rxptr++ = (uint8_t)data;
        }
    }
}

/***************************************************************************
 * Name: spi_sndblock
 *
 * Description:
 *   Send a block of data on SPI
 *
 * Input Parameters:
 *   dev -    Device-specific state data
 *   buffer - A pointer to the buffer of data to be sent
 *   nwords - the length of data to send from the buffer in number of words.
 *            The wordsize is determined by the number of bits-per-word
 *            selected for the SPI interface.  If nbits <= 8, the data is
 *            packed into uint8_t's; if nbits >8, the data is packed into uint16_t's
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifndef CONFIG_SPI_EXCHANGE
static void spi_sndblock(FAR struct spi_dev_s *dev, FAR const void *buffer, size_t nwords)
{
  /* spi_exchange can do this. */

  spi_exchange(dev, buffer, NULL, nwords);
}
#endif

/****************************************************************************
 * Name: spi_recvblock
 *
 * Description:
 *   Revice a block of data from SPI
 *
 * Input Parameters:
 *   dev -    Device-specific state data
 *   buffer - A pointer to the buffer in which to recieve data
 *   nwords - the length of data that can be received in the buffer in number
 *            of words.  The wordsize is determined by the number of bits-per-word
 *            selected for the SPI interface.  If nbits <= 8, the data is
 *            packed into uint8_t's; if nbits >8, the data is packed into uint16_t's
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#ifndef CONFIG_SPI_EXCHANGE
static void spi_recvblock(FAR struct spi_dev_s *dev, FAR void *buffer, size_t nwords)
{
  /* spi_exchange can do this. */

  spi_exchange(dev, NULL, buffer, nwords);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_spiinitialize
 *
 * Description:
 *   Initialize the selected SPI port
 *
 * Input Parameter:
 *   cs - Chip select number (identifying the "logical" SPI port)
 *
 * Returned Value:
 *   Valid SPI device structure reference on succcess; a NULL on failure
 *
 ****************************************************************************/

FAR struct spi_dev_s *up_spiinitialize(int cs)
{
  FAR struct sam_spidev_s *priv;
  irqstate_t flags;
#ifndef CONFIG_SPI_OWNBUS
  uint32_t regaddr;
  uint32_t regval;
#endif

  /* The support SAM parts have only a single SPI port */

  spivdbg("cs=%d\n", cs);
  DEBUGASSERT(cs >= 0 && cs <= SAM_SPI_NCS);

#ifdef CONFIG_SPI_OWNBUS
  /* There is only one device on the bus and, therefore, there is only one
   * supported chip select.  In this case, use the single, pre-allocated
   * chip select structure.
   */

  priv = &g_spidev;

#else
  /* Allocate a new state structure for this chip select.  NOTE that there
   * is no protection if the same chip select is used in two different
   * chip select structures.
   */

  priv = (FAR struct sam_spidev_s *)zalloc(sizeof(struct sam_spidev_s));
  if (!priv)
    {
      spivdbg("ERROR:  Failed to allocate a chip select structure\n", cs);
      return NULL;
    }
#endif

  /* Set up the initial state for this chip select structure.  Other fields
   * were zeroed by zalloc().
   */

  priv->spidev.ops = &g_spiops;
  priv->cs = cs;

#ifndef CONFIG_SPI_OWNBUS
  /* Has the SPI hardware been initialized? */

  if (!g_spinitialized)
#endif
    {
      /* Enable clocking to the SPI block */

      flags = irqsave();
      sam_spi0_enableclk();

      /* Configure multiplexed pins as connected on the board.  Chip select
       * pins must be configured by board-specific logic.
       */

      sam_configgpio(GPIO_SPI0_MISO);
      sam_configgpio(GPIO_SPI0_MOSI);
      sam_configgpio(GPIO_SPI0_SPCK);

      /* Disable SPI clocking */

      putreg32(SPI_CR_SPIDIS, SAM_SPI0_CR);

      /* Execute a software reset of the SPI (twice) */

      putreg32(SPI_CR_SWRST, SAM_SPI0_CR);
      putreg32(SPI_CR_SWRST, SAM_SPI0_CR);
      irqrestore(flags);

      /* Configure the SPI mode register */

      putreg32(SPI_MR_MSTR | SPI_MR_MODFDIS, SAM_SPI0_MR);

      /* And enable the SPI */

      putreg32(SPI_CR_SPIEN, SAM_SPI0_CR);
      up_mdelay(20);

      /* Flush any pending transfers */

      (void)getreg32(SAM_SPI0_SR);
      (void)getreg32(SAM_SPI0_RDR);

#ifndef CONFIG_SPI_OWNBUS
      /* Initialize the SPI semaphore that enforces mutually exclusive
       * access to the SPI registers.
       */

      sem_init(&g_spisem, 0, 1);
      g_spinitialized = true;
#endif
      spi_dumpregs("After initialization");
    }

#ifndef CONFIG_SPI_OWNBUS
  /* Set to mode=0 and nbits=8 and impossible frequency. It is only
   * critical to do this if CONFIG_SPI_OWNBUS is not defined because in
   * that case, the SPI will only be reconfigured if there is a change.
   */

  regaddr = g_csraddr[cs];
  regval  = getreg32(regaddr);
  regval &= ~(SPI_CSR_CPOL | SPI_CSR_NCPHA | SPI_CSR_BITS_MASK);
  regval |= (SPI_CSR_NCPHA | SPI_CSR_BITS(8));
  putreg32(regval, regaddr);

  priv->nbits = 8;
  spivdbg("csr[%08x]=%08x\n", regaddr, regval);
#endif

  return &priv->spidev;
}
#endif /* CONFIG_SAM34_SPI0 */

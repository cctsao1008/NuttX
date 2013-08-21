/*******************************************************************************
 * arch/arm/src/sama5/sam_ehci.c
 *
 *   Copyright (C) 2013 Gregory Nutt. All rights reserved.
 *   Authors: Gregory Nutt <gnutt@nuttx.org>
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
 *******************************************************************************/

/*******************************************************************************
 * Included Files
 *******************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/usb/usb.h>
#include <nuttx/usb/usbhost.h>
#include <nuttx/usb/ehci.h>

#include "up_arch.h"
#include "cache.h"

#include "sam_periphclks.h"
#include "sam_memories.h"
#include "sam_usbhost.h"
#include "chip/sam_ehci.h"

#ifdef CONFIG_SAMA5_EHCI

/*******************************************************************************
 * Pre-processor Definitions
 *******************************************************************************/
/* Configuration ***************************************************************/

/* Configurable number of Queue Head (QH) structures.  The default is one per
 * Root hub port plus one for EP0.
 */

#ifndef CONFIG_SAMA5_EHCI_NQHS
#  define CONFIG_SAMA5_EHCI_NQHS (SAM_EHCI_NRHPORT + 1)
#endif

/* Configurable number of Queue Element Transfer Descriptor (qTDs).  The default
 * is one per root hub plus three from EP0.
 */

#ifndef CONFIG_SAMA5_EHCI_NQTDS
#  define CONFIG_SAMA5_EHCI_NQTDS (SAM_EHCI_NRHPORT + 3)
#endif

/* Configurable size of a request/descriptor buffers */

#ifndef CONFIG_SAMA5_EHCI_BUFSIZE
#  define CONFIG_SAMA5_EHCI_BUFSIZE 128
#endif

/* Debug options */

#ifndef CONFIG_DEBUG
#  undef CONFIG_SAMA5_EHCI_REGDEBUG
#endif

/*******************************************************************************
 * Private Types
 *******************************************************************************/
/* Internal representation of the EHCI Queue Head (QH) */

struct sam_qh_s
{
  /* Fields visible to hardware */

  struct ehci_qh_s hw;         /* Hardware representation of the queue head */

  /* Internal fields used by the EHCI driver */

  uint32_t pad[16];            /* Padding to assure 32-byte alignment */
};

/* Internal representation of the EHCI Queue Element Transfer Descriptor (qTD) */

struct sam_qtd_s
{
  /* Fields visible to hardware */

  struct ehci_qtd_s hw;        /* Hardware representation of the queue head */

  /* Internal fields used by the EHCI driver */
};

/* The following is used to manage lists of free QHs and qTDs */

struct sam_list_s
{
  struct sam_list_s *flink;    /* Link to next entry in the list */
                               /* Variable length entry data follows */
};

/* List traversal callout functions */

typedef int (*foreach_qh_t)(struct sam_qh_s *qh, uint32_t **bp, void *arg);
typedef int (*foreach_qtd_t)(struct sam_qtd_s *qtd, uint32_t **bp, void *arg);

/* This structure describes one endpoint. */

struct sam_epinfo_s
{
  uint8_t          epno;       /* Endpoint number */
  uint8_t          devaddr;    /* Device address */
  uint8_t          xfrtype;    /* See USB_EP_ATTR_XFER_* definitions in usb.h */
  uint8_t          speed;      /* See USB_*_SPEED definitions in ehci.h */
  uint8_t          flags;      /* See EPINFO_FLAG_* definitions above */
  volatile bool    wait;       /* TRUE: Thread is waiting for transfer completion */
  uint16_t         maxpacket;  /* Maximum packet size */
  sem_t            wsem;       /* Semaphore used to wait for transfer completion */
};

/* This structure retains the state of one root hub port */

struct sam_rhport_s
{
  /* Common device fields.  This must be the first thing defined in the
   * structure so that it is possible to simply cast from struct usbhost_s
   * to struct sam_rhport_s.
   */

  struct usbhost_driver_s drvr;

  /* Root hub port status */

  volatile bool connected;     /* Connected to device */
  volatile bool lowspeed;      /* Low speed device attached */
  uint8_t rhpndx;              /* Root hub port index */

  /* The bound device class driver */

  struct usbhost_class_s *class;
};

/* This structure retains the overall state of the USB host controller */

struct sam_ehci_s
{
  volatile bool rhwait;        /* TRUE: Thread is waiting for root hub event */
  sem_t exclsem;               /* Support mutually exclusive access */
  sem_t rhsem;                 /* Semaphore to wait for root hub events */

  struct sam_epinfo_s ep0;     /* Endpoint 0 */
  struct sam_list_s *qhfree;   /* List of free Queue Head (QH) structures */
  struct sam_list_s *qtdfree;  /* List of free Queue Element Transfer Descriptor (qTD) */

  /* Root hub ports */

  struct sam_rhport_s rhport[SAM_EHCI_NRHPORT];
};

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/* Register operations ********************************************************/

#ifdef CONFIG_ENDIAN_BIG
static uint16_t sam_read16(volatile uint16_t *addr);
static uint32_t sam_read32(volatile uint32_t *addr);
static void sam_write16(uint16_t memval, volatile uint16_t *addr);
static void sam_write32(uint32_t memval, volatile uint32_t *addr);
#else
static inline uint16_t sam_read16(volatile uint16_t *addr);
static inline uint32_t sam_read32(volatile uint32_t *addr);
static inline void sam_write16(uint16_t memval, volatile uint16_t *addr);
static inline void sam_write32(uint32_t memval, volatile uint32_t *addr);
#endif

#ifdef CONFIG_SAMA5_EHCI_REGDEBUG
static void sam_printreg(volatile uint32_t *regaddr, uint32_t regval,
         bool iswrite);
static void sam_checkreg(volatile uint32_t *regaddr, uint32_t regval,
         bool iswrite);
static uint32_t sam_getreg(volatile uint32_t *regaddr);
static void sam_putreg(uint32_t regval, volatile uint32_t *regaddr);
#else
static inline uint32_t sam_getreg(volatile uint32_t *regaddr);
static inline void sam_putreg(uint32_t regval, volatile uint32_t *regaddr);
#endif
static int ehci_wait_usbsts(uint32_t maskbits, uint32_t donebits,
         unsigned int delay);

/* Semaphores ******************************************************************/

static void sam_takesem(sem_t *sem);
#define sam_givesem(s) sem_post(s);

/* Allocators ******************************************************************/

static struct sam_qh_s *sam_qh_alloc(void);
static void sam_qh_free(struct sam_qh_s *qh);
static struct sam_qtd_s *sam_qtd_alloc(void);
static void sam_qtd_free(struct sam_qtd_s *qtd);

/* List Management *************************************************************/

static int sam_qh_foreach(struct sam_qh_s *qh, uint32_t *bp,
         foreach_qh_t handler, void *arg);
static int sam_qtd_foreach(struct sam_qh_s *qh, foreach_qtd_t handler,
         void *arg);
static int sam_qtd_discard(struct sam_qtd_s *qtd, uint32_t **bp, void *arg);
static int sam_qh_discard(struct sam_qh_s *qh);

/* Cache Operations ************************************************************/

static int sam_qtd_invalidate(struct sam_qtd_s *qtd, uint32_t **bp, void *arg);
static int sam_qh_invalidate(struct sam_qh_s *qh);
static int sam_qtd_flush(struct sam_qtd_s *qtd, uint32_t **bp, void *arg);
static int sam_qh_flush(struct sam_qh_s *qh);

/* Interrupt Handling **********************************************************/

static int sam_ehci_interrupt(int irq, FAR void *context);

/* USB Host Controller Operations **********************************************/

static int sam_wait(FAR struct usbhost_connection_s *conn,
         FAR const bool *connected);
static int sam_enumerate(FAR struct usbhost_connection_s *conn, int rhpndx);

static int sam_ep0configure(FAR struct usbhost_driver_s *drvr, uint8_t funcaddr,
         uint16_t maxpacketsize);
static int sam_epalloc(FAR struct usbhost_driver_s *drvr,
         const FAR struct usbhost_epdesc_s *epdesc, usbhost_ep_t *ep);
static int sam_epfree(FAR struct usbhost_driver_s *drvr, usbhost_ep_t ep);
static int sam_alloc(FAR struct usbhost_driver_s *drvr,
         FAR uint8_t **buffer, FAR size_t *maxlen);
static int sam_free(FAR struct usbhost_driver_s *drvr, FAR uint8_t *buffer);
static int sam_ioalloc(FAR struct usbhost_driver_s *drvr,
         FAR uint8_t **buffer, size_t buflen);
static int sam_iofree(FAR struct usbhost_driver_s *drvr, FAR uint8_t *buffer);
static int sam_ctrlin(FAR struct usbhost_driver_s *drvr,
         FAR const struct usb_ctrlreq_s *req, FAR uint8_t *buffer);
static int sam_ctrlout(FAR struct usbhost_driver_s *drvr,
         FAR const struct usb_ctrlreq_s *req, FAR const uint8_t *buffer);
static int sam_transfer(FAR struct usbhost_driver_s *drvr, usbhost_ep_t ep,
         FAR uint8_t *buffer, size_t buflen);
static void sam_disconnect(FAR struct usbhost_driver_s *drvr);

/* Initialization **************************************************************/

static int sam_reset(void);

/*******************************************************************************
 * Private Data
 *******************************************************************************/
/* In this driver implementation, support is provided for only a single a single
 * USB device.  All status information can be simply retained in a single global
 * instance.
 */

static struct sam_ehci_s g_ehci;

/* This is the connection/enumeration interface */

static struct usbhost_connection_s g_ehciconn;

/* Pools of pre-allocated data structures.  These will all be linked into the
 * free lists within g_ehci.  These must all be aligned to 32-byte boundaries
 */

/* Queue Head (QH) pool */

static struct sam_qh_s g_ghpool[CONFIG_SAMA5_EHCI_NQHS]
                       __attribute__ ((aligned(32)));

/* Queue Element Transfer Descriptor (qTD) pool */

static struct sam_qtd_s g_qtdpool[CONFIG_SAMA5_EHCI_NQTDS]
                        __attribute__ ((aligned(32)));

/*******************************************************************************
 * Private Functions
 *******************************************************************************/
/*******************************************************************************
 * Register Operations
 *******************************************************************************/
/*******************************************************************************
 * Name: sam_read16
 *
 * Description:
 *   Read 16-bit little endian data
 *
 *******************************************************************************/

#ifdef CONFIG_ENDIAN_BIG
static uint16_t sam_read16(volatile uint16_t *addr)
{
  uint8_t *addr8 = (uint8_t *)addr;

  return (uint16_t)addr8[1] << 8 | (uint16_t)addr[0];
}
#else
static inline uint16_t sam_read16(volatile uint16_t *addr)
{
  return *addr;
}
#endif

/*******************************************************************************
 * Name: sam_read32
 *
 * Description:
 *   Read 32-bit little endian data
 *
 *******************************************************************************/

#ifdef CONFIG_ENDIAN_BIG
static uint32_t sam_read32(volatile uint32_t *addr)
{
  uint16_t *addr16 = (uint16_t *)addr;

  return (uint32_t)sam_read16(&addr16[1]) << 16 |
         (uint32_t)sam_read16(&addr16[0]);
}
#else
static inline uint32_t sam_read32(volatile uint32_t *addr)
{
  return *addr;
}
#endif

/*******************************************************************************
 * Name: sam_write16
 *
 * Description:
 *   Write 16-bit little endian data
 *
 *******************************************************************************/

#ifdef CONFIG_ENDIAN_BIG
static void sam_write16(uint16_t memval, volatile uint16_t *addr)
{
  volatile uint8_t *addr8 = (uint8_t *)addr;

  addr8[0] = memval & 0xff;
  addr8[1] = memval >> 8;
}
#else
static inline void sam_write16(uint16_t memval, volatile uint16_t *addr)
{
  *addr = memval;
}
#endif

/*******************************************************************************
 * Name: sam_write32
 *
 * Description:
 *   Write 32-bit little endian data
 *
 *******************************************************************************/

#ifdef CONFIG_ENDIAN_BIG
static void sam_write32(uint32_t memval, volatile uint32_t *addr)
{
  volatile uint16_t *addr16 = (uint16_t *)addr;

  sam_write16(memval & 0xffff, &add16[0]);
  sam_write16(memval >> 16, &add16[1]);
}
#else
static inline void sam_write32(uint32_t memval, volatile uint32_t *addr)
{
  *addr = memval;
}
#endif

/*******************************************************************************
 * Name: sam_printreg
 *
 * Description:
 *   Print the contents of a SAMA5 EHCI register
 *
 *******************************************************************************/

#ifdef CONFIG_SAMA5_EHCI_REGDEBUG
static void sam_printreg(volatile uint32_t *regaddr, uint32_t regval,
                          bool iswrite)
{
  lldbg("%p%s%08x\n", regaddr, iswrite ? "<-" : "->", regval);
}
#endif

/*******************************************************************************
 * Name: sam_checkreg
 *
 * Description:
 *   Check if it is time to output debug information for accesses to a SAMA5
 *   EHCI register
 *
 *******************************************************************************/

#ifdef CONFIG_SAMA5_EHCI_REGDEBUG
static void sam_checkreg(volatile uint32_t *regaddr, uint32_t regval, bool iswrite)
{
  static uint32_t *prevaddr = NULL;
  static uint32_t preval = 0;
  static uint32_t count = 0;
  static bool     prevwrite = false;

  /* Is this the same value that we read from/wrote to the same register last time?
   * Are we polling the register?  If so, suppress the output.
   */

  if (regaddr == prevaddr && regval == preval && prevwrite == iswrite)
    {
      /* Yes.. Just increment the count */

      count++;
    }
  else
    {
      /* No this is a new address or value or operation. Were there any
       * duplicate accesses before this one?
       */

      if (count > 0)
        {
          /* Yes.. Just one? */

          if (count == 1)
            {
              /* Yes.. Just one */

              sam_printreg(prevaddr, preval, prevwrite);
            }
          else
            {
              /* No.. More than one. */

              lldbg("[repeats %d more times]\n", count);
            }
        }

      /* Save the new address, value, count, and operation for next time */

      prevaddr  = (uint32_t *)regaddr;
      preval    = regval;
      count     = 0;
      prevwrite = iswrite;

      /* Show the new register access */

      sam_printreg(regaddr, regval, iswrite);
    }
}
#endif

/*******************************************************************************
 * Name: sam_getreg
 *
 * Description:
 *   Get the contents of an SAMA5 register
 *
 *******************************************************************************/

#ifdef CONFIG_SAMA5_EHCI_REGDEBUG
static uint32_t sam_getreg(volatile uint32_t *regaddr)
{
  /* Read the value from the register */

  uint32_t regval = *regaddr;

  /* Check if we need to print this value */

  sam_checkreg(regaddr, regval, false);
  return regval;
}
#else
static inline uint32_t sam_getreg(volatile uint32_t *regaddr)
{
  return *regaddr;
}
#endif

/*******************************************************************************
 * Name: sam_putreg
 *
 * Description:
 *   Set the contents of an SAMA5 register to a value
 *
 *******************************************************************************/

#ifdef CONFIG_SAMA5_EHCI_REGDEBUG
static void sam_putreg(uint32_t regval, volatile uint32_t *regaddr)
{
  /* Check if we need to print this value */

  sam_checkreg(regaddr, regval, true);

  /* Write the value */

  *regaddr = regval;
}
#else
static inline void sam_putreg(uint32_t regval, volatile uint32_t *regaddr)
{
  *regaval = regval;
}
#endif

/*******************************************************************************
 * Name: ehci_wait_usbsts
 *
 * Description:
 *   Wait for either (1) a field in the USBSTS register to take a specific
 *   value, (2) for a timeout to occur, or (3) a error to occur.  Return
 *   a value to indicate which terminated the wait.
 *
 *******************************************************************************/

static int ehci_wait_usbsts(uint32_t maskbits, uint32_t donebits,
                            unsigned int delay)
{
  uint32_t regval;
  unsigned int timeout;

  timeout = 0;
  do
    {
      /* Wait 5usec before trying again */

      up_udelay(5);
      timeout += 5;

      /* Read the USBSTS register and check for a system error */

      regval = sam_getreg(&HCOR->usbsts);
      if ((regval & EHCI_INT_SYSERROR) != 0)
        {
          udbg("ERROR: System error: 0x%08X", regval);
          return -EIO;
        }

      /* Mask out the bits of interest */

      regval &= maskbits;

      /* Loop until the masked bits take the specified value or until a
       * timeout occurs.
       */
    }
  while (regval != donebits && timeout < delay);

  /* We got here because either the waited for condition or a timeout
   * occurred.  Return a value to indicate which.
   */
 
  return (regval == donebits) ? OK : -ETIMEDOUT;
}

/*******************************************************************************
 * Semaphores
 *******************************************************************************/
/*******************************************************************************
 * Name: sam_takesem
 *
 * Description:
 *   This is just a wrapper to handle the annoying behavior of semaphore
 *   waits that return due to the receipt of a signal.
 *
 *******************************************************************************/

static void sam_takesem(sem_t *sem)
{
  /* Take the semaphore (perhaps waiting) */

  while (sem_wait(sem) != 0)
    {
      /* The only case that an error should occr here is if the wait was
       * awakened by a signal.
       */

      ASSERT(errno == EINTR);
    }
}

/*******************************************************************************
 * Allocators
 *******************************************************************************/
/*******************************************************************************
 * Name: sam_qh_alloc
 *
 * Description:
 *   Allocate a Queue Head (QH) structure by removing it from the free list
 *
 *******************************************************************************/

static struct sam_qh_s *sam_qh_alloc(void)
{
  struct sam_qh_s *qh;

  /* Remove the QH structure from the freelist */

  qh = (struct sam_qh_s *)g_ehci.qhfree;
  if (qh)
    {
      g_ehci.qhfree = ((struct sam_list_s *)qh)->flink;
      memset(qh, 0, sizeof(struct sam_qh_s));
    }

  return qh;
}

/*******************************************************************************
 * Name: sam_qh_free
 *
 * Description:
 *   Free a Queue Head (QH) structure by returning it to the free list
 *
 *******************************************************************************/

static void sam_qh_free(struct sam_qh_s *qh)
{
  struct sam_list_s *entry = (struct sam_list_s *)qh;

  /* Put the QH structure back into the free list */

  entry->flink  = g_ehci.qhfree;
  g_ehci.qhfree = entry;
}

/*******************************************************************************
 * Name: sam_qtd_alloc
 *
 * Description:
 *   Allocate a Queue Element Transfer Descriptor (qTD) by removing it from the
 *   free list
 *
 *******************************************************************************/

static struct sam_qtd_s *sam_qtd_alloc(void)
{
  struct sam_qtd_s *qtd;

  /* Remove the qTD from the freelist */

  qtd = (struct sam_qtd_s *)g_ehci.qtdfree;
  if (qtd)
    {
      g_ehci.qtdfree = ((struct sam_list_s *)qtd)->flink;
      memset(qtd, 0, sizeof(struct sam_list_s));
    }

  return qtd;
}

/*******************************************************************************
 * Name: sam_edfree
 *
 * Description:
 *   Free a Queue Element Transfer Descriptor (qTD) by returning it to the free
 *   list
 *
 *******************************************************************************/

static void sam_qtd_free(struct sam_qtd_s *qtd)
{
  struct sam_list_s *entry = (struct sam_list_s *)qtd;

  /* Put the qTD back into the free list */

  entry->flink   = g_ehci.qtdfree;
  g_ehci.qtdfree = entry;
}

/*******************************************************************************
 * List Management
 *******************************************************************************/

/*******************************************************************************
 * Name: sam_qh_foreach
 *
 * Description:
 *   Give the first entry in a list of Queue Head (QH) structures, call the
 *   handler for each QH structure in the list (including the one at the head
 *   of the list).
 *
 *******************************************************************************/

static int sam_qh_foreach(struct sam_qh_s *qh, uint32_t *bp, foreach_qh_t handler,
                          void *arg)
{
  struct sam_qh_s *next;
  uintptr_t physaddr;
  int ret;

  DEBUGASSERT(qh && handler);
  while (qh)
    {
      /* Is this the end of the list?  Check the horizontal link pointer (HLP)
       * terminate (T) bit.  If T==1, then the HLP address is not valid.
       */

      if ((qh->hw.hlp & QH_HLP_T) != 0)
        {
          /* Set the next pointer to NULL.  This will terminate the loop. */

          next = NULL;
        }
      else
        {
          physaddr = qh->hw.hlp & QH_HLP_MASK;
          next     = (struct sam_qh_s *)sam_virtramaddr(physaddr);
        }

      /* Perform the user action on this entry.  The action might result in
       * unlinking the entry!  But that is okay because we already have the
       * next QH pointer.
       *
       * Notice that we do not manage the back pointer (bp).  If the callout
       * uses it, it must update it as necessary.
       */

      ret = handler(qh, &bp, arg);

      /* If the handler returns any non-zero value, then terminate the traversal
       * early.
       */

      if (ret != 0)
        {
          return ret;
        }

      /* Set up to visit the next entry */

      qh = next;
    }

  return OK;
}

/*******************************************************************************
 * Name: sam_qtd_foreach
 *
 * Description:
 *   Give a Queue Head (QH) instance, call the handler for each qTD structure
 *   in the queue.
 *
 *******************************************************************************/

static int sam_qtd_foreach(struct sam_qh_s *qh, foreach_qtd_t handler, void *arg)
{
  struct sam_qtd_s *qtd;
  struct sam_qtd_s *next;
  uintptr_t physaddr;
  uint32_t *bp;
  int ret;

  DEBUGASSERT(qh && handler);

  /* Handle the special case where the queue is empty */

  bp = &qh->hw.overlay.nqp;
  if ((*bp & QH_NQP_T) != 0)
    {
      return 0;
    }

  /* Start with the first qTD in the queue */

  physaddr = sam_read32(bp);
  qtd      = (struct sam_qtd_s *)sam_virtramaddr(physaddr);
  next     = NULL;

  /* Now loop until we encounter the end of the qTD list */

  while (qtd)
    {
      /* Is this the end of the list?  Check the next qTD pointer (NQP)
       * terminate (T) bit.  If T==1, then the NQP address is not valid.
       */

      if ((qtd->hw.nqp & QTD_NQP_T) != 0)
        {
          /* Set the next pointer to NULL.  This will terminate the loop. */

          next = NULL;
        }
      else
        {
          physaddr = qtd->hw.nqp & QTD_NQP_NTEP_MASK;
          next     = (struct sam_qtd_s *)sam_virtramaddr(physaddr);
        }

      /* Perform the user action on this entry.  The action might result in
       * unlinking the entry!  But that is okay because we already have the
       * next qTD pointer.
       *
       * Notice that we do not manage the back pointer (bp).  If the callout
       * uses it, it must update it as necessary.
       */

      ret = handler(qtd, &bp, arg);

      /* If the handler returns any non-zero value, then terminate the traversal
       * early.
       */

      if (ret != 0)
        {
          return ret;
        }

      /* Set up to visit the next entry */

      qtd = next;
    }

  return OK;
}

/*******************************************************************************
 * Name: sam_qtd_discard
 *
 * Description:
 *   This is a sam_qtd_foreach callback.  It simply unlinks the QTD, updates
 *   the back pointer, and frees the QTD structure.
 *
 *******************************************************************************/

static int sam_qtd_discard(struct sam_qtd_s *qtd, uint32_t **bp, void *arg)
{
  DEBUGASSERT(qtd && bp && *bp);

  /* Remove the qTD from the list by updating the forward pointer to skip
   * around this qTD.  We do not change that pointer because are repeatedly
   * removing the aTD at the head of the QH list.
   */

  **bp = qtd->hw.nqp;

  /* Then free the qTD */

  sam_qtd_free(qtd);
  return OK;
}

/*******************************************************************************
 * Name: sam_qh_discard
 *
 * Description:
 *   Free the Queue Head (QH) and all qTD's attached to the QH.
 *
 * Assumptions:
 *   The QH structure itself has already been unlinked from whatever list it
 *   may have been in.
 *
 *******************************************************************************/

static int sam_qh_discard(struct sam_qh_s *qh)
{
  int ret;

  DEBUGASSERT(qh);

  /* Free all of the qTD's attached to the QH */

  ret = sam_qtd_foreach(qh, sam_qtd_discard, NULL);
  if (ret < 0)
    {
      udbg("ERROR: sam_qtd_foreach failed: %d\n", ret);
    }

  /* Then free the QH itself */

  sam_qh_free(qh);
  return ret;
}

/*******************************************************************************
 * Cache Operations
 *******************************************************************************/

/*******************************************************************************
 * Name: sam_qtd_invalidate
 *
 * Description:
 *   This is a callback from sam_qtd_foreach.  It simply invalidates D-cache for
 *   address range of the qTD entry.
 *
 *******************************************************************************/

static int sam_qtd_invalidate(struct sam_qtd_s *qtd, uint32_t **bp, void *arg)
{
  /* Invalidate the D-Cache, i.e., force reloading of the D-Cache from memory
   * memory over the specified address range.
   */

  cp15_invalidate_dcache((uintptr_t)&qtd->hw,
                         (uintptr_t)&qtd->hw + sizeof(struct ehci_qtd_s));
  return OK;
}

/*******************************************************************************
 * Name: sam_qh_invalidate
 *
 * Description:
 *   Invalidate the Queue Head and all qTD entries in the queue.
 *
 *******************************************************************************/

static int sam_qh_invalidate(struct sam_qh_s *qh)
{
  /* Invalidate the QH first so that we reload the qTD list head */

  cp15_invalidate_dcache((uintptr_t)&qh->hw,
                         (uintptr_t)&qh->hw + sizeof(struct ehci_qh_s));

  /* Then invalidate all of the qTD entries in the queue */

  return sam_qtd_foreach(qh, NULL, NULL);
}

/*******************************************************************************
 * Name: sam_qtd_flush
 *
 * Description:
 *   This is a callback from sam_qtd_foreach.  It simply flushes D-cache for
 *   address range of the qTD entry.
 *
 *******************************************************************************/

static int sam_qtd_flush(struct sam_qtd_s *qtd, uint32_t **bp, void *arg)
{
  /* Flush the D-Cache, i.e., make the contents of the memory match the contents
   * of the D-Cache in the specified address range.
   */

  cp15_coherent_dcache((uintptr_t)&qtd->hw,
                       (uintptr_t)&qtd->hw + sizeof(struct ehci_qtd_s));
  return OK;
}

/*******************************************************************************
 * Name: sam_qh_flush
 *
 * Description:
 *   Invalidate the Queue Head and all qTD entries in the queue.
 *
 *******************************************************************************/

static int sam_qh_flush(struct sam_qh_s *qh)
{
  /* Flush the QH first */

  cp15_invalidate_dcache((uintptr_t)&qh->hw,
                         (uintptr_t)&qh->hw + sizeof(struct ehci_qh_s));

  /* Then flush all of the qTD entries in the queue */

  return sam_qtd_foreach(qh, NULL, NULL);
}

/*******************************************************************************
 * EHCI Interrupt Handling
 *******************************************************************************/

/*******************************************************************************
 * Name: sam_ehci_interrupt
 *
 * Description:
 *   EHCI interrupt handler
 *
 *******************************************************************************/

static int sam_ehci_interrupt(int irq, FAR void *context)
{
#warning "Missing logic"
  return OK;
}

/*******************************************************************************
 * USB Host Controller Operations
 *******************************************************************************/
/*******************************************************************************
 * Name: sam_wait
 *
 * Description:
 *   Wait for a device to be connected or disconnected to/from a root hub port.
 *
 * Input Parameters:
 *   conn - The USB host connection instance obtained as a parameter from the call to
 *      the USB driver initialization logic.
 *   connected - A pointer to an array of 3 boolean values corresponding to
 *      root hubs 1, 2, and 3.  For each boolean value: TRUE: Wait for a device
 *      to be connected on the root hub; FALSE: wait for device to be
 *      disconnected from the root hub.
 *
 * Returned Values:
 *   And index [0, 1, or 2} corresponding to the root hub port number {1, 2,
 *   or 3} is returned when a device is connected or disconnected. This
 *   function will not return until either (1) a device is connected or
 *   disconnected to/from any root hub port or until (2) some failure occurs.
 *   On a failure, a negated errno value is returned indicating the nature of
 *   the failure
 *
 * Assumptions:
 *   - Called from a single thread so no mutual exclusion is required.
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static int sam_wait(FAR struct usbhost_connection_s *conn,
                    FAR const bool *connected)
{
  irqstate_t flags;
  int rhpndx;

  /* Loop until a change in the connection state changes on one of the root hub
   * ports or until an error occurs.
   */

  flags = irqsave();
  for (;;)
    {
      /* Check for a change in the connection state on any root hub port */

      for (rhpndx = 0; rhpndx < SAM_EHCI_NRHPORT; rhpndx++)
        {
          /* Has the connection state changed on the RH port? */

          if (g_ehci.rhport[rhpndx].connected != connected[rhpndx])
            {
              /* Yes.. Return the RH port number */

              irqrestore(flags);

              udbg("RHPort%d connected: %s\n",
                   rhpndx + 1, g_ehci.rhport[rhpndx].connected ? "YES" : "NO");

              return rhpndx;
            }
        }

      /* No changes on any port. Wait for a connection/disconnection event
       * and check again
       */

      g_ehci.rhwait = true;
      sam_takesem(&g_ehci.rhsem);
    }
}

/*******************************************************************************
 * Name: sam_enumerate
 *
 * Description:
 *   Enumerate the connected device.  As part of this enumeration process,
 *   the driver will (1) get the device's configuration descriptor, (2)
 *   extract the class ID info from the configuration descriptor, (3) call
 *   usbhost_findclass() to find the class that supports this device, (4)
 *   call the create() method on the struct usbhost_registry_s interface
 *   to get a class instance, and finally (5) call the configdesc() method
 *   of the struct usbhost_class_s interface.  After that, the class is in
 *   charge of the sequence of operations.
 *
 * Input Parameters:
 *   conn - The USB host connection instance obtained as a parameter from the call to
 *      the USB driver initialization logic.
 *   rphndx - Root hub port index.  0-(n-1) corresponds to root hub port 1-n.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   - Only a single class bound to a single device is supported.
 *   - Called from a single thread so no mutual exclusion is required.
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static int sam_enumerate(FAR struct usbhost_connection_s *conn, int rhpndx)
{
  struct sam_rhport_s *rhport;

  DEBUGASSERT(rhpndx >= 0 && rhpndx < SAM_EHCI_NRHPORT);
  rhport = &g_ehci.rhport[rhpndx];

  /* Are we connected to a device?  The caller should have called the wait()
   * method first to be assured that a device is connected.
   */

  while (!rhport->connected)
    {
      /* No, return an error */

      udbg("Not connected\n");
      return -ENODEV;
    }

  /* Add EP0 to the control list */
#warning Missing logic

  /* USB 2.0 spec says at least 50ms delay before port reset */

  up_mdelay(100);

  /* Put the root hub port in reset (the SAMA5 supports three downstream ports) */
#warning Missing logic

  /* Wait for the port reset to complete */
#warning Missing logic

  /* Release RH port 1 from reset and wait a bit */
#warning Missing logic

  up_mdelay(200);

  /* Let the common usbhost_enumerate do all of the real work.  Note that the
   * FunctionAddress (USB address) is set to the root hub port number for now.
   */

  uvdbg("Enumerate the device\n");
  return usbhost_enumerate(&g_ehci.rhport[rhpndx].drvr, rhpndx+1, &rhport->class);
}

/************************************************************************************
 * Name: sam_ep0configure
 *
 * Description:
 *   Configure endpoint 0.  This method is normally used internally by the
 *   enumerate() method but is made available at the interface to support
 *   an external implementation of the enumeration logic.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   funcaddr - The USB address of the function containing the endpoint that EP0
 *     controls.  A funcaddr of zero will be received if no address is yet assigned
 *     to the device.
 *   maxpacketsize - The maximum number of bytes that can be sent to or
 *    received from the endpoint in a single data packet
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function will *not* be called from an interrupt handler.
 *
 ************************************************************************************/

static int sam_ep0configure(FAR struct usbhost_driver_s *drvr, uint8_t funcaddr,
                            uint16_t maxpacketsize)
{
  struct sam_rhport_s *rhport = (struct sam_rhport_s *)drvr;

  DEBUGASSERT(rhport &&
              funcaddr >= 0 && funcaddr <= SAM_EHCI_NRHPORT &&
              maxpacketsize < 2048);

  /* We must have exclusive access to the EHCI data structures. */

  sam_takesem(&g_ehci.exclsem);

#warning Missing logic

  sam_givesem(&g_ehci.exclsem);
  return -ENOSYS;
}

/************************************************************************************
 * Name: sam_epalloc
 *
 * Description:
 *   Allocate and configure one endpoint.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   epdesc - Describes the endpoint to be allocated.
 *   ep - A memory location provided by the caller in which to receive the
 *      allocated endpoint desciptor.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function will *not* be called from an interrupt handler.
 *
 ************************************************************************************/

static int sam_epalloc(FAR struct usbhost_driver_s *drvr,
                       const FAR struct usbhost_epdesc_s *epdesc, usbhost_ep_t *ep)
{
  struct sam_rhport_s *rhport = (struct sam_rhport_s *)drvr;
  struct sam_epinfo_s *epinfo;
  int ret  = -ENOMEM;

  /* Sanity check.  NOTE that this method should only be called if a device is
   * connected (because we need a valid low speed indication).
   */

  DEBUGASSERT(rhport && epdesc && ep && rhport->connected);

  /* Allocate a container for the endpoint data */

  epinfo = (struct sam_epinfo_s *)kzalloc(sizeof(struct sam_epinfo_s));
  if (!epinfo)
    {
      udbg("ERROR: Failed to allocate EP info structure\n");
      goto errout;
    }

  /* Initialize the endpoint container */

  sem_init(&epinfo->wsem, 0, 0);

  /* We must have exclusive access to the EHCI data structures. */

  sam_takesem(&g_ehci.exclsem);

#warning Missing logic

  /* Success.. return an opaque reference to the endpoint list container */

  *ep = (usbhost_ep_t)epinfo;
  sam_givesem(&g_ehci.exclsem);
  return OK;

errout_with_semaphore:
  sam_givesem(&g_ehci.exclsem);
  kfree(epinfo);
errout:
  return ret;
}

/************************************************************************************
 * Name: sam_epfree
 *
 * Description:
 *   Free and endpoint previously allocated by DRVR_EPALLOC.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   ep - The endpint to be freed.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function will *not* be called from an interrupt handler.
 *
 ************************************************************************************/

static int sam_epfree(FAR struct usbhost_driver_s *drvr, usbhost_ep_t ep)
{
  struct sam_rhport_s *rhport = (struct sam_rhport_s *)drvr;
  struct sam_epinfo_s *epinfo = (struct sam_epinfo_s *)ep;
  int ret;

  DEBUGASSERT(rhport && epinfo);

  /* There should not be any pending, transfers */
#warning Missing logic

  /* We must have exclusive access to the EHCI data structures. */

  sam_takesem(&g_ehci.exclsem);

#warning Missing logic
  ret = -ENOSYS;

  /* And free the container */

  sem_destroy(&epinfo->wsem);
  kfree(epinfo);
  sam_givesem(&g_ehci.exclsem);
  return ret;
}

/*******************************************************************************
 * Name: sam_alloc
 *
 * Description:
 *   Some hardware supports special memory in which request and descriptor data
 *   can be accessed more efficiently.  This method provides a mechanism to
 *   allocate the request/descriptor memory.  If the underlying hardware does
 *   not support such "special" memory, this functions may simply map to kmalloc.
 *
 *   This interface was optimized under a particular assumption.  It was
 *   assumed that the driver maintains a pool of small, pre-allocated buffers
 *   for descriptor traffic.  NOTE that size is not an input, but an output:
 *   The size of the pre-allocated buffer is returned.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call
 *      to the class create() method.
 *   buffer - The address of a memory location provided by the caller in which
 *      to return the allocated buffer memory address.
 *   maxlen - The address of a memory location provided by the caller in which
 *      to return the maximum size of the allocated buffer memory.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   - Called from a single thread so no mutual exclusion is required.
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static int sam_alloc(FAR struct usbhost_driver_s *drvr,
                     FAR uint8_t **buffer, FAR size_t *maxlen)
{
  int ret = -ENOMEM;
  DEBUGASSERT(drvr && buffer && maxlen);

  /* There is no special requirements for transfer/descriptor buffers. */

  *buffer = (FAR uint8_t *)kmalloc(CONFIG_SAMA5_EHCI_BUFSIZE);
  if (*buffer)
    {
      *maxlen = CONFIG_SAMA5_EHCI_BUFSIZE;
      ret = OK;
    }

  return ret;
}

/*******************************************************************************
 * Name: sam_free
 *
 * Description:
 *   Some hardware supports special memory in which request and descriptor data
 *   can be accessed more efficiently.  This method provides a mechanism to
 *   free that request/descriptor memory.  If the underlying hardware does not
 *   support such "special" memory, this functions may simply map to kfree().
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call
 *      to the class create() method.
 *   buffer - The address of the allocated buffer memory to be freed.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static int sam_free(FAR struct usbhost_driver_s *drvr, FAR uint8_t *buffer)
{
  DEBUGASSERT(drvr && buffer);

  /* No special action is require to free the transfer/descriptor buffer memory */

  kfree(buffer);
  return OK;
}

/************************************************************************************
 * Name: sam_ioalloc
 *
 * Description:
 *   Some hardware supports special memory in which larger IO buffers can
 *   be accessed more efficiently.  This method provides a mechanism to allocate
 *   the request/descriptor memory.  If the underlying hardware does not support
 *   such "special" memory, this functions may simply map to kumalloc.
 *
 *   This interface differs from DRVR_ALLOC in that the buffers are variable-sized.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   buffer - The address of a memory location provided by the caller in which to
 *     return the allocated buffer memory address.
 *   buflen - The size of the buffer required.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function will *not* be called from an interrupt handler.
 *
 ************************************************************************************/

static int sam_ioalloc(FAR struct usbhost_driver_s *drvr, FAR uint8_t **buffer,
                       size_t buflen)
{
  DEBUGASSERT(drvr && buffer && buflen > 0);

  /* The only special requirements for I/O buffers are they might need to be user
   * accessible (depending on how the class driver implements its buffering).
   */

  *buffer = (FAR uint8_t *)kumalloc(buflen);
  return *buffer ? OK : -ENOMEM;
}

/************************************************************************************
 * Name: sam_iofree
 *
 * Description:
 *   Some hardware supports special memory in which IO data can  be accessed more
 *   efficiently.  This method provides a mechanism to free that IO buffer
 *   memory.  If the underlying hardware does not support such "special" memory,
 *   this functions may simply map to kufree().
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   buffer - The address of the allocated buffer memory to be freed.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function will *not* be called from an interrupt handler.
 *
 ************************************************************************************/

static int sam_iofree(FAR struct usbhost_driver_s *drvr, FAR uint8_t *buffer)
{
  DEBUGASSERT(drvr && buffer);

  /* No special action is require to free the I/O buffer memory */

  kufree(buffer);
  return OK;
}

/*******************************************************************************
 * Name: sam_ctrlin and sam_ctrlout
 *
 * Description:
 *   Process a IN or OUT request on the control endpoint.  These methods
 *   will enqueue the request and wait for it to complete.  Only one transfer may
 *   be queued; Neither these methods nor the transfer() method can be called
 *   again until the control transfer functions returns.
 *
 *   These are blocking methods; these functions will not return until the
 *   control transfer has completed.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   req - Describes the request to be sent.  This request must lie in memory
 *      created by DRVR_ALLOC.
 *   buffer - A buffer used for sending the request and for returning any
 *     responses.  This buffer must be large enough to hold the length value
 *     in the request description. buffer must have been allocated using
 *     DRVR_ALLOC
 *
 *   NOTE: On an IN transaction, req and buffer may refer to the same allocated
 *   memory.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure
 *
 * Assumptions:
 *   - Only a single class bound to a single device is supported.
 *   - Called from a single thread so no mutual exclusion is required.
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static int sam_ctrlin(FAR struct usbhost_driver_s *drvr,
                      FAR const struct usb_ctrlreq_s *req,
                      FAR uint8_t *buffer)
{
  struct sam_rhport_s *rhport = (struct sam_rhport_s *)drvr;
  uint16_t len;
  int  ret;

  DEBUGASSERT(rhport && req);

  len = sam_read16((uint16_t*)req->len);
  uvdbg("RHPort%d type: %02x req: %02x value: %02x%02x index: %02x%02x len: %04x\n",
        rhport->rhpndx + 1, req->type, req->req, req->value[1], req->value[0],
        req->index[1], req->index[0], len);

  /* We must have exclusive access to the EHCI hardware and data structures. */

  sam_takesem(&g_ehci.exclsem);

  /* Now perform the transfer */
#warning Missing logic
  ret = -ENOSYS;

  sam_givesem(&g_ehci.exclsem);
  return ret;
}

static int sam_ctrlout(FAR struct usbhost_driver_s *drvr,
                       FAR const struct usb_ctrlreq_s *req,
                       FAR const uint8_t *buffer)
{
  /* sam_ctrlin can handle both directions.  We just need to work around the
   * differences in the function signatures.
   */

  return sam_ctrlin(drvr, req, (uint8_t *)buffer);
}

/*******************************************************************************
 * Name: sam_transfer
 *
 * Description:
 *   Process a request to handle a transfer descriptor.  This method will
 *   enqueue the transfer request and return immediately.  Only one transfer may be
 *   queued;.
 *
 *   This is a blocking method; this functions will not return until the
 *   transfer has completed.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *   ep - The IN or OUT endpoint descriptor for the device endpoint on which to
 *      perform the transfer.
 *   buffer - A buffer containing the data to be sent (OUT endpoint) or received
 *     (IN endpoint).  buffer must have been allocated using DRVR_ALLOC
 *   buflen - The length of the data to be sent or received.
 *
 * Returned Values:
 *   On success, zero (OK) is returned. On a failure, a negated errno value is
 *   returned indicating the nature of the failure:
 *
 *     EAGAIN - If devices NAKs the transfer (or NYET or other error where
 *              it may be appropriate to restart the entire transaction).
 *     EPERM  - If the endpoint stalls
 *     EIO    - On a TX or data toggle error
 *     EPIPE  - Overrun errors
 *
 * Assumptions:
 *   - Only a single class bound to a single device is supported.
 *   - Called from a single thread so no mutual exclusion is required.
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static int sam_transfer(FAR struct usbhost_driver_s *drvr, usbhost_ep_t ep,
                         FAR uint8_t *buffer, size_t buflen)
{
  struct sam_rhport_s *rhport = (struct sam_rhport_s *)drvr;
  struct sam_epinfo_s *epinfo = (struct sam_epinfo_s *)ep;
  int ret;

  DEBUGASSERT(rhport && epinfo && buffer && buflen > 0);

  /* We must have exclusive access to the EHCI hardware and data structures. */

  sam_takesem(&g_ehci.exclsem);

  /* Perform the transfer */
#warning Missing logic
  ret = -ENOSYS;

  sam_givesem(&g_ehci.exclsem);
  return ret;
}

/*******************************************************************************
 * Name: sam_disconnect
 *
 * Description:
 *   Called by the class when an error occurs and driver has been disconnected.
 *   The USB host driver should discard the handle to the class instance (it is
 *   stale) and not attempt any further interaction with the class driver instance
 *   (until a new instance is received from the create() method).  The driver
 *   should not called the class' disconnected() method.
 *
 * Input Parameters:
 *   drvr - The USB host driver instance obtained as a parameter from the call to
 *      the class create() method.
 *
 * Returned Values:
 *   None
 *
 * Assumptions:
 *   - Only a single class bound to a single device is supported.
 *   - Never called from an interrupt handler.
 *
 *******************************************************************************/

static void sam_disconnect(FAR struct usbhost_driver_s *drvr)
{
  struct sam_rhport_s *rhport = (struct sam_rhport_s *)drvr;
  DEBUGASSERT(rhport);

  /* Remove the disconnected port */
#warning Missing logic

  /* Unbind the class */

  rhport->class = NULL;
}

/*******************************************************************************
 * Initialization
 *******************************************************************************/
/*******************************************************************************
 * Name: sam_reset
 *
 * Description:
 *   Set the HCRESET bit in the USBCMD register to reset the EHCI hardware.
 *
 *   Table 2-9. USBCMD � USB Command Register Bit Definitions
 *
 *    "Host Controller Reset (HCRESET) ... This control bit is used by software
 *     to reset the host controller. The effects of this on Root Hub registers
 *     are similar to a Chip Hardware Reset.
 *
 *    "When software writes a one to this bit, the Host Controller resets its
 *     internal pipelines, timers, counters, state machines, etc. to their
 *     initial value. Any transaction currently in progress on USB is
 *     immediately terminated. A USB reset is not driven on downstream
 *     ports.
 *
 *    "PCI Configuration registers are not affected by this reset. All
 *     operational registers, including port registers and port state machines
 *     are set to their initial values. Port ownership reverts to the companion
 *     host controller(s)... Software must reinitialize the host controller ...
 *     in order to return the host controller to an operational state.
 *
 *    "This bit is set to zero by the Host Controller when the reset process is
 *     complete. Software cannot terminate the reset process early by writing a
 *     zero to this register. Software should not set this bit to a one when
 *     the HCHalted bit in the USBSTS register is a zero. Attempting to reset
 *     an actively running host controller will result in undefined behavior."
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero (OK) is returned on success; A negated errno value is returned
 *   on failure.
 *
 * Assumptions:
 * - Called during the initializaation of the EHCI.
 *
 *******************************************************************************/

static int sam_reset(void)
{
  uint32_t regval;
  unsigned int timeout;

  /* "... Software should not set [HCRESET] to a one when the HCHalted bit in
   *  the USBSTS register is a zero. Attempting to reset an actively running
   *   host controller will result in undefined behavior."
   */

  sam_putreg(0, &HCOR->usbcmd);
  timeout = 0;
  do
    {
      /* Wait one microsecond and update the timeout counter */

      up_udelay(1);
      timeout++;

      /* Get the current valud of the USBSTS register.  This loop will terminate
       * when either the timeout exceeds one millisecond or when the HCHalted
       * bit is no longer set in the USBSTS register.
       */

      regval = sam_getreg(&HCOR->usbsts);
    }
  while (((regval & EHCI_USBSTS_HALTED) == 0) && (timeout < 1000));

  /* Is the EHCI still running?  Did we timeout? */

  if ((regval & EHCI_USBSTS_HALTED) == 0)
    {
      udbg("ERROR: Timed out waiting for HCHalted.  USBSTS: %08X", regval);
      return -ETIMEDOUT;
    }

  /* Now we can set the HCReset bit in the USBCMD register to initiate the reset */

  regval  = sam_getreg(&HCOR->usbcmd);
  regval |= EHCI_USBCMD_HCRESET;
  sam_putreg(regval, &HCOR->usbcmd);

  /* Wait for the HCReset bit to become clear */

  do
    {
      /* Wait five microsecondw and update the timeout counter */

      up_udelay(5);
      timeout += 5;

      /* Get the current valud of the USBCMD register.  This loop will terminate
       * when either the timeout exceeds one second or when the HCReset
       * bit is no longer set in the USBSTS register.
       */

      regval = sam_getreg(&HCOR->usbcmd);
    }
  while (((regval & EHCI_USBCMD_HCRESET) != 0) && (timeout < 1000000));

  /* Return either success or a timeout */

  return (regval & EHCI_USBCMD_HCRESET) != 0 ? -ETIMEDOUT : OK;
}

/*******************************************************************************
 * Global Functions
 *******************************************************************************/
/*******************************************************************************
 * Name: sam_ehci_initialize
 *
 * Description:
 *   Initialize USB EHCI host controller hardware.
 *
 * Input Parameters:
 *   controller -- If the device supports more than one EHCI interface, then
 *     this identifies which controller is being intialized.  Normally, this
 *     is just zero.
 *
 * Returned Value:
 *   And instance of the USB host interface.  The controlling task should
 *   use this interface to (1) call the wait() method to wait for a device
 *   to be connected, and (2) call the enumerate() method to bind the device
 *   to a class driver.
 *
 * Assumptions:
 * - This function should called in the initialization sequence in order
 *   to initialize the USB device functionality.
 * - Class drivers should be initialized prior to calling this function.
 *   Otherwise, there is a race condition if the device is already connected.
 *
 *******************************************************************************/

FAR struct usbhost_connection_s *sam_ehci_initialize(int controller)
{
  irqstate_t flags;
  uint32_t regval;
  int ret;
  int i;

  /* Sanity checks */

  DEBUGASSERT(controller == 0);

  /* SAMA5 Configuration *******************************************************/
  /* For High-speed operations, the user has to perform the following:
   *
   *   1) Enable UHP peripheral clock, bit (1 << AT91C_ID_UHPHS) in
   *      PMC_PCER register.
   *   2) Write CKGR_PLLCOUNT field in PMC_UCKR register.
   *   3) Enable UPLL, bit AT91C_CKGR_UPLLEN in PMC_UCKR register.
   *   4) Wait until UTMI_PLL is locked. LOCKU bit in PMC_SR register
   *   5) Enable BIAS, bit AT91C_CKGR_BIASEN in PMC_UCKR register.
   *   6) Select UPLLCK as Input clock of OHCI part, USBS bit in PMC_USB
   *      register.
   *   7) Program the OHCI clocks (UHP48M and UHP12M) with USBDIV field in
   *      PMC_USB register. USBDIV must be 9 (division by 10) if UPLLCK is
   *      selected.
   *   8) Enable OHCI clocks, UHP bit in PMC_SCER register.
   *
   * Steps 1 and 8 are performed here.  Steps 2 through 7 were are performed
   * by sam_clockconfig() earlier in the boot sequence.
   */

  /* Enable UHP peripheral clocking */

  flags = irqsave();
  sam_uhphs_enableclk();

  /* Enable OHCI clocks */

  regval = sam_getreg((volatile uint32_t *)SAM_PMC_SCER);
  regval |= PMC_UHP;
  sam_putreg(regval, (volatile uint32_t *)SAM_PMC_SCER);
  irqrestore(flags);

  /* Note that no pin pinconfiguration is required.  All USB HS pins have
   * dedicated function
   */

  /* Software Configuration ****************************************************/

  uvdbg("Initializing EHCI Stack\n");

  /* Initialize the EHCI state data structure */

  sem_init(&g_ehci.exclsem, 0, 1);
  sem_init(&g_ehci.rhsem,  0, 0);

  /* Initialize EP0 */

  sem_init(&g_ehci.ep0.wsem, 0, 1);

  /* Initialize the root hub port structures */

  for (i = 0; i < SAM_EHCI_NRHPORT; i++)
    {
      struct sam_rhport_s *rhport = &g_ehci.rhport[i];
      rhport->rhpndx              = i;

      /* Initialize the device operations */

      rhport->drvr.ep0configure   = sam_ep0configure;
      rhport->drvr.epalloc        = sam_epalloc;
      rhport->drvr.epfree         = sam_epfree;
      rhport->drvr.alloc          = sam_alloc;
      rhport->drvr.free           = sam_free;
      rhport->drvr.ioalloc        = sam_ioalloc;
      rhport->drvr.iofree         = sam_iofree;
      rhport->drvr.ctrlin         = sam_ctrlin;
      rhport->drvr.ctrlout        = sam_ctrlout;
      rhport->drvr.transfer       = sam_transfer;
      rhport->drvr.disconnect     = sam_disconnect;
    }

  /* Initialize the list of free Queue Head (QH) structures */

  for (i = 0; i < CONFIG_SAMA5_EHCI_NQHS; i++)
    {
      /* Put the QH structure in a free list */

      sam_qh_free(&g_ghpool[i]);
    }

  /* Initialize the list of free Queue Head (QH) structures */

  for (i = 0; i < CONFIG_SAMA5_EHCI_NQTDS; i++)
    {
      /* Put the TD in a free list */

      sam_qtd_free(&g_qtdpool[i]);
    }

  /* EHCI Hardware Configuration ***********************************************/

  /* Reset the EHCI hardware */

  ret = sam_reset();
  if (ret < 0)
    {
      udbg("ERROR: sam_reset failed: %d\n", ret);
      return NULL;
    }

#warning Missing logic

  /* Interrupt Configuration ***************************************************/

  /* Clear pending interrupts */
#warning Missing logic

  /* Enable EHCI interrupts */
#warning Missing logic

  /* Attach USB host controller interrupt handler */

  if (irq_attach(SAM_IRQ_UHPHS, sam_ehci_interrupt) != 0)
    {
      udbg("ERROR: Failed to attach IRQ\n");
      return NULL;
    }

  /* Drive Vbus +5V (the smoke test).  Should be done elsewhere in OTG
   * mode.
   */

  sam_usbhost_vbusdrive(SAM_EHCI_IFACE, true);
  up_mdelay(50);

  /* If there is a USB device in the slot at power up, then we will not
   * get the status change interrupt to signal us that the device is
   * connected.  We need to set the initial connected state accordingly.
   */

  for (i = 0; i < SAM_EHCI_NRHPORT; i++)
    {
#warning Missing logic
    }

  /* Enable interrupts at the interrupt controller */

  up_enable_irq(SAM_IRQ_UHPHS); /* enable USB interrupt */
  uvdbg("USB EHCI Initialized\n");

  /* Initialize and return the connection interface */

  g_ehciconn.wait      = sam_wait;
  g_ehciconn.enumerate = sam_enumerate;
  return &g_ehciconn;
}

#endif /* CONFIG_SAMA5_EHCI */

/*
 * PEB20534 registers
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Id: peb20534.h,v 1.9 2007-01-24 14:17:05 ly Exp $
 */

#define PEB_VENDOR_ID   0x110a
#define PEB_DEVICE_ID   0x2102

/*
 * Global registers.
 */
#define PEB_GCMDR   0x00    /* global command */
#define PEB_GSTAR   0x04    /* global status */
#define PEB_GMODE   0x08    /* global mode */
#define PEB_IQLENR0 0x0c    /* interrupt queue length 1 */
#define PEB_IQLENR1 0x10    /* interrupt queue length 2 */
#define PEB_IQCFGBAR    0x3c    /* interrupt queue cfg base address */
#define PEB_IQPBAR  0x40    /* interrupt queue peripheral base address */
#define PEB_FIFOCR1 0x44    /* fifo control 1 */
#define PEB_FIFOCR2 0x48    /* fifo control 2 */
#define PEB_FIFOCR3 0x4c    /* fifo control 3 */
#define PEB_FIFOCR4 0x34    /* fifo control 4 (rev 2.0) */

#define PEB_0RXBAR  0x14    /* channel RX base address */
#define PEB_1RXBAR  0x18
#define PEB_2RXBAR  0x1c
#define PEB_3RXBAR  0x20
#define PEB_0TXBAR  0x24    /* channel TX base address */
#define PEB_1TXBAR  0x28
#define PEB_2TXBAR  0x2c
#define PEB_3TXBAR  0x30
#define PEB_0CFG    0x50    /* channel configuration */
#define PEB_1CFG    0x5c
#define PEB_2CFG    0x68
#define PEB_3CFG    0x74
#define PEB_0BRDA   0x54    /* channel base RX descr address */
#define PEB_1BRDA   0x60
#define PEB_2BRDA   0x6c
#define PEB_3BRDA   0x78
#define PEB_0BTDA   0x58    /* channel base TX descr address */
#define PEB_1BTDA   0x64
#define PEB_2BTDA   0x70
#define PEB_3BTDA   0x7c
#define PEB_0FRDA   0x98    /* channel first RX descr address */
#define PEB_1FRDA   0x9c
#define PEB_2FRDA   0xa0
#define PEB_3FRDA   0xa4
#define PEB_0FTDA   0xb0    /* channel first TX descr address */
#define PEB_1FTDA   0xb4
#define PEB_2FTDA   0xb8
#define PEB_3FTDA   0xbc
#define PEB_0LRDA   0xc8    /* channel last RX descr address */
#define PEB_1LRDA   0xcc
#define PEB_2LRDA   0xd0
#define PEB_3LRDA   0xd4
#define PEB_0LTDA   0xe0    /* channel last TX descr address */
#define PEB_1LTDA   0xe4
#define PEB_2LTDA   0xe8
#define PEB_3LTDA   0xec

/*
 * Serial communication controller registers.
 */
#define PEB_CMDR    0x00    /* command */
#define PEB_STAR    0x04    /* status */
#define PEB_CCR0    0x08    /* channel configuration 0 */
#define PEB_CCR1    0x0c    /* channel configuration 1 */
#define PEB_CCR2    0x10    /* channel configuration 2 */
#define PEB_ACCM    0x14    /* async control character map */
#define PEB_UDAC    0x18    /* user defined async character */
#define PEB_TSAX    0x1c    /* timeslot assignment Tx */
#define PEB_TSAR    0x20    /* timeslot assignment Rx */
#define PEB_PCMMTX  0x24    /* PCM mask Tx */
#define PEB_PCMMRX  0x28    /* PCM mask Rx */
#define PEB_BRR     0x2c    /* baud rate */
#define PEB_TIMR    0x30    /* timer */
#define PEB_XADR    0x34    /* transmit address */
#define PEB_RADR    0x38    /* receive address */
#define PEB_RAMR    0x3c    /* receive address mask */
#define PEB_RLCR    0x40    /* receive length check */
#define PEB_XNXFR   0x44    /* xon/xoff */
#define PEB_TCR     0x48    /* termination character */
#define PEB_TICR    0x4c    /* Tx immediate character */
#define PEB_SYNCR   0x50    /* synchronization character */
#define PEB_IMR     0x54    /* interrupt mask */
#define PEB_ISR     0x58    /* interrupt status */

/*
 * Peripheral registers.
 */
#define PEB_LCONF   0x300   /* LBI configuration */
#define PEB_SSCCON  0x380   /* SSC control */
#define PEB_SSCBR   0x384   /* SSC baud rate generator */
#define PEB_SSCTB   0x388   /* SSC tx buffer */
#define PEB_SSCRB   0x38c   /* SSC rx buffer */
#define PEB_SSCCSE  0x390   /* SSC chip select enable */
#define PEB_SSCIM   0x394   /* SSC interrupt mask */
#define PEB_GPDIR   0x400   /* general purpose bus direction */
#define PEB_GPDATA  0x404   /* general purpose bus data */
#define PEB_GPIM    0x408   /* general purpose bus interrupt mask */

/*
 * GSTAR register.
 */
#define GSTAR_ARACK 0x00000001ul    /* action request acknowledged */
#define GSTAR_ARF   0x00000002ul    /* action request failed */
#define GSTAR_PLBI  0x00040000ul    /* lbi interrupt */
#define GSTAR_PSSC  0x00080000ul    /* ssc interrupt */
#define GSTAR_CFG   0x00200000ul    /* cfg interrupt */
#define GSTAR_TX0   0x01000000ul    /* transmitter 0 */
#define GSTAR_TX1   0x02000000ul    /* transmitter 1 */
#define GSTAR_TX2   0x04000000ul    /* transmitter 2 */
#define GSTAR_TX3   0x08000000ul    /* transmitter 3 */
#define GSTAR_RX0   0x10000000ul    /* receiver 0 */
#define GSTAR_RX1   0x20000000ul    /* receiver 1 */
#define GSTAR_RX2   0x40000000ul    /* receiver 2 */
#define GSTAR_RX3   0x80000000ul    /* receiver 3 */

/*
 * ISR register and interrupt vectors.
 */
#define ISR_LBI     0xd0000000ul    /* lbi source */
#define ISR_TX      0x40000000ul    /* source id: 0-rx, 1-tx */
#define ISR_SCC     0x02000000ul    /* vector block id: 0-dma, 1-scc */
#define ISR_HI      0x00040000ul    /* host initiated interrupt */
#define ISR_FI      0x00020000ul    /* frame indication */
#define ISR_ERR     0x00010000ul    /* dma error */

#define ISR_ALLS    0x00040000ul    /* all sent */
#define ISR_XDU     0x00010000ul    /* transmit data underrun */
#define ISR_TIN     0x00008000ul    /* timer interrupt */
#define ISR_CSC     0x00004000ul    /* cts change */
#define ISR_XMR     0x00002000ul    /* transmit message repeat */
#define ISR_XPR     0x00001000ul    /* transmit pool ready */
#define ISR_RDO     0x00000080ul    /* receive data overflow */
#define ISR_RFS     0x00000040ul    /* receive frame start */
#define ISR_RSC     0x00000020ul    /* receive status change */
#define ISR_PCE     0x00000010ul    /* protocol error */
#define ISR_PLLA    0x00000008ul    /* dpll sync loss */
#define ISR_CDSC    0x00000004ul    /* dcd change */
#define ISR_RFO     0x00000002ul    /* receive frame overflow */
#define ISR_FLEX    0x00000001ul    /* frame length exceeded */

/*
 * Receive status information (HDLC).
 */
#define RSTA_VFR    0x80    /* valid frame */
#define RSTA_RDO    0x40    /* receive data overflow */
#define RSTA_CRC    0x20    /* crc ok */
#define RSTA_RAB    0x10    /* receive message aborted */
#define RSTA_HA1    0x08    /* high byte address compare */
#define RSTA_HA0    0x04    /* high byte address compare */
#define RSTA_CR     0x02    /* command/response */
#define RSTA_LA     0x01    /* low byte address compare */

/*
 * Channel configuration register (CFGi).
 */
#define CFG_RFI     0x08000000ul    /* receive frame indication disable */
#define CFG_TFI     0x04000000ul    /* transmit frame indication disable */
#define CFG_RERR    0x02000000ul    /* receive error disable */
#define CFG_TERR    0x01000000ul    /* transmit error disable */
#define CFG_RDR     0x00400000ul    /* reset dma receiver */
#define CFG_RDT     0x00200000ul    /* reset dma transmitter */
#define CFG_IDR     0x00100000ul    /* initialize dma receiver */
#define CFG_IDT     0x00080000ul    /* initialize dma transmitter */

/*
 * Command register (CMDR).
 */
#define CMDR_XRES   0x01000000ul    /* transmitter reset */
#define CMDR_RFRD   0x00020000ul    /* receiver reset */
#define CMDR_RRES   0x00010000ul    /* receiver reset */
#define CMDR_STI    0x00000200ul    /* start timer */
#define CMDR_RNR    0x00000001ul    /* receiver not ready (automode) */

/*
 * I/O descriptors bits
 */
#define DESC_FE     0x80000000ul
#define DESC_HOLD   0x40000000ul
#define DESC_HI     0x20000000ul
#define DESC_LEN(v) (((v) >> 16) & 0x1ffful)
#define DESC_RA     0x00000200ul
#define DESC_C      0x40000000ul

/* CCR2 */
#define CCR2_XBRK   0x02000000ul
#define CCR2_RENAB  0x08000000ul

/* CCR0 */
#define CCR0_INTVIS 0x00001000ul
#define CCR0_POWER  0x80000000ul
#define CCR0_NRZI   0x00200000ul

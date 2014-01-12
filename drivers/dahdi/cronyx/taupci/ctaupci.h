/*
 * Defines for Cronyx Tau-PCI adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Id: ctaupci.h,v 1.11 2007-02-09 10:52:05 ly Exp $
 */

/*
 * Basic TauPCI model.
 */
#define BCR     0   /* board control, write only */
#define BSR     0   /* board status, read only */
#define TCK     1   /* firmware load clock, write only */
#define BCCR0       4   /* channel 0 control, write only */
#define BCSR0       4   /* channel 0 status, read only */
#define BCCR1       5   /* channel 1 control, write only */
#define BCSR1       5   /* channel 1 status, read only */
#define BCCR2       6   /* channel 2 control, write only */
#define BCSR2       6   /* channel 2 status, read only */
#define BCCR3       7   /* channel 3 control, write only */
#define BCSR3       7   /* channel 3 status, read only */

/*
 * Board status register bits.
 */
#define BSR_LERR    0x01    /* firmware download error signal */
#define BSR_MBUSY   0x01    /* timeslot mask memory busy */

#define BSR_JOSC_MASK   0x06    /* oscillator frequency */
#define BSR_JOSC_STS1   0x02    /* STS-1 51840 kHz */
#define BSR_JOSC_16 0x04    /* 16384 kHz */
#define BSR_JOSC_T3 0x04    /* T3 44736 kHz */
#define BSR_JOSC_32 0x06    /* 32768 kHz */
#define BSR_JOSC_E3 0x06    /* E3 34368 kHz */

#define BSR_BT_MASK 0x38    /* adapter model */
#define BSR_BT_SERIAL   0x38    /* V.35/RS-530/RS-232 */
#define BSR_BT_HSSI 0x30    /* 1 x HSSI */
#define BSR_BT_E3   0x28    /* 1 x E3 */
#define BSR_BT_4E1G 0x20    /* 4 x E1/G703 */
#define BSR_BT_2E1G 0x18    /* 2 x E1/G703 */
#define BSR_BT_2G703    0x10    /* 2 x G.703 */
#define BSR_BT_2E1  0x08    /* 2 x E1 */

#define BSR_CH23    0x40    /* extension board attached */
#define BSR_BID     0x80    /* blinking identification bit */

/*
 * Channel status register bits.
 */
#define BCSR_DSR    0x01    /* DSR from channel */

#define BCSR_IFT_MASK   0x46    /* interface type */
#define BCSR_IFT_NOCBL  0x46    /* no cable */
#define BCSR_IFT_RS232  0x06    /* RS-232 */
#define BCSR_IFT_V35    0x04    /* V.35 */
#define BCSR_IFT_RS530  0x02    /* RS-530/RS-449 */
#define BCSR_IFT_RS485  0x0a    /* RS-485 (bus) */
#define BCSR_IFT_X21    0x00    /* X.21 */

#define BCSR_CTS    0x08    /* CTS from channel */
#define BCSR_TXCERR 0x10    /* no transmit clock signal */
#define BCSR_RXCERR 0x20    /* no receive clock signal */

/*
 * Board control register bits.
 */
#define BCR_TMS     0x01    /* firmware download TMS signal */
#define BCR_TDI     0x02    /* firmware download TDI signal */
#define BCR_UBRUN   0x04    /* G.703 interface enable flag */
#define BCR_LED     0x10    /* LED control */
#define BCR_RST     0x80    /* PCI reset, min 12 usec */

/*
 * Channel control register bits.
 */
#define BCCR_DTR    0x01    /* DTR to channel */
#define BCCR_TINV   0x02    /* invert TXC */
#define BCCR_TDIR   0x04    /* external clock */
#define BCCR_RINV   0x08    /* invert RXC */
#define BCCR_ASYNC  0x10    /* disable clock control */
#define BCCR_DIS    0x80    /* alternative interface */

/*------------------------------------------------------------
 * Tau/E1 model.
 */
#define E1CFG       0x08    /* adapter control, write only */
#define E1SR        0x08    /* status, read only */
#define E1CR0       0x0c    /* channel control 0, write only */
#define E1CR1       0x0e    /* channel control 1, write only */
#define E1CR2       0x0d    /* channel control 2, write only */
#define E1CR3       0x0f    /* channel control 3, write only */
#define E1CS0       0x20    /* chip select 0, write only */
#define E1CS1       0x22    /* chip select 1, write only */
#define E1CS2       0x30    /* chip select 2, write only */
#define E1CS3       0x32    /* chip select 3, write only */
#define E1DAT       0x26    /* selected chip read/write */

/* Tau-PCI/4E1 and 2E1 model. */
#define E4MD0       0x10    /* mode 0, write only */
#define E4MD1       0x12    /* mode 1, write only */
#define E4MD2       0x11    /* mode 2, write only */
#define E4MD3       0x13    /* mode 3, write only */
#define E4EPS0      0x14    /* ext port select 0, write only */
#define E4EPS1      0x15    /* ext port select 1, write only */
#define E4EPS2      0x16    /* ext port select 2, write only */
#define E4EPS3      0x17    /* ext port select 3, write only */
#define E4CS0       0x18    /* chip select 0, write only */
#define E4CS1       0x1a    /* chip select 1, write only */
#define E4CS2       0x19    /* chip select 2, write only */
#define E4CS3       0x1b    /* chip select 3, write only */
#define E4DAT       0x1e    /* selected chip read/write */
#define E4TSAR      0x09    /* timeslot mask address */
#define E4TSMEM     0x0a    /* timeslot mask memory */
#define E4CMAR      0x1c    /* cross-connector address */
#define E4TLOSR     0x0b    /* cross-connector (ts-loss code) */
#define E4CLOSR     0x1d    /* cross-connector (cas-loss code) */
#define E4CMEM      0x1f    /* cross-connector memory */

#define CFG_MUX     0x01    /* multiplexor */
#define CFG_CMSWAP  0x02    /* swap cross-connection matrix */
#define SR_CMBUSY   0x01    /* cross-connection swap pending */

/*
 * Tau/E1 channel control register bits.
 */
#define E1CR_CLK_INT    0x00    /* transmit clock - internal */
#define E1CR_CLK_RCV0   0x01    /* transmit clock - RCLK0 */
#define E1CR_CLK_RCV1   0x02    /* transmit clock - RCLK1 */
#define E1CR_CLK_RCV2   0x03    /* transmit clock - RCLK2 */
#define E1CR_CLK_RCV3   0x04    /* transmit clock - RCLK3 */
#define E1CR_CLK_MASK   0x07    /* transmit clock mask */

#define E1CR_LLOOP  0x10    /* enable data, ignore los */
#define E1CR_GRAW   0x20    /* raw G.703 mode enable */
#define E1CR_PHONY  0x40    /* PCM mode enable */
#define E1CR_GRUN   0x80    /* global run flag */

/*
 * Tau/E1 status register bits.
 */
#define E1SR_IRQ0   0x01    /* channel 0 E1 interrupt */
#define E1SR_IRQ1   0x02    /* channel 1 E1 interrupt */
#define E1SR_IRQ2   0x04    /* channel 2 E1 interrupt */
#define E1SR_IRQ3   0x08    /* channel 3 E1 interrupt */
#define E1SR_TP0    0x10    /* twisted pair on channel 0 */
#define E1SR_TP1    0x20    /* twisted pair on channel 1 */
#define E1SR_TP2    0x40    /* twisted pair on channel 2 */
#define E1SR_TP3    0x80    /* twisted pair on channel 3 */

/*------------------------------------------------------------
 * Tau/G.703 model.
 */
#define GMD0        0x20    /* mode 0, write only */
#define GMD1        0x22    /* mode 1, write only */
#define GMD2        0x30    /* mode 2, write only */
#define GMD3        0x32    /* mode 3, write only */
#define GLS0        0x24    /* line status 0, read/write */
#define GLS1        0x26    /* line status 1, read/write */
#define GLS2        0x34    /* line status 2, read/write */
#define GLS3        0x36    /* line status 3, read/write */
#define GSIO        0x28    /* serial i/o, write only */

/*
 * Tau/G.703 mode register 0/1 bits.
 */
#define GMD_2048    0x00    /* 2048 kbit/sec */
#define GMD_1024    0x02    /* 1024 kbit/sec */
#define GMD_512     0x03    /* 512 kbit/sec */
#define GMD_256     0x04    /* 256 kbit/sec */
#define GMD_128     0x05    /* 128 kbit/sec */
#define GMD_64      0x06    /* 64 kbit/sec */
#define GMD_RATE_MASK   0x07    /* rate mask */

#define GMD_RSYNC   0x08    /* receive synchronization */
#define GMD_SCR     0x10    /* scrambler enable (mode PCM2D) */

#define GMD_RENABLE 0x00    /* normal mode, auto remote loop enabled */
#define GMD_RDISABLE    0x40    /* normal mode, auto remote loop disabled */
#define GMD_RREFUSE 0x80    /* send the remote loop request sequence */
#define GMD_RREQUEST    0xc0    /* send the remote loop refuse sequence */
#define GMD_LCR_MASK    0xc0    /* remote loop control mask */

/*
 * Tau/G.703 line status register 0/1 bits.
 */
#define GLS_BPV     0x01    /* bipolar violation */
#define GLS_ERR     0x02    /* test error */

#define GLS_DB0     0x00    /* level 0.0 dB */
#define GLS_DB95    0x04    /* level -9.5 dB */
#define GLS_DB195   0x08    /* level -19.5 dB */
#define GLS_DB285   0x0c    /* level -28.5 dB */
#define GLS_DBMASK  0x0c    /* signal level mask */

#define GLS_LREQ    0x10    /* channel 0 remote loop request */
#define GLS_CD      0x20    /* alternative signal CD for phony mode */
#define GLS_SDO     0x80    /* chip 0 serial data output */

/*
 * Tau/G.703 serial i/o register bits.
 */
#define GSIO_SDI    0x04    /* serial data input */
#define GSIO_SCLK   0x08    /* serial data clock */
#define GSIO_CS0    0x10    /* chip select 0 */
#define GSIO_CS1    0x20    /* chip select 1 */
#define GSIO_CS2    0x40    /* chip select 2 */
#define GSIO_CS3    0x80    /* chip select 3 */

/*
 * Tau-PCI/2E1(4E1) EPS register bits.
 */
#define E1EPS_PTSM  0x04    /* framed data channel */

/*------------------------------------------------------------
 * Tau-PCI/E3 model.
 */
#define E3CR0       0x08    /* configuration register 0 */
#define E3CR1       0x09    /* configuration register 1 */
#define E3SR0       0x09    /* status register 0 */
#define E3TSTR      0x0A    /* data path test register */
#define E3ER0       0x0B    /* ER byte 0 (r/w) */
#define E3ER1       0x0C    /* ER byte 1 */
#define E3ER2       0x0D    /* ER byte 2 */
#define E3ELR0      0x0E    /* ELR Byte 0 */
#define E3ELR1      0x0F    /* ELR Byte 1 */

/*
 * Tau-PCI/E3 configuration register 0.
 */
#define E3CR0_BRDEN 0x01    /* DS3150 unlock */
#define E3CR0_SYNC  0x02    /* E3 receive clock */
#define E3CR0_MONEN 0x04    /* monitoring mode */
#define E3CR0_LBO   0x08    /* long cable */
#define E3CR0_NORMAL    0x00    /* normal mode */
#define E3CR0_AISE3 0x10    /* E3 AIS */
#define E3CR0_AIST3 0x20    /* T3 AIS */
#define E3CR0_BER   0x30    /* BER teser */
#define E3CR0_LLOOP 0x40    /* local loop */
#define E3CR0_RLOOP 0x80    /* remote loop */

/*
 * Tau-PCI/E3 configuration register 1.
 */
#define E3CR1_SRCLR 0x01    /* clear status register (sr0) */

/*
 * Tau-PCI/E3 status register 0.
 */
#define E3SR0_DM    0x01    /* transmit error */
#define E3SR0_LOS   0x02    /* lost of synchronization */
#define E3SR0_AIS   0x04    /* AIS signal detected */

/*------------------------------------------------------------
 * Siemens PEB20534 controller.
 */

/*
 * Global registers.
 */
#define GCMDR(b)    (b)->base[PEB_GCMDR/4]
#define GSTAR(b)    (b)->base[PEB_GSTAR/4]
#define GMODE(b)    (b)->base[PEB_GMODE/4]
#define IQLENR0(b)  (b)->base[PEB_IQLENR0/4]
#define IQLENR1(b)  (b)->base[PEB_IQLENR1/4]
#define IQCFGBAR(b) (b)->base[PEB_IQCFGBAR/4]
#define IQPBAR(b)   (b)->base[PEB_IQPBAR/4]
#define FIFOCR1(b)  (b)->base[PEB_FIFOCR1/4]
#define FIFOCR2(b)  (b)->base[PEB_FIFOCR2/4]
#define FIFOCR3(b)  (b)->base[PEB_FIFOCR3/4]
#define FIFOCR4(b)  (b)->base[PEB_FIFOCR4/4]

#define RXBAR(c)    (c)->core_regs1[PEB_0RXBAR/4]
#define TXBAR(c)    (c)->core_regs1[PEB_0TXBAR/4]
#define CFG(c)      (c)->core_regs2[PEB_0CFG/4]
#define BRDA(c)     (c)->core_regs2[PEB_0BRDA/4]
#define FRDA(c)     (c)->core_regs1[PEB_0FRDA/4]
#define LRDA(c)     (c)->core_regs1[PEB_0LRDA/4]
#define BTDA(c)     (c)->core_regs2[PEB_0BTDA/4]
#define FTDA(c)     (c)->core_regs1[PEB_0FTDA/4]
#define LTDA(c)     (c)->core_regs1[PEB_0LTDA/4]

/*
 * Serial communication controller registers.
 */
#define CMDR(c)     (c)->scc_regs[PEB_CMDR/4]
#define STAR(c)     (c)->scc_regs[PEB_STAR/4]
#define CCR0(c)     (c)->scc_regs[PEB_CCR0/4]
#define CCR1(c)     (c)->scc_regs[PEB_CCR1/4]
#define CCR2(c)     (c)->scc_regs[PEB_CCR2/4]
#define BRR(c)      (c)->scc_regs[PEB_BRR/4]
#define TIMR(c)     (c)->scc_regs[PEB_TIMR/4]
#define RLCR(c)     (c)->scc_regs[PEB_RLCR/4]
#define SCC_IMR(c)  (c)->scc_regs[PEB_IMR/4]
#define ISR(c)      (c)->scc_regs[PEB_ISR/4]
#define TSAX(c)     (c)->scc_regs[PEB_TSAX/4]
#define TSAR(c)     (c)->scc_regs[PEB_TSAR/4]

/*
 * Peripheral registers.
 */
#define LCONF(b)    (b)->base[PEB_LCONF/4]
#define GPDIR(b)    (b)->base[PEB_GPDIR/4]
#define GPDATA(b)   (b)->base[PEB_GPDATA/4]
#define GPIM(b)     (b)->base[PEB_GPIM/4]

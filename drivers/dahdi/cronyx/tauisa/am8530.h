/*
 * AMD Am83C30 serial communication controller registers.
 *
 * Copyright (C) 1996-2013 Cronyx Telecom (www.cronyx.ru, info@cronyx.ru)
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: am8530.h,v 1.2 2006-05-31 15:19:21 ly Exp $
 */

/*
 * Read/write registers.
 */
#define AM_IVR		2u	/* rw2 - interrupt vector register */
#define AM_DAT		8u	/* rw8 - data buffer register */
#define AM_TCL		12u	/* rw12 - time constant low */
#define AM_TCH		13u	/* rw13 - time constant high */
#define AM_SICR		15u	/* rw15 - status interrupt control reg */

/*
 * Write only registers.
 */
#define AM_CR		0u	/* w0 - command register */
#define AM_IMR		1u	/* w1 - interrupt mode register */
#define AM_RCR		3u	/* w3 - receive control register */
#define AM_PMR		4u	/* w4 - tx/rx parameters and modes reg */
#define AM_TCR		5u	/* w5 - transmit control register */
#define AM_SAF		6u	/* w6 - sync address field */
#define AM_SFR		7u	/* w7 - sync flag register */
#define AM_MICR		9u	/* w9 - master interrupt control reg */
#define AM_MCR		10u	/* w10 - misc control register */
#define AM_CMR		11u	/* w11 - clock mode register */
#define AM_BCR		14u	/* w14 - baud rate control register */

/*
 * Read only registers.
 */
#define AM_SR		0u	/* r0 - status register */
#define AM_RSR		1u	/* r1 - receive status register */
#define AM_IPR		3u	/* r3 - interrupt pending register */
#define AM_MSR		10u	/* r10 - misc status register */

/*
 * Enhanced mode registers.
 * In enhanced mode registers PMR(w4), TCR(w5) become readable.
 */
#define AM_FBCL		6u	/* r6 - frame byte count low */
#define AM_FBCH		7u	/* r7 - frame byte count high */
#define AM_RCR_R	9u	/* r9 - read RCR(w3) */
#define AM_MCR_R	11u	/* r11 - read MCR(w10) */
#define AM_SFR_R	14u	/* r14 - read SFR(w7') */

#define AM_A		32u	/* channel A offset */

/*
 * Interrupt vector register
 */
#define IVR_A		0x08u	/* channel A status */
#define IVR_REASON	0x06u	/* interrupt reason mask */
#define IVR_TXRDY	0x00u	/* transmit buffer empty */
#define IVR_STATUS	0x02u	/* external status interrupt */
#define IVR_RX		0x04u	/* receive character available */
#define IVR_RXERR	0x06u	/* special receive condition */

/*
 * Interrupt mask register
 */
#define IMR_EXT		0x01u	/* ext interrupt enable */
#define IMR_TX		0x02u	/* ext interrupt enable */
#define IMR_PARITY	0x04u	/* ext interrupt enable */

#define IMR_RX_FIRST	0x08u	/* ext interrupt enable */
#define IMR_RX_ALL	0x10u	/* ext interrupt enable */
#define IMR_RX_ERR	0x18u	/* ext interrupt enable */

#define IMR_WD_RX	0x20u	/* wait/request follows receiver fifo */
#define IMR_WD_REQ	0x40u	/* wait/request function as request */
#define IMR_WD_ENABLE	0x80u	/* wait/request pin enable */

/*
 * Master interrupt control register
 */
#define MICR_VIS	0x01u	/* vector includes status */
#define MICR_NV		0x02u	/* no interrupt vector */
#define MICR_DLC	0x04u	/* disable lower chain */
#define MICR_MIE	0x08u	/* master interrupt enable */
#define MICR_HIGH	0x10u	/* status high */
#define MICR_NINTACK	0x20u	/* interrupt masking without INTACK */

#define MICR_RESET_A	0x80u	/* channel reset A */
#define MICR_RESET_B	0x40u	/* channel reset B */
#define MICR_RESET_HW	0xc0u	/* force hardware reset */

/*
 * Receive status register
 */
#define RSR_FRME	0x10u	/* framing error */
#define RSR_RXOVRN	0x20u	/* rx overrun error */

/*
 * Command register
 */
#define CR_RST_EXTINT	0x10u	/* reset external/status irq */
#define CR_TX_ABORT	0x18u	/* send abort (SDLC) */
#define CR_RX_NXTINT	0x20u	/* enable irq on next rx character */
#define CR_RST_TXINT	0x28u	/* reset tx irq pending */
#define CR_RST_ERROR	0x30u	/* error reset */
#define CR_RST_HIUS	0x38u	/* reset highest irq under service */

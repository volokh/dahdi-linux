/*
 * DDK for Cronyx Tau-ISA adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Copyright (C) 1999-2013 Cronyx Telecom (www.cronyx.ru, info@cronyx.ru).
 * Author: Serge Vakulenko <vak@cronyx.ru>
 * Author: Roman Kurakin <rik@cronyx.ru>
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 */

/*
 * Mode register 0 (MD0) bits.
 */
#define MD0_STOPB_1	0u	/* 1 stop bit */
#define MD0_STOPB_15	1u	/* 1.5 stop bits */
#define MD0_STOPB_2	2u	/* 2 stop bits */

#define MD0_MODE_ASYNC	 0u	/* asynchronous mode */
#define MD0_MODE_EXTSYNC 3u	/* external byte-sync mode */
#define MD0_MODE_HDLC	 4u	/* HDLC mode */

/*
 * Mode register 1 (MD1) bits.
 */
#define MD1_PAR_NO	0u	/* no parity */
#define MD1_PAR_CMD	1u	/* parity bit appended by command */
#define MD1_PAR_EVEN	2u	/* even parity */
#define MD1_PAR_ODD	3u	/* odd parity */

#define MD1_CLEN_8	0u	/* 8 bits/character */
#define MD1_CLEN_7	1u	/* 7 bits/character */
#define MD1_CLEN_6	2u	/* 6 bits/character */
#define MD1_CLEN_5	3u	/* 5 bits/character */

#define MD1_CLK_1	0u	/* 1/1 clock rate */
#define MD1_CLK_16	1u	/* 1/16 clock rate */
#define MD1_CLK_32	2u	/* 1/32 clock rate */
#define MD1_CLK_64	3u	/* 1/64 clock rate */

#define MD1_ADDR_NOCHK	0u	/* do not check address field */
#define MD1_ADDR_SNGLE1 1u	/* single address 1 */
#define MD1_ADDR_SNGLE2 2u	/* single address 2 */
#define MD1_ADDR_DUAL	3u	/* dual address */

/*
 * Mode register 2 (MD2) bits.
 */
#define MD2_FDX 	0u	/* full duplex communication */
#define MD2_RLOOP	1u	/* remote loopback (auto echo) */
#define MD2_LLOOP	3u	/* local+remote loopback */

#define MD2_DPLL_CLK_8	0u	/* x8 ADPLL clock rate */
#define MD2_DPLL_CLK_16 1u	/* x16 ADPLL clock rate */
#define MD2_DPLL_CLK_32 2u	/* x32 ADPLL clock rate */

#define MD2_ENCOD_NRZ	     0u	/* NRZ encoding */
#define MD2_ENCOD_NRZI	     1u	/* NRZI encoding */
#define MD2_ENCOD_MANCHESTER 4u	/* Manchester encoding */
#define MD2_ENCOD_FM0	     5u	/* FM0 encoding */
#define MD2_ENCOD_FM1	     6u	/* FM1 encoding */

/*
 * DMA priority control register (PCR) values.
 */
#define PCR_PRIO_0_1	0u	/* priority c0r > c0t > c1r > c1t */
#define PCR_PRIO_1_0	1u	/* priority c1r > c1t > c0r > c0t */
#define PCR_PRIO_RX_TX	2u	/* priority c0r > c1r > c0t > c1t */
#define PCR_PRIO_TX_RX	3u	/* priority c0t > c1t > c0r > c1r */
#define PCR_PRIO_ROTATE 4u	/* rotation priority -c0r-c0t-c1r-c1t- */

/*
 * Board control register 2 bits.
 */
#define BCR2_INVTXC0	0x10u	/* channel 0 invert transmit clock */
#define BCR2_INVTXC1	0x20u	/* channel 1 invert transmit clock */
#define BCR2_INVRXC0	0x40u	/* channel 0 invert receive clock */
#define BCR2_INVRXC1	0x80u	/* channel 1 invert receive clock */

#define BCR2_BUS_UNLIM	0x01u	/* unlimited DMA master burst length */
#define BCR2_BUS_RFST	0x02u	/* fast read cycle bus timing */
#define BCR2_BUS_WFST	0x04u	/* fast write cycle bus timing */

/*
 * Receive/transmit clock source register (RXS/TXS) bits - from hdc64570.h.
 */
#define CLK_MASK	  0x70u	/* RXC/TXC clock input mask */
#define CLK_LINE	  0x00u	/* RXC/TXC line input */
#define CLK_INT 	  0x40u	/* internal baud rate generator */

#define CLK_RXS_LINE_NS   0x20u	/* RXC line with noise suppression */
#define CLK_RXS_DPLL_INT  0x60u	/* ADPLL based on internal BRG */
#define CLK_RXS_DPLL_LINE 0x70u	/* ADPLL based on RXC line */

#define CLK_TXS_RECV	  0x60u	/* receive clock */

/*
 * Control register (CTL) bits - from hdc64570.h.
 */
#define CTL_RTS_INV	0x01u	/* RTS control bit (inverted) */
#define CTL_SYNCLD	0x04u	/* load SYN characters */
#define CTL_BRK 	0x08u	/* async: send break */
#define CTL_IDLE_MARK	0u	/* HDLC: when idle, transmit mark */
#define CTL_IDLE_PTRN	0x10u	/* HDLC: when idle, transmit an idle pattern */
#define CTL_UDRN_ABORT	0u	/* HDLC: on underrun - abort */
#define CTL_UDRN_FCS	0x20u	/* HDLC: on underrun - send FCS/flag */

#define B_NEXT(b)   (*(unsigned short*)(b).data)	/* next descriptor ptr */
#define B_PTR(b)    (*(unsigned long*) ((b).data+2))	/* ptr to data buffer */
#define B_LEN(b)    (*(unsigned short*)((b).data+6))	/* data buffer length */
#define B_STATUS(b) (*(unsigned short*)((b).data+8))	/* buf status, see FST */

static unsigned char cte_in (unsigned short base, unsigned char reg);
static void cte_out (unsigned short base, unsigned char reg, unsigned char val);
static unsigned char cte_ins (unsigned short base, unsigned char reg, unsigned char mask);
static unsigned char cte_in2 (unsigned short base, unsigned char reg);
static void cte_out2 (unsigned short base, unsigned char reg, unsigned char val);
static void ctg_outx (tauisa_chan_t * c, unsigned char reg, unsigned char val);
static unsigned char ctg_inx (tauisa_chan_t * c, unsigned char reg);
static unsigned char cte_in2d (tauisa_chan_t * c);
static void cte_out2d (tauisa_chan_t * c, unsigned char val);
static void cte_out2c (tauisa_chan_t * c, unsigned char val);

//static int scc_rx_check (tauisa_chan_t * c);
//static int scc_read (tauisa_chan_t * c, unsigned char *d, int len);
//static int scc_write (tauisa_chan_t * c, unsigned char *d, int len);
//static int scc_read_byte (tauisa_chan_t * c);
//static int scc_write_byte (tauisa_chan_t * c, unsigned char b);

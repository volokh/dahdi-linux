/*
 * Defines for Cronyx-Sigma adapter driver.
 *
 * Copyright (C) 1994-2001 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 *
 * Copyright (C) 1998-2003 Cronyx Engineering.
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * Copyright (C) 2006-2008 Cronyx Engineering.
 * Author: Leo Yuriev, <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: sigma-ddk.h,v 1.12 2009-08-31 12:21:09 ly Exp $
 */

#ifndef DDK_PREPROC
#	include "ddk-host.h"
#	include "ddk-arch.h"
#endif

#define SIGMA_NBRD	3	/* the max number of installed boards */
#define SIGMA_NPORT	32	/* the number of i/o ports per board */
#define SIGMA_DMABUFSZ	1600

/*
 * Asynchronous channel mode -------------------------------------------------
 */

/* Parity mode */
#define	SIGMA_PAR_NONE	0	/* no parity */
#define	SIGMA_PAR_EVEN	1	/* even parity */
#define	SIGMA_PAR_ODD	2	/* odd parity */
#define	SIGMA_PAR_0	3	/* force parity 0 */
#define	SIGMA_PAR_1	4	/* force parity 1 */

typedef struct {	/* async channel option register 1 */
	u8 charlen:4;	/* character length, 5..8 */
	u8 ignpar:1;	/* ignore parity */
	u8 parmode:2;	/* parity mode */
	u8 parity:1;	/* parity */
} sigma_cor1_async_t;

typedef struct {	/* async channel option register 2 */
	u8 dsrae:1;	/* DSR automatic enable */
	u8 ctsae:1;	/* CTS automatic enable */
	u8 rtsao:1;	/* RTS automatic output enable */
	u8 rlm:1;	/* remote loopback mode enable */
	u8 pad:1;
	u8 etc:1;	/* embedded transmitter cmd enable */
	u8 ixon:1;	/* in-band XON/XOFF enable */
	u8 ixany:1;	/* XON on any character */
} sigma_cor2_async_t;

typedef struct {	/* async channel option register 3 */
	u8 stopb:3;	/* stop bit length */
	u8 pad:1;
	u8 scde:1;	/* special char detection enable */
	u8 flowct:1;	/* flow control transparency mode */
	u8 rngde:1;	/* range detect enable */
	u8 escde:1;	/* extended spec. char detect enable */
} sigma_cor3_async_t;

typedef struct {	/* async channel option register 6 */
	u8 parerr:3;	/* parity/framing error actions */
	u8 brk:2;	/* action on break condition */
	u8 inlcr:1;	/* translate NL to CR on input */
	u8 icrnl:1;	/* translate CR to NL on input */
	u8 igncr:1;	/* discard CR on input */
} sigma_cor6_async_t;

typedef struct {	/* async channel option register 7 */
	u8 ocrnl:1;	/* translate CR to NL on output */
	u8 onlcr:1;	/* translate NL to CR on output */
	u8 pad:3;
	u8 fcerr:1;	/* process flow ctl err chars enable */
	u8 lnext:1;	/* LNext option enable */
	u8 istrip:1;	/* strip 8-bit on input */
} sigma_cor7_async_t;

typedef struct {	/* async channel options */
	sigma_cor1_async_t cor1;	/* channel option register 1 */
	sigma_cor2_async_t cor2;	/* channel option register 2 */
	sigma_cor3_async_t cor3;	/* option register 3 */
	sigma_cor6_async_t cor6;	/* channel option register 6 */
	sigma_cor7_async_t cor7;	/* channel option register 7 */
	u8 schr1;	/* special character register 1 (XON) */
	u8 schr2;	/* special character register 2 (XOFF) */
	u8 schr3;	/* special character register 3 */
	u8 schr4;	/* special character register 4 */
	u8 scrl;	/* special character range low */
	u8 scrh;	/* special character range high */
	u8 lnxt;	/* LNext character */
} sigma_opt_async_t;

/*
 * HDLC channel mode ---------------------------------------------------------
 */
/* Address field length option */
#define	SIGMA_AFLO_1OCT	0	/* address field is 1 octet in length */
#define	SIGMA_AFLO_2OCT	1	/* address field is 2 octet in length */

/* Clear detect for X.21 data transfer phase */
#define	SIGMA_CLRDET_DISABLE	0	/* clear detect disabled */
#define	SIGMA_CLRDET_ENABLE	1	/* clear detect enabled */

/* Addressing mode */
#define	SIGMA_ADMODE_NOADDR	0	/* no address */
#define	SIGMA_ADMODE_4_1	1	/* 4 * 1 byte */
#define	SIGMA_ADMODE_2_2	2	/* 2 * 2 byte */

/* FCS append */
#define	SIGMA_FCS_NOTPASS	0	/* receive CRC is not passed to the host */
#define	SIGMA_FCS_PASS	1	/* receive CRC is passed to the host */

/* CRC modes */
#define	SIGMA_CRC_INVERT	0	/* CRC is transmitted inverted (CRC V.41) */
#define	SIGMA_CRC_DONT_INVERT	1	/* CRC is not transmitted inverted (CRC-16) */

/* Send sync pattern */
#define	SIGMA_SYNC_00		0	/* send 00h as pad char (NRZI encoding) */
#define	SIGMA_SYNC_AA		1	/* send AAh (Manchester/NRZ encoding) */

/* FCS preset */
#define	SIGMA_FCSP_ONES	0	/* FCS is preset to all ones (CRC V.41) */
#define	SIGMA_FCSP_ZEROS	1	/* FCS is preset to all zeros (CRC-16) */

/* idle mode */
#define	SIGMA_IDLE_FLAG	0	/* idle in flag */
#define	SIGMA_IDLE_MARK	1	/* idle in mark */

/* CRC polynomial select */
#define	SIGMA_POLY_V41	0	/* x^16+x^12+x^5+1 (HDLC, preset to 1) */
#define	SIGMA_POLY_16	1	/* x^16+x^15+x^2+1 (bisync, preset to 0) */

typedef struct {	/* hdlc channel option register 1 */
	u8 ifflags:4;	/* number of inter-frame flags sent */
	u8 admode:2;	/* addressing mode */
	u8 clrdet:1;	/* clear detect for X.21 data transfer phase */
	u8 aflo:1;	/* address field length option */
} sigma_cor1_hdlc_t;

typedef struct {	/* hdlc channel option register 2 */
	u8 dsrae:1;	/* DSR automatic enable */
	u8 ctsae:1;	/* CTS automatic enable */
	u8 rtsao:1;	/* RTS automatic output enable */
	u8 zero1:1;
	u8 crcninv:1;	/* CRC invertion option */
	u8 zero2:1;
	u8 fcsapd:1;	/* FCS append */
	u8 zero3:1;
} sigma_cor2_hdlc_t;

typedef struct {	/* hdlc channel option register 3 */
	u8 padcnt:3;	/* pad character count */
	u8 idle:1;	/* idle mode */
	u8 nofcs:1;	/* FCS disable */
	u8 fcspre:1;	/* FCS preset */
	u8 syncpat:1;	/* send sync pattern */
	u8 sndpad:1;	/* send pad characters before flag enable */
} sigma_cor3_hdlc_t;

typedef struct {		/* hdlc channel options */
	sigma_cor1_hdlc_t cor1;	/* hdlc channel option register 1 */
	sigma_cor2_hdlc_t cor2;	/* hdlc channel option register 2 */
	sigma_cor3_hdlc_t cor3;	/* hdlc channel option register 3 */
	u8 rfar1;	/* receive frame address register 1 */
	u8 rfar2;	/* receive frame address register 2 */
	u8 rfar3;	/* receive frame address register 3 */
	u8 rfar4;	/* receive frame address register 4 */
	u8 cpsr;	/* CRC polynomial select */
} sigma_opt_hdlc_t;

/* Board type */
#define SIGMA_MODEL_OLD     0	/* old Sigmas */
#define SIGMA_MODEL_22      1	/* Sigma-22 */
#define SIGMA_MODEL_800     2	/* Sigma-800 */

/* Channel type */
#define SIGMA_TYPE_NONE		0	/* no channel */
#define SIGMA_TYPE_ASYNC	1	/* pure asynchronous RS-232 channel */
#define SIGMA_TYPE_SYNC_RS232	2	/* pure synchronous RS-232 channel */
#define SIGMA_TYPE_SYNC_V35	3	/* pure synchronous V.35 channel */
#define SIGMA_TYPE_SYNC_RS449	4	/* pure synchronous RS-449 channel */
#define SIGMA_TYPE_UNIV_RS232	5	/* sync/async RS-232 channel */
#define SIGMA_TYPE_UNIV_RS449	6	/* sync/async RS-232/RS-449 channel */
#define SIGMA_TYPE_UNIV_V35	7	/* sync/async RS-232/V.35 channel */
#define SIGMA_TYPE_UNIV_UNKNOWN	8	/* sync/async, unknown interface */

#define SIGMA_MODE_ASYNC         0	/* asynchronous mode */
#define SIGMA_MODE_HDLC          1	/* bit-sync mode (HDLC) */

/*
 * CD2400 channel state structure --------------------------------------------
 */

typedef struct {	/* channel option register 4 */
	u8 thr:4;	/* FIFO threshold */
	u8 pad:1;
	u8 cts_zd:1;	/* detect 1 to 0 transition on the CTS */
	u8 cd_zd:1;	/* detect 1 to 0 transition on the CD */
	u8 dsr_zd:1;	/* detect 1 to 0 transition on the DSR */
} sigma_cor4_t;

typedef struct {	/* channel option register 5 */
	u8 rx_thr:4;	/* receive flow control FIFO threshold */
	u8 pad:1;
	u8 cts_od:1;	/* detect 0 to 1 transition on the CTS */
	u8 cd_od:1;	/* detect 0 to 1 transition on the CD */
	u8 dsr_od:1;	/* detect 0 to 1 transition on the DSR */
} sigma_cor5_t;

typedef struct {	/* receive clock option register */
	u8 clk:3;	/* receive clock source */
	u8 encod:2;	/* signal encoding NRZ/NRZI/Manchester */
	u8 dpll:1;	/* DPLL enable */
	u8 pad:1;
	u8 tlval:1;	/* transmit line value */
} sigma_rcor_t;

typedef struct {	/* transmit clock option register */
	u8 pad1:1;
	u8 llm:1;	/* local loopback mode */
	u8 pad2:1;
	u8 ext1x:1;	/* external 1x clock mode */
	u8 pad3:1;
	u8 clk:3;	/* transmit clock source */
} sigma_tcor_t;

typedef struct {
	sigma_cor4_t cor4;	/* channel option register 4 */
	sigma_cor5_t cor5;	/* channel option register 5 */
	sigma_rcor_t rcor;	/* receive clock option register */
	sigma_tcor_t tcor;	/* transmit clock option register */
} sigma_chan_opt_t;

typedef enum {			/* line break mode */
	BRK_IDLE,		/* normal line mode */
	BRK_SEND,		/* start sending break */
	BRK_STOP		/* stop sending break */
} sigma_break_t;

#define SIGMA_BUS_NORMAL	0	/* normal bus timing */
#define SIGMA_BUS_FAST1		1	/* fast bus timing (Sigma-22 and -800) */
#define SIGMA_BUS_FAST2		2	/* fast bus timing (Sigma-800) */
#define SIGMA_BUS_FAST3		3	/* fast bus timing (Sigma-800) */

typedef struct {		/* board options */
	u8 fast;		/* bus master timing (Sigma-22 and -800) */
} sigma_board_opt_t;

#define SIGMA_NCHIP    4	/* the number of controllers per board */
#define SIGMA_NCHAN    16	/* the number of channels on the board */

typedef struct {
	u8 tbuffer[2][SIGMA_DMABUFSZ];
	u8 rbuffer[2][SIGMA_DMABUFSZ];
} sigma_buf_t;

typedef struct _sigma_chan_t {
	struct _sigma_board_t *board;	/* board pointer */
	u8 type;	/* channel type */
	u8 num;		/* channel number, 0..15 */
	unsigned port;	/* base port address */
	u32 oscfreq;	/* oscillator frequency in Hz */
	u32 rxbaud;	/* receiver speed */
	u32 txbaud;	/* transmitter speed */
	u8 mode;	/* channel mode */
	sigma_chan_opt_t opt;	/* common channel options */
	sigma_opt_async_t aopt;	/* async mode options */
	sigma_opt_hdlc_t hopt;	/* hdlc mode options */
	u8 *arbuf;	/* receiver A dma buffer */
	u8 *brbuf;	/* receiver B dma buffer */
	u8 *atbuf;	/* transmitter A dma buffer */
	u8 *btbuf;	/* transmitter B dma buffer */
	u32 arphys;	/* receiver A phys address */
	u32 brphys;	/* receiver B phys address */
	u32 atphys;	/* transmitter A phys address */
	u32 btphys;	/* transmitter B phys address */
	u8 dtr;	/* DTR signal value */
	u8 rts;	/* RTS signal value */

	u64 rintr;	/* receive interrupts */
	u64 tintr;	/* transmit interrupts */
	u64 ibytes;	/* input bytes */
	u64 ipkts;	/* input packets */
	u64 ierrs;	/* input errors */
	u64 obytes;	/* output bytes */
	u64 opkts;	/* output packets */
	u64 oerrs;	/* output errors */

	void *sys;
	int debug;
	void *attach[2];
	u8 *received_data;
	int received_len;
	int overflow;

	void (*call_on_rx) (struct _sigma_chan_t *, char *, int);
	void (*call_on_tx) (struct _sigma_chan_t *, void *, int);
	void (*call_on_err) (struct _sigma_chan_t *, int);

} sigma_chan_t;

typedef struct _sigma_board_t {
	u8 type;	/* board type */
	u8 num;	/* board number, 0..2 */
	unsigned port;	/* base board port, 0..3f0 */
	int irq;	/* irq {3 5 7 10 11 12 15} */
	u8 dma;	/* DMA request {5 6 7} */
	char name[32];	/* board version name */
	u8 nuniv;	/* number of universal channels */
	u8 nsync;	/* number of sync. channels */
	u8 nasync;	/* number of async. channels */
	u8 if0type;	/* chan0 interface RS-232/RS-449/V.35 */
	u8 if8type;	/* chan8 interface RS-232/RS-449/V.35 */
	u16 bcr0;	/* BCR0 image */
	u16 bcr0b;	/* BCR0b image */
	u16 bcr1;	/* BCR1 image */
	u16 bcr1b;	/* BCR1b image */
	sigma_board_opt_t opt;	/* board options */
	sigma_chan_t chan[SIGMA_NCHAN];	/* channel structures */
	void *sys;
} sigma_board_t;

extern u32 sigma_rxbaud, sigma_txbaud;
extern int sigma_univ_mode, sigma_sync_mode, sigma_iftype;

extern sigma_chan_opt_t chan_opt_dflt;	/* default mode-independent options */
extern sigma_opt_async_t opt_async_dflt;	/* default async options */
extern sigma_opt_hdlc_t opt_hdlc_dflt;	/* default hdlc options */
extern sigma_board_opt_t board_opt_dflt;	/* default board options */

typedef struct _sigma_dat_tst_t sigma_dat_tst_t;

int sigma_probe_board (unsigned port, int irq, int dma);
void sigma_init (sigma_board_t * b, int num, unsigned port, int irq, int dma);
void sigma_init_board (sigma_board_t * b, int num, unsigned port, int irq, int dma,
		       int chain, int rev, int osc, int mod, int rev2, int osc2, int mod2);
void sigma_init_22 (sigma_board_t * b, int num, unsigned port, int irq, int dma, int rev, int osc);
void sigma_init_800 (sigma_board_t * b, int num, unsigned port, int irq, int dma, int chain);
int sigma_download (unsigned port, const u8 *firmware, u32 bits, const sigma_dat_tst_t * tst);
int sigma_setup_board (sigma_board_t * b, const u8 *firmware, u32 bits, const sigma_dat_tst_t * tst);
void sigma_setup_chan (sigma_chan_t * c);
void sigma_update_chan (sigma_chan_t * c);
void sigma_set_dtr (sigma_chan_t * c, int on);
void sigma_set_rts (sigma_chan_t * c, int on);
void sigma_led (sigma_board_t * b, int on);
void sigma_cmd (unsigned base, int cmd);
void sigma_reinit_board (sigma_board_t * b);
int sigma_get_dsr (sigma_chan_t * c);
int sigma_get_cts (sigma_chan_t * c);
int sigma_get_cd (sigma_chan_t * c);
void sigma_clock (u32 hz, u32 ba, int *clk, int *div);

/* DDK errors */
#define SIGMA_ERR_FRAME	 1
#define SIGMA_ERR_CRC		 2
#define SIGMA_ERR_OVERRUN	 3
#define SIGMA_ERR_OVERFLOW	 4
#define SIGMA_ERR_UNDERRUN	 5
#define SIGMA_ERR_BREAK	 6

/* clock sources */
#define SIGMA_CLK_INT	 0
#define SIGMA_CLK_EXT	 6
#define SIGMA_CLK_RCV	 7
#define SIGMA_CLK_DPLL	 8
#define SIGMA_CLK_DPLL_EXT	 14

void sigma_probe_irq (sigma_board_t * b, int irq);
void sigma_int_handler (sigma_board_t * b);

int sigma_find (unsigned *board_ports);
int sigma_open_board (sigma_board_t * b, int num, unsigned port, int irq, int dma);
void sigma_close_board (sigma_board_t * b);

void sigma_start_chan (sigma_chan_t * c, sigma_buf_t * cb, u32 phys);

#define SIGMA_PORT_AUTO		-1
#define SIGMA_PORT_RS232	0
#define SIGMA_PORT_V35		1
#define SIGMA_PORT_RS449	2

void sigma_set_port (sigma_chan_t * c, int port_type);
int sigma_get_port (sigma_chan_t * c);

void sigma_enable_receive (sigma_chan_t * c, int on);
void sigma_enable_transmit (sigma_chan_t * c, int on);
int sigma_receive_enabled (sigma_chan_t * c);
int sigma_transmit_enabled (sigma_chan_t * c);

void sigma_set_baud (sigma_chan_t *, u32 baud);
int sigma_set_mode (sigma_chan_t * c, int mode);
void sigma_set_loop (sigma_chan_t * c, int on);
void sigma_set_nrzi (sigma_chan_t * c, int nrzi);
void sigma_set_dpll (sigma_chan_t * c, int on);

u32 sigma_get_baud (sigma_chan_t * c);
int sigma_get_loop (sigma_chan_t * c);
int sigma_get_nrzi (sigma_chan_t * c);
int sigma_get_dpll (sigma_chan_t * c);

int sigma_send_packet (sigma_chan_t * c, char *data, int len, void *attachment);
int sigma_buf_free (sigma_chan_t * c);

void sigma_register_transmit (sigma_chan_t * c, void (*func) (sigma_chan_t * c, void *attachment, int len));
void sigma_register_receive (sigma_chan_t * c, void (*func) (sigma_chan_t * c, char *data, int len));
void sigma_register_error (sigma_chan_t * c, void (*func) (sigma_chan_t * c, int data));
void sigma_intr_off (sigma_board_t * b);
void sigma_intr_on (sigma_board_t * b);
int sigma_checkintr (sigma_board_t * b);

/* Async functions */
void sigma_transmitter_ctl (sigma_chan_t * c, int start);
void sigma_purge_transmit (sigma_chan_t * c);
int sigma_is_transmit (sigma_chan_t * c);
void sigma_xflow_ctl (sigma_chan_t * c, int on);
void sigma_send_break (sigma_chan_t * c, int msec);
void sigma_set_async_param (sigma_chan_t * c, int baud, int bits, int parity,
			    int stop2, int ignpar, int rtscts, int ixon, int ixany, int symstart, int symstop);

extern unsigned sigma_porttab[], sigma_irqtab[], sigma_dmatab[];

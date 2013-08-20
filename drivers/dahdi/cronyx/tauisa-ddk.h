/*
 * Defines for Cronyx Tau-ISA adapter DDK.
 *
 * Copyright (C) 1994-2003 Cronyx Engineering.
 * Author: Serge Vakulenko, <vak@cronyx.ru>
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
 * $Id: tauisa-ddk.h,v 1.14 2009-10-07 14:30:14 ly Exp $
 */

#ifndef DDK_PREPROC
#	include "ddk-host.h"
#	include "ddk-arch.h"
#endif

#define TAUISA_NBRD	3	/* the maximum number of installed boards */
#define TAUISA_NPORT	32	/* the number of i/o ports per board */
#define TAUISA_NCHAN	2	/* the number of channels on the board */
#define TAUISA_NBUF	4	/* the number of buffers per direction */
#define TAUISA_DMABUFSZ	1600	/* buffer size */
#define TAUISA_SCCBUFSZ	50

/*
 * There are tree models of Tau adapters.
 * Each of two channels of the adapter is assigned a type:
 *
 *		Channel 0	Channel 1
 * ------------------------------------------
 * Tau		TAUISA_SERIAL	TAUISA_SERIAL
 * Tau/E1	TAUISA_E1	TAUISA_E1_SERIAL
 * Tau/G703	TAUISA_G703	TAUISA_G703_SERIAL
 *
 * Each channel could work in one of several modes:
 *
 *		Channel 0	Channel 1
 * ------------------------------------------
 * Tau		TAUISA_MODE_ASYNC,	TAUISA_MODE_ASYNC,
 *		TAUISA_MODE_HDLC	TAUISA_MODE_HDLC
 * ------------------------------------------
 * Tau/E1	TAUISA_MODE_E1,	TAUISA_MODE_E1,
 *		TAUISA_MODE_E1 & TAUISA_CFG_D,	TAUISA_MODE_E1 & TAUISA_CFG_D,
 *				TAUISA_MODE_ASYNC,
 *				TAUISA_MODE_HDLC
 * ------------------------------------------
 * Tau/G703	TAUISA_MODE_G703, 	TAUISA_MODE_G703,
 *				TAUISA_MODE_ASYNC,
 *				TAUISA_MODE_HDLC
 * ------------------------------------------
 */
#define TAUISA_MODEL_BASIC	0	/* Tau - basic model */
#define TAUISA_MODEL_E1		1	/* Tau/E1 */
#define TAUISA_MODEL_G703	2	/* Tau/G.703 */
#define TAUISA_MODEL_E1_c	3	/* Tau/E1 revision C */
#define TAUISA_MODEL_E1_d	4	/* Tau/E1 revision D with phony mode support */
#define TAUISA_MODEL_G703_d	5	/* Tau/G.703 revision D */
#define TAUISA_MODEL_2BASIC	6	/* Tau2 - basic model */
#define TAUISA_MODEL_2E1	7	/* Tau2/E1 */
#define TAUISA_MODEL_2E1_d	8	/* Tau2/E1 with phony mode support */
#define TAUISA_MODEL_2G703	9	/* Tau2/G.703 */

#define TAUISA_NONE		0
#define TAUISA_SERIAL		1
#define TAUISA_E1		2
#define TAUISA_G703		4
#define TAUISA_E1_SERIAL	(TAUISA_E1 | TAUISA_SERIAL)
#define TAUISA_G703_SERIAL	(TAUISA_G703 | TAUISA_SERIAL)

#define TAUISA_MODE_ASYNC 	0	/* asynchronous mode */
#define TAUISA_MODE_HDLC	1	/* bit-sync mode (HDLC) */
#define TAUISA_MODE_G703	2
#define TAUISA_MODE_E1		3

#define TAUISA_CFG_A		0
#define TAUISA_CFG_B		1
#define TAUISA_CFG_C		2
#define TAUISA_CFG_D		3

/* E1/G.703 interfaces - i0, i1
 * Digital interface   - d0
 *
 *
 * Configuration
 * ---------------------------------------------------
 * TAUISA_CFG_A	 | i0<->ct0   i1<->ct1
 * ---------------------------------------------------
 * TAUISA_CFG_B	 | i0<->ct0   d0<->ct1
 *		 |  ^
 *		 |  |
 *		 |  v
 *		 | i1
 * ---------------------------------------------------
 * TAUISA_CFG_C	 | ct0<->i0<->ct1
 *		 |	  ^
 *		 |	  |
 *		 |	  v
 *		 |	 i1
 * ---------------------------------------------------
 * TAUISA_CFG_D	 | i0(e1)<->hdlc<->hdlc<->ct0(e1)
 * ONLY TAU/E1	 | i1(e1)<->hdlc<->hdlc<->ct1(e1)
 *		 |
 */

/*
 * Option CLK is valid for both E1 and G.703 models.
 * Options RATE, PCE, TEST are for G.703 only.
 */
#define TAUISA_CLK_INT	0	/* internal transmit clock source */
#define TAUISA_CLK_RCV	1	/* transmit clock source = receive */
#define TAUISA_CLK_XFER	2	/* tclk = receive clock of another channel */

#define TAUISA_TEST_DISABLED	0	/* test disabled, normal operation */
#define TAUISA_TEST_ZEROS 	1	/* test "all zeros" */
#define TAUISA_TEST_ONES 	2	/* test "all ones" */
#define TAUISA_TEST_01		3	/* test "0/1" */

typedef struct {
	unsigned long bpv;	/* bipolar violations */
	unsigned long fse;	/* frame sync errors */
	unsigned long crce;	/* CRC errors */
	unsigned long rcrce;	/* remote CRC errors (E-bit) */
	unsigned long uas;	/* unavailable seconds */
	unsigned long les;	/* line errored seconds */
	unsigned long es;	/* errored seconds */
	unsigned long bes;	/* bursty errored seconds */
	unsigned long ses;	/* severely errored seconds */
	unsigned long oofs;	/* out of frame seconds */
	unsigned long css;	/* controlled slip seconds */
	unsigned long dm;	/* degraded minutes */
} tauisa_gstat_t;

#define TAUISA_E1_NOALARM	0x0001	/* no alarm present */
#define TAUISA_E1_RA		0x0002	/* receiving far loss of framing */
#define TAUISA_E1_AIS		0x0008	/* receiving all ones */
#define TAUISA_E1_LOF		0x0020	/* loss of framing */
#define TAUISA_E1_LOS		0x0040	/* loss of signal */
#define TAUISA_E1_AIS16		0x0100	/* receiving all ones in timeslot 16 */
#define TAUISA_E1_RDMA		0x0200	/* receiving alarm in timeslot 16 */
#define TAUISA_E1_LOMF		0x0400	/* loss of multiframe sync */
#define TAUISA_E1_TSTREQ	0x0800	/* test code detected */
#define TAUISA_E1_TSTERR	0x1000	/* test error */

typedef struct {
	unsigned char data[10];
} tauisa_desc_t;

typedef struct {
	unsigned char stopb:2;	/* stop bit length */
	unsigned char :2;
	unsigned char cts_rts_dcd:1;	/* auto-enable CTS/DCD/RTS */
	unsigned char mode:3;	/* protocol mode */
} tauisa_md0_async_t;

typedef struct {
	unsigned char crcpre:1;	/* CRC preset 1s / 0s */
	unsigned char ccitt:1;	/* CRC-CCITT / CRC-16 */
	unsigned char crc:1;	/* CRC enable */
	unsigned char :1;
	unsigned char cts_dcd:1;	/* auto-enable CTS/DCD */
	unsigned char mode:3;	/* protocol mode */
} tauisa_md0_hdlc_t;

typedef struct {
	unsigned char parmode:2;	/* parity mode */
	unsigned char rxclen:2;	/* receive character length */
	unsigned char txclen:2;	/* transmit character length */
	unsigned char clk:2;	/* clock rate */
} tauisa_md1_async_t;

typedef struct {
	unsigned char :6;
	unsigned char addr:2;	/* address field check */
} tauisa_md1_hdlc_t;

typedef struct {
	unsigned char loop:2;	/* loopback mode */
	unsigned char :1;
	unsigned char dpll_clk:2;	/* ADPLL clock rate */
	unsigned char encod:3;	/* signal encoding NRZ/NRZI/etc. */
} tauisa_md2_t;

typedef struct {
	unsigned char prio:3;	/* priority of channels */
	unsigned char noshare:1;	/* 1 - chan holds the bus until end of data */
	/*
	 * 0 - all channels share the the bus hold
	 */
	unsigned char release:1;	/* 1 - release the bus between transfers */
	/*
	 * 0 - hold the bus until all transfers done
	 */
} tauisa_pcr_t;

typedef struct {		/* hdlc channel options */
	tauisa_md0_hdlc_t md0;	/* mode register 0 */
	tauisa_md1_hdlc_t md1;	/* mode register 1 */
	unsigned char ctl;	/* control register */
	unsigned char sa0;	/* sync/address register 0 */
	unsigned char sa1;	/* sync/address register 1 */
	unsigned char rxs;	/* receive clock source */
	unsigned char txs;	/* transmit clock source */
} tauisa_opt_hdlc_t;

typedef struct {
	tauisa_md2_t md2;	/* mode register 2 */
	unsigned char dma_rrc;	/* DMA mode receive FIFO ready level */
	unsigned char dma_trc0;	/* DMA mode transmit FIFO empty mark */
	unsigned char dma_trc1;	/* DMA mode transmit FIFO full mark */
	unsigned char pio_rrc;	/* port i/o mode receive FIFO ready level */
	unsigned char pio_trc0;	/* port i/o transmit FIFO empty mark */
	unsigned char pio_trc1;	/* port i/o transmit FIFO full mark */
} tauisa_chan_opt_t;

typedef struct {		/* E1/G.703 channel options */
	unsigned char hdb3;	/* encoding HDB3/AMI */
	unsigned char pce;	/* precoder enable */
	unsigned char test;	/* test mode 0/1/01/disable */
	unsigned char crc4;	/* E1 CRC4 enable */
	unsigned char cas;	/* E1 signalling mode CAS/CCS */
	unsigned char higain;	/* E1 high gain amplifier (30 dB) */
	unsigned char phony;	/* E1 phony mode */
	unsigned char pce2;	/* old PCM2 precoder compatibility */
	u32 rate;	/* data rate 2048/1024/512/256/128/64 kbit/s */
	unsigned short level;	/* G.703 input signal level, -cB */
} tauisa_opt_g703_t;

typedef struct {
	unsigned char bcr2;	/* board control register 2 */
	tauisa_pcr_t pcr;	/* DMA priority control register */
	unsigned char clk0;	/* E1/G.703 chan 0 txclk src int/rcv/rclki */
	unsigned char clk1;	/* E1/G.703 chan 1 txclk src int/rcv/rclki */
	unsigned char cfg;	/* E1 configuration II/HI/K */
	u32 s0;	/* E1 channel 0 timeslot mask */
	u32 s1;	/* E1 channel 1 timeslot mask */
	u32 s2;	/* E1 subchannel pass-through timeslot mask */
} tauisa_board_opt_t;

typedef struct {
	unsigned char tbuffer[TAUISA_NBUF][TAUISA_DMABUFSZ];	/* transmit buffers */
	unsigned char rbuffer[TAUISA_NBUF][TAUISA_DMABUFSZ];	/* receive buffers  */
	tauisa_desc_t descbuf[4 * TAUISA_NBUF];	/* descriptors */
	/*
	 * double size for alignment
	 */
} tauisa_buf_t;

typedef struct {
	unsigned short DAR, DARB, SAR, SARB, CDA, EDA, BFL, BCR, DSR, DMR, FCT, DIR, DCR, TCNT, TCONR, TCSR, TEPR;
} tauisa_dmareg_t;

typedef struct _tauisa_chan_t {
	unsigned short MD0, MD1, MD2, CTL, RXS, TXS, TMC, CMD, ST0,
		ST1, ST2, ST3, FST, IE0, IE1, IE2, FIE, SA0, SA1, IDL, TRB, RRC, TRC0, TRC1, CST;
	tauisa_dmareg_t RX;	/* RX DMA/timer registers */
	tauisa_dmareg_t TX;	/* TX DMA/timer registers */

	unsigned char num;	/* channel number, 0..1 */
	struct _tauisa_board_t *board;	/* board pointer */
	u32 baud;	/* data rate */
	unsigned char type;	/* channel type */
	unsigned char mode;	/* channel mode */
	tauisa_chan_opt_t opt;	/* common channel options */
	tauisa_opt_hdlc_t hopt;	/* hdlc mode options */
	tauisa_opt_g703_t gopt;	/* E1/G.703 options */
	unsigned char dtr;	/* DTR signal value */
	unsigned char rts;	/* RTS signal value */
	unsigned char lx;	/* LXT input bit settings */

	unsigned char *tbuf[TAUISA_NBUF];	/* transmit buffer */
	tauisa_desc_t *tdesc;	/* transmit buffer descriptors */
	u32 tphys[TAUISA_NBUF];	/* transmit buffer phys address */
	u32 tdphys[TAUISA_NBUF];	/* transmit descr phys addresses */
	int tn;			/* first active transmit buffer */
	int te;			/* first active transmit buffer */

	unsigned char *rbuf[TAUISA_NBUF];	/* receive buffers */
	tauisa_desc_t *rdesc;	/* receive buffer descriptors */
	u32 rphys[TAUISA_NBUF];	/* receive buffer phys address */
	u32 rdphys[TAUISA_NBUF];	/* receive descr phys addresses */
	int rn;			/* first active receive buffer */

	unsigned long rintr;	/* receive interrupts */
	unsigned long tintr;	/* transmit interrupts */
	unsigned long mintr;	/* modem interrupts */
	unsigned long ibytes;	/* input bytes */
	unsigned long ipkts;	/* input packets */
	unsigned long ierrs;	/* input errors */
	unsigned long obytes;	/* output bytes */
	unsigned long opkts;	/* output packets */
	unsigned long oerrs;	/* output errors */

	unsigned status;	/* line status bit mask */
	unsigned long totsec;	/* total seconds elapsed */
	unsigned long cursec;	/* total seconds elapsed */
	unsigned long degsec;	/* degraded seconds */
	unsigned long degerr;	/* errors during degraded seconds */
	tauisa_gstat_t currnt;	/* current 15-min interval data */
	tauisa_gstat_t total;	/* total statistics data */
	tauisa_gstat_t interval[48];	/* 12 hour period data */

	void *attach[TAUISA_NBUF];	/* system dependent data per buffer */
	void *sys;		/* system dependent data per channel */
	int debug;

	int e1_first_int;
	unsigned char *sccrx, *scctx;	/* pointers to SCC rx and tx buffers */
	int sccrx_empty, scctx_empty;	/* flags : set when buffer is empty  */
	int sccrx_b, scctx_b;	/* first byte in queue      */
	int sccrx_e, scctx_e;	/* first free byte in queue */

	/*
	 * pointers to callback functions
	 */
	void (*call_on_tx) (struct _tauisa_chan_t *, void *, int);
	void (*call_on_rx) (struct _tauisa_chan_t *, char *, int);
	void (*call_on_msig) (struct _tauisa_chan_t *);
	void (*call_on_scc) (struct _tauisa_chan_t *);
	void (*call_on_err) (struct _tauisa_chan_t *, int);

} tauisa_chan_t;

typedef struct _tauisa_board_t {
	unsigned port;		/* base board port, 200..3e0 */
	unsigned short num;	/* board number, 0..2 */
	signed char irq;	/* intterupt request {3 5 7 10 11 12 15} */
	unsigned char dma;	/* DMA request {5 6 7} */
	u32 osc;	/* oscillator frequency: 10MHz or 8.192 */
	unsigned char type;	/* board type Tau/TauE1/TauG703 */
	char name[32];		/* board name */
	unsigned char bcr0;	/* BCR0 image */
	unsigned char bcr1;	/* BCR1 image */
	unsigned char bcr2;	/* BCR2 image */
	unsigned char gmd0;	/* G.703 MD0 register image */
	unsigned char gmd1;	/* G.703 MD1 register image */
	unsigned char gmd2;	/* G.703 MD2 register image */
	unsigned char e1cfg;	/* E1 CFG register image */
	unsigned char e1syn;	/* E1 SYN register image */
	tauisa_board_opt_t opt;	/* board options */
	tauisa_chan_t chan[TAUISA_NCHAN];	/* channel structures */
} tauisa_board_t;

typedef struct _tauisa_dat_tst_t tauisa_dat_tst_t;

int tauisa_probe_board (unsigned short port, int irq, int dma);
void tauisa_init (tauisa_board_t * b, int num, unsigned short port, int irq, int dma,
		  const unsigned char *firmware, long bits, const tauisa_dat_tst_t * tst, const unsigned char *firmware2);
void tauisa_init_board (tauisa_board_t * b, int num, unsigned short port, int irq, int dma, int type, long osc);
int tauisa_setup_board (tauisa_board_t * b, const unsigned char *firmware, long bits, const tauisa_dat_tst_t * tst);
void tauisa_setup_e1 (tauisa_board_t * b);
void tauisa_setup_g703 (tauisa_board_t * b);
void tauisa_setup_chan (tauisa_chan_t * c);
void tauisa_start_receiver (tauisa_chan_t * c, int dma, u32 buf1, unsigned len, u32 buf, u32 lim);
void tauisa_start_transmitter (tauisa_chan_t * c, int dma, u32 buf1,
			       unsigned len, u32 buf, u32 lim);
void tauisa_set_dtr (tauisa_chan_t * c, int on);
void tauisa_set_rts (tauisa_chan_t * c, int on);
void tauisa_set_brk (tauisa_chan_t * c, int on);
void tauisa_led (tauisa_board_t * b, int on);
void tauisa_reinit_board (tauisa_board_t * b);
void tauisa_reinit_chan (tauisa_chan_t * c);
int tauisa_get_dsr (tauisa_chan_t * c);
int tauisa_get_cd (tauisa_chan_t * c);
int tauisa_get_cts (tauisa_chan_t * c);
int tauisa_get_lq (tauisa_chan_t * c);
void tauisa_compute_clock (long hz, long baud, int *txbr, int *tmc);
void tauisa_probe_irq (tauisa_board_t * b, int irq);
void tauisa_int_handler (tauisa_board_t * b);
void tauisa_g703_timer (tauisa_chan_t * c);

#define TAUISA_FRAME			1
#define TAUISA_CRC			2
#define TAUISA_OVERRUN			3
#define TAUISA_OVERFLOW			4
#define TAUISA_UNDERRUN			5
#define TAUISA_SCC_OVERRUN		6
#define TAUISA_SCC_FRAME		7
#define TAUISA_SCC_OVERFLOW		8

int tauisa_open_board (tauisa_board_t * b, int num, unsigned short port, int irq, int dma);
void tauisa_close_board (tauisa_board_t * b);
int tauisa_find (unsigned short *board_ports);

int tauisa_set_config (tauisa_board_t * b, int cfg);
int tauisa_set_clk (tauisa_chan_t * c, int clk);
int tauisa_set_ts (tauisa_chan_t * c, u32 ts);
int tauisa_set_subchan (tauisa_board_t * b, u32 ts);
int tauisa_set_higain (tauisa_chan_t * c, int on);
int tauisa_set_phony (tauisa_chan_t * c, int on);

#define tauisa_get_config(b)    ((b)->opt.cfg)
#define tauisa_get_subchan(b)   ((b)->opt.s2)
#define tauisa_get_higain(c)    ((c)->gopt.higain)
#define tauisa_get_phony(c)     ((c)->gopt.phony)
int tauisa_get_clk (tauisa_chan_t * c);
u32 tauisa_get_ts (tauisa_chan_t * c);

void tauisa_start_chan (tauisa_chan_t * c, tauisa_buf_t * cb, u32 phys);
void tauisa_enable_receive (tauisa_chan_t * c, int on);
void tauisa_enable_transmit (tauisa_chan_t * c, int on);
int tauisa_receive_enabled (tauisa_chan_t * c);
int tauisa_transmit_enabled (tauisa_chan_t * c);

void tauisa_set_baud (tauisa_chan_t * c, u32 baud);
u32 tauisa_get_baud (tauisa_chan_t * c);

void tauisa_set_dpll (tauisa_chan_t * c, int on);
int tauisa_get_dpll (tauisa_chan_t * c);

void tauisa_set_nrzi (tauisa_chan_t * c, int on);
int tauisa_get_nrzi (tauisa_chan_t * c);

void tauisa_set_loop (tauisa_chan_t * c, int on);
int tauisa_get_loop (tauisa_chan_t * c);

void tauisa_set_invtxc (tauisa_chan_t * c, int on);
int tauisa_get_invtxc (tauisa_chan_t * c);
void tauisa_set_invrxc (tauisa_chan_t * c, int on);
int tauisa_get_invrxc (tauisa_chan_t * c);

int tauisa_buf_free (tauisa_chan_t * c);
int tauisa_send_packet (tauisa_chan_t * c, unsigned char *data, int len, void *attachment);

void tauisa_start_scc (tauisa_chan_t * c, char *rxbuf, char *txbuf);

void tauisa_register_transmit (tauisa_chan_t * c, void (*func) (tauisa_chan_t *, void *attachment, int len));
void tauisa_register_receive (tauisa_chan_t * c, void (*func) (tauisa_chan_t *, char *data, int len));
void tauisa_register_error (tauisa_chan_t * c, void (*func) (tauisa_chan_t * c, int data));
void tauisa_register_modem (tauisa_chan_t * c, void (*func) (tauisa_chan_t * c));
void tauisa_register_scc (tauisa_chan_t * c, void (*func) (tauisa_chan_t * c));

extern long tauisa_baud;
extern unsigned char tauisa_chan_mode;
extern tauisa_board_opt_t tauisa_board_opt_dflt;	/* default board options */
extern tauisa_chan_opt_t tauisa_chan_opt_dflt;	/* default channel options */
extern tauisa_opt_hdlc_t tauisa_opt_hdlc_dflt;	/* default hdlc mode options */
extern tauisa_opt_g703_t tauisa_opt_g703_dflt;	/* default E1/G.703 options */
extern short tauisa_porttab[], tauisa_irqtab[], tauisa_dmatab[];

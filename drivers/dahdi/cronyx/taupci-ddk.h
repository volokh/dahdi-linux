/*
 * Cronyx Tau-PCI DDK definitions.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Copyright (C) 1999-2013 Cronyx Telecom (www.cronyx.ru, info@cronyx.ru)
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 * Author: Roman Kurakin, <rik@cronyx.ru>
 * Author: Leo Yuriev, <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Id: taupci-ddk.h,v 1.47 2009-09-18 12:19:47 ly Exp $
 */

#define TAUPCI_NCHAN	4			/* the number of channels on the board */
#define TAUPCI_NBUF	16			/* the number of rx/tx buffers per channel, min 2, power of 2! */
#define TAUPCI_BUFSZ	1664			/* i/o buffer size (52*32) */
#define TAUPCI_MTU	(TAUPCI_BUFSZ - 4)	/* hdlc-status and infineon's bug workaround */
#define TAUPCI_QSZ	64			/* intr queue size (min 32, power of 2!) */

#define TAUPCI_PCI_VENDOR_ID	0x110A
#define TAUPCI_PCI_DEVICE_ID	0x2102
#define TAUPCI_BAR1_SIZE	0x0800u

#define TAUPCI_CROSS_WIDTH  128

#define TAUPCI_NONE     0   /* no channel */
#define TAUPCI_DATA     1   /* no physical interface */
#define TAUPCI_SERIAL   2   /* V.35/RS-530/RS-232/X.21 */
#define TAUPCI_G703     3   /* G.703 (unframed only) */
#define TAUPCI_E1       4   /* E1 */
#define TAUPCI_E3       5   /* E3 */
#define TAUPCI_HSSI     6   /* HSSI */
#define TAUPCI_T3       7   /* T3 */
#define TAUPCI_STS1     8   /* STS1 */

#define TAUPCI_CAS_OFF      0
#define TAUPCI_CAS_SET      1
#define TAUPCI_CAS_PASS     2
#define TAUPCI_CAS_CROSS    3

#define TAUPCI_GSYN_INT 0   /* internal transmit clock source */
#define TAUPCI_GSYN_RCV 1   /* transmit clock source = receive */
#define TAUPCI_GSYN_RCV0    2   /* tclk = rclk from channel 0 */
#define TAUPCI_GSYN_RCV1    3   /* ...from channel 1 */
#define TAUPCI_GSYN_RCV2    4   /* ...from channel 2 */
#define TAUPCI_GSYN_RCV3    5   /* ...from channel 3 */

#define TAUPCI_ESTS_UNKNOWN 0
#define TAUPCI_ESTS_NOALARM 0x0001  /* no alarm present */
#define TAUPCI_ESTS_TXE     0x0002  /* Transmit error */
#define TAUPCI_ESTS_LOS     0x0004  /* loss of signal */
#define TAUPCI_ESTS_AIS     0x0008  /* receiving all ones */
#define TAUPCI_ESTS_LOF     0x0010  /* loss of framing */
#define TAUPCI_ESTS_AIS16   0x0020  /* receiving all ones in timeslot 16 */
#define TAUPCI_ESTS_LOMF    0x0040  /* loss of multiframe sync */
#define TAUPCI_ESTS_RDMA    0x0080  /* receiving alarm in timeslot 16 */
#define TAUPCI_ESTS_RA      0x0100  /* receiving far loss of framing */
#define TAUPCI_ESTS_TSTREQ  0x0200  /* test code detected */
#define TAUPCI_ESTS_TSTERR  0x0400  /* test error */

#define TAUPCI_E3STS_LOS	TAUPCI_ESTS_LOS
#define TAUPCI_E3STS_AIS	TAUPCI_ESTS_AIS
#define TAUPCI_E3STS_TXE	TAUPCI_ESTS_TXE

#define TAUPCI_FRAME		1
#define TAUPCI_CRC		2
#define TAUPCI_UNDERRUN		3
#define TAUPCI_OVERRUN		4
#define TAUPCI_OVERFLOW		5

#define TAUPCI_DEAD_20534   (1ul << 0)
#define TAUPCI_DEAD_HW      (1ul << 1)
#define TAUPCI_DEAD_FW      (1ul << 2)
#define TAUPCI_DEAD_ABUS    (1ul << 3)
#define TAUPCI_DEAD_CS0     (1ul << 4)
#define TAUPCI_DEAD_CS1     (1ul << 5)
#define TAUPCI_DEAD_CS2     (1ul << 6)
#define TAUPCI_DEAD_CS3     (1ul << 7)
#define TAUPCI_DEAD_DXC     (TAUPCI_DEAD_HW | TAUPCI_DEAD_FW)

#define TAUPCI_MODEL_1E3    1   /* 1 channel E3 */
#define TAUPCI_MODEL_1HSSI  2   /* 1 channel HSSI */
#define TAUPCI_MODEL_LITE   3   /* 1 channel V.35/RS */
#define TAUPCI_MODEL_2RS    4   /* 2 channels V.35/RS */
#define TAUPCI_MODEL_2G703  5   /* 2 channels G703 */
#define TAUPCI_MODEL_OE1    6   /* old-model 2 channels E1 */
#define TAUPCI_MODEL_OE1_2RS    7   /* old-mode 2 channels E1 + 2 channels V.35/RS*/
#define TAUPCI_MODEL_4RS    8   /* 4 channels V.35/RS */
#define TAUPCI_MODEL_2G703_2RS  9   /* 2 channels G.703 + 2 channels V.35/RS */
#define TAUPCI_MODEL_4G703  10   /* 4 channels G.703 */
#define TAUPCI_MODEL_2E1_4  11  /* 2 channels E1, 4 data ports */
#define TAUPCI_MODEL_2E1_2RS    12  /* 2 channels E1 + 2 channels V.35/RS */
#define TAUPCI_MODEL_4E1    13  /* 4 channels E1 */

#define TAUPCI_CABLE_RS232      0
#define TAUPCI_CABLE_V35        1
#define TAUPCI_CABLE_RS530      2
#define TAUPCI_CABLE_X21        3
#define TAUPCI_CABLE_RS485      4
#define TAUPCI_CABLE_NOT_ATTACHED   9
#define TAUPCI_CABLE_COAX       10
#define TAUPCI_CABLE_TP         11

typedef struct {
	unsigned bpv;	/* bipolar violations */
	unsigned fse;	/* frame sync errors */
	unsigned crce;	/* CRC errors */
	unsigned rcrce;	/* remote CRC errors (E-bit) */
	unsigned uas;	/* unavailable seconds */
	unsigned les;	/* line errored seconds */
	unsigned es;	/* errored seconds */
	unsigned bes;	/* bursty errored seconds */
	unsigned ses;	/* severely errored seconds */
	unsigned oofs;	/* out of frame seconds */
	unsigned css;	/* controlled slip seconds */
	unsigned dm;	/* degraded minutes */
} taupci_gstat_t;

#define TAUPCI_REGIO_INB	0
#define TAUPCI_REGIO_IN		1
#define TAUPCI_REGIO_INS	2
#define TAUPCI_REGIO_INX	3
#define TAUPCI_REGIO_INB_OUTB	4
#define TAUPCI_REGIO_OUTB	5
#define TAUPCI_REGIO_OUTX	6
#define TAUPCI_REGIO_R_W	7
#define TAUPCI_REGIO_OUT_IN	8
#define TAUPCI_REGIO_OUTB_INB	9

//-----------------------------------------------------------------------------

#ifndef DDK_PREPROC
#	include "ddk-host.h"
#	include "ddk-arch.h"
#endif

#ifndef DDK_USERMODE_ONLY
#ifndef TAUPCI_DDK_DLL
#   if defined(_NTDDK_) || defined(_WINDOWS)
#       ifdef TAUPCI_DDK_IMP
#           define TAUPCI_DDK_DLL __declspec(dllexport)
#       else
#           define TAUPCI_DDK_DLL __declspec(dllimport)
#       endif
#   else
#       define TAUPCI_DDK_DLL
#   endif
#endif

typedef u8 taupci_dxc_t[TAUPCI_CROSS_WIDTH];

typedef struct {
	u32 len;	/* data buffer length, fe, hold, hi */
	u32 next;	/* next descriptor pointer */
	u32 data;	/* pointer to data buffer */
	u32 status;	/* complete, receive abort, fe, len */
	u32 fe;		/* pointer to frame end descriptor */
	u32 pad[3];	/* LY: pad to cache-like aling (32 bytes) */
} taupci_desc_t;

typedef struct {
	u8 bufs[TAUPCI_NBUF][TAUPCI_BUFSZ];   /* transmit buffers */
} taupci_buf_t;

typedef struct {
	volatile u32 iq_tx[TAUPCI_NCHAN][TAUPCI_QSZ];   /* tx intr queue */
	volatile u32 iq_rx[TAUPCI_NCHAN][TAUPCI_QSZ];   /* rx intr queue */
	volatile u32 iq_cfg[32];
	volatile u32 iq_lbi[32];
	taupci_desc_t iq_td[TAUPCI_NCHAN][TAUPCI_NBUF];   /* transmit buffer descriptors */
	taupci_desc_t iq_rd[TAUPCI_NCHAN][TAUPCI_NBUF];   /* receive buffer descriptors */
} taupci_iq_t;

typedef struct _cp_chan_t {
	volatile u32 *scc_regs;
	volatile u32 *core_regs1;
	volatile u32 *core_regs2;
	u8 *tbuf[TAUPCI_NBUF]; /* transmit buffers */
	u32 tx_dma[TAUPCI_NBUF];    /* transmit buffer _dma address */
	u32 td_dma[TAUPCI_NBUF];   /* transmit descr _dma addresses */
	u8 *rbuf[TAUPCI_NBUF]; /* receive buffers */
	u32 rx_dma[TAUPCI_NBUF];    /* receive buffer _dma address */
	u32 rd_dma[TAUPCI_NBUF];   /* receive descr _dma addresses */
	u8 GMD, GLS, E1CS, E1CR, E1EPS;
	u8 num;    /* channel number, 0..1 */
	u8 type;   /* channel type */
	struct _cp_board_t *board;  /* board pointer */
	u32 baud;  /* data rate, bps */
	unsigned mios;      /* max i/o size */
	unsigned qlen;      /* */
	unsigned raw_mios;  /* */
	unsigned dtr:1;     /* DTR signal value */
	unsigned rts:1;     /* RTS signal value */
	unsigned dpll:1;    /* dpll mode */
	unsigned nrzi:1;    /* nrzi mode */
	unsigned invtxc:1;  /* invert tx clock */
	unsigned invrxc:1;  /* invert rx clock */
	unsigned lloop:1;   /* local loopback mode */
	unsigned rloop:1;   /* remote loopback mode */
	unsigned scrambler:1;   /* G.703 scrambler enable */
	unsigned higain:1;  /* E1 high gain mode */
	unsigned cas:2;     /* E1 CAS mode */
	unsigned crc4:1;    /* E1 enable CRC4 */
	unsigned phony:1;   /* E1 phony mode */
	unsigned unframed:1;    /* E1 unframed mode */
	unsigned monitor:1; /* E1 monitoring mode */
	unsigned ais:1;     /* E3 AIS */
	unsigned losais:1;  /* E3 AIS on LOS */
	unsigned ber:1;     /* E3 BER */
	unsigned t3_long:1; /* E3 cable length */
	unsigned ds_ien:1;  /* DS Interrupts enabled */
	unsigned hdlc_shareflags:1; /* LY: share (e.g join) begin-frame and end-frame flags */
	unsigned hdlc_crclen:2; /* LY: crc length 0 - disabled, 1 - 16 bit, 2 - 32 bit */
	unsigned voice:1;
	u8 setup_cnt;
	u8 gsyn;   /* G.703 clock mode */
	u8 dir;    /* E1 direction mode */
	u8 e3cr1;     /* e3cr1 clone */
	u32 ts, dacs_ts;   /* E1 timeslot mask */
	u32 scc_imr, dma_imr;
	u32 ccr0;  /* CCR0 clone */
	u32 ccr1;  /* CCR1 clone */
	u32 ccr2;  /* CCR2 clone */
	u8 ccr;    /* CCR image */
	u8 gmd;    /* G.703 MDi register image */
	u8 e1cr;   /* E1 CR register image */
	u8 ds21x54;    /* new tranceiver flag */
	unsigned e1_intr; /* LY: ds21554 irq-counter, workaround pcb-bug. */
	u64 rintr; /* receive interrupts */
	u64 tintr; /* transmit interrupts */
	u64 ibytes;    /* input bytes */
	u64 obytes;    /* output bytes */
	u64 ipkts; /* input packets */
	u64 opkts; /* output packets */
	unsigned underrun;  /* output underrun errors */
	unsigned overrun;   /* input overrun errors */
	unsigned frame; /* input frame errors */
	unsigned crc;   /* input crc errors */
	unsigned status;    /* E1/G.703 line status bit mask */
	unsigned prev_status;   /* E1/G.703 line previsious status bit mask */
	unsigned totsec;    /* total seconds elapsed */
	unsigned cursec;    /* current seconds elapsed */
	unsigned degsec;    /* degraded seconds */
	unsigned degerr;    /* errors during degraded seconds */
	taupci_gstat_t currnt;  /* current 15-min interval data */
	taupci_gstat_t total;   /* total statistics data */
	taupci_gstat_t interval[48];    /* 12 hour period data */
	unsigned e3csec_5;  /* 1/5 of second counter */
	unsigned e3tsec;    /* total seconds coounter */
	unsigned e3ccv; /* E3 current 15-min cv errors */
	u64 e3tcv; /* E3 total cv errors */
	unsigned e3icv[48]; /* E3 12 hour period cv errors */
	volatile u32 *iq_tx;    /* tx intr queue */
	volatile u32 *iq_rx;    /* tx intr queue */
	unsigned irn, itn;
	taupci_desc_t *tdesc;   /* transmit buffer descriptors */
	unsigned tn;        /* first active transmit buffer */
	unsigned te;        /* first empty transmit buffer */
	taupci_desc_t *rdesc;   /* receive buffer descriptors */
	unsigned rn;        /* first active receive buffer */
	void *tag[TAUPCI_NBUF]; /* system dependent data per buffer */
	void *sys;      /* system dependent data per channel */
	void (*transmit) (struct _cp_chan_t * c, void *tag, int len);
	void (*receive) (struct _cp_chan_t * c, u8 *data, int len);
	void (*error) (struct _cp_chan_t * c, int reason);
} taupci_chan_t;

typedef struct _cp_board_t {
	taupci_chan_t chan[TAUPCI_NCHAN];   /* channel structures */

	volatile u32 *base;  /* base address of adapter registers */
	u8 num;    /* board number, 0..5 */
	u8 model;  /* board type Tau/TauE1/TauG703 */

#define TAUPCI_CROSS_PENDING    0x01
#define TAUPCI_CROSS_WAITING    0x02
	u8 dxc_status;
	volatile u8 action_pending;

	u8 mux;    /* E1 mux mode */
	u8 bcr;    /* BCR image */
	u8 e1cfg;  /* E1 CFG register image */
	u8 gpp_idle;   /* idle bits of gp port */
	u8 E1DATA;

	u8 irq_enabled;    /* SCC Interrupts enabled */
	u8 hardware_model; /* firmware type */
	u32 osc;   /* oscillator frequency */
	const char *name;   /* board version name */

	u64 intr;  /* interrupt counter */

	struct {
		u8 *active, *shadow;
		u8 pending[TAUPCI_CROSS_WIDTH * 2];
		u8 flip[2][TAUPCI_CROSS_WIDTH * 2];
	} dxc;

#ifdef TAUPCI_CUSTOM_FIRMWARE
#   define TAUPCI_FW_E3_B   1
#   define TAUPCI_FW_2E1_B  2
#   define TAUPCI_FW_2E1_A  3
#   define TAUPCI_FW_4E1_B  6
#   define TAUPCI_FW_4E1_A  7
#   define TAUPCI_FW_MAX_SIZE       (128*1024)
#   define TAUPCI_FW_MAX_ID    8
	u8 *custom_firmware_data[TAUPCI_FW_MAX_ID];    /* external firmware */
	unsigned custom_firmware_size[TAUPCI_FW_MAX_ID];    /* external firmware */
#endif

	void *sys;
	unsigned dead;
} taupci_board_t;

/* Initialization. */
u32 TAUPCI_DDK_DLL taupci_init (taupci_board_t*, int num, volatile u32 * base, taupci_iq_t * iq, u32 iq_dma);
void TAUPCI_DDK_DLL taupci_halt (taupci_board_t*);
void TAUPCI_DDK_DLL taupci_hard_reset (taupci_board_t*);
u32 TAUPCI_DDK_DLL taupci_regio (taupci_chan_t*, int op, int reg, u32 val);

/* Callback registration. */
void TAUPCI_DDK_DLL taupci_register_transmit (taupci_chan_t*, void (*func) (taupci_chan_t *, void *, int));
void TAUPCI_DDK_DLL taupci_register_receive (taupci_chan_t*, void (*func) (taupci_chan_t *, u8 *, int));
void TAUPCI_DDK_DLL taupci_register_error (taupci_chan_t*, void (*func) (taupci_chan_t *, int));

/* Data transmittion. */
void TAUPCI_DDK_DLL taupci_start_chan (taupci_chan_t*, taupci_buf_t * tx, u32 tx_dma, taupci_buf_t * rx, u32 rx_dma);
void TAUPCI_DDK_DLL taupci_stop_chan (taupci_chan_t*);
void TAUPCI_DDK_DLL taupci_poweroff_chan (taupci_chan_t*);
void TAUPCI_DDK_DLL taupci_start_e1 (taupci_chan_t*);
void TAUPCI_DDK_DLL taupci_stop_e1 (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_transmit_space (taupci_chan_t*);
#define TAUPCI_SEND_NOSPACE     -1
#define TAUPCI_SEND_INVALID_LEN -2
int TAUPCI_DDK_DLL taupci_send_packet (taupci_chan_t*, u8 * data, unsigned len, void *tag);
int TAUPCI_DDK_DLL taupci_set_mru (taupci_chan_t*, unsigned mios);
void TAUPCI_DDK_DLL taupci_set_lloop (taupci_chan_t*, int on);

/* Interrupt control. */
#define TAUPCI_IRQ_STORM    -1
#define TAUPCI_IRQ_FAILED   -2
int TAUPCI_DDK_DLL taupci_handle_interrupt (taupci_board_t*);
int TAUPCI_DDK_DLL taupci_is_interrupt_pending (taupci_board_t*);
void TAUPCI_DDK_DLL taupci_enable_interrupt (taupci_board_t*, int on);

/* G.703 timer. */
void TAUPCI_DDK_DLL taupci_g703_timer (taupci_chan_t*);
/* E3 timer. */
void TAUPCI_DDK_DLL taupci_e3_timer (taupci_chan_t*);

/* LY: E1 timer, workaround for pcb-bug. */
void TAUPCI_DDK_DLL taupci_e1_timer (taupci_chan_t*);

/* LED control. */
void TAUPCI_DDK_DLL taupci_led (taupci_board_t*, int on);

/* Modem signals. */
void TAUPCI_DDK_DLL taupci_set_dtr (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_rts (taupci_chan_t*, int on);
int TAUPCI_DDK_DLL taupci_get_dsr (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_get_cd (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_get_cts (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_get_txcerr (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_get_rxcerr (taupci_chan_t*);

/* HDLC parameters. */
void TAUPCI_DDK_DLL taupci_set_baud (taupci_chan_t*, int baud);
void TAUPCI_DDK_DLL taupci_set_dpll (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_nrzi (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_invtxc (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_invrxc (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_hdlcmode (taupci_chan_t*, int bits /* 0, 16, 32 */, int share_flags);

/* Channel status, cable type. */
int TAUPCI_DDK_DLL taupci_get_rloop (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_get_lq (taupci_chan_t*);
int TAUPCI_DDK_DLL taupci_get_cable (taupci_chan_t*);

/* E1/G.703 parameters. */
void TAUPCI_DDK_DLL taupci_set_gsyn (taupci_chan_t*, int syn);
void TAUPCI_DDK_DLL taupci_set_ts (taupci_chan_t*, u32 ts);
void TAUPCI_DDK_DLL taupci_set_dir (taupci_chan_t*, int dir);
void TAUPCI_DDK_DLL taupci_set_mux (taupci_board_t*, int on);
void TAUPCI_DDK_DLL taupci_set_dxc_ts (taupci_board_t*, u8 * dxc);
void TAUPCI_DDK_DLL taupci_set_dxc_cas (taupci_board_t*, u8 * dxc);
void TAUPCI_DDK_DLL taupci_set_higain (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_cas (taupci_chan_t*, int mode);
void TAUPCI_DDK_DLL taupci_set_crc4 (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_phony (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_unfram (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_scrambler (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_monitor (taupci_chan_t*, int on);

/* E3 parameters. */
void TAUPCI_DDK_DLL taupci_set_rloop (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_ber (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_t3_long (taupci_chan_t*, int on);
void TAUPCI_DDK_DLL taupci_set_losais (taupci_chan_t*, int on);

int TAUPCI_DDK_DLL taupci_dacs (taupci_board_t*, int ch_a, int ts_a, int ch_b, int ts_b, int on, int include_cas);
unsigned TAUPCI_DDK_DLL taupci_hamming (u32 ts);
#endif /* DDK_USERMODE_ONLY */

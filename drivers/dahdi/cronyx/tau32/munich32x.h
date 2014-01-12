/*
 * Multichannel Network Interface Controller for HDLC (MUNICH32)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
 * Author: Leo Yuriev <ly@cronyx.ru>, http://leo.yuriev.ru
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 * $Id: munich32x.h,v 1.30 2009-07-10 11:34:12 ly Exp $
 */

#ifndef __M32X_UserCommands
#	define __M32X_UserCommands 0
#endif

#ifndef ARRAY_LENGTH
#	define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a[0]))
#endif

#ifndef ARRAY_END
#	define ARRAY_END(a) (&(a[ARRAY_LENGTH(a)]))
#endif

#define MX32_PHONY_STUB_FILL 1

/*--- types predefined by M32X hardware ------------------------------------------------------ */

#define M32X_RegistersBar1Size   0x100
#define M32X_PCI_CLOCK 33000000.0

enum M32X_LOOP_COMMANDS {	/* defines the M32X loop bits for PCM action */
	M32X_NO_LOOP = 0,	/* no loop */
	M32X_LOOP_FULL_INT = 1,	/* complete internal loop */
	M32X_LOOP_FULL_EXT = 2,	/* complete external loop (mirror) */
	M32X_LOOP_OFF = 3,	/* switch loops off */
	M32X_LOOP_CHAN_INT = 5,	/* channelwise internal loop */
	M32X_LOOP_CHAN_EXT = 6	/* channelwise external loop (mirror) */
};

typedef union tag_M32X_CCB_Action {
	struct {
		unsigned:2;
		unsigned ia:1;	/* interrupt attention (lbi) */
		unsigned loop:3;	/* loop control */
		unsigned res:1;	/* reset */
		unsigned:1;
		unsigned cnum:5;	/* channel number (lbi - 1 bit) */
		unsigned:1;
		unsigned ico:1;	/* initialize channel only (lbi) */
		unsigned in:1;	/* initialization */
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_CCB_Action;

enum M32X_MODE1_RATES {		/* defines the M32X bitrates for PCM core */
	M32X_RATE_1536 = 0,	/* 1.536 Mbit/s */
	M32X_RATE_1544 = 4,	/* 1.544 Mbit/s */
	M32X_RATE_3088 = 5,	/* 3.088 Mbit/s */
	M32X_RATE_6176 = 6,	/* 6.176 Mbit/s */
	M32X_RATE_2048 = 8,	/* 2.048 Mbit/s */
	M32X_RATE_4096 = 9,	/* 4.096 Mbit/s */
	M32X_RATE_8192 = 10	/* 8.192 Mbit/s */
};

typedef union tag_M32X_TimeslotAssignment {
	struct {
		unsigned rx_mask:8;	/* mask of timeslot bits for receive */
		unsigned rx_chan:5;	/* receive to this channel */
		unsigned rti:1;	/* receive timeslot inhibit (ignore) */
		unsigned:2;
		unsigned tx_mask:8;	/* mask of timeslot bits for transmit */
		unsigned tx_chan:5;	/* transmit from this channel */
		unsigned tti:1;	/* transmit timeslot inhibit (tristate) */
		unsigned:2;
	} fields ddk_packed;
	u32 raw ddk_packed;
} M32X_TimeslotAssignment;

enum M32X_CHANNEL_MODES {
	M32X_MODE_TRANSPARENT_A = 0,	/* transparent mode A */
	M32X_MODE_TRANSPARENT_B = 1,	/* transparent mode B or R */
	M32X_MODE_V110 = 2,	/* V.110 / X.30 mode */
	M32X_MODE_HDLC = 3	/* HDLC mode (lbi) */
};

enum M32X_COMMANDS {
	/*
	 * bits
	 */
	M32X___RA = 0x01,
	M32X___RO = 0x02,
	M32X___TH = 0x04,
	M32X___TA = 0x08,
	M32X___TO = 0x10,
	M32X___TI = 0x20,
	M32X___RI = 0x40,
	M32X___NITBS = 0x80,

	/*
	 * receive command group
	 */
	M32X_RECEIVE_CLEAR = 0,
	M32X_RECEIVE_FAST_ABORT = M32X___RA,
	M32X_RECEIVE_OFF = M32X___RO,
	M32X_RECEIVE_ABORT = M32X___RO | M32X___RA,
	M32X_RECEIVE_JUMP = M32X___RI,
	M32X_RECEIVE_INIT = M32X___RI | M32X___RA,
	M32X_RECEIVE_BITS = M32X___RA | M32X___RI | M32X___RO,

	/*
	 * transmit command group 1
	 */
	M32X_TRANSMIT_CLEAR = 0,
	M32X_TRANSMIT_FAST_ABORT = M32X___TA,
	M32X_TRANSMIT_OFF = M32X___TO,
	M32X_TRANSMIT_ABORT = M32X___TO | M32X___TA,
	M32X_TRANSMIT_JUMP = M32X___TI,
	M32X_TRANSMIT_INIT = M32X___TI | M32X___TA | M32X___NITBS,

	/*
	 * transmit command group 2
	 */
	M32X_TRANSMIT_HOLD = M32X___TH,
	M32X_TRANSMIT_BITS = M32X___TA | M32X___TI | M32X___TO | M32X___TH | M32X___NITBS
};

enum M32X_CIMASK {
	M32X_CIMASK_FE2 = 0x80,	/* transmit - data sent indication */
	M32X_CIMASK_SFE = 0x40,	/* receive - short frame detected */
	M32X_CIMASK_IFC = 0x20,	/* receive - idle/flag change */
	M32X_CIMASK_CH = 0x10,	/* receive - V.110 change of framing */
	M32X_CIMASK_TE = 0x08,	/* transmit - buffer error */
	M32X_CIMASK_RE = 0x04,	/* receive - framing error/overflow */
	M32X_CIMASK_FIR = 0x02,	/* receive - done indication */
	M32X_CIMASK_FIT = 0x01	/* transmit - done indication */
};

typedef union tag_M32X_ChannelSpecification_flags_a {
	struct {
		unsigned iftf:1;	/* interframe time fill: 0-7e, 1-ff */
		unsigned mode:2;	/* transmission mode */
		unsigned fa:1;	/* flag adjustment */
		unsigned trv:2;	/* transmission rate of V.110 */
		unsigned crc:1;	/* hdlc: crc32 or crc16; * transparent B: mode R or B */
		unsigned inv:1;	/* data inversion */
		unsigned tflag_cs:8;	/* flag in transparent mode, cs in HDLC mode */
		/*
		 * LY: these changes for easy manage both command & nibts
		 * unsigned command : 7;  // channel command
		 * unsigned nitbs  : 1;  // new itbs valid
		 */
		unsigned command_nitbs:8;	/* channel command & itbs flag */
		unsigned cimask:8;	/* per channel interrupt mask */
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_ChannelSpecification_flags_a;

typedef union tag_M32X_ChannelSpecification_flags_b {
	struct {
		unsigned itbs:6;	/* individual tx buffer size */
		unsigned:26;
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_ChannelSpecification_flags_b;

typedef struct tag_M32X_ChannelSpecification {
	M32X_ChannelSpecification_flags_a flags_a;
	u32 frda;	/* first receive descriptor address */
	u32 ftda;	/* first transmit descriptor address */
	M32X_ChannelSpecification_flags_b flags_b;
} M32X_ChannelSpecification;

typedef struct tag_M32X_ccb {
	M32X_CCB_Action Action;
	u32 reserved_a;
	u32 reserved_b;
	M32X_TimeslotAssignment Timeslots[32];
	M32X_ChannelSpecification chan[32];
	volatile u32 CurrentRxDescriptors[32];
	volatile u32 CurrentTxDescriptors[32];
} M32X_ccb;

typedef struct tag_M32X_LBI_ccb {
	M32X_CCB_Action Action;
	M32X_ChannelSpecification Channel_0, Channel_1;
	volatile u32 crda_0, crda_1;
	volatile u32 ctda_0, ctda_1;
} M32X_LBI_ccb;

typedef union tag_M32X_RegistersBar1_CMD {
	struct {
		unsigned arpcm:1;	/* action request - pcm core */
		unsigned arlbi:1;	/* action request - local bus */
		unsigned:2;
		unsigned timr:1;	/* timer run */
		unsigned:11;
		unsigned timv:16;	/* timer value */
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_RegistersBar1_CMD;

  /*
     * M32X Global Registers
   */
typedef struct tag_M32X_RegistersBar1 {
	union {
		u32 all ddk_packed;	/* 214 Configuration */
		struct {
			unsigned lbe:1;	/* little/big endian byte swap */
			unsigned dbe:1;	/* demux burst enable */
			unsigned lcd:2;	/* lbi timer/clock division */
			unsigned iom:1;	/* IOM-2 mode select */
			unsigned lbi:1;	/* lbi mode select (gp low) */
			unsigned ssc:1;	/* ssc mode select (gp high) */
			unsigned gpie:1;	/* gp bus interrupt enable */
			unsigned cst:1;	/* clock source timer */
			unsigned:23;
		} fields ddk_packed;
	} CONF_rw_00;

	u32 CMD_w_04;		/* 217 Command */
	u32 STAT_rw_08;
	u32 IMASK_rw_0C;	/* 220 Interrupt Mask */
	u32 reserved_10;	/* */
	u32 PIQBA_rw_14;	/* 221 Peripheral Interrupt Queue Base Address */
	u32 PIQL_rw_18;		/* 221 Peripheral Interrupt Queue Length */
	u32 reserved_1C;	/* */

	/*
	 * Serial PCM Core Registers
	 */
	union {
		u32 all ddk_packed;	/* 222 Mode1 */
		struct {
			unsigned mfl:13;	/* max frame len */
			unsigned mfld:1;	/* max frame len check disable */
			unsigned rid:1;	/* rx interrupt disable */
			unsigned ren:1;	/* rx enable */
			unsigned rbs:3;	/* rx bit shift */
			unsigned rts:3;	/* rx timeslot */
			unsigned tbs:3;	/* tx bit shift */
			unsigned tts:3;	/* tx timeslot */
			unsigned pcm:4;	/* PCM transmittion rate */
		} fields ddk_packed;
	} MODE1_rw_20;

	union {
		u32 all ddk_packed;	/* 225 Mode2 */
		struct {
			unsigned tsr:1;	/* tsp falling edge */
			unsigned rsf:1;	/* rsp falling edge */
			unsigned txr:1;	/* tx data rising edge */
			unsigned rxf:1;	/* rx data falling edge */
			unsigned teim:1;	/* tsync error interrupt mask */
			unsigned reim:1;	/* rsync error interrupt mask */
			unsigned lsim:1;	/* late stop interrupt mask */
			unsigned spoll:1;	/* slow poll */
			unsigned hpoll:1;	/* hold poll */
			unsigned:7;
			unsigned:16;
		} fields ddk_packed;
	} MODE2_rw_24;

	u32 CCBA_rw_28;		/* 227 CC Block Indirect Address */
	u32 TXPOLL_rw_2C;	/* 228 Tx Poll */
	u32 TIQBA_rw_30;	/* 233 Tx Interrupt Queue Base Address */

	u32 TIQL_rw_34;		/* 233 Tx Interrupt Queue Length */
	u32 RIQBA_rw_38;	/* 234 rx Interrupt Queue Base Address */

	u32 RIQL_rw_3C;		/* 234 rx Interrupt Queue Length */

	/*
	 * LBI Registers
	 */
	u32 LCONF_rw_40;	/* 235 LBI Configuration */
	u32 LCCBA_rw_44;	/* 239 LBI CC Block Indirect Address */
	u32 reserved_48;	/* */
	u32 LTRAN_w_4C;		/* 239 LBI Start Transfer */
	u32 LTIQBA_rw_50;	/* 240 LBI Tx Interrupt Queue Base Address */
	u32 LTIQL_rw_54;	/* 240 LBI Tx Interrupt Queue Length */
	u32 LRIQBA_rw_58;	/* 241 LBI rx Interrupt Queue Base Address */
	u32 LRIQL_rw_5C;	/* 241 LBI rx Interrupt Queue Length */
	u32 LREG0_rw_60;	/* 242 LBI Indirect External Config 0 */
	u32 LREG1_rw_64;	/* 242 LBI Indirect External Config 1 */
	u32 LREG2_rw_68;	/* 243 LBI Indirect External Config 2 */
	u32 LREG3_rw_6C;	/* 243 LBI Indirect External Config 3 */
	u32 LREG4_rw_70;	/* 244 LBI Indirect External Config 4 */
	u32 LREG5_rw_74;	/* 244 LBI Indirect External Config 5 */
	u32 LREG6_rw_78;	/* 245 LBI Indirect External Config 6 */
	u32 LSTAT_r_7C;		/* 248 LBI Status */

	/*
	 * GPP Registers
	 */
	u32 GPDIR_rw_80;	/* 249 General Purpose Bus Direction */
	u32 GPDATA_rw_84;	/* 249 General Purpose Bus Data */
	u32 GPOD_rw_88;		/* 250 General Purpose Bus Open Drain */
	u32 reserved_8C;	/* - */

	/*
	 * SCC Registers
	 */
	u32 SSCCON_rw_90;	/* 251 SSC Control */
	u32 SSCBR_rw_94;	/* 254 SSC Baud Rate Generator */
	u32 SSCTB_rw_98;	/* 255 SSC Tx Buffer */
	u32 SSCRB_r_9C;		/* 255 SSC rx Buffer */
	u32 SSCCSE_rw_A0;	/* 256 SSC Chip Select Enable */
	u32 SSCIM_rw_A4;	/* 257 SSC Interrupt Mask Register */
	u32 reserved_A8;	/* - */
	u32 reserved_AC;	/* - */

	/*
	 * IOMR-2 Registers
	 */
	u32 IOMCON1_rw_B0;	/* 258 IOMR-2 Control 1 */
	u32 IOMCON2_rw_B4;	/* 261 IOMR-2 Control 2 */
	u32 IOMSTAT_r_B8;	/* 263 IOMR-2 Status */
	u32 reserved_BC;	/* - */
	u32 IOMCIT0_rw_C0;	/* 265 IOMR-2 C/I Tx chan 0-3 */
	u32 IOMCIT1_rw_C4;	/* 265 IOMR-2 C/I Tx chan 4-7 */
	u32 IOMCIR0_r_C8;	/* 266 IOMR-2 C/I rx chan 0-3 */
	u32 IOMCIR1_r_CC;	/* 266 IOMR-2 C/I rx chan 4-7 */
	u32 IOMTMO_rw_D0;	/* 267 IOMR-2 Tx Monitor */
	u32 IOMRMO_r_D4;	/* 267 IOMR-2 rx Monitor */
	u32 reserved_D8;	/* - */
	u32 reserved_DC;	/* - */

	/*
	 * Mailbox Registers
	 */
	u32 MBCMD_rw_E0;	/* Mailbox Command */
	u32 MBDATA1_rw_E4;	/* Mailbox Data 1 */
	u32 MBDATA2_rw_E8;	/* Mailbox Data 2 */
	u32 MBDATA3_rw_EC;	/* Mailbox Data 3 */
	u32 MBDATA4_rw_F0;	/* Mailbox Data 4 */
	u32 MBDATA5_rw_F4;	/* Mailbox Data 5 */
	u32 MBDATA6_rw_F8;	/* Mailbox Data 6 */
	u32 MBDATA7_rw_FC;	/* Mailbox Data 7 */
} M32X_RegistersBar1;

enum M32X_STAT_BITS {
	M32X_STAT_PTI = 0x8000ul,	/* serial PCM tx interrupt */
	M32X_STAT_PRI = 0x4000ul,	/* serial PCM rx interrupt */
	M32X_STAT_LTI = 0x2000ul,	/* LBI tx interrupt */
	M32X_STAT_LRI = 0x1000ul,	/* LBI rx interrupt */
	M32X_STAT_IOMI = 0x0800ul,	/* peripheral IOM-2 interrupt */
	M32X_STAT_SSCI = 0x0400ul,	/* peripheral SSC interrupt */
	M32X_STAT_LBII = 0x0200ul,	/* peripheral LBI interrupt */
	M32X_STAT_MBII = 0x0100ul,	/* peripheral mailbox interrupt */
	M32X_IMASK_xxx = 0x0080ul,
	M32X_STAT_TI = 0x0040ul,	/* timer interrupt */
	M32X_STAT_TSPA = 0x0020ul,	/* PCM TSP asynchronous */
	M32X_STAT_RSPA = 0x0010ul,	/* PCM RSP asynchronous */
	M32X_STAT_LBIF = 0x0008ul,	/* LBI fail */
	M32X_STAT_LBIA = 0x0004ul,	/* LBI acknowledgement */
	M32X_STAT_PCMF = 0x0002ul,	/* serial PCM fail */
	M32X_STAT_PCMA = 0x0001ul	/* serial PCM acknowledgement */
};

#define M32X_IO_FE	0x80000000ul
#define M32X_IO_HOLD	0x40000000ul
#define M32X_IO_HI	0x20000000ul
#define M32X_IO_NO	0x1FFF0000ul
#define M32X_IO_NO_S	16u
#define M32X_TX_V110	0x00008000ul
#define M32X_TX_NO13	0x00001000ul
#define M32X_TX_CSM	0x00000800ul
#define M32X_TX_FNUM	0x000007FFul
#define M32X_RX_C	0x40000000ul
#define M32X_RX_STATUS	0x0000FF00ul

typedef union tag_M32X_RxDescriptorFlags1 {
	struct {
		unsigned:16;
		unsigned no:13;	/* receive buffer size in length, multiple 4 */
		unsigned hi:1;	/* interrupt host after this descriptor */
		unsigned hold:1;	/* do not branch to next descriptor */
		unsigned:1;
	} fields ddk_packed;
	u32 all_1 ddk_packed;
} M32X_RxDescriptorFlags1;

typedef union tag_M32X_RxDescriptorFlags2 {
	struct {
		unsigned:8;
		unsigned rof:1;	/* internal receive buffer overflow */
		unsigned ra:1;	/* receive abort */
		unsigned lfd:1;	/* long frame detected */
		unsigned nob:1;	/* not 8-bit divisible */
		unsigned crco:1;	/* crc error */
		unsigned loss:1;	/* sync pattern loss */
		unsigned sf:1;	/* short frame (for cs=0 only) */
		unsigned:1;
		unsigned bno:13;	/* number of length received */
		unsigned:1;
		unsigned c:1;	/* descriptor complete (done) */
		unsigned fe:1;	/* frame end */
	} fields ddk_packed;
	u32 all_2 ddk_packed;
} M32X_RxDescriptorFlags2;

typedef struct tag_M32X_RxDescriptor {
	M32X_RxDescriptorFlags1 u1;
	u32 next;	/* next descriptor address */
	u32 data;	/* data buffer address */
	M32X_RxDescriptorFlags2 u2;
} M32X_RxDescriptor;

typedef union tag_M32X_TxDescriptorFlags {
	struct {
		unsigned fnum:11;	/* fnum+1 flags between frames */
		unsigned csm:1;	/* do not generate CRC */
		unsigned no13:1;	/* byte count MSB */
		unsigned:2;
		unsigned v110:1;	/* v.110 only */
		unsigned no:13;	/* byte count */
		unsigned hi:1;	/* interrupt host after this descriptor */
		unsigned hold:1;	/* do not branch to next descriptor */
		unsigned fe:1;	/* frame end */
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_TxDescriptorFlags;

typedef struct tag_M32X_TxDescriptor {
	M32X_TxDescriptorFlags u;
	u32 next;	/* next descriptor address */
	u32 data;	/* data buffer address */
	u32 pad;
} M32X_TxDescriptor;

typedef union tag_M32X_TxInterruptQueue {
	struct {
		unsigned channel_number:5;
		unsigned late_stop:1;
		unsigned fe2:1;
		unsigned fo:1;	/* buffer chain underrun */
		unsigned err:1;
		unsigned:2;
		unsigned fi:1;	/* end of frame */
		unsigned hi:1;	/* host initiated intr_count */
		unsigned:3;
		unsigned:8;
		unsigned designator:8;
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_TxInterruptQueueItem;

typedef union tag_M32X_RxInterruptQueue {
	struct {
		unsigned channel_number:5;
		unsigned:2;
		unsigned fo:1;	/* buffer chain overrun */
		unsigned err:1;	/* framing error */
		unsigned sf:1;	/* short frame */
		unsigned ifc:1;	/* idle/flag change */
		unsigned fi:1;	/* end of frame */
		unsigned hi:1;	/* host initiated intr_count */
		unsigned x:1;
		unsigned sa:1;
		unsigned sb:1;
		unsigned e1:1;
		unsigned e2:1;
		unsigned e3:1;
		unsigned e4:1;
		unsigned e5:1;
		unsigned e6:1;
		unsigned e7:1;
		unsigned frc:1;
		unsigned designator:8;
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_RxInterruptQueueItem;

typedef union tag_M32X_PeripheralInterruptQueueItem {
	struct {
		unsigned info:24;
		unsigned designator:8;
	} fields ddk_packed;
	u32 all ddk_packed;
} M32X_PeripheralInterruptQueueItem;

enum M32X_INTERRUPT_VECTORS {
	M32X_IV_TRANSMIT = 0x20,	/* PCM transmit interrupt */
	M32X_IV_RECEIVE = 0x30,	/* PCM receive interrupt */
	M32X_IV_LBI_TRANSMIT = 0x60,	/* LBI DMA transmit interrupt */
	M32X_IV_LBI_RECEIVE = 0x70,	/* LBI DMA receive interrupt */
	M32X_IV_LBI_MAILBOX = 0xb0,	/* LBI mailbox interrupt */
	M32X_IV_SSC_TRANSMIT = 0xa0,	/* SSC transmit interrupt */
	M32X_IV_SSC_RECEIVE = 0xa1,	/* SSC receive interrupt */
	M32X_IV_IOM_TRANSMIT = 0x90,	/* IOM transmit interrupt */
	M32X_IV_IOM_RECEIVE = 0x91,	/* IOM receive interrupt */
	M32X_IV_GP = 0x85	/* GP bus interrupt */
};

/*--- software defined types --------------------------------------------------------------------- */

/*
 * M32X_Customize
 */
#define M32X_RxStubBufferSize		128	/* must be >= 128! */
#define M32X_TxStubBufferSizePerChannel	4
#define M32X_MaxRequests		TAU32_MAX_REQUESTS	/* requests allocation table size (M32X_UseChannels * 16) */
#define M32X_MaxBuffers			TAU32_MAX_BUFFERS	/* descriptors allocation table size (M32X_UseChannels * 16) */
#define M32X_InterruptQueueSize		(TAU32_MAX_BUFFERS * 2)	/* should be power of 2, bus less than 1024 */
#define M32X_InterruptQueueGap		8	/* the minimal gap, used for check overflow of Interrupt Queues */
#define M32X_ActionWaitMaxLoops		16	/* max loop count for synchronously wait for action acknowledge */
#define M32X_IoGapLength		8	/* preferenced tx/rx descriptors chain gap */
#define M32X_MaxActionsPerRequest	16
#define M32X_MaxInterruptsLoop		1000

enum M32X_START_RESULTS {
	M32X_RR_OK,
	M32X_RR_INTERRUPT,
	M32X_RR_TIMER,
	M32X_RR_ACTION
};

enum M32X_ACTION_STATUS {
	M32X_ACTION_VOID,
	M32X_ACTION_PENDING,
	M32X_PAUSE_PENDING,
	M32X_ACTION_OK,
	M32X_ACTION_FAIL,
	M32X_ACTION_TIMEOUT
};

struct _M32X_dma;
struct tag_M32X_InternalRequest;
struct tag_M32X_UserRequest;

typedef struct _M32X_dma {
	union {
		M32X_TxDescriptor tx;
		M32X_RxDescriptor rx;
		u32 raw[4];
		struct {
			union {
				struct {
					/*
					 * common bits for RX & TX descriptors
					 */
					unsigned:16;
					unsigned no:13;
					unsigned hi:1;
					unsigned hold:1;
					unsigned:1;
				} fields ddk_packed;
				u32 all_1 ddk_packed;
			} u;
			u32 next;
			u32 data;
			u32 all_2;
		} io ddk_packed;
	} u ddk_packed;
	u32 pa;
	struct _M32X_dma *from;
	union {
		struct _M32X_dma *__next;
		struct tag_M32X_InternalRequest *ir;
	} entry;
} M32X_dma_t;

typedef union tag_M32X_InternalStep {
	struct {
		unsigned short code:8;
		unsigned short tx:1;
		unsigned short rx:1;
		unsigned short config_new:1;
		unsigned short config_clear:1;
		unsigned short pause_count:4;
	} fields ddk_packed;
	u16 all ddk_packed;
} M32X_InternalStep;

enum M32X_STEPS {
	M32X_STEP_NOP,
	M32X_STEP_TX,
	M32X_STEP_RX,
	M32X_STEP_CONFIG_NEW,
	M32X_STEP_CONFIG_CLEAR,
	M32X_STEP_PAUSE,
	M32X_STEP_PAUSE_LONG,
	M32X_STEP_SET_LOOP_INT,
	M32X_STEP_SET_LOOP_EXT,
	M32X_STEP_SET_LOOP_OFF,
	M32X_STEP_MULTI_ABORT,
	M32X_STEP_MULTI_OFF,
	M32X_STEP_MULTI_RESTORE,
	M32X_CLK_OFF = 254,
	M32X_CLK_ON = 255
};

typedef struct tag_M32X_InternalRequest {
	M32X_ur_t *ur;
	struct {
		ddk_queue_entry_t entry;
		struct _M32X_dma *desc;
	} Io ddk_packed;
	struct {
		ddk_queue_entry_t entry;
		s8 RefCounter;
		u8 Total, Current, c;
		M32X_InternalStep Steps[M32X_MaxActionsPerRequest];
	} Manage ddk_packed;
#ifdef M32X_USER_DATA_REQUEST
	M32X_USER_DATA_REQUEST
#endif
} M32X_ir_t;

struct tag_M32X {
	u32 PCM_CCB_PhysicalAddress;
	M32X_ccb PCM_ccb;

	u32 LBI_CCB_PhysicalAddress;
	M32X_LBI_ccb LBI_ccb;

	volatile M32X_RegistersBar1 *RegistersBar1_Hardware;
	u32 pa;

	M32X_dma_t *ffd, *lfd;
	ddk_queue_head_t free_ir;
	unsigned tiqp, riqp, piqp;

	ddk_queue_head_t rq_in;
	ddk_queue_head_t rq_a;
	M32X_TimeslotAssignment TargetTimeslots[32];

	struct {
		ddk_bitops_t TimeslotsChanges;
		ddk_bitops_t tx, rx;
	} cm;

	/*
	 * chan array (vectorized)
	 */
	M32X_dma_t *last_tx[32];
	M32X_dma_t *halt_rx[32];
	M32X_ir_t *AutoActions[M32X_UseChannels];
	char tx_running[M32X_UseChannels];
	char rx_running[M32X_UseChannels];
	char tx_should_running[M32X_UseChannels];
	char rx_should_running[M32X_UseChannels];
	unsigned current_config[M32X_UseChannels];
	unsigned target_config[M32X_UseChannels];
	ddk_queue_head_t txq[M32X_UseChannels];
	ddk_queue_head_t rxq[M32X_UseChannels];

	M32X_uta uta[32];
	M32X_UserContext *uc;
	u8 CurrentLoopmode, TargetLoopmode;
	u8 DeepFlags;
	u8 HaveChanges;
	ddk_bitops_t RxFireMask, RxWatchdogMask;
	ddk_bitops_t TxFireMask;
	enum M32X_ACTION_STATUS as;
#ifdef M32X_USER_DATA
	M32X_USER_DATA
#endif
	M32X_ir_t RequestsPool[M32X_MaxRequests];
	M32X_dma_t DescriptorsPool[(M32X_IoGapLength + 1) * M32X_UseChannels * 2 + 32 * 2 + M32X_UseChannels];
	/*
	 * common-rx/tx-stub buffers
	 */
	u8 rx_stub_buffer[M32X_RxStubBufferSize];
#if MX32_PHONY_STUB_FILL
	u8 tx_stub_buffer[M32X_TxStubBufferSizePerChannel * M32X_UseChannels];
#else
	u8 tx_stub_buffer[M32X_TxStubBufferSizePerChannel];
#endif
	volatile u32 tiq[M32X_InterruptQueueSize];
	volatile u32 riq[M32X_InterruptQueueSize];
	volatile u32 piq[M32X_InterruptQueueSize];
};

#define M32X_ALLMASK ((u32)(					\
	(M32X_UseChannels < 32)					\
		? (1ul << M32X_UseChannels) - 1 : ~0ul))

/*--- M32X public/interface code -------------------------------- */

#if !defined (M32X_INTERNAL_CALL)
#	define M32X_INTERNAL_CALL
#endif

#ifndef M32X_INTERFACE_CALL
#	define M32X_INTERFACE_CALL
#endif

M32X_INTERFACE_CALL void M32X_Initialize (M32X* b,
					  void *Bar1VirtualAddress, u32 pa, M32X_UserContext * uc);
M32X_INTERFACE_CALL void M32X_DestructiveHalt (M32X* b, char CancelRequests);

M32X_INTERFACE_CALL char M32X_SubmitRequest (M32X* b, M32X_ur_t * ur);
M32X_INTERFACE_CALL char M32X_CancelRequest (M32X* b, M32X_ur_t * ur, char BreakIfRunning);
M32X_INTERFACE_CALL char M32X_HandleInterrupt (M32X* b);
M32X_INTERFACE_CALL enum M32X_START_RESULTS M32X_Start1 (M32X* b);
M32X_INTERFACE_CALL enum M32X_START_RESULTS M32X_Start2 (M32X* b);
M32X_INTERFACE_CALL char M32X_IsInterruptPending (M32X* b);
M32X_INTERFACE_CALL char M32X_IsRxOnlyTMA (M32X* b);
M32X_INTERFACE_CALL void M32X_SetPhonyFill_lsbf (M32X* b, unsigned cn, u32 pattern);

/*--------------------------------------------------------------------------------------------------- */

#if DDK_DEBUG
static void M32X_Bar1_Dump (M32X_RegistersBar1 * RegistersBar1);
static void M32X_Bar1_DumpNonZeroWriteables (M32X_RegistersBar1 * RegistersBar1);
static void M32X_DumpChannelSpecification (M32X* b, unsigned number);
static void M32X_DumpAction (M32X* b, u32 action);
static void M32X_DumpActionLite (M32X* b, u32 action);
#endif

static __forceinline void M32X_Bar1_Clear (M32X_RegistersBar1 * RegistersBar1_Target);
static unsigned M32X_BuildTimestotAssignment (M32X* b);
static void M32X_ch2ts (M32X* b, ddk_bitops_t Mask, unsigned c);
static __forceinline void M32X_Channel_ClearConfig_a (M32X_ChannelSpecification * cs);
static __forceinline void M32X_Pause_256bits (M32X* b, unsigned DueFrames);
static void M32X_Stall_256bits (M32X* b, unsigned DueFrames);
static void M32X_Request_FireLoop (M32X* b);
static void M32X_Request_Start (M32X* b, M32X_ir_t * ir);
static M32X_dma_t *M32X_Tx_GetFirst (M32X* b, unsigned c);
static M32X_dma_t *M32X_Rx_GetFirst (M32X* b, unsigned c);
static void M32X_Request_TryComplete (M32X* b, M32X_ir_t * ir);
static __forceinline void M32X_Actions_Complete (M32X* b);
static __forceinline void M32X_Tx_Complete (M32X* b, unsigned c);
static __forceinline void M32X_Rx_Complete (M32X* b, unsigned c);
static void M32X_Channel_BuildConfig (M32X* b, unsigned c);
static void M32X_Actions_FireLoop (M32X* b);
static void M32X_Actions_QueueOrStart (M32X* b, M32X_ir_t * ir);
static void M32X_Tx_HandleInterrupts (M32X* b);
static void M32X_Rx_HandleInterrupts (M32X* b);
static void M32X_AutoAction_Rx (M32X* b, unsigned c, unsigned Command);
static void M32X_AutoAction_Tx (M32X* b, unsigned c, unsigned Command);
static __forceinline void M32X_request_pause (M32X* b, M32X_ir_t * ir, unsigned DueFrames);
static __forceinline void M32X_UpdateRunningMask (M32X* b, unsigned c, ddk_bitops_t * pTxMask, ddk_bitops_t * pRxMask);
static void M32X_UpdateRunningMaskAllOff (M32X* b);

static void M32X_Rx_FireLoopAll (M32X* b);
static void M32X_Rx_FireLoopChan (M32X* b, unsigned c);
static void __M32X_Rx_FireLoopChan (M32X* b, unsigned c);
static void __M32X_Rx_FireLoop (M32X* b, unsigned c);

static void M32X_Tx_FireLoopAll (M32X* b);
static void M32X_Tx_FireLoopChan (M32X* b, unsigned c);
static void __M32X_Tx_FireLoopChan (M32X* b, unsigned c);
static void __M32X_Tx_FireLoop (M32X* b, unsigned c);

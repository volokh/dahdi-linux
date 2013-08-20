/*
 * Ioctl interface to Cronyx drivers bundle.
 *
 * Copyright (C) 2006-2009 Cronyx Engineering.
 * Author: Leo Yuriev, <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: cserial.h.in,v 1.36 2009-07-08 10:34:33 ly Exp $
 */

#ifndef __CRONYX_SERIAL_H__
#define __CRONYX_SERIAL_H__

#ifndef CRONYX_MAJOR_BASE
#	define CRONYX_MAJOR_BASE	222
#endif

#define CRONYX_MJR_BINDER		(CRONYX_MAJOR_BASE + 0)
#define CRONYX_MJR_SYNC_SERIAL		(CRONYX_MAJOR_BASE + 1)
#define CRONYX_MJR_ASYNC_SERIAL		(CRONYX_MAJOR_BASE + 2)
#define CRONYX_MJR_ASYNC_CALLOUT	(CRONYX_MAJOR_BASE + 3)
#define CRONYX_CONTROL_DEVICE		0

#define CRONYX_SVC_NONE			0
#define CRONYX_SVC_DIRECT		1
#define CRONYX_SVC_TTY_ASYNC		2
#define CRONYX_SVC_TTY_SYNC		3

#define CRONYX_ITEM_MAXNAME		64
#define CRONYX_ITEM_MAXDEEP		8
#define CRONYX_MINOR_MAX		256

#ifndef __KERNEL__
#	include <sys/types.h>
	typedef __int8_t s8;
	typedef __uint8_t u8;
	typedef __int16_t s16;
	typedef __uint16_t u16;
	typedef __int32_t s32;
	typedef __uint32_t u32;
	typedef __int64_t s64;
	typedef __uint64_t u64;
#endif /* __KERNEL__ */

/*
 * General channel statistics.
 */
struct cronyx_serial_statistics {
	u64 rintr;	/* receive interrupts */
	u64 tintr;	/* transmit interrupts */
	u64 mintr;	/* modem interrupts */
	u64 ibytes;	/* input bytes */
	u64 ipkts;	/* input packets */
	u64 ierrs;	/* input errors */
	u64 obytes;	/* output bytes */
	u64 opkts;	/* output packets */
	u64 oerrs;	/* output errors */
};

/*
 * Statistics for E1/G703 channels.
 */
struct cronyx_e1_counters {
	u64 bpv;	/* bipolar violations */
	u64 fse;	/* frame sync errors */
	u64 crce;	/* CRC errors */
	u64 rcrce;	/* remote CRC errors (E-bit) */
	u32 uas;	/* unavailable seconds */
	u32 les;	/* line errored seconds */
	u32 es;		/* errored seconds */
	u32 bes;	/* bursty errored seconds */
	u32 ses;	/* severely errored seconds */
	u32 oofs;	/* out of frame seconds */
	u32 css;	/* controlled slip seconds */
	u32 dm;		/* degraded minutes */
};

struct cronyx_e1_statistics {
	u32 status;	/* line status bit mask */
	u32 cursec;	/* seconds in current interval */
	u32 totsec;	/* total seconds elapsed */
	struct cronyx_e1_counters currnt;	/* current 15-min interval data */
	struct cronyx_e1_counters total;	/* total statistics data */
	struct cronyx_e1_counters interval[48];	/* 12 hour period data */
};

struct cronyx_e3_statistics {
	u32 status;
	u32 cursec;
	u32 totsec;
	u32 ccv;
	u32 tcv;
	u32 icv[48];
};

/*
 * Receive error codes.
 */
#define CRONYX_ERR_FRAMING		1	/* framing error */
#define CRONYX_ERR_CHECKSUM		2	/* parity/CRC error */
#define CRONYX_ERR_BREAK		3	/* break state */
#define CRONYX_ERR_OVERFLOW		4	/* receive fifo overflow */
#define CRONYX_ERR_UNDERFLOW		5	/* transmit fifo underrun */
#define CRONYX_ERR_OVERRUN		6	/* receive buffer overrun */
#define CRONYX_ERR_UNDERRUN		7	/* transmit buffer underrun */
#define CRONYX_ERR_BUS			8	/* system bus is too busy (e.g PCI) */

/*
 * E1 channel status.
 */
#define CRONYX_E1_LOS			0x0001	/* loss of signal */
#define CRONYX_E3_LOS			CRONYX_E1_LOS		/* -"- */
#define CRONYX_E1_LOF			0x0002	/* loss of framing */
#define CRONYX_E1_AIS			0x0004	/* receiving all ones */
#define CRONYX_E1_LOMF			0x0008	/* loss of multiframe sync */
#define CRONYX_E1_CRC4E			0x0010	/* crc4 errors */
#define CRONYX_E1_AIS16			0x0020	/* receiving all ones in timeslot 16 */
#define CRONYX_E1_RA			0x0040	/* receiving far loss of framing */
#define CRONYX_E1_RDMA			0x0080	/* receiving alarm in timeslot 16 */
#define CRONYX_E1_TSTREQ		0x0100	/* test code detected */
#define CRONYX_E1_TSTERR		0x0200	/* test error */
#define CRONYX_E1_CASERR		0x0400	/* LY: bad CAS with cas-strict option (raise by hardware) */
#define CRONYX_E3_TXE			0x0800	/* Transmit error */
#define CRONYX_E1_PENDING		0x1000	/* setup pending */
#define CRONYX_E1_OFF			0x2000	/* off */

struct cronyx_binder_enum_t {
	int from, total;
	int evolution, push;
	int ids[16];
};

struct cronyx_item_info_t {
	char name[CRONYX_ITEM_MAXNAME], alias[CRONYX_ITEM_MAXNAME];
	int id, parent, svc, minor, type, order;
};

struct cronyx_param_t {
	long value, extra;
};

struct cronyx_proto_t {
	char proname[8];
};

struct cronyx_dxc_t {		/* cross-connector parameters */
	unsigned char ts[32];	/* timeslot number */
	unsigned char link[32];	/* E1 link number */
};

struct cronyx_ctl_t {
	int target_id, param_id;
	union {
		struct cronyx_proto_t proto;
		struct cronyx_param_t param;
		struct cronyx_dxc_t dxc;
		struct cronyx_serial_statistics stat_channel;
		struct cronyx_e1_statistics stat_e1;
		struct cronyx_e3_statistics stat_e3;
	} u;
};

#define CRONYX_BUNDLE_VER	_IOR ('x', 1, char [256])
#define CRONYX_ITEM_ENUM	_IOWR ('x', 2, struct cronyx_binder_enum_t)
#define CRONYX_ITEM_INFO	_IOWR ('x', 2, struct cronyx_item_info_t)
#define CRONYX_SELF_INFO	_IOR ('x', 2, struct cronyx_item_info_t)
#define CRONYX_PUSH_EVO		_IOW ('x', 2, int)

#define CRONYX_GET		_IOR ('x', 3, struct cronyx_ctl_t)
#define CRONYX_SET		_IOW ('x', 3, struct cronyx_ctl_t)

enum cronyx_param_id {
	cronyx_void = 0,
#define CRONYX_MODE_ASYNC		1
#define CRONYX_MODE_HDLC		2
#define CRONYX_MODE_PHONY		3
#define CRONYX_MODE_VOICE		56
	cronyx_channel_mode,
#define CRONYX_LOOP_NONE		4
#define CRONYX_LOOP_INTERNAL		5
#define CRONYX_LOOP_LINEMIRROR		6
#define CRONYX_LOOP_REMOTE		7
	cronyx_loop_mode,
#define CRONYX_NRZ			8
#define CRONYX_NRZI			9
#define CRONYX_HDB3			10
#define CRONYX_AMI			11
	cronyx_line_code,
#define CRONYX_MODE_SEPARATE		12
#define CRONYX_MODE_SPLIT		13
#define CRONYX_MODE_MUX			14
#define CRONYX_MODE_B			55
	cronyx_adapter_mode,
#define CRONYX_CASMODE_OFF		15
#define CRONYX_CASMODE_SET		16
#define CRONYX_CASMODE_PASS		17
#define CRONYX_CASMODE_CROSS		18
	cronyx_cas_mode,
#define CRONYX_ICLK_NORMAL		19
#define CRONYX_ICLK_RX			20
#define CRONYX_ICLK_TX			21
#define CRONYX_ICLK			22
	cronyx_invclk_mode,
#define CRONYX_LEDMODE_DEFAULT		0x00
#define CRONYX_LEDMODE_4IRQ		0x01
#define CRONYX_LEDMODE_4RX		0x02
#define CRONYX_LEDMODE_4TX		0x04
#define CRONYX_LEDMODE_4ERR		0x08
#define CRONYX_LEDMODE_ON		0x10
#define CRONYX_LEDMODE_OFF		0x20
#define CRONYX_LEDMODE_CADENCE		0x30
	cronyx_led_mode,
#define CRONYX_NONE			23
#define CRONYX_RS232			24
#define CRONYX_RS449			25
#define CRONYX_RS530			26
#define CRONYX_E1			27
#define CRONYX_E3			28
#define CRONYX_V35			29
#define CRONYX_G703			30
#define CRONYX_X21			31
#define CRONYX_RS485			32
#define CRONYX_COAX			33
#define CRONYX_TP			34
#define CRONYX_UNKNOWN			35
#define CRONYX_SERIAL			36
#define CRONYX_AUTO			37
	cronyx_port_or_cable_type,
#define CRONYX_E1CLK_MANAGED		38
#define CRONYX_E1CLK_INTERNAL		39
#define CRONYX_E1CLK_RECEIVE		40
#define CRONYX_E1CLK_RECEIVE_CHAN0	41
#define CRONYX_E1CLK_RECEIVE_CHAN1	42
#define CRONYX_E1CLK_RECEIVE_CHAN2	43
#define CRONYX_E1CLK_RECEIVE_CHAN3	44
	cronyx_sync_mode,

	cronyx_timeslots_use,
	cronyx_timeslots_subchan,
	cronyx_baud,
	cronyx_iface_bind,
	cronyx_debug,
	cronyx_add_dlci,
	cronyx_modem_status,

	cronyx_crc4,
	cronyx_dpll,
	cronyx_higain,
	cronyx_master,
	cronyx_unframed,
	cronyx_monitor,
	cronyx_scrambler,
	cronyx_t3_long,

	cronyx_inlevel_sdb,

	cronyx_mtu,
	cronyx_qlen,

	cronyx_proto,
	cronyx_dxc,
	cronyx_stat_channel,
	cronyx_stat_e1,
	cronyx_stat_e3,

	cronyx_reset,
	cronyx_clear_stat,
	cronyx_ec_delay,
	cronyx_qlen_limit,

#define CRONYX_CRC_NONE			45
#define CRONYX_CRC_16			46
#define CRONYX_CRC_32			47
	cronyx_crc_mode,

#define CRONYX_HDLC_2FLAGS		49
#define CRONYX_HDLC_SHARE		50
	cronyx_hdlc_flags,

#define CRONYX_CAS_ITU			51
#define CRONYX_CAS_STRICT		52
	cronyx_cas_flags,

#define CRONYX_IFACE_UP			53
#define CRONYX_IFACE_DOWN		54
	cronyx_iface_updown,

	cronyx_ts_test,

/* LY: определены выше
	#define CRONYX_MODE_B		55
	#define CRONYX_MODE_VOICE	56
*/

	cronyx_flag_channel2link	= 1024,
	cronyx_flag_canfail		= 2048
};

#define CRONYX_ITEM_REMOVED	0
#define CRONYX_ITEM_ADAPTER	1
#define CRONYX_ITEM_INTERFACE	2
#define CRONYX_ITEM_CHANNEL	3

#ifdef __KERNEL__

#	define CRONYX_PARITY_NONE	51
#	define CRONYX_PARITY_EVEN	52
#	define CRONYX_PARITY_ODD	53
#	define CRONYX_PARITY_0		54
#	define CRONYX_PARITY_1		55

typedef struct _cronyx_proto_t cronyx_proto_t;
typedef struct _cronyx_binder_item cronyx_binder_item_t;

void cronyx_binder_register_protocol(cronyx_proto_t *);
void cronyx_binder_unregister_protocol(cronyx_proto_t *);
int cronyx_binder_register_item (int parent, const char *prefix, int order_parent, const char *alias, int order_total,
				 cronyx_binder_item_t *);
int cronyx_binder_unregister_item (int id);
int cronyx_binder_minor_get (cronyx_binder_item_t *, int service);
void cronyx_binder_minor_put (cronyx_binder_item_t *);
void cronyx_receive (cronyx_binder_item_t *, struct sk_buff *skb);
void cronyx_receive_error (cronyx_binder_item_t *, int error);
void cronyx_transmit_done (cronyx_binder_item_t *);
void cronyx_modem_event (cronyx_binder_item_t *);
void cronyx_transmit_error (cronyx_binder_item_t *, int error);
char *cronyx_format_irq (int raw_irq, int apic_irq);

struct cronyx_e1ts_map_t {
	int length;
	s8 ts_index[32];
};

#ifdef CRONYX_LYSAP
#	include "lysap-linux.h"
#elif defined (__KERNEL__)
	typedef struct _lysap_stream_t lysap_stream_t;
	typedef struct _lysap_trunk_t lysap_trunk_t;
	typedef struct _lysap_trunk_config_t lysap_trunk_config_t;
	typedef struct _LYSAP_DeviceConfig_t LYSAP_DeviceConfig;
	typedef struct _LYSAP_StreamConfig_t LYSAP_StreamConfig;
	typedef struct _LYSAP_DeviceBitCounters_t LYSAP_DeviceBitCounters;
	typedef struct _lysap_buf_t lysap_buf_t;
#endif /* CRONYX_LYSAP */

/*
 * Hardware channel driver structure.
 */
struct sk_buff;

typedef struct _cronyx_dispatch_t {
	void (*second_timer) (cronyx_binder_item_t *);

	/*
	 * Interface for protocol
	 */
	int (*link_up) (cronyx_binder_item_t *);
	void (*link_down) (cronyx_binder_item_t *);
#ifndef CRONYX_LYSAP
	int (*transmit) (cronyx_binder_item_t *, struct sk_buff *);
#endif
	void (*set_dtr) (cronyx_binder_item_t *, int val);
	void (*set_rts) (cronyx_binder_item_t *, int val);
	int (*query_dtr) (cronyx_binder_item_t *);
	int (*query_rts) (cronyx_binder_item_t *);
	int (*query_dsr) (cronyx_binder_item_t *);
	int (*query_cts) (cronyx_binder_item_t *);
	int (*query_dcd) (cronyx_binder_item_t *);

	/*
	 * Control interface
	 */
	int (*ioctl) (cronyx_binder_item_t *, unsigned cmd, long arg);
	int (*ctl_get) (cronyx_binder_item_t *, struct cronyx_ctl_t *);
	int (*ctl_set) (cronyx_binder_item_t *, struct cronyx_ctl_t *);
	void (*notify_proto_changed) (cronyx_binder_item_t *);

	/* zaptel/asterisk.org dacs (timeslot cross-connection) */
	int (*phony_get_e1ts_map) (cronyx_binder_item_t *, struct cronyx_e1ts_map_t *);
	int (*phony_dacs) (cronyx_binder_item_t *, int a_ts, cronyx_binder_item_t * b, int b_ts);

#ifndef CRONYX_LYSAP
	/*
	 * Interface for async protocol
	 */
	void (*set_async_param) (cronyx_binder_item_t *, int baud, int bits, int parity,
				 int stop2, int ignpar, int rtscts, int ixon, int ixany, int symstart, int symstop);
	void (*send_break) (cronyx_binder_item_t *, int msec);
	void (*send_xon) (cronyx_binder_item_t *);
	void (*send_xoff) (cronyx_binder_item_t *);
	void (*start_transmitter) (cronyx_binder_item_t *);
	void (*stop_transmitter) (cronyx_binder_item_t *);
	void (*flush_transmit_buffer) (cronyx_binder_item_t *);
#else
	/*
	 * LYSAP interface
	 */
	int (*lysap_conform) (cronyx_binder_item_t *, lysap_trunk_config_t *,
				   LYSAP_DeviceConfig *, LYSAP_StreamConfig *,
				   unsigned PayloadMtu, unsigned LongestSector);
	u64 (*lysap_freq) (cronyx_binder_item_t *, u64 freq, int probe);
	unsigned (*lysap_get_status) (cronyx_binder_item_t *);
	int (*lysap_set_clock_master) (cronyx_binder_item_t *, int enable);
	void (*lysap_get_bitcnt) (cronyx_binder_item_t *, LYSAP_DeviceBitCounters *);
#endif
	struct device *(*get_device)(cronyx_binder_item_t *);
} cronyx_dispatch_t;

struct _cronyx_binder_item {
	int id;			/* binder's id */
	unsigned char type;	/* binder's type */
	unsigned char debug;	/* debug level, 0..2 */
	unsigned char running;	/* running, 0..1 */
	unsigned char svc;
	int order;
	char name[CRONYX_ITEM_MAXNAME], alias[CRONYX_ITEM_MAXNAME];

	struct module *provider;

	void *hw;		/* hardware layer private data */
	int mtu;		/* max packet size */
	int fifosz;		/* total hardware i/o buffer size */
	int minor;		/* minor number 1..255, assigned by binder */

	cronyx_proto_t *proto;	/* protocol interface data */
	void *sw;		/* protocol private data */

	cronyx_dispatch_t dispatch;
};

int cronyx_ctl_get (cronyx_binder_item_t *, struct cronyx_ctl_t *);
int cronyx_ctl_set (cronyx_binder_item_t *, struct cronyx_ctl_t *);
int cronyx_param_get (cronyx_binder_item_t *, int id, long *value);
int cronyx_param_set (cronyx_binder_item_t *, int id, long value);

/*
 * Protocol driver structure.
 */
struct _cronyx_proto_t {
	cronyx_proto_t *next;
	char *name;

	/*
	 * Interface for channel
	 */
	void (*notify_receive) (cronyx_binder_item_t *, struct sk_buff *);
	void (*notify_receive_error) (cronyx_binder_item_t *, int error);
	void (*notify_transmit_done) (cronyx_binder_item_t *);
	void (*notify_modem_event) (cronyx_binder_item_t *);
	void (*notify_transmit_error) (cronyx_binder_item_t *, int error);

	/*
	 * Interface for binder
	 */
	int (*open) (cronyx_binder_item_t *);
	void (*release) (cronyx_binder_item_t *);
	int (*read) (cronyx_binder_item_t *, unsigned flg, char *buf, int len);
	int (*write) (cronyx_binder_item_t *, unsigned flg, const char *buf, int len);
	int (*select) (cronyx_binder_item_t *, struct poll_table_struct *st, struct file *filp);
	struct fasync_struct *fasync;

	/*
	 * Control interface
	 */
	int (*attach) (cronyx_binder_item_t *);
	int (*detach) (cronyx_binder_item_t *);
	int (*ioctl) (cronyx_binder_item_t *, unsigned cmd, long arg);
	int (*ctl_get) (cronyx_binder_item_t *, struct cronyx_ctl_t *);
	int (*ctl_set) (cronyx_binder_item_t *, struct cronyx_ctl_t *);

#ifdef CRONYX_LYSAP
	/*
	 * LYSAP interface
	 */
	void (*lysap_receive) (cronyx_binder_item_t *, lysap_buf_t *);
	void (*lysap_transmit_done) (cronyx_binder_item_t *, lysap_buf_t *);
#endif

	unsigned dispatch_priority:1;	/* LY: if >0 would't deffered, but call proto directly from irq-handler */
	unsigned chanmode_lock:1;
	unsigned timeslots_lock:1;
	unsigned mtu_lock:1;
};

#	define CRONYX_LEDSTATE_OFF	0	/* led must be off */
#	define CRONYX_LEDSTATE_OK	1	/* normal state */
#	define CRONYX_LEDSTATE_MIRROR	2	/* line-mirror or/and bert indication */
#	define CRONYX_LEDSTATE_WARN	3	/* remote alarm, ais, etc, (short flashes) */
#	define CRONYX_LEDSTATE_ALARM	4	/* jitter limit, slips, etc, (fast blinking) */
#	define CRONYX_LEDSTATE_FAILED	5	/* los, lof, crc-errors, etc, (slow blinking) */
#	define CRONYX_LEDSTATE_MAX	6

struct cronyx_led_flasher_t {
	struct list_head entry;
#ifdef CONFIG_PREEMPT_RT
	int use_rawlock;
	union {
		raw_spinlock_t *raw;
		spinlock_t *normal;
		void *ptr;
	} lock;
#else
	spinlock_t *lock;
#endif	/* CONFIG_PREEMPT_RT */
	long time_off;
	struct timer_list timer;
	char mode, last, counter, kick;
	u32 cadence;
	void *tag;
	void (*led_set) (void *, int on);
	int (*get_state) (void *);
};

#ifdef CONFIG_PREEMPT_RT
	void cronyx_led_init_ex (struct cronyx_led_flasher_t *, int raw_spinlock,
		void *spinlock, void *tag, void (*led_set) (void *, int on), int (*get_state) (void *));
	static inline void cronyx_led_init (struct cronyx_led_flasher_t *f, spinlock_t *spinlock,
		void *tag, void (*led_set) (void *, int on), int (*get_state) (void *)) {
		cronyx_led_init_ex (f, 0, spinlock, tag, led_set, get_state);
	}
#else
	void cronyx_led_init (struct cronyx_led_flasher_t *, spinlock_t *, void *tag, void (*led_set) (void *, int on),
		   int (*get_state) (void *));
#	define cronyx_led_init_ex(f, raw_flag, spinlock, tag, led_set, led_state) \
		cronyx_led_init (f, spinlock, tag, led_set, led_state)
#endif /* CONFIG_PREEMPT_RT */

void cronyx_led_destroy (struct cronyx_led_flasher_t *);
void cronyx_led_kick (struct cronyx_led_flasher_t *, unsigned kick);
int cronyx_led_ctl (struct cronyx_led_flasher_t *, unsigned cmd, struct cronyx_ctl_t *ctl);
int cronyx_led_getblink (struct cronyx_led_flasher_t *);

void binder_deffered_flush (void);
int binder_deffered_queue1 (cronyx_binder_item_t *, char *reason, void (*call) (cronyx_binder_item_t *));
int binder_deffered_queue2 (cronyx_binder_item_t *, char *reason, void (*call) (cronyx_binder_item_t *, void *), void *param);
int cronyx_get_modem_status (cronyx_binder_item_t *);
int cronyx_probe_irq (int irq, void *ptr, void (*irq_updown) (void *, int));
cronyx_binder_item_t *cronyx_minor2item (int minor);
cronyx_binder_item_t *cronyx_binder_item_get_parent(cronyx_binder_item_t *h,
																										int type);

#endif /* __KERNEL__ */
#endif /* __CRONYX_SERIAL_H__ */

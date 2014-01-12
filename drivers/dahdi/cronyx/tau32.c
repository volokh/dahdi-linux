/*
 * Cronyx Tau-PCI/32 adapter driver for Linux.
 *
 * Copyright (C) 2003-2009 Cronyx Engineering, http://www.cronyx.ru
 * Author: Leo Yuriev <ly@cronyx.ru>, http://leo.yuriev.ru
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: tau32.c,v 1.144 2009-07-09 17:54:44 ly Exp $
 */
#include "module.h"
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <asm/io.h>
#include "cserial.h"

#if !defined (__GNUC__) || (__GNUC__ < 3)
#	error "Cronyx Tau-PCI/32 module required at least GCC 3.0"
#endif

#ifndef CONFIG_PCI
#	error "Cronyx Tau-PCI/32 module required PCI-bus support."
#endif

#ifndef DMA_32BIT_MASK
#	define DMA_32BIT_MASK 0x00000000FFFFFFFFull
#endif

/* #define LY_DEEP_DEBUG */

#define TAU32_DEFAULT_MTU		1504
#define TAU32_DEFAULT_QUEUE_LENGTH	8

/* Module information */
MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx driver for Tau-PCI/32 adapters serie\n");
MODULE_LICENSE ("Dual BSD/GPL");

#define TAU32_CHANNELS	4
#include "tau32-ddk.h"

#ifndef TAU32_hdlc_shareflags
#	define TAU32_hdlc_shareflags 0
#endif

struct _tau32_board_t;
struct _tau32_chan_t;
struct _tau32_request_t;
struct _tau32_iface_t;

#if defined (CRONYX_LYSAP) && defined (CONFIG_PREEMPT_RT) && defined (LYSAP_USE_RAW_LOCK)
#	define TAU32_USE_RAW_LOCK
#endif

#ifdef TAU32_USE_RAW_LOCK
#	define tau32_spinlock_t raw_spinlock_t
#else
#	define tau32_spinlock_t spinlock_t
#endif

typedef struct _tau32_request_t {
	TAU32_UserRequest ddk;	/* must be first for typecast */
	struct list_head entry;
#ifdef CRONYX_LYSAP
	lysap_buf_t *lysap;
#else
	struct sk_buff *skb;
#endif				/* CRONYX_LYSAP */
} tau32_request_t;

typedef struct _tau32_chan_t {
	struct _tau32_board_t *board;
	int rx_pending, tx_pending;
#ifndef CRONYX_LYSAP
	unsigned tx_phony_fifo;
#endif
	unsigned io_qlen, io_raw_fill, io_ph_len;
	unsigned phony_chunk;
	struct list_head pending_rx_list;
	struct list_head pending_tx_list;
#ifdef CRONYX_LYSAP
	unsigned chunk, scale;
#endif /* CRONYX_LYSAP */
	struct _stat_t {
		u64 rintr;
		u64 tintr;
		u64 ibytes;
		u64 obytes;
		u64 ipkts;
		u64 opkts;
		u64 overrun;
		u64 frame;
		u64 crc;
		u64 underrun;
		u64 mintr;
		u64 oerrs;
	} stat;
	struct cronyx_e1ts_map_t e1ts_map;

	struct _config_t {
		u32 ts;
		union {
			struct {
				unsigned link:8;
				unsigned phony:1;
				unsigned voice:1;
				unsigned up:1;
				unsigned nocrc:1;
				unsigned crc32:1;
#ifdef TAU32_hdlc_shareflags
				unsigned share_flags:1;
#endif
			};
			unsigned all_misc;
		};
	} last_config, config;
	cronyx_binder_item_t sys;
	char overrun, underrun, dacs, need_tsmap;
} tau32_chan_t;

typedef struct _tau32_iface_t {
	struct _tau32_board_t *board;
	struct {
		u32 acc_status, last_acc_status;
		unsigned long degerr;
		unsigned long degsec;
		unsigned long last_bpv;	/* bipolar violations */
		unsigned long last_fse;	/* frame sync errors */
		unsigned long last_crce;	/* CRC errors */
		unsigned long last_rcrce;	/* remote CRC errors (E-bit) */
		unsigned long last_slips;
		unsigned long crce;
		struct cronyx_e1_statistics e1_stat;
		unsigned cfg_update_pending:8;
		unsigned casmode:8, crc4:1, higain:1, monitor:1, scrambler:1, ami:1;
		unsigned loop_int:1, loop_mirror:1, cas_strict:1;
		u32 ddk_e1_config, ddk_e1_ts_mask;
	} state;
	cronyx_binder_item_t binder_data;
} tau32_iface_t;

#define TAU32_FATAL	-1
#define TAU32_HALTED	0
#define TAU32_RUNNING	1

typedef struct _tau32_board_t {
	TAU32_UserContext ddk;	/* must be first for typecast */
	dma_addr_t iomem;
	int irq_apic, irq_raw;	/* != 0 if we connected to IRQ */
	tau32_spinlock_t lock;
	struct pci_dev *pdev;
	signed char ok, unframed, gsyn;
	u32 unframed_speed;
	tau32_iface_t ifaces[TAU32_MAX_INTERFACES];
	u64 intr_count;
#ifdef CRONYX_LYSAP
	tau32_chan_t *clock_master;
	u64 lygen_freq;
#endif /* CRONYX_LYSAP */
	struct list_head pending_list;
	struct timer_list commit_timer;
	tau32_chan_t *channels[TAU32_CHANNELS];
	u8 tsbox[TAU32_TIMESLOTS];
	cronyx_binder_item_t binder_data;
	struct cronyx_led_flasher_t led_flasher;
} tau32_board_t;

may_static int dev_counter = -1;

#if LINUX_VERSION_CODE >= 0x020600
may_static mempool_t *tau32_request_pool;
#endif

may_static int tau32_init_adapter (tau32_board_t *b);
may_static int tau32_halt_adapter (tau32_board_t *b);
may_static void tau32_hard_reset (tau32_board_t *b);
may_static void tau32_free_adapter (tau32_board_t *b);
may_static void tau32_schedule_commit (tau32_board_t *b);
may_static void tau32_clr_e1_stat (tau32_iface_t * i);
may_static void tau32_count_int (tau32_board_t *b, unsigned flash);

may_static int tau32_query_dcd (cronyx_binder_item_t *h);
may_static int tau32_query_cts (cronyx_binder_item_t *h);
may_static int tau32_query_dsr (cronyx_binder_item_t *h);
may_static int tau32_query_rtsdtr (cronyx_binder_item_t *h);
may_static void tau32_down (cronyx_binder_item_t *h);
may_static int tau32_up (cronyx_binder_item_t *h);
may_static int tau32_ctl_set (cronyx_binder_item_t *h, struct cronyx_ctl_t *ctl);
may_static int tau32_ctl_get (cronyx_binder_item_t *h, struct cronyx_ctl_t *ctl);
may_static int tau32_get_e1ts_map (cronyx_binder_item_t *h, struct cronyx_e1ts_map_t *e1ts_map);

/*
	may_static int tau32_phony_dacs (cronyx_binder_item_t * a,
		int a_ts, cronyx_binder_item_t * b, int b_ts);
*/

may_static void TAU32_CALLBACK_TYPE tau32_status_callback (TAU32_UserContext * pContext, int Item, unsigned NotifyBits);
may_static void TAU32_CALLBACK_TYPE tau32_error_callback (TAU32_UserContext * pContext, int Item, unsigned NotifyBits);
may_static void TAU32_CALLBACK_TYPE tau32_rx_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest);
may_static void TAU32_CALLBACK_TYPE tau32_munich32x_cfg_callback (TAU32_UserContext * pContext,
							      TAU32_UserRequest * pUserRequest);
may_static void TAU32_CALLBACK_TYPE tau32_e1_cfg_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest);
may_static tau32_request_t *tau32_rx_alloc (tau32_chan_t * c, tau32_request_t * r);
may_static tau32_request_t *tau32_tx_alloc (tau32_chan_t * c, tau32_request_t * r);
may_static tau32_request_t *tau32_cfg_alloc (tau32_chan_t * c);
may_static int tau32_cfg_submit (tau32_board_t *b, tau32_request_t * r);
may_static void tau32_rx_make_queue (tau32_chan_t * c, tau32_request_t * r);
may_static void tau32_tx_make_queue (tau32_chan_t * c, tau32_request_t * r);
may_static void tau32_free_request (tau32_request_t * r);
may_static int tau32_have_pending (tau32_board_t *b);
may_static void tau32_notify_proto_changed (cronyx_binder_item_t *h);

#ifndef CRONYX_LYSAP
may_static void TAU32_CALLBACK_TYPE tau32_tx_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest);
may_static int tau32_transmit (cronyx_binder_item_t *h, struct sk_buff *skb);
may_static int tau32_phony_fill (tau32_chan_t * c, void *data_buffer, unsigned data_length);
#endif

may_static void tau32_second_timer (cronyx_binder_item_t *h);
may_static int tau32_update_e1_config (tau32_iface_t * i);
may_static int tau32_update_chan_config (tau32_chan_t * c);
may_static int tau32_update_dxc (tau32_board_t *b);
may_static void tau32_cancel_queue (tau32_chan_t * c, struct list_head *queue, int *pending, int force);

may_static __inline void tau32_cancel_tx (tau32_chan_t * c, int force)
{
	tau32_cancel_queue (c, &c->pending_tx_list, &c->tx_pending, force);
}

may_static __inline void tau32_cancel_rx (tau32_chan_t * c, int force)
{
	tau32_cancel_queue (c, &c->pending_rx_list, &c->rx_pending, force);
}

#ifdef CRONYX_LYSAP
may_static u64 tau32_lysap_freq (cronyx_binder_item_t *, u64 in_freq, int probe);
may_static unsigned tau32_lysap_get_status (cronyx_binder_item_t *);
may_static int tau32_lysap_conform (cronyx_binder_item_t *, lysap_trunk_config_t *,
				    LYSAP_DeviceConfig *, LYSAP_StreamConfig *,
				    unsigned PayloadMtu, unsigned LongestSector);
may_static int tau32_set_clock_master (cronyx_binder_item_t *, int enable);
may_static void tau32_get_bitcnt (cronyx_binder_item_t *, LYSAP_DeviceBitCounters *r);
may_static void TAU32_CALLBACK_TYPE tau32_tx_callback_lysap (TAU32_UserContext *, TAU32_UserRequest *);
#endif /* CRONYX_LYSAP */
may_static struct device *tau32_get_device(cronyx_binder_item_t *h);

may_static const char device_name[] = "Cronyx Tau-PCI/32";

may_static cronyx_dispatch_t dispatch_tab = {
	.link_up = tau32_up,
	.link_down = tau32_down,
#ifndef CRONYX_LYSAP
	.transmit = tau32_transmit,
#endif
	.query_dtr = tau32_query_rtsdtr,
	.query_rts = tau32_query_rtsdtr,
	.query_dsr = tau32_query_dsr,
	.query_cts = tau32_query_cts,
	.query_dcd = tau32_query_dcd,
	.ctl_set = tau32_ctl_set,
	.ctl_get = tau32_ctl_get,
	.phony_get_e1ts_map = tau32_get_e1ts_map,
	.notify_proto_changed = tau32_notify_proto_changed,
#ifdef CRONYX_LYSAP
	.lysap_conform = tau32_lysap_conform,
	.lysap_freq = tau32_lysap_freq,
	.lysap_get_status = tau32_lysap_get_status,
	.lysap_set_clock_master = tau32_set_clock_master,
	.lysap_get_bitcnt = tau32_get_bitcnt,
#endif /* CRONYX_LYSAP */
	.get_device = tau32_get_device,
};

may_static unsigned get_status (tau32_iface_t *i)
{
	TAU32_E1_State * hw = &i->board->ddk.InterfacesInfo[i->binder_data.order];
	unsigned result = 0;

	if (i->state.cfg_update_pending)
		return CRONYX_E1_PENDING;
	if (i->state.acc_status & TAU32_E1OFF)
		return CRONYX_E1_OFF;
	if (i->state.acc_status & TAU32_RCL)
		return CRONYX_E1_LOS;
	if (i->state.acc_status & TAU32_RUA1)
		return CRONYX_E1_AIS;
	if (i->state.acc_status & TAU32_RFAS)
		return CRONYX_E1_LOF;

	if (i->state.acc_status & TAU32_RRA)
		result |= CRONYX_E1_RA;
	if ((i->state.acc_status & TAU32_BCAS) == CRONYX_E1_CASERR) {
		result |= CRONYX_E1_CASERR;
		if (i->state.acc_status & TAU32_RCAS)
			result |= CRONYX_E1_LOMF;
	} else if (i->state.acc_status & TAU32_RSA1)
		result |= CRONYX_E1_AIS16;
	else {
		if (i->state.acc_status & (TAU32_RCAS | TAU32_RSA0 | TAU32_RSA1))
			result |= CRONYX_E1_LOMF;
		else if (i->state.acc_status & TAU32_RDMA)
			result |= CRONYX_E1_RDMA;
	}
	if ((i->state.acc_status & TAU32_RCRC4) != 0 || i->state.crce
		|| hw->Crc4Errors != i->state.last_crce)
		result |= CRONYX_E1_CRC4E;
	return result;
}

may_static u32 ts2speed (u32 ts)
{
	return 64000ul * hweight32 (ts);
}

may_static u32 unfram_speed2ts (u32 baud)
{
	switch (baud) {
		case 64000 * 1:
			return 0x00000001ul;
		case 64000 * 2:
			return 0x00000003ul;
		case 64000 * 4:
			return 0x0000000Ful;
		case 64000 * 8:
			return 0x000000FFul;
		case 64000 * 16:
			return 0x0000FFFFul;
		case 64000 * 32:
			return 0xFFFFFFFFul;
		default:
			return 0;
	}
}

may_static unsigned unfram_speed2mode (u32 baud)
{
	switch (baud) {
		case 64000 * 1:
			return TAU32_unframed_64;
		case 64000 * 2:
			return TAU32_unframed_128;
		case 64000 * 4:
			return TAU32_unframed_256;
		case 64000 * 8:
			return TAU32_unframed_512;
		case 64000 * 16:
			return TAU32_unframed_1024;
		case 64000 * 32:
			return TAU32_unframed_2048;
		default:
			return TAU32_LineAIS;
	}
}

may_static void tau32_clr_e1_stat (tau32_iface_t * i)
{
	if (i->board) {
		TAU32_E1_State * hw = &i->board->ddk.InterfacesInfo[i->binder_data.order];

		i->state.acc_status = hw->Status;
		i->state.degerr = 0;
		i->state.degsec = 0;
		memset (&i->state.e1_stat, 0, sizeof (i->state.e1_stat));
	}
}

may_static int tau32_is_tsmap_locked (tau32_chan_t * c)
{
	if (c->config.up && c->dacs)
		return 1;
	if (c->sys.proto && c->sys.proto->timeslots_lock)
		return 1;
	if (c->need_tsmap)
		return 1;
	return 0;
}

may_static int tau32_update_e1_config (tau32_iface_t * i)
{
	u32 ts_mask = 0;
	u32 cfg;
	int error;

	if (unlikely (i->board->ok != TAU32_RUNNING))
		return -ENODEV;

	if (i->state.loop_int)
		cfg = TAU32_LineLoopInt;
	else if (i->state.loop_mirror)
		cfg = TAU32_LineLoopExt;
	else {
#ifdef CRONYX_LYSAP
		cfg = TAU32_LineNormal | TAU32_ais_on_los | TAU32_ais_on_lof;
#else
		unsigned n;

		cfg = TAU32_LineOff;
		for (n = 0; n < TAU32_CHANNELS; n++) {
			tau32_chan_t *c = i->board->channels[n];

			if (c && c->config.link == i->binder_data.order && c->config.ts) {
				cfg = TAU32_LineNormal | TAU32_ais_on_los | TAU32_ais_on_lof;
				break;
			}
		}
		if (cfg == TAU32_LineOff && i->state.ddk_e1_config) {
			unsigned other_e1_status = i->board->ddk.InterfacesInfo[i->binder_data.order ^ 1].Status;

			if (i->state.casmode != CRONYX_CASMODE_CROSS ||
			(other_e1_status & (TAU32_E1OFF | TAU32_LOS | TAU32_AIS | TAU32_LOF | TAU32_AIS16 | TAU32_LOMF)) != 0)
				cfg = TAU32_LineAIS;
		}
#endif
	}

	if (i->state.ami)
		cfg |= TAU32_tx_ami | TAU32_rx_ami;
	if (i->state.higain)
		cfg |= TAU32_higain;
	if (i->state.monitor)
		cfg |= TAU32_monitor;

	if (i->board->unframed) {
		if (i->binder_data.order == 0) {
			ts_mask = unfram_speed2ts (i->board->unframed_speed);
			if (i->state.scrambler)
				cfg |= TAU32_scrambler;
			cfg |= unfram_speed2mode (i->board->unframed_speed);
		} else
			cfg = TAU32_LineOff;
	} else {
		cfg |= TAU32_auto_ais;	/* LY: transmit AIS if no data available for E1, e.g. no any channel up */
		switch (i->state.casmode) {
			case CRONYX_CASMODE_OFF:
				cfg |= TAU32_framed_no_cas;
				break;
			case CRONYX_CASMODE_SET:
				cfg |= TAU32_framed_cas_set;
				break;
			case CRONYX_CASMODE_CROSS:
				if (i->board->ddk.Interfaces > 1) {
					cfg |= i->state.loop_int ? TAU32_framed_cas_pass : TAU32_framed_cas_cross;
					break;
				}
				i->state.casmode = CRONYX_CASMODE_PASS;
			case CRONYX_CASMODE_PASS:
				cfg |= TAU32_framed_cas_pass;
				break;
		}
		if (i->state.casmode != CRONYX_CASMODE_OFF && i->state.cas_strict)
			cfg |= TAU32_strict_cas;
		if (i->state.crc4)
			cfg |= TAU32_crc4_mf;
	}
	if (cfg != i->state.ddk_e1_config || ts_mask != i->state.ddk_e1_ts_mask) {
		tau32_request_t *r = tau32_cfg_alloc (0);

		if (unlikely (r == 0))
			return -ENOMEM;

		error = tau32_update_dxc (i->board);
		if (error < 0) {
			tau32_free_request (r);
			return error;
		}

		r->ddk.Command = TAU32_Configure_E1;
		r->ddk.Io.InterfaceConfig.Interface = i->binder_data.order ? TAU32_E1_B : TAU32_E1_A;
		r->ddk.Io.InterfaceConfig.Config = cfg;
		r->ddk.Io.InterfaceConfig.UnframedTsMask = ts_mask;
		if (unlikely (!tau32_cfg_submit (i->board, r))) {
			printk (KERN_ERR "ce: Adapter #%d - error update E1-config\n", i->board->binder_data.order);
			return -EIO;
		}
		i->state.cfg_update_pending = 3;
		i->state.ddk_e1_config = cfg;
		i->state.ddk_e1_ts_mask = ts_mask;
		tau32_clr_e1_stat (i);
	}
	return 0;
}

may_static void tau32_notify_proto_changed (cronyx_binder_item_t *h)
{
	unsigned long flags;

	if (h->type == CRONYX_ITEM_CHANNEL) {
		tau32_chan_t *c = (tau32_chan_t *) h->hw;

		spin_lock_irqsave (&c->board->lock, flags);
		if (!c->sys.proto)
			c->need_tsmap = 0;
		spin_unlock_irqrestore (&c->board->lock, flags);
	}
}

may_static int tau32_get_e1ts_map (cronyx_binder_item_t *h, struct cronyx_e1ts_map_t *e1ts_map)
{
	int error;

	if (h->type != CRONYX_ITEM_CHANNEL)
		error = -EINVAL;
	else {
		unsigned long flags;
		tau32_chan_t *c = (tau32_chan_t *) h->hw;

		spin_lock_irqsave (&c->board->lock, flags);
		error = 0;
		if (!c->need_tsmap) {
			c->need_tsmap = 1;
			error = tau32_update_dxc (c->board);
		}
		memcpy (e1ts_map, &c->e1ts_map, sizeof (c->e1ts_map));
		spin_unlock_irqrestore (&c->board->lock, flags);
	}
	return error;
}

may_static int tau32_is_cas_cross (tau32_board_t *b)
{
	return b->ifaces[0].state.casmode == CRONYX_CASMODE_CROSS || b->ifaces[1].state.casmode == CRONYX_CASMODE_CROSS;
}

may_static unsigned map_ts (tau32_chan_t * c, int cross_cas_ts)
{
	int i, ts0;

	ts0 = 0;
	if (c->config.phony) {
		/* LY: workaround for Infineon bug, try to don't use TS0 in TMA mode. */
		if ((c->board->tsbox[0] & 31) == c->sys.order)
			c->board->tsbox[0] = 0x80;
		ts0++;
	}

	if (!c->config.up && !c->need_tsmap) {
		/*
		 * LY: look only for currently mapped ts, and don't lock any other
		 */
		for (i = ts0; i < TAU32_TIMESLOTS; i++)
			if (i != cross_cas_ts && c->board->tsbox[i] == (c->sys.order | 0x60)) {
				c->board->tsbox[i] &= ~0x40;
				break;
			}
	} else {
		do {
			/*
			 * LY: look for previosly mapped ts
			 */
			for (i = ts0; i < TAU32_TIMESLOTS; i++)
				if (i != cross_cas_ts && c->board->tsbox[i] == (c->sys.order | 0x60))
					goto done;
			/*
			 * LY: look for never mapped ts
			 */
			for (i = ts0; i < TAU32_TIMESLOTS; i++)
				if (i != cross_cas_ts && c->board->tsbox[i] & 0x80)
					goto done;
			/*
			 * LY: look for any ts which is not used/mapped now
			 */
			for (i = ts0; i < TAU32_TIMESLOTS; i++)
				if (i != cross_cas_ts && c->board->tsbox[i] & 0x40)
					goto done;
			/*
			 * LY: look for any ts which is not mapped now
			 */
			for (i = ts0; i < TAU32_TIMESLOTS; i++)
				if (i != cross_cas_ts && c->board->tsbox[i] & 0x20)
					goto done;
		} while (cross_cas_ts < 0 && ts0--);
		return TAU32_CROSS_OFF;
	done:
		c->board->tsbox[i] = c->sys.order;
	}
	return i;
}

may_static int tau32_update_dxc (tau32_board_t *b)
{
	tau32_chan_t *c;
	TAU32_CrossMatrix cross;
	u32 phony_mask, bypass_mask;
	int i;
	tau32_request_t *r;
	int may_have_changes = 0;
	int cross_cas_ts = -1;

	if (unlikely (b->ok != TAU32_RUNNING))
		return -ENODEV;

	r = tau32_cfg_alloc (0);
	if (unlikely (r == 0))
		return -ENOMEM;

	memset (&r->ddk.Io.TimeslotsAssignment, 0, sizeof (r->ddk.Io.TimeslotsAssignment));
	memset (cross, TAU32_CROSS_OFF, sizeof (cross));
	phony_mask = 0;
	bypass_mask = 0xFFFFFFFFul;

	if (b->unframed) {
		r->ddk.Command = TAU32_Timeslots_Channel;
		r->ddk.Io.ChannelConfig.Channel = 0;
		r->ddk.Io.ChannelConfig.AssignedTsMask = unfram_speed2ts (b->unframed_speed);
		for (i = 0; i < TAU32_CHANNELS; i++) {
			c = b->channels[i];
			if (unlikely (c == 0) || c->config.up == 0 || c->config.link >= b->ddk.Interfaces)
				continue;

			bypass_mask = 0;
			may_have_changes = 1;

			if (c->sys.order)
				c->config.ts = 0;
			else {
				c->config.ts = r->ddk.Io.ChannelConfig.AssignedTsMask;
				c->config.link = 0;
			}
		}
	} else {
		tau32_chan_t *cas_chan = 0;

		if (tau32_is_cas_cross (b)) {
			cross_cas_ts = 16;
			for (i = 0; i < TAU32_CHANNELS; i++) {
				c = b->channels[i];
				if (unlikely (c == 0 || c->config.link >= b->ddk.Interfaces))
					continue;
				if (!c->config.phony || !(c->config.up | c->need_tsmap))
					continue;
				if (c->config.ts & (1ul << 16)) {
					cas_chan = c;
					break;
				}
			}
		}

		for (i = 0; i < TAU32_TIMESLOTS; i++) {
			if (i == cross_cas_ts)
				b->tsbox[16] = 0x80;
			else if (b->tsbox[i] < 0x80)
				b->tsbox[i] |= 0x60;
		}

		r->ddk.Command = TAU32_Timeslots_Complete;
		for (i = -1; i < TAU32_CHANNELS; i++) {
			int j, n;
			unsigned char line[TAU32_TIMESLOTS];

			if (i < 0)
				c = cas_chan;
			else {
				c = b->channels[i];
				if (c == cas_chan)
					continue;
			}

			if (c == NULL || c->config.link >= b->ddk.Interfaces || !c->config.ts)
				continue;

			n = 0;
			for (j = 0; j < TAU32_TIMESLOTS; j++) {
				if (c->config.ts & (1ul << j)) {
					unsigned l, k;

					if (c == cas_chan) {
						k = j;
						b->tsbox[j] = c->sys.order;
					} else {
						k = map_ts (c, cross_cas_ts);
						for (l = 0; l < n; l++) {
							if (line[l] != cross_cas_ts && line[l] > k) {
								unsigned char t;

								t = line[l];
								line[l] = k;
								k = t;
							}
						}
					}
					line[n++] = k;
				}
			}

			bypass_mask &= ~c->config.ts;
			may_have_changes |= c->config.up | c->need_tsmap;
			n = 0;
			c->e1ts_map.length = 0;
			for (j = 0; j < TAU32_TIMESLOTS; j++) {
				if (c->config.ts & (1ul << j)) {
					unsigned k = line[n++];
					unsigned l = (c->config.link + 1) * TAU32_TIMESLOTS + j;

					if (k < TAU32_TIMESLOTS) {
						r->ddk.Io.TimeslotsAssignment.Complete[k].RxChannel =
							r->ddk.Io.TimeslotsAssignment.Complete[k].TxChannel =
							c->sys.order;
						r->ddk.Io.TimeslotsAssignment.Complete[k].TxFillmask =
							r->ddk.Io.TimeslotsAssignment.Complete[k].RxFillmask = 0xFF;
						cross[k] = l;
						if (c->config.phony) {
							phony_mask |= 1ul << k;
							c->e1ts_map.ts_index[c->e1ts_map.length++] =
								(cross_cas_ts < 0) ? j : k;
						}
					} else if (k == TAU32_CROSS_OFF)
						c->config.ts &= ~(1ul << j);
					cross[l] = k;
#ifdef LY_DEEP_DEBUG
					if (c->config.up)
						printk ("ce%d: %d = %d <- %d.%d\n", c->sys.order, j, k, l / 32 - 1,
							l % 32);
#endif
				}
			}
		}

		for (i = 0; i < TAU32_TIMESLOTS; i++) {
			if (bypass_mask & (1ul << i)) {
				cross[i + 64] = i + 32;
				cross[i + 32] = i + 64;
			}
		}
	}

#ifdef LY_DEEP_DEBUG
	int j;

	for (i = 0; i < TAU32_CHANNELS; i++) {

		c = b->channels[i];
		if (c == NULL || c->config.link >= b->ddk.Interfaces || !c->config.up)
			continue;

		printk ("ce%d chan-ts:", i);
		for (j = 0; j < TAU32_TIMESLOTS; j++) {
			if (r->ddk.Io.TimeslotsAssignment.Complete[j].RxChannel == i
			    && r->ddk.Io.TimeslotsAssignment.Complete[j].RxFillmask)
				printk (" %2d", j);
		}
		printk ("\n");
	}
	for (j = 0; j < TAU32_TIMESLOTS * 3; j++)
		if (cross[j] != TAU32_CROSS_OFF)
			printk ("cross: %d.%d <- %d.%d\n", j / 32, j % 32, cross[j] / 32, cross[j] % 32);
		else
			printk ("cross: %d.%d <- OFF\n", j / 32, j % 32);
	printk ("phony-mask: %08X\n", phony_mask);
#endif

	if (may_have_changes) {
		if (unlikely (!tau32_cfg_submit (b, r))) {
			printk (KERN_ERR "ce: Adapter #%d - error update ts-map\n", b->binder_data.order);
			return -EIO;
		}
	} else
		tau32_free_request (r);

	if (cross_cas_ts > 0) {
		if (unlikely (!TAU32_SetCrossMatrixCas (b->ddk.pControllerObject, cross))) {
			printk (KERN_ERR "ce: Adapter #%d - error update cas-dxc\n", b->binder_data.order);
			return -EIO;
		}
	} else
		TAU32_SetCrossMatrixCas (b->ddk.pControllerObject, 0);

	/* LY-TODO: put here DACS's logic for update the cross[]. */

	if (unlikely (!TAU32_SetCrossMatrix (b->ddk.pControllerObject, cross, phony_mask))) {
		printk (KERN_ERR "ce: Adapter #%d - error update ts-dxc\n", b->binder_data.order);
		return -EIO;
	}

	/*
	 * update modem sences
	 */
	for (i = 0; i < TAU32_CHANNELS; i++) {
		c = b->channels[i];
		if (unlikely (c == 0 || c->config.link >= b->ddk.Interfaces))
			continue;

		c->last_config.phony = c->config.phony;
		if (c->config.ts != c->last_config.ts || c->config.link != c->last_config.link) {
			if (c->config.up)
				c->last_config.ts = c->config.ts;
			if (c->config.link < b->ddk.Interfaces)
				c->last_config.link = c->config.link;
#if defined (CRONYX_LYSAP)
			cronyx_modem_event (&c->sys);
#else
			spin_unlock (&b->lock);
			cronyx_modem_event (&c->sys);
			spin_lock (&b->lock);
#endif /* CRONYX_LYSAP */
		}
	}

	return 0;
}

may_static int tau32_update_chan_config (tau32_chan_t * c)
{
	int i;
	tau32_request_t *r;
	tau32_board_t *b = c->board;
	int error;

	if (unlikely (b->ok != TAU32_RUNNING))
		return -ENODEV;

	if (c->config.up && c->config.link >= b->ddk.Interfaces)
		return -EINVAL;

	if (!b->unframed) {
		if (!c->config.phony)
			c->config.ts &= ~(1ul << 0);
		if (c->config.link < b->ddk.Interfaces && b->ifaces[c->config.link].state.casmode == CRONYX_CASMODE_SET)
			c->config.ts &= ~(1ul << 16);
	}

	if (c->config.all_misc != c->last_config.all_misc || c->config.ts != c->last_config.ts) {
		for (i = 0; i < TAU32_CHANNELS; i++) {
			tau32_chan_t *cx = b->channels[i];

			if (0 == cx || c == cx)
				continue;
			if ((c->config.ts & cx->config.ts & (1ul << 16))
			    && tau32_is_cas_cross (b) && tau32_is_tsmap_locked (cx))
				return -EBUSY;
			if (cx->config.link == c->config.link && (cx->config.ts & c->config.ts)) {
				if (tau32_is_tsmap_locked (cx))
					return -EBUSY;
				cx->config.ts &= ~c->config.ts;
			}
		}
		error = tau32_update_dxc (b);
		if (error < 0)
			return error;
	}

	r = tau32_cfg_alloc (c);
	if (unlikely (r == 0))
		return -ENOMEM;

	if (!c->last_config.up && c->config.up) {
		if (c->config.ts == 0) {
			c->config.ts = c->last_config.ts;
			tau32_free_request (r);
			return -EINVAL;
		}
		r->ddk.Command |= TAU32_Rx_Start | TAU32_Tx_Start | TAU32_Configure_Commit;
	}

	r->ddk.Command |= TAU32_Configure_Channel;
	r->ddk.Io.ChannelConfig.Config = TAU32_HDLC | TAU32_fr_rx_splitcheck | TAU32_fr_rx_fitcheck | TAU32_fr_tx_auto;
	if (c->config.phony)
		r->ddk.Io.ChannelConfig.Config = TAU32_TMA | TAU32_tma_nopack;
	else {
		if (c->config.nocrc)
			r->ddk.Io.ChannelConfig.Config |= TAU32_hdlc_nocrc;
		else if (c->config.crc32)
			r->ddk.Io.ChannelConfig.Config |= TAU32_hdlc_crc32;
#if TAU32_hdlc_shareflags
		if (c->config.share_flags)
			r->ddk.Io.ChannelConfig.Config |= TAU32_hdlc_shareflags;
#endif
	}

	if (c->last_config.up && !c->config.up)
		r->ddk.Command |= TAU32_Rx_Stop | TAU32_Tx_Stop | TAU32_Configure_Commit;

	i = (r->ddk.Command & TAU32_Configure_Commit) != 0;
	if (unlikely (!tau32_cfg_submit (b, r))) {
		printk (KERN_ERR "ce: error config channel %s\n", c->sys.name);
		return -EIO;
	}
	c->last_config = c->config;
	if (i == 0)
		tau32_schedule_commit (b);

	if (b->ddk.Interfaces > 1)
		tau32_update_e1_config (&b->ifaces[1]);
	tau32_update_e1_config (&b->ifaces[0]);

	return 0;
}

may_static void commit_cfg (unsigned long arg)
{
	tau32_board_t *b = (tau32_board_t *) arg;
	tau32_request_t *r;
	unsigned long flags;

	spin_lock_irqsave (&b->lock, flags);
	if (likely (b->ok == TAU32_RUNNING)) {
		r = tau32_cfg_alloc (0);
		if (unlikely (r == 0))
			tau32_schedule_commit (b);
		else {
			r->ddk.Command = TAU32_Configure_Commit;
			tau32_cfg_submit (b, r);
		}
	}
	spin_unlock_irqrestore (&b->lock, flags);
}

may_static void tau32_schedule_commit (tau32_board_t *b)
{
	b->commit_timer.function = commit_cfg;
	b->commit_timer.data = (unsigned long) b;
	mod_timer (&b->commit_timer, jiffies + (HZ + 19) / 20);	/* 50 msec */
}

may_static int tau32_ctl_get (cronyx_binder_item_t *h, struct cronyx_ctl_t *ctl)
{
	int n, error;
	tau32_chan_t *c;
	tau32_board_t *b;
	tau32_iface_t *i;
	unsigned long flags;

	if (h->hw == 0)
		return -ENODEV;

	switch (h->type) {
		default:
			return -ENODEV;
		case CRONYX_ITEM_ADAPTER:
			b = (tau32_board_t *) h->hw;
			c = 0;
			i = 0;
			break;
		case CRONYX_ITEM_INTERFACE:
			i = (tau32_iface_t *) h->hw;
			b = (tau32_board_t *) i->board;
			c = 0;
			break;
		case CRONYX_ITEM_CHANNEL:
			c = (tau32_chan_t *) h->hw;
			b = (tau32_board_t *) c->board;
			i = 0;
			if ((ctl->param_id & cronyx_flag_channel2link) != 0 && c->config.link < b->ddk.Interfaces)
				i = &b->ifaces[c->config.link];
			break;
	}

	if (b->ok != TAU32_RUNNING)
		return -ENODEV;

	if (b->unframed && c != 0 && c->sys.order)
		return -EBUSY;

	if (b->unframed && i != 0 && i->binder_data.order)
		return -EBUSY;

	error = 0;
	switch (ctl->param_id) {
		case cronyx_channel_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: get-mode\n");
			if (c == 0)
				error = -EINVAL;
			else if (! c->config.phony)
				ctl->u.param.value = CRONYX_MODE_HDLC;
			else
				ctl->u.param.value = (!b->unframed && c->config.voice) ? CRONYX_MODE_VOICE : CRONYX_MODE_PHONY;
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: get-crc-mode\n");
			if (c == 0 || c->config.phony)
				error = -EINVAL;
			else if (c->config.nocrc)
				ctl->u.param.value = CRONYX_CRC_NONE;
			else if (c->config.crc32)
				ctl->u.param.value = CRONYX_CRC_32;
			else
				ctl->u.param.value = CRONYX_CRC_16;
			break;

		case cronyx_hdlc_flags:
			CRONYX_LOG_1 (h, "tau32.ctl: get-hdlc-flags\n");
			if (c == 0 || c->config.phony)
				error = -EINVAL;
			else if (c->config.share_flags)
				ctl->u.param.value = CRONYX_HDLC_SHARE;
			else
				ctl->u.param.value = CRONYX_HDLC_2FLAGS;
			break;

		case cronyx_loop_mode:
		case cronyx_loop_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: get-loop\n");
			if (i == 0)
				error = -EINVAL;
			else {
				ctl->u.param.value = CRONYX_LOOP_NONE;
				if (i->state.loop_int)
					ctl->u.param.value = CRONYX_LOOP_INTERNAL;
				if (i->state.loop_mirror)
					ctl->u.param.value = CRONYX_LOOP_LINEMIRROR;
			}
			break;

		case cronyx_line_code:
		case cronyx_line_code | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: get-line\n");
			if (i == 0)
				error = -EINVAL;
			else
				ctl->u.param.value = i->state.ami ? CRONYX_AMI : CRONYX_HDB3;
			break;

		case cronyx_adapter_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: get-adapter\n");
			if (h->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				ctl->u.param.value = CRONYX_MODE_MUX;
			break;

		case cronyx_cas_flags:
		case cronyx_cas_flags | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: get-cas-flags\n");
			if (i == 0 || i->state.casmode == CRONYX_CASMODE_OFF)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else
				ctl->u.param.value = i->state.cas_strict ? CRONYX_CAS_STRICT : CRONYX_CAS_ITU;
			break;

		case cronyx_cas_mode:
		case cronyx_cas_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: get-cas\n");
			if (i == 0)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else
				ctl->u.param.value = i->state.casmode;
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: get-led\n");
			if (h->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_GET, ctl);
			break;

		case cronyx_port_or_cable_type:
			CRONYX_LOG_1 (h, "tau32.ctl: get-cable-type\n");
			if (i == 0)
				error = -EINVAL;
			else {
				ctl->u.param.value = CRONYX_COAX;
				if (b->ddk.CableTypeJumpers & (i->binder_data.order ? TAU32_B_MASK : TAU32_A_MASK))
					ctl->u.param.value = CRONYX_TP;
			}
			break;

		case cronyx_sync_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: get-clock\n");
			if (h->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else {
				switch (b->gsyn) {
					case TAU32_SYNC_LYGEN:
						ctl->u.param.value = CRONYX_E1CLK_MANAGED;
						break;
					default:
					case TAU32_SYNC_INTERNAL:
						ctl->u.param.value = CRONYX_E1CLK_INTERNAL;
						break;
					case TAU32_SYNC_RCV_A:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE_CHAN0;
						if (b->ddk.Interfaces < 2)
							ctl->u.param.value = CRONYX_E1CLK_RECEIVE;
						break;
					case TAU32_SYNC_RCV_B:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE_CHAN1;
						break;
				}
			}
			break;

		case cronyx_timeslots_use:
			CRONYX_LOG_1 (h, "tau32.ctl: get-timeslots\n");
			if (c == 0)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else
				ctl->u.param.value = c->config.up ? c->last_config.ts : c->config.ts;
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (h, "tau32.ctl: get-baud\n");
			if (c == 0)
				error = -EINVAL;
			else if (b->unframed) {
				if (c->sys.order == 0)
					ctl->u.param.value = b->unframed_speed;
				else
					error = -EBUSY;
			} else {
				ctl->u.param.value = ts2speed (c->config.up ? c->last_config.ts : c->config.ts);
				if (ctl->u.param.value == 0)
					error = -ENODEV;
			}
			break;

		case cronyx_iface_bind:
			CRONYX_LOG_1 (h, "tau32.ctl: get-iface-bind\n");
			if (c == 0 || b->ddk.Interfaces < 2)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else {
				ctl->u.param.value = c->config.up ? c->last_config.link : c->config.link;
				if (ctl->u.param.value >= b->ddk.Interfaces)
					error = -EINVAL;
			}
			break;

		case cronyx_crc4:
		case cronyx_crc4 | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: get-crc4\n");
			if (i == 0)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else
				ctl->u.param.value = i->state.crc4;
			break;

		case cronyx_higain:
		case cronyx_higain | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: get-higain\n");
			if (i == 0)
				error = -EINVAL;
			else
				ctl->u.param.value = i->state.higain;
			break;

		case cronyx_unframed:
			CRONYX_LOG_1 (h, "tau32.ctl: get-unframed\n");
			if (i == 0)
				error = -EINVAL;
			else
				ctl->u.param.value = b->unframed;
			break;

		case cronyx_monitor:
			CRONYX_LOG_1 (h, "tau32.ctl: get-monitor\n");
			if (i == 0)
				error = -EINVAL;
			else
				ctl->u.param.value = i->state.monitor;
			break;

		case cronyx_scrambler:
			CRONYX_LOG_1 (h, "tau32.ctl: get-scrambler\n");
			if (i == 0)
				error = -EINVAL;
			else if (!b->unframed)
				error = -EBUSY;
			else
				ctl->u.param.value = i->state.scrambler;
			break;

		case cronyx_mtu:
			CRONYX_LOG_1 (h, "tau32.ctl: get-mtu\n");
			if (c == 0)
				error = -EINVAL;
			else
				ctl->u.param.value = c->sys.mtu;
			break;

		case cronyx_qlen:
			CRONYX_LOG_1 (h, "tau32.ctl: get-qlen\n");
			if (h->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else
				ctl->u.param.value = c->io_qlen;
			break;

		case cronyx_stat_channel:
			CRONYX_LOG_1 (h, "tau32.ctl: get-stat-channel\n");
			if (c == 0)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_channel.rintr = c->stat.rintr;
				ctl->u.stat_channel.tintr = c->stat.tintr;
				ctl->u.stat_channel.ibytes = c->stat.ibytes;
				ctl->u.stat_channel.obytes = c->stat.obytes;
				ctl->u.stat_channel.ipkts = c->stat.ipkts;
				ctl->u.stat_channel.opkts = c->stat.opkts;
				ctl->u.stat_channel.ierrs = c->stat.overrun + c->stat.frame + c->stat.crc;
				ctl->u.stat_channel.oerrs = c->stat.underrun + c->stat.oerrs;
				ctl->u.stat_channel.mintr = c->stat.mintr;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_stat_e1:
		case cronyx_stat_e1 | cronyx_flag_channel2link:
			CRONYX_LOG_3 (h, "tau32.ctl: get-stat-e1\n");
			if (i == 0)
				error = -EINVAL;
			else {
				memset (&ctl->u.stat_e1, 0, sizeof (ctl->u.stat_e1));
				ctl->u.stat_e1.status = CRONYX_E1_PENDING;
				if (i->state.cfg_update_pending == 0) {
					ctl->u.stat_e1.status = get_status (i);
					ctl->u.stat_e1.cursec = i->state.e1_stat.cursec;
					ctl->u.stat_e1.totsec = i->state.e1_stat.totsec + i->state.e1_stat.cursec;

					ctl->u.stat_e1.currnt.bpv = i->state.e1_stat.currnt.bpv;
					ctl->u.stat_e1.currnt.fse = i->state.e1_stat.currnt.fse;
					ctl->u.stat_e1.currnt.crce = i->state.e1_stat.currnt.crce;
					ctl->u.stat_e1.currnt.rcrce = i->state.e1_stat.currnt.rcrce;
					ctl->u.stat_e1.currnt.uas = i->state.e1_stat.currnt.uas;
					ctl->u.stat_e1.currnt.les = i->state.e1_stat.currnt.les;
					ctl->u.stat_e1.currnt.es = i->state.e1_stat.currnt.es;
					ctl->u.stat_e1.currnt.bes = i->state.e1_stat.currnt.bes;
					ctl->u.stat_e1.currnt.ses = i->state.e1_stat.currnt.ses;
					ctl->u.stat_e1.currnt.oofs = i->state.e1_stat.currnt.oofs;
					ctl->u.stat_e1.currnt.css = i->state.e1_stat.currnt.css;
					ctl->u.stat_e1.currnt.dm = i->state.e1_stat.currnt.dm;

					ctl->u.stat_e1.total.bpv =
						i->state.e1_stat.total.bpv + i->state.e1_stat.currnt.bpv;
					ctl->u.stat_e1.total.fse =
						i->state.e1_stat.total.fse + i->state.e1_stat.currnt.fse;
					ctl->u.stat_e1.total.crce =
						i->state.e1_stat.total.crce + i->state.e1_stat.currnt.crce;
					ctl->u.stat_e1.total.rcrce =
						i->state.e1_stat.total.rcrce + i->state.e1_stat.currnt.rcrce;
					ctl->u.stat_e1.total.uas =
						i->state.e1_stat.total.uas + i->state.e1_stat.currnt.uas;
					ctl->u.stat_e1.total.les =
						i->state.e1_stat.total.les + i->state.e1_stat.currnt.les;
					ctl->u.stat_e1.total.es =
						i->state.e1_stat.total.es + i->state.e1_stat.currnt.es;
					ctl->u.stat_e1.total.bes =
						i->state.e1_stat.total.bes + i->state.e1_stat.currnt.bes;
					ctl->u.stat_e1.total.ses =
						i->state.e1_stat.total.ses + i->state.e1_stat.currnt.ses;
					ctl->u.stat_e1.total.oofs =
						i->state.e1_stat.total.oofs + i->state.e1_stat.currnt.oofs;
					ctl->u.stat_e1.total.css =
						i->state.e1_stat.total.css + i->state.e1_stat.currnt.css;
					ctl->u.stat_e1.total.dm =
						i->state.e1_stat.total.dm + i->state.e1_stat.currnt.dm;
					for (n = 0;  n < sizeof (ctl->u.stat_e1.interval) / sizeof (ctl->u.stat_e1.interval[0]);  n++) {
						ctl->u.stat_e1.interval[n].bpv = i->state.e1_stat.interval[n].bpv;
						ctl->u.stat_e1.interval[n].fse = i->state.e1_stat.interval[n].fse;
						ctl->u.stat_e1.interval[n].crce = i->state.e1_stat.interval[n].crce;
						ctl->u.stat_e1.interval[n].rcrce = i->state.e1_stat.interval[n].rcrce;
						ctl->u.stat_e1.interval[n].uas = i->state.e1_stat.interval[n].uas;
						ctl->u.stat_e1.interval[n].les = i->state.e1_stat.interval[n].les;
						ctl->u.stat_e1.interval[n].es = i->state.e1_stat.interval[n].es;
						ctl->u.stat_e1.interval[n].bes = i->state.e1_stat.interval[n].bes;
						ctl->u.stat_e1.interval[n].ses = i->state.e1_stat.interval[n].ses;
						ctl->u.stat_e1.interval[n].oofs = i->state.e1_stat.interval[n].oofs;
						ctl->u.stat_e1.interval[n].css = i->state.e1_stat.interval[n].css;
						ctl->u.stat_e1.interval[n].dm = i->state.e1_stat.interval[n].dm;
					}
				}
			}
			break;

		default:
			CRONYX_LOG_3 (h, "tau32.ctl: get-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}
	return error;
}

may_static void tau32_update_qlen (tau32_chan_t * c, unsigned qlen)
{
	if (qlen)
		c->io_qlen = c->io_raw_fill = c->io_ph_len = qlen;
	if (c->io_ph_len < TAU32_IO_QUEUE)
		c->io_ph_len = TAU32_IO_QUEUE;

	if (c->config.phony) {
		if (c->io_ph_len < TAU32_DEFAULT_QUEUE_LENGTH)
			c->io_ph_len = TAU32_DEFAULT_QUEUE_LENGTH;
		if (c->io_ph_len <= c->io_raw_fill + c->io_raw_fill)
			c->io_ph_len = c->io_raw_fill + c->io_raw_fill + 1;
		if (c->io_ph_len > 256)
			c->io_ph_len = 256;
		if (c->io_raw_fill >= c->io_ph_len)
			c->io_raw_fill = c->io_ph_len - 1;
	}
	tau32_rx_make_queue (c, 0);
	if (c->config.phony)
		tau32_tx_make_queue (c, 0);
	c->sys.fifosz = c->io_qlen * c->sys.mtu;
	CRONYX_LOG_1 (&c->sys, "ce: update-qlen tx-pending %d, rx-pending %d, (%d, %d, %d)\n",
		      c->tx_pending, c->rx_pending, c->io_qlen, c->io_raw_fill, c->io_ph_len);
}

may_static int tau32_ctl_set (cronyx_binder_item_t *h, struct cronyx_ctl_t *ctl)
{
	int n, error;
	tau32_chan_t *c;
	tau32_board_t *b;
	tau32_iface_t *i;
	unsigned long flags;

	if (h->hw == 0)
		return -ENODEV;

	switch (h->type) {
		default:
			return -ENODEV;
		case CRONYX_ITEM_ADAPTER:
			b = (tau32_board_t *) h->hw;
			c = 0;
			i = 0;
			break;
		case CRONYX_ITEM_INTERFACE:
			i = (tau32_iface_t *) h->hw;
			b = (tau32_board_t *) i->board;
			c = 0;
			break;
		case CRONYX_ITEM_CHANNEL:
			c = (tau32_chan_t *) h->hw;
			b = (tau32_board_t *) c->board;
			i = 0;
			if ((ctl->param_id & cronyx_flag_channel2link) != 0 && c->config.link < b->ddk.Interfaces)
				i = &b->ifaces[c->config.link];
			break;
	}

	if (b->ok != TAU32_RUNNING)
		return -ENODEV;

	if (b->unframed && c != 0 && c->sys.order)
		return -EBUSY;

	if (b->unframed && i != 0 && i->binder_data.order)
		return -EBUSY;

	error = 0;
	switch (ctl->param_id) {
		case cronyx_channel_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: set-mode\n");
			if (c == 0)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_PHONY && ctl->u.param.value != CRONYX_MODE_VOICE
			&& ctl->u.param.value != CRONYX_MODE_HDLC)
				error = -EINVAL;
			else if (b->unframed && ctl->u.param.value == CRONYX_MODE_VOICE)
				error = -EINVAL;
			else if (h->proto && h->proto->chanmode_lock)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				c->config.phony = ctl->u.param.value == CRONYX_MODE_PHONY || ctl->u.param.value == CRONYX_MODE_VOICE;
				c->config.voice = ctl->u.param.value == CRONYX_MODE_VOICE;
				error = tau32_update_chan_config (c);
				if (error < 0)
					tau32_update_qlen (c, 0);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: set-crc-mode\n");
			if (c == 0 || c->config.phony)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_CRC_NONE:
						c->config.nocrc = 1;
						c->config.crc32 = 0;
						error = tau32_update_chan_config (c);
						break;
					case CRONYX_CRC_16:
						c->config.nocrc = 0;
						c->config.crc32 = 0;
						error = tau32_update_chan_config (c);
						break;
					case CRONYX_CRC_32:
						c->config.nocrc = 0;
						c->config.crc32 = 1;
						error = tau32_update_chan_config (c);
						break;
					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_hdlc_flags:
			CRONYX_LOG_1 (h, "tau32.ctl: set-hdlc-flags\n");
			if (c == 0 || c->config.phony)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_HDLC_2FLAGS:
						c->config.share_flags = 0;
						error = tau32_update_chan_config (c);
						break;
					case CRONYX_HDLC_SHARE:
						c->config.share_flags = 1;
						error = tau32_update_chan_config (c);
						break;
					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_loop_mode:
		case cronyx_loop_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: set-loop\n");
			if (i == 0)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_LOOP_NONE:
						i->state.loop_int = 0;
						i->state.loop_mirror = 0;
						error = tau32_update_e1_config (i);
						break;

					case CRONYX_LOOP_INTERNAL:
						i->state.loop_int = 1;
						i->state.loop_mirror = 0;
						error = tau32_update_e1_config (i);
						break;

					case CRONYX_LOOP_LINEMIRROR:
						i->state.loop_int = 0;
						i->state.loop_mirror = 1;
						error = tau32_update_e1_config (i);
						break;

					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_line_code:
		case cronyx_line_code | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: set-line\n");
			if (i == 0)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_HDB3 && ctl->u.param.value != CRONYX_AMI)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				i->state.ami = ctl->u.param.value == CRONYX_AMI;
				error = tau32_update_e1_config (i);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_adapter_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: set-adapter\n");
			if (h->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_MUX)
				error = -EINVAL;
			break;

		case cronyx_cas_flags:
		case cronyx_cas_flags | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: set-cas-flags\n");
			if (i == 0 || i->state.casmode == CRONYX_CASMODE_OFF)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_CAS_ITU:
						i->state.cas_strict = 0;
						error = tau32_update_e1_config (i);
						break;
					case CRONYX_CAS_STRICT:
						i->state.cas_strict = 1;
						error = tau32_update_e1_config (i);
						break;
					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_cas_mode:
		case cronyx_cas_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: set-cas\n");
			if (i == 0)
				error = -EINVAL;
			else if (ctl->u.param.value < CRONYX_CASMODE_OFF || ctl->u.param.value > CRONYX_CASMODE_CROSS)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (ctl->u.param.value != i->state.casmode) {
					for (n = 0; n < TAU32_CHANNELS; n++) {
						tau32_chan_t *cx = b->channels[n];

						if (!cx || !(cx->config.ts & (1ul << 16)))
							continue;
						if (ctl->u.param.value == CRONYX_CASMODE_SET &&
						    tau32_is_tsmap_locked (cx)) {
							error = -EBUSY;
							break;
						}
						if (cx->need_tsmap
						    && (ctl->u.param.value == CRONYX_CASMODE_CROSS
							|| i->state.casmode == CRONYX_CASMODE_CROSS)) {
							error = -EBUSY;
							break;
						}
					}
				}
				if (error == 0) {
					i->state.casmode = ctl->u.param.value;
					error = tau32_update_e1_config (i);
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_sync_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: set-clock\n");
			if (h->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else {
				int new_gsyn;

				spin_lock_irqsave (&b->lock, flags);
				new_gsyn = b->gsyn;
				switch (ctl->u.param.value) {
#ifdef CRONYX_LYSAP
					case CRONYX_E1CLK_MANAGED:
						new_gsyn = TAU32_SYNC_LYGEN;
						break;
#endif /* CRONYX_LYSAP */
					case CRONYX_E1CLK_INTERNAL:
						new_gsyn = TAU32_SYNC_INTERNAL;
						break;
					case CRONYX_E1CLK_RECEIVE:
					case CRONYX_E1CLK_RECEIVE_CHAN0:
						new_gsyn = TAU32_SYNC_RCV_A;
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN1:
						if (b->ddk.Interfaces > 1) {
							new_gsyn = TAU32_SYNC_RCV_B;
							break;
						}
					default:
						error = -EINVAL;
				}
				if (new_gsyn != b->gsyn) {
#ifdef CRONYX_LYSAP
					if (ctl->u.param.value == TAU32_SYNC_LYGEN) {
						if (b->lygen_freq == 0)
							b->lygen_freq = TAU32_SetGeneratorFrequency
								(b->ddk.pControllerObject, TAU32_LYGEN_RESET);
					}
#endif /* CRONYX_LYSAP */
					if (TAU32_SetSyncMode (b->ddk.pControllerObject, new_gsyn)) {
						b->gsyn = new_gsyn;
					} else
						error = -EIO;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_timeslots_use:
			CRONYX_LOG_1 (h, "tau32.ctl: set-timeslots\n");
			if (c == 0)
				error = -EINVAL;
			else if (c->dacs && c->config.up)
				error = -EBUSY;
			else if (h->proto && h->proto->timeslots_lock)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);

				if (c->config.link >= b->ddk.Interfaces)
					error = -EINVAL;
				else
					for (n = 0; n < TAU32_CHANNELS; n++) {
						tau32_chan_t *cx = b->channels[n];

						if (0 == cx || c == cx)
							continue;
						if ((ctl->u.param.value & cx->config.ts & (1ul << 16))
						    && tau32_is_cas_cross (b) && tau32_is_tsmap_locked (cx)) {
							error = -EBUSY;
							break;
						}
						if (cx->config.link == c->config.link &&
						    (cx->config.ts & ctl->u.param.value)
						    && tau32_is_tsmap_locked (cx)) {
							error = -EBUSY;
							break;
						}
					}
				if (error == 0) {
					c->config.ts = ctl->u.param.value;
					c->phony_chunk = hweight32 (c->config.ts);
					error = tau32_update_chan_config (c);
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (h, "tau32.ctl: set-baud\n");
			if (c == 0)
				error = -EINVAL;
			else if (!b->unframed)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (!unfram_speed2ts (ctl->u.param.value))
					error = -EINVAL;
				else {
					b->unframed_speed = ctl->u.param.value;
					error = tau32_update_e1_config (&b->ifaces[0]);
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_iface_bind:
			CRONYX_LOG_1 (h, "tau32.ctl: set-iface-bind\n");
			if (c == 0)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else {
				if (ctl->u.param.value >= b->ddk.Interfaces)
					error = -EINVAL;
				else {
					spin_lock_irqsave (&b->lock, flags);
					c->config.link = ctl->u.param.value;
					error = tau32_update_chan_config (c);
					spin_unlock_irqrestore (&b->lock, flags);
				}
			}
			break;

		case cronyx_crc4:
		case cronyx_crc4 | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: set-crc4\n");
			if (i == 0)
				error = -EINVAL;
			else if (b->unframed)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				i->state.crc4 = ctl->u.param.value != 0;
				error = tau32_update_e1_config (i);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_higain:
		case cronyx_higain | cronyx_flag_channel2link:
			CRONYX_LOG_1 (h, "tau32.ctl: set-higain\n");
			if (i == 0)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				i->state.higain = ctl->u.param.value != 0;
				error = tau32_update_e1_config (i);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_unframed:
			CRONYX_LOG_1 (h, "tau32.ctl: set-unframed\n");
			if (i == 0)
				error = -EINVAL;
			else if (i->binder_data.order)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				b->unframed = ctl->u.param.value != 0;
				error = tau32_update_e1_config (i);
				if (error >= 0 && b->ddk.Interfaces > 1)
					error = tau32_update_e1_config (&b->ifaces[1]);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_monitor:
			CRONYX_LOG_1 (h, "tau32.ctl: set-monitor\n");
			if (i == 0)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				i->state.monitor = ctl->u.param.value != 0;
				error = tau32_update_e1_config (i);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_scrambler:
			CRONYX_LOG_1 (h, "tau32.ctl: set-scrambler\n");
			if (i == 0)
				error = -EINVAL;
			else if (!b->unframed)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				i->state.scrambler = ctl->u.param.value != 0;
				error = tau32_update_e1_config (i);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (h, "tau32.ctl: set-led\n");
			if (h->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_SET, ctl);
			break;

		case cronyx_mtu:
			CRONYX_LOG_1 (h, "tau32.ctl: set-mtu\n");
			if (h->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (ctl->u.param.value < 8 || ctl->u.param.value > TAU32_MTU)
				error = -EINVAL;
			else if (h->proto && h->proto->mtu_lock)
				error = -EBUSY;
			else {
				c->sys.mtu = ctl->u.param.value;
				c->sys.fifosz = c->io_qlen * c->sys.mtu;
			}
			break;

		case cronyx_qlen:
			CRONYX_LOG_1 (h, "tau32.ctl: set-qlen\n");
			if (h->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (ctl->u.param.value < 2 || ctl->u.param.value > 32)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				tau32_update_qlen (c, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_clear_stat:
			CRONYX_LOG_1 (h, "tau32.ctl: clear-stat\n");
			spin_lock_irqsave (&b->lock, flags);
			switch (h->type) {
				default:
					error = -EINVAL;
					break;
				case CRONYX_ITEM_CHANNEL:
					memset (&c->stat, 0, sizeof (c->stat));
					c->underrun = 0;
					c->overrun = 0;
					break;
				case CRONYX_ITEM_INTERFACE:
					tau32_clr_e1_stat (i);
					break;
				case CRONYX_ITEM_ADAPTER:
					for (n = 0; n < TAU32_CHANNELS; n++)
						if (b->channels[n])
							memset (&b->channels[n]->stat, 0, sizeof (c->stat));
					tau32_clr_e1_stat (&b->ifaces[0]);
					tau32_clr_e1_stat (&b->ifaces[1]);
					break;
			}
			spin_unlock_irqrestore (&b->lock, flags);
			break;

		case cronyx_ts_test:
			if (h->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = TAU32_Diag (b->ddk.pControllerObject,
								 TAU32_CRONYX_T,
								 (ctl->u.param.value << 5) + h->order);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		default:
			CRONYX_LOG_3 (h, "tau32.ctl: set-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}
	return error;
};

#ifdef CRONYX_LYSAP
#	include "tau32-lysap.inc"
#endif /* CRONYX_LYSAP */

may_static void tau32_cancel_queue (tau32_chan_t * c, struct list_head *queue, int *pending, int force)
{
	if (!list_empty (queue)) {
		struct list_head deffered_cancel;

		INIT_LIST_HEAD (&deffered_cancel);
		do {
			tau32_request_t *r = list_entry (queue->prev, tau32_request_t, entry);

			list_del (&r->entry);
			if (TAU32_CancelRequest (c->board->ddk.pControllerObject, &r->ddk, force)) {
				if (pending) {
					BUG_ON (*pending < 1);
					(*pending)--;
				}
				/* LY: wait for DMA-FIFO. */
				udelay (10);
				tau32_free_request (r);
			} else
				list_add_tail (&r->entry, &deffered_cancel);
		} while (!list_empty (queue));
		list_splice (&deffered_cancel, queue);
	}
}

may_static int tau32_up (cronyx_binder_item_t *h)
{
	unsigned long flags;
	int error;
	tau32_chan_t *c = h->hw;

#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	if (!try_module_get (THIS_MODULE))
		return -EAGAIN;
#endif

	spin_lock_irqsave (&c->board->lock, flags);
	error = 0;
	if (unlikely (c->board->ok != TAU32_RUNNING))
		error = -ENODEV;
	else if (unlikely (c->config.ts == 0 && (c->sys.order || !c->board->unframed)))
		error = -EBADFD;
	else {
		if (likely (!error)) {
			if (c->config.up) {
				spin_unlock_irqrestore (&c->board->lock, flags);
#if LINUX_VERSION_CODE < 0x020600
				MOD_DEC_USE_COUNT;
#else
				module_put (THIS_MODULE);
#endif
				return 0;
			}
			c->dacs = 0;
			c->rx_pending = c->tx_pending = 0;
#ifndef CRONYX_LYSAP
			c->tx_phony_fifo = 0;
#endif
			c->overrun = c->underrun = 0;
			c->config.up = 1;
			tau32_rx_make_queue (c, 0);
			if (c->config.phony)
				tau32_tx_make_queue (c, 0);
			CRONYX_LOG_1 (&c->sys, "ce: up-prepare tx-qlen %d, rx-qlen %d, (%d, %d, %d)\n",
				      c->tx_pending, c->rx_pending, c->io_qlen, c->io_raw_fill, c->io_ph_len);
			error = tau32_update_chan_config (c);
			if (unlikely (error)) {
				c->config.up = 0;
				tau32_cancel_tx (c, 0);
				tau32_cancel_rx (c, 0);
				tau32_update_chan_config (c);
			} else
				h->running = 1;
		}
	}
	spin_unlock_irqrestore (&c->board->lock, flags);

	if (unlikely (error)) {
#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put (THIS_MODULE);
#endif
	}
	return error;
}

may_static void tau32_down (cronyx_binder_item_t *h)
{
	unsigned long flags, fuse;
	tau32_chan_t *c = h->hw;

	spin_lock_irqsave (&c->board->lock, flags);
	if (unlikely (!c->config.up)) {
		spin_unlock_irqrestore (&c->board->lock, flags);
		return;
	}
	c->config.up = 0;
	h->running = 0;
	fuse = jiffies;

	tau32_cancel_rx (c, 1);
	while (!list_empty (&c->pending_rx_list)
	       || !list_empty (&c->pending_tx_list)) {
		spin_unlock_irqrestore (&c->board->lock, flags);
		set_current_state (TASK_INTERRUPTIBLE);
		schedule_timeout (1);
		spin_lock_irqsave (&c->board->lock, flags);
		if (jiffies - fuse > HZ) {
			printk (KERN_WARNING "ce: timeout wait-for io-done %s\n", c->sys.name);
			tau32_cancel_tx (c, 1);
			break;
		}
		if (signal_pending (current)) {
			tau32_cancel_tx (c, 1);
			break;
		}
	}

	if (tau32_update_chan_config (c) < 0)
		printk (KERN_ERR "ce: error while down channel %s\n", c->sys.name);

	while (!list_empty (&c->board->pending_list)
	       || !list_empty (&c->pending_rx_list)
	       || !list_empty (&c->pending_tx_list)) {
		spin_unlock_irqrestore (&c->board->lock, flags);
		set_current_state (TASK_INTERRUPTIBLE);
		schedule_timeout (1);
		spin_lock_irqsave (&c->board->lock, flags);
		if (jiffies - fuse > (signal_pending (current) ? HZ : HZ * 5)) {
			printk (KERN_WARNING "ce: timeout wait-for io-stop %s\n", c->sys.name);
			break;
		}
	}
	spin_unlock_irqrestore (&c->board->lock, flags);

#if LINUX_VERSION_CODE < 0x020600
	MOD_DEC_USE_COUNT;
#else
	module_put (THIS_MODULE);
#endif
}

may_static int tau32_query_rtsdtr (cronyx_binder_item_t *h)
{
	tau32_chan_t *c;
	tau32_board_t *b;
	tau32_iface_t *i;

	switch (h->type) {
		default:
			return 0;
		case CRONYX_ITEM_INTERFACE:
			i = (tau32_iface_t *) h->hw;
			b = (tau32_board_t *) i->board;
			c = 0;
			break;
		case CRONYX_ITEM_CHANNEL:
			c = (tau32_chan_t *) h->hw;
			b = (tau32_board_t *) c->board;
			i = 0;
			if (c->config.link < b->ddk.Interfaces)
				i = &b->ifaces[c->config.link];
			break;
	}

	return b->ok == TAU32_RUNNING && i
		&& (b->ddk.InterfacesInfo[i->binder_data.order].Status & TAU32_E1OFF) == 0
		&& (!c || (c->config.ts && c->config.up));
}

may_static int tau32_query_dsr (cronyx_binder_item_t *h)
{
	tau32_chan_t *c;
	tau32_board_t *b;
	tau32_iface_t *i;

	switch (h->type) {
		default:
			return 0;
		case CRONYX_ITEM_INTERFACE:
			i = (tau32_iface_t *) h->hw;
			b = (tau32_board_t *) i->board;
			c = 0;
			break;
		case CRONYX_ITEM_CHANNEL:
			c = (tau32_chan_t *) h->hw;
			b = (tau32_board_t *) c->board;
			i = 0;
			if (c->config.link < b->ddk.Interfaces)
				i = &b->ifaces[c->config.link];
			break;
	}

	return b->ok == TAU32_RUNNING && i
		&& (b->ddk.InterfacesInfo[i->binder_data.order].Status & (TAU32_RCL | TAU32_RLOS | TAU32_RUA1 | TAU32_E1OFF)) == 0
		&& (! c || (c->config.ts && c->config.up));
}

may_static int tau32_query_cts (cronyx_binder_item_t *h)
{
	tau32_chan_t *c;
	tau32_board_t *b;
	tau32_iface_t *i;

	switch (h->type) {
		default:
			return 0;
		case CRONYX_ITEM_INTERFACE:
			i = (tau32_iface_t *) h->hw;
			b = (tau32_board_t *) i->board;
			c = 0;
			break;
		case CRONYX_ITEM_CHANNEL:
			c = (tau32_chan_t *) h->hw;
			b = (tau32_board_t *) c->board;
			i = 0;
			if (c->config.link < b->ddk.Interfaces)
				i = &b->ifaces[c->config.link];
			break;
	}

	return b->ok == TAU32_RUNNING && i
		&& (b->ddk.InterfacesInfo[i->binder_data.order].Status & (TAU32_RCL | TAU32_RRA | TAU32_RDMA | TAU32_E1OFF)) == 0
		&& (! c || (c->config.ts && c->config.up));
}

may_static int tau32_query_dcd (cronyx_binder_item_t *h)
{
	tau32_chan_t *c;
	tau32_board_t *b;
	tau32_iface_t *i;

	switch (h->type) {
		default:
			return 0;
		case CRONYX_ITEM_INTERFACE:
			i = (tau32_iface_t *) h->hw;
			b = (tau32_board_t *) i->board;
			break;
		case CRONYX_ITEM_CHANNEL:
			c = (tau32_chan_t *) h->hw;
			b = (tau32_board_t *) c->board;
			i = 0;
			if (c->config.link < b->ddk.Interfaces)
				i = &b->ifaces[c->config.link];
			break;
	}

	return b->ok == TAU32_RUNNING && i
		&& (b->ddk.InterfacesInfo[i->binder_data.order].Status & (TAU32_RCL | TAU32_E1OFF)) == 0;
}

may_static void tau32_free_request (tau32_request_t * r)
{
	if (likely (r != 0)) {
		if (unlikely (r->ddk.pInternal != 0))
			BUG ();
#ifdef CRONYX_LYSAP
		if (r->lysap)
			lysap_buf_put (r->lysap);
#else
		if (r->skb)
			dev_kfree_skb_any (r->skb);
#endif /* CRONYX_LYSAP */

#if LINUX_VERSION_CODE < 0x020600
		kfree (r);
#else
		mempool_free (r, tau32_request_pool);
#endif
	}
}

may_static tau32_request_t *tau32_cfg_alloc (tau32_chan_t * c)
{
	tau32_request_t *r;

#if LINUX_VERSION_CODE < 0x020600
	r = kmalloc (sizeof (tau32_request_t), GFP_ATOMIC);
#else
	r = mempool_alloc (tau32_request_pool, GFP_ATOMIC);
#endif

	if (unlikely (r == 0)) {
		printk (KERN_ERR "ce: no memory for cfg-request channel %s\n", c->sys.name);
		return 0;
	}
	r->ddk.pCallback = 0;
	r->ddk.Io.ChannelNumber = c ? c->sys.order : ~0u;
	r->ddk.Command = 0;
	r->ddk.pInternal = 0;
#ifdef CRONYX_LYSAP
	r->lysap = 0;
#else
	r->skb = 0;
#endif /* CRONYX_LYSAP */
	return r;
}

may_static int tau32_cfg_submit (tau32_board_t *b, tau32_request_t * r)
{
	if (unlikely (b->ok != TAU32_RUNNING)) {
		tau32_free_request (r);
		return 0;
	}
	if (r->ddk.pCallback == 0) {
		r->ddk.pCallback = tau32_munich32x_cfg_callback;
		if (r->ddk.Command == TAU32_Configure_E1)
			r->ddk.pCallback = tau32_e1_cfg_callback;
	}
	list_add_tail (&r->entry, &b->pending_list);
	if (unlikely (!TAU32_SubmitRequest (b->ddk.pControllerObject, &r->ddk))) {
		printk (KERN_ERR "ce: Adapter #%d - cfg-submit error cmd:%x, ch:%d\n", b->binder_data.order,
			 r->ddk.Command, r->ddk.Io.ChannelNumber);
		list_del (&r->entry);
		tau32_free_request (r);
		return 0;
	}
	return 1;
}

may_static void TAU32_CALLBACK_TYPE tau32_munich32x_cfg_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest)
{
	tau32_request_t *r = (tau32_request_t *) pUserRequest;
	tau32_board_t *b = (tau32_board_t *) pContext;

	list_del (&r->entry);
	if (likely (b->ok == TAU32_RUNNING)) {
		tau32_count_int (b, CRONYX_LEDMODE_4IRQ);
		if (likely ((r->ddk.ErrorCode & TAU32_ERROR_CANCELLED) == 0)) {
			if (unlikely (r->ddk.ErrorCode))
				printk (KERN_ERR
					"ce: Adapter #%d - munich32x-config-commit error:%x cmd:%x, ch:%d\n",
					b->binder_data.order, r->ddk.ErrorCode, r->ddk.Command, r->ddk.Io.ChannelNumber);
		}
	}
	tau32_free_request (r);
}

may_static void TAU32_CALLBACK_TYPE tau32_e1_cfg_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest)
{
	tau32_request_t *r = (tau32_request_t *) pUserRequest;
	tau32_board_t *b = (tau32_board_t *) pContext;

	list_del (&r->entry);
	if (likely (b->ok == TAU32_RUNNING)) {
		tau32_count_int (b, CRONYX_LEDMODE_4IRQ);
		if (likely ((r->ddk.ErrorCode & TAU32_ERROR_CANCELLED) == 0)) {
			if (unlikely (r->ddk.ErrorCode))
				printk (KERN_ERR
					"ce: Adapter #%d - e1-config-commit error:%x cmd:%x, ch:%d\n", b->binder_data.order,
					r->ddk.ErrorCode, r->ddk.Command, r->ddk.Io.ChannelNumber);
		}
	}
	tau32_free_request (r);
}

#ifndef CRONYX_LYSAP

may_static void TAU32_CALLBACK_TYPE tau32_tx_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest)
{
	tau32_request_t *r = (tau32_request_t *) pUserRequest;
	tau32_board_t *b = (tau32_board_t *) pContext;
	tau32_chan_t *c = b->channels[r->ddk.Io.ChannelNumber];

	list_del (&r->entry);
	pci_unmap_single (c->board->pdev, r->ddk.Io.Tx.PhysicalDataAddress, r->ddk.Io.Tx.DataLength, PCI_DMA_TODEVICE);
	if (likely (b->ok == TAU32_RUNNING)) {
		CRONYX_LOG_3 (&c->sys, "tau32.transmited %d bytes, error 0x%X\n",
			      r->ddk.Io.Tx.Transmitted, r->ddk.ErrorCode);
		tau32_count_int (b, CRONYX_LEDMODE_4TX);
		++c->stat.tintr;
		BUG_ON (c->tx_pending < 1);
		--c->tx_pending;

		if (likely ((r->ddk.ErrorCode & TAU32_ERROR_CANCELLED) == 0 && c->config.up)) {
			if (unlikely (r->ddk.ErrorCode)) {
				CRONYX_LOG_1 (&c->sys, "tau32.tx-notify code 0x%X\n", r->ddk.ErrorCode);
				cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4ERR);

				if (r->ddk.ErrorCode & TAU32_ERROR_BUS) {
					CRONYX_LOG_1 (&c->sys, "tau32.transmit underrun (PCI-bus is too busy)\n");
					c->stat.underrun++;
					spin_unlock (&b->lock);
					cronyx_transmit_error (&c->sys, CRONYX_ERR_BUS);
					spin_lock (&b->lock);
				} else if (r->ddk.ErrorCode & TAU32_ERROR_TX_PROTOCOL) {
					/* LY: just a crazy Infineon... */
					CRONYX_LOG_1 (&c->sys, "tau32.transmit HDLC-engine error (Infineon's bug)\n");
					c->stat.oerrs++;
					spin_unlock (&b->lock);
					cronyx_transmit_error (&c->sys, CRONYX_ERR_UNDERRUN);
					spin_lock (&b->lock);
				}
			} else {
				c->stat.opkts++;
				c->stat.obytes += r->ddk.Io.Tx.Transmitted;
			}

			if (c->config.phony) {
				if (likely (c->tx_phony_fifo > 0))
					--c->tx_phony_fifo;

				tau32_tx_make_queue (c, r);
				r = 0;

				if (unlikely (c->tx_phony_fifo == 0)) {
					if (!c->underrun) {
						c->underrun = 1;
						CRONYX_LOG_1 (&c->sys, "tau32.transmit phony-underrun (fifo empty)\n");
					}
					c->stat.underrun++;
					spin_unlock (&b->lock);
					cronyx_transmit_error (&c->sys, CRONYX_ERR_UNDERFLOW);
					spin_lock (&b->lock);
				}
			}

			spin_unlock (&b->lock);
			cronyx_transmit_done (&c->sys);
			spin_lock (&b->lock);
		}
	}
	tau32_free_request (r);
}

may_static int tau32_phony_fill (tau32_chan_t * c, void *data_buffer, unsigned data_length)
{
	int i;
	tau32_request_t *r;

	if (likely (c->phony_chunk) && unlikely (data_length % c->phony_chunk != 0)) {
		CRONYX_LOG_1 (&c->sys, "tau32: wrong chunk of phony-tx-data (%d/%d)\n",
			      c->phony_chunk, data_length);
		return -EIO;
	}

	/* LY: тонкий момент:
		- в tx_phony_fifo cчитается кол-во буферов в очереди на передачу,
		  которые заполненны данными пользователя;
		- значение "0" говорит о том, что данных пользователя нет
		  и уже был зафиксирован underrun. При этом первый,
                  он же "нулевой" буфер в целочке уже передается в E1;
		- для DAHDI/Zaptel при underrun будет оптимальна запись (помещение
		  полученной от пользователя порции данных) в первый буфер,
		  так как при этом есть шанс, что в E1 только начало буфера
		  уйдет с 0xFF, а хвост будет заполнен полезными данными.
		  Другими словами, плотность верятности последствий underrun
		  смещена в сторону меньшей деструктивности;
		- для многих других применений, в особенности когда "underrun"
		  легален (например пакетная передача с байт-стаффингом) запись
		  в первый буфер будет означать ПОТЕРЮ как минимум начала порции
		  данных, а если пакет мал, то и целиком, за счет FIFO на стороне
		  DMA и потокового (не когерентного) отображения буфера в PCI-окно.
		  Другими словами, в случае не-голоса та же логика может приводить
		  к устойчивому потоку ошибок;
		- в результате решено ввести режим CRONYX_MODE_VOICE и флажок
		  "config.voice" в этом драйвере.
	*/
	if (unlikely (c->tx_phony_fifo < 1) && (! c->config.voice || c->board->unframed))
		c->tx_phony_fifo = 1;
	tau32_tx_make_queue (c, 0);

	if (unlikely (c->tx_phony_fifo >= c->io_raw_fill)) {
		CRONYX_LOG_1 (&c->sys, "tau32.warning: too many phony-tx-data (%d/%d)\n",
				c->tx_phony_fifo, c->tx_pending);
		return 0;
	}

	i = 0;
	list_for_each_entry (r, &c->pending_tx_list, entry) {
		if (i == c->tx_phony_fifo) {
			if (unlikely (r->ddk.ErrorCode != 0)) {
				CRONYX_LOG_1 (&c->sys, "tau32: tx-phony-fill error/cancel pending (0x%X)\n",
					      r->ddk.ErrorCode);
				return -EIO;
			}
			if (unlikely (r->ddk.Io.Tx.DataLength != data_length)) {
				CRONYX_LOG_1 (&c->sys, "tau32: wrong length of phony-tx-data (%d/%d)\n",
					      r->ddk.Io.Tx.DataLength, data_length);
				return -EIO;
			}
			c->tx_phony_fifo++;
			c->underrun = 0;
			memcpy (r->skb->data, data_buffer, data_length);
			return data_length;
		}
		i++;
	}
	CRONYX_LOG_1 (&c->sys, "tau32: tx-phony-fifo update failed (%d/%d)\n",
			c->tx_phony_fifo, c->tx_pending);
	return -EIO;
}

may_static int tau32_transmit (cronyx_binder_item_t *h, struct sk_buff *skb)
{
	unsigned long flags;
	int result;
	tau32_chan_t *c = h->hw;
	tau32_request_t *r;

	spin_lock_irqsave (&c->board->lock, flags);
	if (unlikely (c->board->ok != TAU32_RUNNING)) {
		result = -ENODEV;
		goto done;
	}

	if (unlikely (!c->config.up)) {
		CRONYX_LOG_2 (&c->sys, "tau32.interface is down\n");
		result = -EBADFD;
		goto done;
	}

	if (c->config.phony) {
		result = tau32_phony_fill (c, skb->data, skb->len);
		goto done;
	}

	if (unlikely (c->tx_pending > c->io_ph_len)) {
		CRONYX_LOG_2 (&c->sys, "tau32.tx-queue is full\n");
		result = 0;
		goto done;
	}
#if LINUX_VERSION_CODE < 0x020600
	r = kmalloc (sizeof (tau32_request_t), GFP_ATOMIC);
#else
	r = mempool_alloc (tau32_request_pool, GFP_ATOMIC);
#endif
	if (unlikely (r == 0)) {
		printk (KERN_ERR "ce: no memory for I/O request channel %s\n", c->sys.name);
		result = -ENOMEM;
		goto done;
	}
	r->ddk.pInternal = 0;
#ifdef CRONYX_LYSAP
	r->lysap = 0;
#endif /* CRONYX_LYSAP */

	r->skb = skb_get (skb);
	r->ddk.Io.Tx.Channel = c->sys.order;
	r->ddk.Command = TAU32_Tx_Data;
	r->ddk.Io.Tx.DataLength = skb->len;
	r->ddk.Io.Tx.PhysicalDataAddress = pci_map_single (c->board->pdev, skb->data, skb->len, PCI_DMA_TODEVICE);
	r->ddk.pCallback = tau32_tx_callback;

	CRONYX_LOG_3 (h, "tau32.send b:%d c:%d\n", c->board->binder_data.order, c->sys.order);
	++c->tx_pending;
	list_add_tail (&r->entry, &c->pending_tx_list);
	if (unlikely (!TAU32_SubmitRequest (c->board->ddk.pControllerObject, &r->ddk))) {
		CRONYX_LOG_1 (&c->sys, "tau32.send error\n");
		--c->tx_pending;
		list_del (&r->entry);
		tau32_free_request (r);
		result = -EIO;
		goto done;
	}
	result = skb->len;

done:
	spin_unlock_irqrestore (&c->board->lock, flags);
	return result;
}

#endif /* ! CRONYX_LYSAP */

may_static tau32_request_t *tau32_tx_alloc (tau32_chan_t * c, tau32_request_t * r)
{
#ifndef CRONYX_LYSAP
	unsigned buf_len;
#endif

	if (unlikely (r == 0)) {
#if LINUX_VERSION_CODE < 0x020600
		r = kmalloc (sizeof (tau32_request_t), GFP_ATOMIC);
#else
		r = mempool_alloc (tau32_request_pool, GFP_ATOMIC);
#endif
		if (unlikely (r == 0)) {
			printk (KERN_ERR "ce: no memory for I/O request channel %s\n", c->sys.name);
			return 0;
		}
		r->ddk.pInternal = 0;
#ifdef CRONYX_LYSAP
		r->lysap = 0;
#else
	} else {
		if (r->skb)
			dev_kfree_skb_any (r->skb);
#endif /* CRONYX_LYSAP */
	}

#ifdef CRONYX_LYSAP
	BUG_ON (! lysap_link (&c->sys));
	if (unlikely (r->lysap == 0)) {
		r->lysap = lysap_buf_alloc (c->sys.sw);
		if (unlikely (r->lysap == 0)) {
			printk (KERN_ERR "ce: no memory for rx-i/o buffer channel %s\n", c->sys.name);
			lysap_link (&c->sys)->dev.Info.Error_NoBuffer++;
			mempool_free (r, tau32_request_pool);
			return 0;
		}
		BUG_ON (! lysap_buf_check (r->lysap));
	}
	BUG_ON (! lysap_buf_check (r->lysap));
	memset (r->lysap->buf.payload, 0xFF, c->chunk);
	BUG_ON (! lysap_buf_check (r->lysap));
	r->ddk.Io.Tx.PhysicalDataAddress = pci_map_single (c->board->pdev, r->lysap->buf.payload, c->chunk, PCI_DMA_TODEVICE);
	BUG_ON (! lysap_buf_check (r->lysap));
	r->ddk.Io.Tx.Channel = c->sys.order;
	r->ddk.Command = TAU32_Tx_Data;
	r->ddk.Io.Tx.DataLength = c->chunk;
	r->ddk.pCallback = tau32_tx_callback_lysap;
	BUG_ON (! lysap_buf_check (r->lysap));

#else

	buf_len = c->sys.mtu;
	if (buf_len > TAU32_MTU)
		buf_len = TAU32_MTU;

	r->skb = dev_alloc_skb (buf_len);
	if (unlikely (r->skb == 0)) {
		printk (KERN_ERR "ce: no memory for tx-i/o buffer channel %s\n", c->sys.name);
#if LINUX_VERSION_CODE < 0x020600
		kfree (r);
#else
		mempool_free (r, tau32_request_pool);
#endif
		return 0;
	}

	memset (r->skb->data, (c->config.voice && ! c->board->unframed) ? 0xD5 : 0xFF, buf_len);
	r->ddk.Io.Tx.PhysicalDataAddress = pci_map_single (c->board->pdev, r->skb->data, buf_len, PCI_DMA_TODEVICE);
	r->ddk.Io.Tx.Channel = c->sys.order;
	r->ddk.Command = TAU32_Tx_Data;
	r->ddk.Io.Tx.DataLength = buf_len;
	r->ddk.pCallback = tau32_tx_callback;
#endif /* CRONYX_LYSAP */

	return r;
}

may_static void tau32_tx_make_queue (tau32_chan_t * c, tau32_request_t * r)
{
	if (likely (c->config.up && c->board->ok == TAU32_RUNNING)) {
		while (c->tx_pending < c->io_ph_len) {
			r = tau32_tx_alloc (c, r);
			if (unlikely (r == 0))
				break;
			c->tx_pending++;
			list_add_tail (&r->entry, &c->pending_tx_list);
			if (unlikely (!TAU32_SubmitRequest (c->board->ddk.pControllerObject, &r->ddk))) {
				printk (KERN_ERR "ce: transmit-prepare error channel %s\n", c->sys.name);
				list_del (&r->entry);
				c->tx_pending--;
				break;
			}
			r = 0;
		}
	}
	tau32_free_request (r);
}

may_static tau32_request_t *tau32_rx_alloc (tau32_chan_t * c, tau32_request_t * r)
{
#ifndef CRONYX_LYSAP
	unsigned buf_len;
#endif

	if (unlikely (r == 0)) {
#if LINUX_VERSION_CODE < 0x020600
		r = kmalloc (sizeof (tau32_request_t), GFP_ATOMIC);
#else
		r = mempool_alloc (tau32_request_pool, GFP_ATOMIC);
#endif
		if (unlikely (r == 0)) {
			printk (KERN_ERR "ce: no memory for rx-i/o request channel %s\n", c->sys.name);
			return 0;
		}
		r->ddk.pInternal = 0;
	} else {
#ifdef CRONYX_LYSAP
		if (likely (r->lysap))
			lysap_buf_put (r->lysap);
#else
		if (r->skb)
			dev_kfree_skb_any (r->skb);
#endif /* CRONYX_LYSAP */
	}

#ifdef CRONYX_LYSAP
	BUG_ON (! lysap_link (&c->sys));
	r->lysap = lysap_buf_alloc (c->sys.sw);
	if (unlikely (r->lysap == 0)) {
		printk (KERN_ERR "ce: no memory for rx-i/o buffer channel %s\n", c->sys.name);
		lysap_link (&c->sys)->dev.Info.Error_NoBuffer++;
		mempool_free (r, tau32_request_pool);
		return 0;
	}
	BUG_ON (! lysap_buf_check (r->lysap));

	r->ddk.Io.Rx.PhysicalDataAddress = pci_map_single (c->board->pdev,
		r->lysap->buf.payload, c->chunk, PCI_DMA_FROMDEVICE);
	r->ddk.Io.Rx.Channel = c->sys.order;
	r->ddk.Command = TAU32_Rx_Data;
	r->ddk.Io.Rx.BufferLength = c->chunk;
	r->ddk.pCallback = tau32_rx_callback;

#else

	buf_len = c->sys.mtu + (c->config.phony ? 0 : 4);	/* LY: yes, we heed 4 bytes more (see Tau32-DDK's docs) */
	if (buf_len > TAU32_MTU)
		buf_len = TAU32_MTU;

	r->skb = dev_alloc_skb (buf_len);

	if (unlikely (r->skb == 0)) {
		printk (KERN_ERR "ce: no memory for rx-i/o buffer channel %s\n", c->sys.name);
#if LINUX_VERSION_CODE < 0x020600
		kfree (r);
#else
		mempool_free (r, tau32_request_pool);
#endif
		return 0;
	}

	r->ddk.Io.Rx.PhysicalDataAddress = pci_map_single (c->board->pdev, r->skb->data, buf_len, PCI_DMA_FROMDEVICE);

	r->ddk.Io.Rx.Channel = c->sys.order;
	r->ddk.Command = TAU32_Rx_Data;
	r->ddk.Io.Rx.BufferLength = buf_len;
	r->ddk.pCallback = tau32_rx_callback;
#endif /* CRONYX_LYSAP */
	return r;
}

may_static void TAU32_CALLBACK_TYPE tau32_rx_callback (TAU32_UserContext * pContext, TAU32_UserRequest * pUserRequest)
{
	tau32_request_t *r = (tau32_request_t *) pUserRequest;
	tau32_board_t *b = (tau32_board_t *) pContext;
	tau32_chan_t *c = b->channels[r->ddk.Io.ChannelNumber];

	list_del (&r->entry);
	pci_unmap_single (c->board->pdev, r->ddk.Io.Rx.PhysicalDataAddress, r->ddk.Io.Rx.BufferLength, PCI_DMA_FROMDEVICE);
	if (unlikely (b->ok != TAU32_RUNNING))
		tau32_free_request (r);
	else {
		CRONYX_LOG_3 (&c->sys, "tau32.received %d bytes%s, error 0x%X\n",
			      r->ddk.Io.Rx.Received, r->ddk.Io.Rx.FrameEnd ? ", frame-end" : "", r->ddk.ErrorCode);
		tau32_count_int (b, CRONYX_LEDMODE_4RX);
		c->stat.rintr++;
		BUG_ON (c->rx_pending < 1);
		--c->rx_pending;

		if (likely ((r->ddk.ErrorCode & TAU32_ERROR_CANCELLED) == 0 && c->config.up)) {
			CRONYX_LOG_1 (&c->sys, "tau32.rx-notify code 0x%X\n", r->ddk.ErrorCode);
			if (unlikely (r->ddk.ErrorCode & TAU32_ERROR_BUS)) {
				CRONYX_LOG_1 (&c->sys, "tau32.receive overrun (PCI-bus is too busy)\n");
				c->stat.overrun++;
				spin_unlock (&b->lock);
				cronyx_receive_error (&c->sys, CRONYX_ERR_BUS);
				spin_lock (&b->lock);
			} else if (likely (r->ddk.ErrorCode == TAU32_SUCCESSFUL)) {
				if (r->ddk.Io.Rx.FrameEnd || c->config.phony)
					c->stat.ipkts++;
				if (likely (r->ddk.Io.Rx.Received)) {
					c->stat.ibytes += r->ddk.Io.Rx.Received;
#ifdef CRONYX_LYSAP
					BUG_ON (! lysap_buf_check (r->lysap));
					BUG_ON (! lysap_link (&c->sys));
					r->lysap->buf.length = c->chunk;
					c->sys.proto->lysap_receive (&c->sys, r->lysap);
					r->lysap = 0;
#else
					if (c->sys.proto && r->skb) {
						skb_put (r->skb, r->ddk.Io.Rx.Received);
						spin_unlock (&b->lock);
						cronyx_receive (&c->sys, r->skb);
						spin_lock (&b->lock);
						r->skb = 0;
					}
#endif
				}
			} else {
				cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4ERR);
				CRONYX_LOG_1 (&c->sys, "tau32.receive error 0x%X\n", r->ddk.ErrorCode);
			}
		}
		tau32_rx_make_queue (c, r);
	}
}

may_static void tau32_rx_make_queue (tau32_chan_t * c, tau32_request_t * r)
{
	if (likely (c->config.up && c->board->ok == TAU32_RUNNING)) {
		while (c->rx_pending < c->io_ph_len) {
			r = tau32_rx_alloc (c, r);
			if (unlikely (r == 0))
				return;
			c->rx_pending++;
			list_add_tail (&r->entry, &c->pending_rx_list);
			if (unlikely (!TAU32_SubmitRequest (c->board->ddk.pControllerObject, &r->ddk))) {
				printk (KERN_ERR "ce: receive-prepare error channel %s\n", c->sys.name);
				list_del (&r->entry);
				c->rx_pending--;
				break;
			}
			c->overrun = 0;
			r = 0;
		}
	}
	tau32_free_request (r);
}

may_static void append_text (char *buffer, unsigned size, char *node)
{
#if LINUX_VERSION_CODE < 0x020600
	unsigned len = strlen (node);

	if (buffer[0])
		len += 2;
	if (strlen (buffer) + len < size) {
		if (buffer[0])
			strcat (buffer, ", ");
		strcat (buffer, node);
	}
#else
	unsigned len = strlen (buffer);

	if (len < size)
		snprintf (buffer + len, size - len, "%s%s", len ? ", " : "", node);
	buffer[size - 1] = 0;
#endif
}

may_static void TAU32_CALLBACK_TYPE tau32_error_callback (TAU32_UserContext * pContext, int Item, unsigned NotifyBits)
{
	char msg[256];
	tau32_board_t *b = (tau32_board_t *) pContext;

	if (unlikely (b->ok != TAU32_RUNNING))
		return;
	msg[0] = 0;
	tau32_count_int (b, CRONYX_LEDMODE_4IRQ | CRONYX_LEDMODE_4ERR);
	if (NotifyBits & (TAU32_ERROR_FAIL | TAU32_ERROR_TIMEOUT
			  | TAU32_ERROR_INT_OVER_TX | TAU32_ERROR_INT_OVER_RX | TAU32_ERROR_INT_STORM)) {

		if (NotifyBits & TAU32_ERROR_FAIL)
			append_text (msg, sizeof (msg), "command failed");
		if (NotifyBits & TAU32_ERROR_TIMEOUT)
			append_text (msg, sizeof (msg), "command timeout");
		if (NotifyBits & TAU32_ERROR_INT_OVER_TX)
			append_text (msg, sizeof (msg), "tx-interrupt queue overlow");
		if (NotifyBits & TAU32_ERROR_INT_OVER_RX)
			append_text (msg, sizeof (msg), "rx-interrupt queue overlow");
		if (NotifyBits & TAU32_ERROR_INT_STORM)
			append_text (msg, sizeof (msg), "interrupt storm");

		printk (KERN_ERR
			"ce: Adapter #%d - fatal failure (0x%x: %s, %d), need reset & restart\n",
			b->binder_data.order, NotifyBits, msg, Item);
		b->ok = TAU32_FATAL;
		TAU32_DisableInterrupts (b->ddk.pControllerObject);
		return;
	}
	if (Item >= 0) {
		/*
		 * channel error
		 */
		 tau32_chan_t *c;

		if (Item >= TAU32_CHANNELS)
			return;

		c = b->channels[Item];

		CRONYX_LOG_3 (&c->sys, "tau32.error 0x%X, ch %d, %s\n", NotifyBits, Item, c->config.up ? "up" : "down");
		if (c->config.up) {
			if (NotifyBits & TAU32_ERROR_TX_UNDERFLOW) {
				append_text (msg, sizeof (msg), "transmit underrun");
				CRONYX_LOG_2 (&c->sys, "transmit underrun (%d pending)\n", c->tx_pending);
				c->stat.tintr++;
				c->stat.underrun++;
				spin_unlock (&b->lock);
				cronyx_transmit_error (&c->sys, CRONYX_ERR_UNDERRUN);
				spin_lock (&b->lock);
			}
			if (NotifyBits & TAU32_ERROR_RX_OVERFLOW) {
				if (!c->overrun) {
					c->overrun = 1;
					append_text (msg, sizeof (msg), "receive overrun");
					CRONYX_LOG_2 (&c->sys, "receive overrun (%d pending)\n", c->rx_pending);
				}
				c->stat.rintr++;
				c->stat.overrun++;
				spin_unlock (&b->lock);
				cronyx_receive_error (&c->sys, CRONYX_ERR_OVERRUN);
				spin_lock (&b->lock);
			}
			if (c->config.phony) {
				if (NotifyBits & (TAU32_WARN_TX_JUMP | TAU32_WARN_RX_JUMP)) {
					tau32_request_t *r;

					r = tau32_cfg_alloc (c);
					if (r) {
						r->ddk.Command = TAU32_Rx_Stop | TAU32_Tx_Stop | TAU32_Configure_Commit;
						r->ddk.Io.ChannelConfig.Config = TAU32_TMA | TAU32_tma_nopack;
						tau32_cfg_submit (b, r);
					}
					r = tau32_cfg_alloc (c);
					if (r) {
						r->ddk.Command =
							TAU32_Rx_Start | TAU32_Tx_Start | TAU32_Configure_Commit;
						r->ddk.Io.ChannelConfig.Config = TAU32_TMA | TAU32_tma_nopack;
						tau32_cfg_submit (b, r);
					}
				}

				if (NotifyBits & TAU32_WARN_TX_JUMP) {
					append_text (msg, sizeof (msg), "transmit io-chain-jump");
					c->stat.tintr++;
					c->stat.underrun++;
					spin_unlock (&b->lock);
					cronyx_transmit_error (&c->sys, CRONYX_ERR_BREAK);
					spin_lock (&b->lock);
				}
				if (NotifyBits & TAU32_WARN_RX_JUMP) {
					append_text (msg, sizeof (msg), "receive io-chain-jump");
					c->stat.rintr++;
					c->stat.overrun++;
					spin_unlock (&b->lock);
					cronyx_receive_error (&c->sys, CRONYX_ERR_BREAK);
					spin_lock (&b->lock);
				}
			}
			if (NotifyBits & TAU32_ERROR_RX_FRAME)
				append_text (msg, sizeof (msg), "hdlc-frame error");
			if (NotifyBits & TAU32_ERROR_RX_ABORT)
				append_text (msg, sizeof (msg), "hdlc-abort detected");
			if (NotifyBits & TAU32_ERROR_RX_SHORT)
				append_text (msg, sizeof (msg), "short frame");
			if (NotifyBits & (TAU32_ERROR_RX_LONG | TAU32_ERROR_RX_SPLIT | TAU32_ERROR_RX_UNFIT))
				append_text (msg, sizeof (msg), "too long frame");

			if (NotifyBits & (TAU32_ERROR_RX_FRAME | TAU32_ERROR_RX_ABORT
					  | TAU32_ERROR_RX_SHORT | TAU32_ERROR_RX_LONG
					  | TAU32_ERROR_RX_SYNC | TAU32_ERROR_RX_SPLIT | TAU32_ERROR_RX_UNFIT)) {
				CRONYX_LOG_2 (&c->sys, "hdlc-framing error 0x%x\n", NotifyBits);
				c->stat.rintr++;
				c->stat.frame++;
				spin_unlock (&b->lock);
				cronyx_receive_error (&c->sys, CRONYX_ERR_FRAMING);
				spin_lock (&b->lock);
			}
			if (NotifyBits & TAU32_ERROR_RX_CRC) {
				append_text (msg, sizeof (msg), "hdlc-crc error");
				c->stat.rintr++;
				c->stat.crc++;
				spin_unlock (&b->lock);
				cronyx_receive_error (&c->sys, CRONYX_ERR_CHECKSUM);
				spin_lock (&b->lock);
			}
			if (msg[0])
				CRONYX_LOG_1 (&c->sys, "rx/tx error %s\n", msg);
		}
	} else {
		printk (KERN_DEBUG "ce: Adapter #%d - general error 0x%X\n", b->binder_data.order, NotifyBits);
	}
}

may_static void TAU32_CALLBACK_TYPE tau32_status_callback (TAU32_UserContext * pContext, int Item, unsigned NotifyBits)
{
	tau32_board_t *b = (tau32_board_t *) pContext;

	if (unlikely (b->ok != TAU32_RUNNING))
		return;
	tau32_count_int (b, CRONYX_LEDMODE_4IRQ);
	if (Item >= 0) {
		/* LY: e1 status */
		tau32_iface_t *i = &b->ifaces[Item];

		i->state.acc_status |= b->ddk.InterfacesInfo[Item].Status;
#ifdef LY_DEEP_DEBUG
		if (NotifyBits & (TAU32_JITTER | TAU32_LOTC | TAU32_RSLIP | TAU32_TSLIP)) {
			unsigned status = b->ddk.InterfacesInfo[Item].Status;

			printk ("E1-state-changed 0x%X: ",
				NotifyBits & (TAU32_JITTER | TAU32_LOTC | TAU32_RSLIP | TAU32_TSLIP));
			if (status & TAU32_JITTER)
				printk (" TAU32_JITTER");
			if (status & TAU32_LOTC)
				printk (" TAU32_LOTC");
			if (status & TAU32_RSLIP)
				printk (" TAU32_RSLIP");
			if (status & TAU32_TSLIP)
				printk (" TAU32_TSLIP");
			printk ("\n");
		}
#endif
		if (NotifyBits & (TAU32_RCL | TAU32_RLOS | TAU32_RUA1 | TAU32_E1OFF
			| TAU32_RRA | TAU32_RDMA | TAU32_RSA1 | TAU32_RFAS)) {
			/*
			 * modem status event
			 */
			int n;

			if (i->board->ddk.Interfaces > 1) {
				tau32_update_e1_config (&i->board->ifaces[1]);
				tau32_update_e1_config (&i->board->ifaces[0]);
			}
			for (n = 0; n < TAU32_CHANNELS; n++) {
				tau32_chan_t *c = b->channels[n];

				if (c && c->config.link == Item && c->config.ts && c->config.up) {
					c->stat.mintr++;
					spin_unlock (&b->lock);
					cronyx_modem_event (&c->sys);
					spin_lock (&b->lock);
				}
			}
		}
	} else {
		/* LY: adapter status */
#ifdef LY_DEEP_DEBUG
		printk (KERN_DEBUG "ce: Adapter #%d - status 0x%X (delta 0x%X)\n", b->binder_data.order,
			b->ddk.AdapterStatus, NotifyBits);
#endif
	}
}

may_static int tau32_led_state (void *tag)
{
	tau32_board_t *b = (tau32_board_t *) tag;
	unsigned status_link1, status_link0;

	if (b->ok != TAU32_RUNNING)
		return CRONYX_LEDSTATE_OFF;
	if (b->ddk.AdapterStatus & TAU32_FRLOMF)
		return CRONYX_LEDSTATE_ALARM;

	status_link0 = b->ifaces[0].state.acc_status | b->ifaces[0].state.last_acc_status;
	if (b->ifaces[0].state.crce || b->ddk.InterfacesInfo[0].Crc4Errors != b->ifaces[0].state.last_crce)
		status_link0 |= TAU32_RCRC4;

	status_link1 = status_link0;
	if (b->ddk.Interfaces > 1) {
		status_link1 = b->ifaces[1].state.acc_status | b->ifaces[1].state.last_acc_status;
		if (b->ifaces[1].state.crce || b->ddk.InterfacesInfo[1].Crc4Errors != b->ifaces[1].state.last_crce)
			status_link1 |= TAU32_RCRC4;
	}

	if ((status_link0 & status_link1 & TAU32_E1OFF) != 0)
		return CRONYX_LEDSTATE_OFF;

	if (b->ifaces[0].state.loop_mirror | b->ifaces[1].state.loop_mirror)
		return CRONYX_LEDSTATE_MIRROR;
	if ((status_link0 | status_link1) &
	    (TAU32_RCL | TAU32_RLOS | TAU32_RFAS | TAU32_RCRC4 | TAU32_RCAS | TAU32_RCRC4LONG))
		return CRONYX_LEDSTATE_FAILED;
	if ((status_link0 | status_link1) & (TAU32_JITTER | TAU32_LOTC | TAU32_RSLIP | TAU32_TSLIP))
		return CRONYX_LEDSTATE_ALARM;
	if ((status_link0 | status_link1) & (TAU32_RUA1 | TAU32_RRA | TAU32_RSA1 | TAU32_RSA0 | TAU32_RDMA))
		return CRONYX_LEDSTATE_WARN;
	return CRONYX_LEDSTATE_OK;
}

may_static void tau32_count_int (tau32_board_t *b, unsigned flash)
{
	b->intr_count++;
	cronyx_led_kick (&b->led_flasher, flash);
}

#if LINUX_VERSION_CODE >= 0x020613
may_static irqreturn_t tau32_irq_handler (int irq, void *dev)
#elif LINUX_VERSION_CODE >= 0x020600
may_static irqreturn_t tau32_irq_handler (int irq, void *dev, struct pt_regs *regs)
#else
may_static void tau32_irq_handler (int irq, void *dev, struct pt_regs *regs)
#endif
{
	tau32_board_t *b = dev;

#ifdef CONFIG_SMP
	if (! TAU32_IsInterruptPending (b->ddk.pControllerObject)) {
#if LINUX_VERSION_CODE >= 0x020600
		return IRQ_NONE;
#else
		return;
#endif
	}
	spin_lock (&b->lock);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4IRQ);
	TAU32_HandleInterrupt (b->ddk.pControllerObject);
#else /* CONFIG_SMP */
	spin_lock (&b->lock);
	if (!TAU32_HandleInterrupt (b->ddk.pControllerObject)) {
		spin_unlock (&b->lock);
#if LINUX_VERSION_CODE >= 0x020600
		return IRQ_NONE;
#else
		return;
#endif
	}
#endif /* CONFIG_SMP */
	spin_unlock (&b->lock);
#if LINUX_VERSION_CODE >= 0x020600
	return IRQ_HANDLED;
#else
	return;
#endif
}

may_static void tau32_notify_modem_event (tau32_board_t *b, int iface)
{
	int i;

	for (i = 0; i < TAU32_CHANNELS; i++) {
		tau32_chan_t *c = b->channels[i];

		if (c && c->config.link == iface && c->config.ts) {
			spin_unlock (&b->lock);
			cronyx_modem_event (&c->sys);
			spin_lock (&b->lock);
		}
	}
}

may_static void tau32_update_e1_stat (tau32_iface_t *i)
{
	u32 bpv, fse, rcrce, css, pcv, status;
	TAU32_E1_State * hw = &i->board->ddk.InterfacesInfo[i->binder_data.order];

	if (i->state.cfg_update_pending) {
		if (! --i->state.cfg_update_pending)
			return;
		i->state.last_acc_status = 0;
		tau32_clr_e1_stat (i);
		tau32_notify_modem_event (i->board, 0);
	}

	status = i->state.acc_status;
	i->state.last_acc_status = status;
	i->state.acc_status = hw->Status;
	i->state.crce = 0;
	if (! (hw->Status & TAU32_E1OFF)) {
		i->state.crce = hw->Crc4Errors - i->state.last_crce;
		bpv = hw->RxViolations - i->state.last_bpv;
		fse = hw->FasErrors - i->state.last_fse;
		rcrce = hw->FarEndBlockErrors - i->state.last_rcrce;
		css = hw->TransmitSlips + hw->ReceiveSlips - i->state.last_slips;

		pcv = fse + i->state.crce;
		if (status & (TAU32_RCL | TAU32_RUA1))
			i->state.e1_stat.currnt.uas++;

		if (! (status & TAU32_RCL)) {
			if (bpv)
				i->state.e1_stat.currnt.les++;
			if (bpv | pcv | (status & TAU32_RLOS))
				i->state.e1_stat.currnt.es++;

			if (status & TAU32_RLOS)
				i->state.e1_stat.currnt.oofs++;
			if (css)
				i->state.e1_stat.currnt.css++;
		}

		if (bpv > 2048 || pcv > 832)
			i->state.e1_stat.currnt.ses++;
		else {
			if (pcv > 1)
				i->state.e1_stat.currnt.bes++;
			i->state.degsec++;
			i->state.degerr += bpv + pcv;
		}
		i->state.e1_stat.currnt.bpv += bpv;
		i->state.e1_stat.currnt.fse += fse;
		i->state.e1_stat.currnt.crce += i->state.crce;
		i->state.e1_stat.currnt.rcrce += rcrce;
	}

	if (++i->state.e1_stat.cursec % 60 == 0) {
		if (i->state.degerr > i->state.degsec * 2048 / 1000)
			i->state.e1_stat.currnt.dm++;
		i->state.degsec = 0;
		i->state.degerr = 0;
	}

	if (i->state.e1_stat.cursec == 15 * 60) {
		int n;

		for (n = 47; n > 0; --n)
			i->state.e1_stat.interval[n] = i->state.e1_stat.interval[n - 1];
		i->state.e1_stat.interval[0] = i->state.e1_stat.currnt;
		i->state.e1_stat.total.bpv += i->state.e1_stat.currnt.bpv;
		i->state.e1_stat.total.fse += i->state.e1_stat.currnt.fse;
		i->state.e1_stat.total.crce += i->state.e1_stat.currnt.crce;
		i->state.e1_stat.total.rcrce += i->state.e1_stat.currnt.rcrce;
		i->state.e1_stat.total.uas += i->state.e1_stat.currnt.uas;
		i->state.e1_stat.total.les += i->state.e1_stat.currnt.les;
		i->state.e1_stat.total.es += i->state.e1_stat.currnt.es;
		i->state.e1_stat.total.bes += i->state.e1_stat.currnt.bes;
		i->state.e1_stat.total.ses += i->state.e1_stat.currnt.ses;
		i->state.e1_stat.total.oofs += i->state.e1_stat.currnt.oofs;
		i->state.e1_stat.total.css += i->state.e1_stat.currnt.css;
		i->state.e1_stat.total.dm += i->state.e1_stat.currnt.dm;
		i->state.e1_stat.currnt.bpv = 0;
		i->state.e1_stat.currnt.fse = 0;
		i->state.e1_stat.currnt.crce = 0;
		i->state.e1_stat.currnt.rcrce = 0;
		i->state.e1_stat.currnt.uas = 0;
		i->state.e1_stat.currnt.les = 0;
		i->state.e1_stat.currnt.es = 0;
		i->state.e1_stat.currnt.bes = 0;
		i->state.e1_stat.currnt.ses = 0;
		i->state.e1_stat.currnt.oofs = 0;
		i->state.e1_stat.currnt.css = 0;
		i->state.e1_stat.currnt.dm = 0;

		i->state.e1_stat.totsec += i->state.e1_stat.cursec;
		i->state.e1_stat.cursec = 0;
	}
	i->state.last_bpv = hw->RxViolations;
	i->state.last_fse = hw->FasErrors;
	i->state.last_crce = hw->Crc4Errors;
	i->state.last_rcrce = hw->FarEndBlockErrors;
	i->state.last_slips = hw->TransmitSlips + hw->ReceiveSlips;
}

may_static void tau32_second_timer (cronyx_binder_item_t *h)
{
	unsigned long flags;
	tau32_board_t *b = (tau32_board_t *) h->hw;

	if (b->ok == TAU32_RUNNING) {
		spin_lock_irqsave (&b->lock, flags);
		tau32_update_e1_stat (&b->ifaces[0]);
		if (b->ddk.Interfaces > 1)
			tau32_update_e1_stat (&b->ifaces[1]);
		spin_unlock_irqrestore (&b->lock, flags);
	}
}

may_static void tau32_hard_reset (tau32_board_t *b)
{
	u16 pci_command = 0;

	pci_read_config_word (b->pdev, PCI_COMMAND, &pci_command);
	TAU32_BeforeReset (&b->ddk);
	pci_write_config_word (b->pdev, PCI_COMMAND, 0);
	pci_write_config_dword (b->pdev, TAU32_PCI_RESET_ADDRESS, TAU32_PCI_RESET_ON);
	udelay (5);
	pci_write_config_dword (b->pdev, TAU32_PCI_RESET_ADDRESS, TAU32_PCI_RESET_OFF);
	udelay (10);
	pci_write_config_word (b->pdev, PCI_COMMAND, pci_command);
}

may_static int tau32_have_pending (tau32_board_t *b)
{
	int i;

	if (!list_empty (&b->pending_list))
		return 1;
	for (i = 0; i < TAU32_CHANNELS; i++) {
		tau32_chan_t *c = b->channels[i];

		if (c != NULL && (!list_empty (&c->pending_rx_list)
				  || !list_empty (&c->pending_tx_list)))
			return 1;
	}
	return 0;
}

may_static int tau32_halt_adapter (tau32_board_t *b)
{
	unsigned long flags, fuse;
	int i;


	if (b->ok != TAU32_HALTED) {
		if (b->ok == TAU32_RUNNING) {
			for (i = 0; i < TAU32_CHANNELS; i++) {
				tau32_chan_t *c = b->channels[i];

				if (c && c->config.up) {
					spin_lock_irqsave (&b->lock, flags);
					c->config.up = 0;
					tau32_update_chan_config (c);
					spin_unlock_irqrestore (&b->lock, flags);
				}
			}
		}

		if (b->binder_data.id > 0) {
			int error = cronyx_binder_unregister_item (b->binder_data.id);

			if (error < 0) {
				printk (KERN_ERR "ce: Adapter #%d - unable unregister from binder, error = %d\n",
					b->binder_data.order, error);
				return error;
			}
			cronyx_led_destroy (&b->led_flasher);
		}

		del_timer_sync (&b->commit_timer);
		if (b->ok == TAU32_RUNNING) {
			commit_cfg ((unsigned long) b);
			fuse = jiffies;
			spin_lock_irqsave (&b->lock, flags);
			while (tau32_have_pending (b)) {
				spin_unlock_irqrestore (&b->lock, flags);
				set_current_state (TASK_INTERRUPTIBLE);
				schedule_timeout (1);
				spin_lock_irqsave (&b->lock, flags);
				if (signal_pending (current) || jiffies - fuse > 5 * HZ)
					break;
			}

			b->ok = TAU32_HALTED;
			TAU32_DisableInterrupts (b->ddk.pControllerObject);
			spin_unlock_irqrestore (&b->lock, flags);
		}

		free_irq (b->pdev->irq, b);

		b->ok = TAU32_HALTED;
		/* LY: wait for delayed DMA-FIFO ops from MUNICH32X. */
		set_current_state (TASK_INTERRUPTIBLE);
		schedule_timeout (1);
		TAU32_DestructiveHalt (b->ddk.pControllerObject, 1);
		BUG_ON (tau32_have_pending (b));

		/* LY: wait gain for delayed DMA-FIFO ops from MUNICH32X. */
		set_current_state (TASK_INTERRUPTIBLE);
		schedule_timeout (1);
	}
	pci_disable_device (b->pdev);
	return 0;
}

may_static void tau32_free_adapter (tau32_board_t *b)
{
	if (b != 0) {
		int i;

		if (b->iomem && tau32_halt_adapter (b) < 0) {
			printk (KERN_EMERG "ce: Adapter #%d - unable to release\n", b->binder_data.order);
			BUG ();
		}

		for (i = 0; i < TAU32_CHANNELS; i++) {
			tau32_chan_t *c = b->channels[i];

			if (c)
				kfree (c);
		}
		if (b->ddk.pControllerObject) {
			memset (b->ddk.pControllerObject, 0, TAU32_ControllerObjectSize);
			pci_free_consistent (b->pdev, TAU32_ControllerObjectSize,
				b->ddk.pControllerObject, b->ddk.ControllerObjectPhysicalAddress);
		}
		if (b->ddk.PciBar1VirtualAddress)
			iounmap (b->ddk.PciBar1VirtualAddress);

		if (b->iomem) {
			release_mem_region (b->iomem, TAU32_PCI_IO_BAR1_SIZE);
			b->pdev->irq = b->irq_raw;
			pci_set_drvdata (b->pdev, NULL);
		}
		kfree (b);
	}
}

may_static void tau32_led_set (void *tag, int on)
{
	tau32_board_t *b = (tau32_board_t *) tag;

	if (b->ok != TAU32_HALTED)
		TAU32_LedSet (b->ddk.pControllerObject, on);
}

may_static struct device *tau32_get_device(cronyx_binder_item_t *h)
{
	if(h && h->type==CRONYX_ITEM_ADAPTER)
		return &((tau32_board_t *)h->hw)->pdev->dev;
	return NULL;
}

may_static int tau32_init_adapter (tau32_board_t *b)
{
	int i, error;

	error = tau32_halt_adapter (b);
	if (error < 0)
		return error;

	INIT_LIST_HEAD (&b->pending_list);
	tau32_hard_reset (b);
	if (pci_enable_device (b->pdev))
		return -ENODEV;
	pci_set_master (b->pdev);
	if (TAU32_Initialize (&b->ddk, 0)) {
		memset (&b->ifaces, 0, sizeof (b->ifaces));
		memset (&b->tsbox, 0x80, sizeof (b->tsbox));
		b->unframed_speed = 2048000;
		b->gsyn = TAU32_SYNC_INTERNAL;
		b->unframed = 0;
		b->intr_count = 0;

		b->binder_data.dispatch = dispatch_tab;
		b->binder_data.dispatch.second_timer = tau32_second_timer;
		b->binder_data.type = CRONYX_ITEM_ADAPTER;
		b->binder_data.hw = b;
		b->binder_data.provider = THIS_MODULE;
		error = cronyx_binder_register_item (0, "tau32", b->binder_data.order, 0, -1, &b->binder_data);
		if (error < 0) {
			printk (KERN_ERR "ce: Adapter #%d - unable register on binder\n", b->binder_data.order);
			return error;
		}
		b->ifaces[0].board = b;
		b->ifaces[0].binder_data.dispatch = dispatch_tab;
		b->ifaces[0].binder_data.type = CRONYX_ITEM_INTERFACE;
		b->ifaces[0].binder_data.hw = &b->ifaces[0];
		b->ifaces[0].state.casmode = CRONYX_CASMODE_OFF;
		error = cronyx_binder_register_item (b->binder_data.id, "e1", 0, 0, -1, &b->ifaces[0].binder_data);
		if (error < 0) {
			printk (KERN_ERR "ce: Adapter #%d - unable register interface #%d on binder\n",
				b->binder_data.order, 0);
			cronyx_binder_unregister_item (b->binder_data.id);
			return error;
		}

		if (b->ddk.Interfaces > 1) {
			b->ifaces[1].board = b;
			b->ifaces[1].binder_data.dispatch = dispatch_tab;
			b->ifaces[1].binder_data.type = CRONYX_ITEM_INTERFACE;
			b->ifaces[1].binder_data.hw = &b->ifaces[1];
			b->ifaces[1].state.casmode = CRONYX_CASMODE_OFF;
			error = cronyx_binder_register_item (b->binder_data.id, "e1", 1, 0, -1, &b->ifaces[1].binder_data);
			if (error < 0) {
				printk (KERN_ERR "ce: Adapter #%d - unable register interface #%d on binder\n",
					b->binder_data.order, 1);
				cronyx_binder_unregister_item (b->binder_data.id);
				return error;
			}
		}

		for (i = 0; i < TAU32_CHANNELS; i++) {
			tau32_chan_t *c = b->channels[i];

			if (c) {
				memset (c, 0, sizeof (tau32_chan_t));
				INIT_LIST_HEAD (&c->pending_rx_list);
				INIT_LIST_HEAD (&c->pending_tx_list);
				c->board = b;
				c->sys.dispatch = dispatch_tab;
				c->sys.type = CRONYX_ITEM_CHANNEL;
				c->sys.hw = c;
				c->sys.mtu = TAU32_DEFAULT_MTU;
				tau32_update_qlen (c, TAU32_DEFAULT_QUEUE_LENGTH);
				error = cronyx_binder_register_item (b->binder_data.id, 0, i, "ce",
								   b->binder_data.order * TAU32_CHANNELS + i, &c->sys);
				if (error < 0) {
					printk (KERN_ERR "ce: Adapter #%d - unable register channel #%d on binder\n",
						b->binder_data.order, i);
					cronyx_binder_unregister_item (b->binder_data.id);
					return error;
				}
			}
		}
	} else {
		printk (KERN_ERR
			"ce: Adapter #%d - init error 0x%08x, dead bits 0x%08x\n",
			b->binder_data.order, b->ddk.InitErrors, b->ddk.DeadBits);
		return -EIO;
	}

	b->ok = TAU32_RUNNING;
#ifdef TAU32_USE_RAW_LOCK
	cronyx_led_init_ex (&b->led_flasher, 1, &b->lock, b, tau32_led_set, tau32_led_state);
	if (request_irq (b->pdev->irq, tau32_irq_handler, IRQF_NODELAY, device_name, b)
#else
	cronyx_led_init	(&b->led_flasher, &b->lock, b, tau32_led_set, tau32_led_state);
	if (request_irq (b->pdev->irq, tau32_irq_handler, IRQF_SHARED, device_name, b)
#endif /* TAU32_USE_RAW_LOCK */
		  != 0) {
			printk (KERN_ERR "ce: Adapter #%d - can't get irq %s\n",
				b->binder_data.order, cronyx_format_irq (b->irq_raw, b->pdev->irq));
			cronyx_binder_unregister_item (b->binder_data.id);
			return -EBUSY;
	}
	b->irq_apic = b->pdev->irq;
	TAU32_EnableInterrupts (b->ddk.pControllerObject);
	return 0;
}

may_static int __init tau32_probe(struct pci_dev *dev, const struct pci_device_id *ident)
{
	tau32_board_t *b;
	char *model_suffix;
	int i, error;
	dma_addr_t bus_address;

	++dev_counter;
	error = -ENOMEM;
	b = kzalloc (sizeof (tau32_board_t), GFP_KERNEL);
	if (!b) {
		printk (KERN_ERR "ce: Adapter #%d - out of core-memory %d\n", dev_counter, (unsigned) sizeof (tau32_board_t));
		goto ballout;
	}

	b->iomem = pci_resource_start (dev, 0);
	if (request_mem_region (b->iomem, TAU32_PCI_IO_BAR1_SIZE, device_name) == 0) {
		printk (KERN_ERR "ce: Adapter #%d - io-region busy at 0x%lx, other driver loaded?\n",
			b->binder_data.order, (unsigned long) b->iomem);
		b->iomem = 0;
		error = -EBUSY;
		goto ballout;
	}

	spin_lock_init (&b->lock);
	init_timer (&b->commit_timer);
	b->ok = TAU32_HALTED;
	b->binder_data.order = dev_counter;
	b->pdev = dev;
	b->irq_raw = dev->irq;
	pci_set_drvdata (dev, b);

	if (pci_set_dma_mask (dev, DMA_32BIT_MASK)
#if LINUX_VERSION_CODE >= 0x020600
	|| pci_set_consistent_dma_mask (dev, DMA_32BIT_MASK)
#endif
	) {
		printk (KERN_ERR "ce: Adapter #%d - no suitable DMA available\n", b->binder_data.order);
		goto ballout;
	}

	b->ddk.pControllerObject = pci_alloc_consistent (dev, TAU32_ControllerObjectSize, &bus_address);
	if (!b->ddk.pControllerObject) {
		printk (KERN_ERR "ce: Adapter #%d - out of dma-memory %d\n", b->binder_data.order, TAU32_ControllerObjectSize);
		goto ballout;
	}
	BUG_ON (bus_address >= 0xFFFFFFFFul - TAU32_ControllerObjectSize);
	b->ddk.ControllerObjectPhysicalAddress = (u32) bus_address;

	for (i = 0; i < TAU32_CHANNELS; i++) {
		tau32_chan_t *c = kzalloc (sizeof (tau32_chan_t), GFP_KERNEL);

		if (!c) {
			printk (KERN_ERR "ce: Adapter #%d - out of channel-memory %d\n",
				b->binder_data.order, (unsigned) sizeof (tau32_chan_t));
			goto ballout;
		}
		b->channels[i] = c;
	}

	b->ddk.PciBar1VirtualAddress = ioremap_nocache (b->iomem, TAU32_PCI_IO_BAR1_SIZE);
	if (! b->ddk.PciBar1VirtualAddress) {
		printk (KERN_ERR "ce: Adapter #%d - unable ioremap() pci region 0x%lx\n",
			b->binder_data.order, (unsigned long) b->iomem);
		goto ballout;
	}
	b->ddk.pErrorNotifyCallback = tau32_error_callback;
	b->ddk.pStatusNotifyCallback = tau32_status_callback;
	if (tau32_init_adapter (b) < 0) {
		error = -EIO;
		goto ballout;
	}

	switch (b->ddk.Model) {
		default:
			model_suffix = "-unknown";
			break;
		case TAU32_BASE:
			model_suffix = "";
			break;
		case TAU32_LITE:
			model_suffix = "-Lite";
			break;
	}
	printk (KERN_INFO "ce: Adapter #%d - <Cronyx Tau-PCI/32%s> at 0x%lx irq %s\n",
		b->binder_data.order, model_suffix, (unsigned long) b->iomem,
		cronyx_format_irq (b->irq_raw, b->irq_apic));

	return 0;

ballout:
	tau32_free_adapter (b);
	return error;
}

may_static void __exit tau32_remove (struct pci_dev *dev)
{
	tau32_board_t *b;

	b = pci_get_drvdata (dev);
	if (b) {
		tau32_free_adapter (b);
		pci_set_drvdata (dev, 0);
	}
}

may_static struct pci_device_id tau32_ids[] = {
	{TAU32_PCI_VENDOR_ID, TAU32_PCI_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 (unsigned long) "Infineon 20321 / MUNICH32X"},
	{0,}
};

may_static struct pci_driver tau32_driver = {
	.name = "Cronyx Tau32-PCI",
	.probe = tau32_probe,
#if LINUX_VERSION_CODE < 0x020600
	.remove = tau32_remove,
#else
	.remove = __exit_p (tau32_remove),
#endif
	.id_table = tau32_ids
};

int __init init_module (void)
{
	int err;

#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#else
	tau32_request_pool = mempool_create_kmalloc_pool (64, sizeof (tau32_request_t));
	if (!tau32_request_pool)
		return -ENOMEM;
#endif
#if LINUX_VERSION_CODE < 0x02060A
	err = pci_module_init (&tau32_driver);
#else
	err = pci_register_driver (&tau32_driver);
#endif

#if LINUX_VERSION_CODE >= 0x020600
	if (err < 0)
		mempool_destroy (tau32_request_pool);
#endif
	return err;
}

void __exit cleanup_module (void)
{
	pci_unregister_driver (&tau32_driver);
#if LINUX_VERSION_CODE >= 0x020600
	mempool_destroy (tau32_request_pool);
#endif
}

MODULE_DEVICE_TABLE (pci, tau32_ids);

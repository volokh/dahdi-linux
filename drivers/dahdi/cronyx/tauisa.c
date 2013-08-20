/*
 * Cronyx Tau-ISA driver for Linux.
 *
 * Copyright (C) 1998-2005 Cronyx Engineering.
 * Author: (C) 1998 Alexander Kvitchenko <aak@cronyx.ru>
 *
 * This source was derived from:
 * Cronyx-Tau adapter driver for Linux.
 * by Dmitry Gorodchanin <begemot@bgm.rosprint.net> (C) 1997
 *
 * It was rewritten using Cronyx-Tau DDK (Driver Developper's Toolkit)
 *
 * Copyright (C) 2006-2009 Cronyx Engineering.
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: tauisa.c,v 1.42 2009-10-08 09:15:09 ly Exp $
 */
#include "module.h"
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <linux/isa.h>
#include <asm/io.h>
#include <asm/dma.h>
#include "tauisa-ddk.h"
#include "cserial.h"

#if !defined(CONFIG_ISA) && !defined(CONFIG_GENERIC_ISA_DMA) && !defined(CONFIG_ISA_DMA_API)
#	error "Cronyx Tau-ISA module required ISA-bus support."
#endif

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx driver Tau-ISA adapters serie\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

typedef struct _tauisa_adapter_t {
	tauisa_board_t ddk;
	spinlock_t lock;
	tauisa_buf_t *buffers[TAUISA_NCHAN];
	cronyx_binder_item_t channels[TAUISA_NCHAN];
	cronyx_binder_item_t interfaces[TAUISA_NCHAN];
	cronyx_binder_item_t binder_data;
	int modem_status[TAUISA_NCHAN];
	struct cronyx_led_flasher_t led_flasher;
} tauisa_adapter_t;

/* module parameters */
int port[TAUISA_NBRD], irq[TAUISA_NBRD], dma[TAUISA_NBRD];

#ifdef MODULE_PARM
MODULE_PARM (port, "1-" __MODULE_STRING (TAUISA_NBRD) "i");
MODULE_PARM (irq, "1-" __MODULE_STRING (TAUISA_NBRD) "i");
MODULE_PARM (dma, "1-" __MODULE_STRING (TAUISA_NBRD) "i");
#else
module_param_array (port, int, NULL, 0644);
module_param_array (irq, int, NULL, 0644);
module_param_array (dma, int, NULL, 0644);
#endif
MODULE_PARM_DESC (port, "I/O base port for each adapter");
MODULE_PARM_DESC (irq, "IRQ for each adapter");
MODULE_PARM_DESC (dma, "DMA for each adapter");

static tauisa_adapter_t *adapters[TAUISA_NBRD];

static int tauisa_ctl_get (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	int i, error;
	tauisa_chan_t *c;
	tauisa_adapter_t *b;
	unsigned long flags;

	if (item->hw == NULL)
		return -ENODEV;

	if (item->type == CRONYX_ITEM_ADAPTER) {
		c = NULL;
		b = (tauisa_adapter_t *) item->hw;
	} else {
		c = (tauisa_chan_t *) item->hw;
		b = (tauisa_adapter_t *) c->board;
	}

	error = 0;
	switch (ctl->param_id) {
		case cronyx_channel_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else
				ctl->u.param.value = c->gopt.phony ? CRONYX_MODE_PHONY : CRONYX_MODE_HDLC;
			break;

		case cronyx_adapter_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-adapter\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else {
				switch (b->ddk.opt.cfg) {
					default:
						error = -EINVAL;
						break;
					case TAUISA_CFG_A:
						ctl->u.param.value = CRONYX_MODE_SEPARATE;
						break;
					case TAUISA_CFG_B:
						ctl->u.param.value = CRONYX_MODE_B;
						break;
					case TAUISA_CFG_C:
						ctl->u.param.value = CRONYX_MODE_SPLIT;
						break;
				}
			}
			break;

		case cronyx_higain:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-higain\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode != TAUISA_MODE_E1)
				error = -EINVAL;
			else
				ctl->u.param.value = c->gopt.higain;
			break;

		case cronyx_sync_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-clock\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode < TAUISA_MODE_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (tauisa_get_clk (c)) {
					default:
					case TAUISA_CLK_INT:
						ctl->u.param.value = CRONYX_E1CLK_INTERNAL;
						break;
					case TAUISA_CLK_RCV:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE;
						break;
					case TAUISA_CLK_XFER:
						ctl->u.param.value =
							c->
							num ? CRONYX_E1CLK_RECEIVE_CHAN0 : CRONYX_E1CLK_RECEIVE_CHAN1;
						break;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_timeslots_use:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-timeslots\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (c->mode != TAUISA_MODE_E1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_ts (c);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_timeslots_subchan:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-subchan\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else if (b->ddk.opt.cfg == TAUISA_CFG_A || b->ddk.chan[0].mode != TAUISA_MODE_E1)
				error = -EINVAL;
			else
				ctl->u.param.value = b->ddk.opt.s2;
			break;

		case cronyx_stat_channel:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-stat-channel\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_channel.rintr = c->rintr;
				ctl->u.stat_channel.tintr = c->tintr;
				ctl->u.stat_channel.mintr = c->mintr;
				ctl->u.stat_channel.ibytes = c->ibytes;
				ctl->u.stat_channel.ipkts = c->ipkts;
				ctl->u.stat_channel.ierrs = c->ierrs;
				ctl->u.stat_channel.obytes = c->obytes;
				ctl->u.stat_channel.opkts = c->opkts;
				ctl->u.stat_channel.oerrs = c->oerrs;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_stat_e1:
			CRONYX_LOG_3 (item, "tauisa.ctl: get-stat-e1\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode < TAUISA_MODE_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_e1.status = CRONYX_E1_PENDING;
				if (! tauisa_get_loop (c)) {
					ctl->u.stat_e1.status = 0;
					if (c->status & TAUISA_E1_LOS)
						ctl->u.stat_e1.status |= CRONYX_E1_LOS;
					if (c->status & TAUISA_E1_LOF)
						ctl->u.stat_e1.status |= CRONYX_E1_LOF;
					if (c->status & TAUISA_E1_AIS)
						ctl->u.stat_e1.status |= CRONYX_E1_AIS;
					if (c->status & TAUISA_E1_LOMF)
						ctl->u.stat_e1.status |= CRONYX_E1_LOMF;
					if (c->status & TAUISA_E1_AIS16)
						ctl->u.stat_e1.status |= CRONYX_E1_AIS16;
					if (c->status & TAUISA_E1_RA)
						ctl->u.stat_e1.status |= CRONYX_E1_RA;
					if (c->status & TAUISA_E1_RDMA)
						ctl->u.stat_e1.status |= CRONYX_E1_RDMA;
					if (c->status & TAUISA_E1_TSTREQ)
						ctl->u.stat_e1.status |= CRONYX_E1_TSTREQ;
					if (c->status & TAUISA_E1_TSTERR)
						ctl->u.stat_e1.status |= CRONYX_E1_TSTERR;
				}

				ctl->u.stat_e1.cursec = c->cursec;
				ctl->u.stat_e1.totsec = c->totsec + c->cursec;

				ctl->u.stat_e1.currnt.bpv = c->currnt.bpv;
				ctl->u.stat_e1.currnt.fse = c->currnt.fse;
				ctl->u.stat_e1.currnt.crce = c->currnt.crce;
				ctl->u.stat_e1.currnt.rcrce = c->currnt.rcrce;
				ctl->u.stat_e1.currnt.uas = c->currnt.uas;
				ctl->u.stat_e1.currnt.les = c->currnt.les;
				ctl->u.stat_e1.currnt.es = c->currnt.es;
				ctl->u.stat_e1.currnt.bes = c->currnt.bes;
				ctl->u.stat_e1.currnt.ses = c->currnt.ses;
				ctl->u.stat_e1.currnt.oofs = c->currnt.oofs;
				ctl->u.stat_e1.currnt.css = c->currnt.css;
				ctl->u.stat_e1.currnt.dm = c->currnt.dm;

				ctl->u.stat_e1.total.bpv = c->total.bpv + c->currnt.bpv;
				ctl->u.stat_e1.total.fse = c->total.fse + c->currnt.fse;
				ctl->u.stat_e1.total.crce = c->total.crce + c->currnt.crce;
				ctl->u.stat_e1.total.rcrce = c->total.rcrce + c->currnt.rcrce;
				ctl->u.stat_e1.total.uas = c->total.uas + c->currnt.uas;
				ctl->u.stat_e1.total.les = c->total.les + c->currnt.les;
				ctl->u.stat_e1.total.es = c->total.es + c->currnt.es;
				ctl->u.stat_e1.total.bes = c->total.bes + c->currnt.bes;
				ctl->u.stat_e1.total.ses = c->total.ses + c->currnt.ses;
				ctl->u.stat_e1.total.oofs = c->total.oofs + c->currnt.oofs;
				ctl->u.stat_e1.total.css = c->total.css + c->currnt.css;
				ctl->u.stat_e1.total.dm = c->total.dm + c->currnt.dm;
				for (i = 0; i < 48; ++i) {
					ctl->u.stat_e1.interval[i].bpv = c->interval[i].bpv;
					ctl->u.stat_e1.interval[i].fse = c->interval[i].fse;
					ctl->u.stat_e1.interval[i].crce = c->interval[i].crce;
					ctl->u.stat_e1.interval[i].rcrce = c->interval[i].rcrce;
					ctl->u.stat_e1.interval[i].uas = c->interval[i].uas;
					ctl->u.stat_e1.interval[i].les = c->interval[i].les;
					ctl->u.stat_e1.interval[i].es = c->interval[i].es;
					ctl->u.stat_e1.interval[i].bes = c->interval[i].bes;
					ctl->u.stat_e1.interval[i].ses = c->interval[i].ses;
					ctl->u.stat_e1.interval[i].oofs = c->interval[i].oofs;
					ctl->u.stat_e1.interval[i].css = c->interval[i].css;
					ctl->u.stat_e1.interval[i].dm = c->interval[i].dm;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-baud\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_baud (c);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_loop_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-loop\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_loop (c) ? CRONYX_LOOP_INTERNAL : CRONYX_LOOP_NONE;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_dpll:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-dpll\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode >= TAUISA_MODE_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_dpll (c);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_port_or_cable_type:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-port-type\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else
				switch (c->mode) {
					case TAUISA_MODE_ASYNC:
						ctl->u.param.value = CRONYX_RS232;
						break;
					case TAUISA_MODE_HDLC:
						ctl->u.param.value = CRONYX_SERIAL;
						break;
					case TAUISA_MODE_G703:
					case TAUISA_MODE_E1:
						ctl->u.param.value = CRONYX_TP;
						break;
					default:
						ctl->u.param.value = CRONYX_UNKNOWN;
				}
			break;

		case cronyx_line_code:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-line\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode >= TAUISA_MODE_G703)
				ctl->u.param.value = CRONYX_HDB3;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_nrzi (c) ? CRONYX_NRZI : CRONYX_NRZ;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_invclk_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-invclk\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_invtxc (c) ? CRONYX_ICLK_TX : CRONYX_ICLK_NORMAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-led\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_GET, ctl);
			break;

		case cronyx_inlevel_sdb:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-inlevel\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode != TAUISA_MODE_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = tauisa_get_lq (c);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: get-crc-mode\n");
			ctl->u.param.value = CRONYX_CRC_16;
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			break;

		default:
			CRONYX_LOG_2 (item, "tauisa.ctl: get-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}

	return error;
}

static int tauisa_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	int i, error;
	tauisa_chan_t *c;
	tauisa_adapter_t *b;
	unsigned long flags;

	if (item->hw == NULL)
		return -ENODEV;

	if (item->type == CRONYX_ITEM_ADAPTER) {
		c = NULL;
		b = (tauisa_adapter_t *) item->hw;
	} else {
		c = (tauisa_chan_t *) item->hw;
		b = (tauisa_adapter_t *) c->board;
	}

	error = 0;
	switch (ctl->param_id) {
		case cronyx_channel_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_PHONY && ctl->u.param.value != CRONYX_MODE_HDLC)
				error = -EINVAL;
			else if (ctl->u.param.value == CRONYX_MODE_PHONY
				 && b->ddk.type != TAUISA_MODEL_E1_d && b->ddk.type != TAUISA_MODEL_2E1_d)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (tauisa_set_phony (c, ctl->u.param.value == CRONYX_MODE_PHONY) < 0 && ctl->u.param.value == CRONYX_MODE_PHONY)
					error = -EINVAL;
				else {
					// item->mtu = c->mru;  // LY: TODO ?
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_adapter_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-adapter\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					default:
						i = 0;
						break;
					case CRONYX_MODE_SEPARATE:
						i = TAUISA_CFG_A;
						break;
					case CRONYX_MODE_B:
						i = TAUISA_CFG_B;
						break;
					case CRONYX_MODE_SPLIT:
						i = TAUISA_CFG_C;
						break;
				}
				if (i == 0 || tauisa_set_config (&b->ddk, i) < 0)
					error = -EINVAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_higain:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-higain\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (tauisa_set_higain (c, ctl->u.param.value) < 0)
					error = -EINVAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_sync_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-clock\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					default:
						i = 0;
						break;
					case CRONYX_E1CLK_RECEIVE:
						i = TAUISA_CLK_RCV;
						break;
					case CRONYX_E1CLK_INTERNAL:
						i = TAUISA_CLK_INT;
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN0:
						i = (c->num == 0) ? TAUISA_CLK_RCV : TAUISA_CLK_XFER;
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN1:
						i = (c->num == 1) ? TAUISA_CLK_RCV : TAUISA_CLK_XFER;
						break;
				}
				if (i == 0 || tauisa_set_clk (c, i) < 0)
					error = -EINVAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_timeslots_use:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-timeslots\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (tauisa_set_ts (c, ctl->u.param.value) < 0)
					error = -EINVAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_timeslots_subchan:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-subchan\n");
			if (item->type != CRONYX_ITEM_ADAPTER || b->ddk.opt.cfg == TAUISA_CFG_A)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (tauisa_set_subchan (&b->ddk, ctl->u.param.value) < 0)
					error = -EINVAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_clear_stat:
			CRONYX_LOG_1 (item, "tauisa.ctl: clear-stat\n");
			spin_lock_irqsave (&b->lock, flags);
			switch (item->type) {
				default:
					error = -EINVAL;
					break;
				case CRONYX_ITEM_CHANNEL:
					c->rintr = 0;
					c->tintr = 0;
					c->mintr = 0;
					c->ibytes = 0;
					c->ipkts = 0;
					c->ierrs = 0;
					c->obytes = 0;
					c->opkts = 0;
					c->oerrs = 0;
					break;
				case CRONYX_ITEM_INTERFACE:
					c->cursec = 0;
					c->totsec = 0;
					memset (&c->currnt, 0, sizeof (c->currnt));
					memset (&c->total, 0, sizeof (c->total));
					memset (&c->interval, 0, sizeof (c->interval));
					break;
				case CRONYX_ITEM_ADAPTER:
					for (i = 0; i < TAUISA_NCHAN; i++) {
						c = (tauisa_chan_t *) b->channels[i].hw;
						if (c) {
							c->rintr = 0;
							c->tintr = 0;
							c->mintr = 0;
							c->ibytes = 0;
							c->ipkts = 0;
							c->ierrs = 0;
							c->obytes = 0;
							c->opkts = 0;
							c->oerrs = 0;
							c->cursec = 0;
							c->totsec = 0;
							memset (&c->currnt, 0, sizeof (c->currnt));
							memset (&c->total, 0, sizeof (c->total));
							memset (&c->interval, 0, sizeof (c->interval));
						}
					}
					break;
			}
			spin_unlock_irqrestore (&b->lock, flags);
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-baud\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (c->mode == TAUISA_MODE_E1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				tauisa_set_baud (c, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_loop_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-loop\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_LOOP_INTERNAL && ctl->u.param.value != CRONYX_LOOP_NONE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				tauisa_set_loop (c, ctl->u.param.value == CRONYX_LOOP_INTERNAL);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_dpll:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-dpll\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode != TAUISA_MODE_HDLC)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				tauisa_set_dpll (c, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_line_code:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-line\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else
				switch (ctl->u.param.value) {
					case CRONYX_NRZI:
					case CRONYX_NRZ:
						if (c->type == TAUISA_MODE_HDLC) {
							spin_lock_irqsave (&b->lock, flags);
							tauisa_set_nrzi (c, ctl->u.param.value == CRONYX_NRZI);
							spin_unlock_irqrestore (&b->lock, flags);
						} else
							error = -EINVAL;
						break;

					case CRONYX_HDB3:
						if (c->type == TAUISA_MODE_G703 || c->type == TAUISA_MODE_E1)
							break;
						/*
						 * LY: no break here
						 */
					case CRONYX_AMI:
						/*
						 * LY: no break here
						 */
					default:
						error = -EINVAL;
				}
			break;

		case cronyx_invclk_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-invclk\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else if (c->mode != TAUISA_MODE_HDLC)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_ICLK_NORMAL && ctl->u.param.value != CRONYX_ICLK_TX)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				tauisa_set_invtxc (c, ctl->u.param.value == CRONYX_ICLK_TX);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-led\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_SET, ctl);
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (item, "tauisa.ctl: set-crc-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL || ctl->u.param.value != CRONYX_CRC_16)
				error = -EINVAL;
			break;

		default:
			CRONYX_LOG_2 (item, "tauisa.ctl: set-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}

	return error;
}

static int tauisa_up (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_1 (h, "tauisa.up\n");
	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	tauisa_enable_receive (c, 1);
	tauisa_enable_transmit (c, 1);
	tauisa_set_dtr (c, 1);
	tauisa_set_rts (c, 1);

	if (h->running) {
		spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
		return 0;
	}
	h->running = 1;

	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif

	return 0;
}

static void tauisa_down (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;
	int saved_running;
	unsigned long flags;

	CRONYX_LOG_1 (h, "tauisa.down\n");
	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);

	tauisa_set_rts (c, 0);
	tauisa_set_dtr (c, 0);
	saved_running = h->running;
	h->running = 0;
	tauisa_enable_receive (c, 0);
	tauisa_enable_transmit (c, 0);

	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
	if (saved_running) {
#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put (THIS_MODULE);
#endif
	}
}

static int tauisa_transmit (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	tauisa_chan_t *c = h->hw;
	int result;
	unsigned long flags;

	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	result = tauisa_send_packet (c, skb->data, skb->len, 0);
	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
	switch (result) {
		case 0:
			CRONYX_LOG_2 (h, "ct: send %d bytes\n", (int) skb->len);
			return skb->len;	/* success */
		case -1:
			return 0;	/* no free buffers */
		default:
			CRONYX_LOG_1 (h, "ct: send error #%d\n", result);
			return -EIO;	/* error */
	}
}

static void tauisa_dtr (cronyx_binder_item_t * h, int val)
{
	tauisa_chan_t *c = h->hw;
	unsigned long flags;

	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	tauisa_set_dtr (c, val);
	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
}

static void tauisa_rts (cronyx_binder_item_t * h, int val)
{
	tauisa_chan_t *c = h->hw;
	unsigned long flags;

	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	tauisa_set_rts (c, val);
	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
}

static int tauisa_query_dtr (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;

	return c->dtr;
}

static int tauisa_query_rts (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;

	return c->rts;
}

static int tauisa_query_dsr (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;
	unsigned long flags;
	int val;

	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	val = tauisa_get_dsr (c);
	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
	return val;
}

static int tauisa_query_cts (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;
	unsigned long flags;
	int val;

	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	val = tauisa_get_cts (c);
	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
	return val;
}

static int tauisa_query_dcd (cronyx_binder_item_t * h)
{
	tauisa_chan_t *c = h->hw;
	unsigned long flags;
	int val;

	spin_lock_irqsave (&((tauisa_adapter_t *) c->board)->lock, flags);
	val = tauisa_get_cd (c);
	spin_unlock_irqrestore (&((tauisa_adapter_t *) c->board)->lock, flags);
	return val;
}

static void irq_receive (tauisa_chan_t * c, char *data, int len)
{
	cronyx_binder_item_t *h = c->sys;
	struct sk_buff *skb;
	tauisa_adapter_t *b = (tauisa_adapter_t *) c->board;

	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4RX);
	if (!h->proto)
		return;
	CRONYX_LOG_2 (h, "receive len=%d\n", len);
	skb = dev_alloc_skb (len);
	if (!skb) {
		printk (KERN_ERR "ct: no memory for I/O buffer.");
		return;
	}
	skb_put (skb, len);
	memcpy (skb->data, data, len);
	spin_unlock (&b->lock);
	cronyx_receive (h, skb);
	spin_lock (&b->lock);
}

static void irq_transmit (tauisa_chan_t * c, void *attachment, int len)
{
	cronyx_binder_item_t *h = c->sys;
	tauisa_adapter_t *b = (tauisa_adapter_t *) c->board;

	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4TX);
	CRONYX_LOG_2 (h, "transmit len=%d\n", len);
	if (h->proto) {
		spin_unlock (&b->lock);
		cronyx_transmit_done (h);
		spin_lock (&b->lock);
	}
}

static void irq_error (tauisa_chan_t * c, int data)
{
	cronyx_binder_item_t *h = c->sys;
	tauisa_adapter_t *b = (tauisa_adapter_t *) c->board;

	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4ERR);
	if (h->proto) {
		spin_unlock (&b->lock);
		switch (data) {
			case TAUISA_FRAME:
				CRONYX_LOG_1 (h, "ct: framing error\n");
				cronyx_receive_error (h, CRONYX_ERR_FRAMING);
				break;
			case TAUISA_CRC:
				CRONYX_LOG_1 (h, "ct: crc error\n");
				cronyx_receive_error (h, CRONYX_ERR_CHECKSUM);
				break;
			case TAUISA_UNDERRUN:
				CRONYX_LOG_1 (h, "ct: transmit underrun\n");
				cronyx_receive_error (h, CRONYX_ERR_UNDERRUN);
				break;
			case TAUISA_OVERRUN:
				CRONYX_LOG_1 (h, "ct: receive overrun\n");
				cronyx_receive_error (h, CRONYX_ERR_OVERRUN);
				break;
			case TAUISA_OVERFLOW:
				CRONYX_LOG_1 (h, "ct: receive overflow\n");
				cronyx_receive_error (h, CRONYX_ERR_OVERFLOW);
				break;
			default:
				CRONYX_LOG_1 (h, "ct: error #%d\n", data);
				break;
		}
		spin_lock (&b->lock);
	}
}

static void irq_modem (tauisa_chan_t * c)
{
	cronyx_binder_item_t *h = c->sys;
	tauisa_adapter_t *b = (tauisa_adapter_t *) c->board;
	int modem_status;

	spin_unlock (&b->lock);
	modem_status = cronyx_get_modem_status (h);
	CRONYX_LOG_2 (h, "tauisa.modem_event (0x%X -> 0x%X)\n", b->modem_status[c->num], modem_status);
	b->modem_status[c->num] = modem_status;
	cronyx_modem_event (h);
	spin_lock (&b->lock);
}

static void tauisa_led_set (void *tag, int on)
{
	tauisa_led (&((tauisa_adapter_t *) tag)->ddk, on);
}

static int tauisa_led_state (void *tag)
{
	tauisa_adapter_t *b = (tauisa_adapter_t *) tag;
	tauisa_chan_t *c;
	int result = CRONYX_LEDSTATE_OFF;

	for (c = b->ddk.chan; c < b->ddk.chan + TAUISA_NCHAN; c++) {
		cronyx_binder_item_t *item;

		if (c->type == TAUISA_NONE)
			continue;

		item = c->sys;
		if (item->running) {
			if (c->mode >= TAUISA_MODE_G703) {
				if (c->status & (TAUISA_E1_LOF | TAUISA_E1_LOS | TAUISA_E1_LOMF))
					return CRONYX_LEDSTATE_FAILED;
				if (c->status & TAUISA_E1_TSTERR)
					result = CRONYX_LEDSTATE_ALARM;
				if ((c->
				     status & (TAUISA_E1_RA | TAUISA_E1_AIS | TAUISA_E1_AIS16 | TAUISA_E1_RDMA))
				    != 0 && result < CRONYX_LEDSTATE_WARN)
					result = CRONYX_LEDSTATE_WARN;
				if ((c->status & TAUISA_E1_NOALARM) && (result < CRONYX_LEDSTATE_OK))
					result = CRONYX_LEDSTATE_OK;
			} else {
				if (!tauisa_get_cd (c))
					return CRONYX_LEDSTATE_FAILED;
				if (!tauisa_get_cts (c))
					result = CRONYX_LEDSTATE_ALARM;
				if (!tauisa_get_dsr (c) && result < CRONYX_LEDSTATE_WARN)
					result = CRONYX_LEDSTATE_WARN;
				if (result < CRONYX_LEDSTATE_OK)
					result = CRONYX_LEDSTATE_OK;
			}
		}
	}

	return result;
}

#if LINUX_VERSION_CODE >= 0x020613
static irqreturn_t irq_handler (int irq, void *dev)
#elif LINUX_VERSION_CODE >= 0x020600
static irqreturn_t irq_handler (int irq, void *dev, struct pt_regs *regs)
#else
static void irq_handler (int irq, void *dev, struct pt_regs *regs)
#endif
{
	tauisa_adapter_t *b = (tauisa_adapter_t *) dev;

	spin_lock (&b->lock);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4IRQ);
	tauisa_int_handler (&b->ddk);

	spin_unlock (&b->lock);
#if LINUX_VERSION_CODE >= 0x020600
	return IRQ_HANDLED;
#endif
}

static void tauisa_second_timer (cronyx_binder_item_t * item)
{
	unsigned long flags;
	tauisa_chan_t *c = (tauisa_chan_t *) item->hw;
	tauisa_adapter_t *b = (tauisa_adapter_t *) c->board;
	int modem_status;

	if (c->mode == TAUISA_MODE_G703) {
		spin_lock_irqsave (&b->lock, flags);
		tauisa_g703_timer (c);
		spin_unlock_irqrestore (&b->lock, flags);
	}
	modem_status = cronyx_get_modem_status (item);
	spin_lock_irqsave (&b->lock, flags);
	if ((modem_status ^ b->modem_status[c->num]) & (TIOCM_CD | TIOCM_CTS | TIOCM_DSR)) {
		CRONYX_LOG_2 (item, "tauisa.modem_event (0x%X -> 0x%X)\n", b->modem_status[c->num], modem_status);
		b->modem_status[c->num] = modem_status;
		c->mintr++;
		spin_unlock_irqrestore (&b->lock, flags);
		cronyx_modem_event (item);
	} else
		spin_unlock_irqrestore (&b->lock, flags);
}

static cronyx_dispatch_t dispatch_tab = {
	.link_up = tauisa_up,
	.link_down = tauisa_down,
	.transmit = tauisa_transmit,
	.set_dtr = tauisa_dtr,
	.set_rts = tauisa_rts,
	.query_dtr = tauisa_query_dtr,
	.query_rts = tauisa_query_rts,
	.query_dsr = tauisa_query_dsr,
	.query_cts = tauisa_query_cts,
	.query_dcd = tauisa_query_dcd,
	.ctl_get = tauisa_ctl_get,
	.ctl_set = tauisa_ctl_set
};

static const char *tauisa_iface_name (int type)
{
	switch (type) {
		default:
		case TAUISA_SERIAL:
			return "s";
		case TAUISA_E1:
		case TAUISA_E1_SERIAL:
			return "e1";
		case TAUISA_G703:
		case TAUISA_G703_SERIAL:
			return "g703";
	}
}

int init_module (void)
{
	tauisa_adapter_t *b;
	tauisa_chan_t *c;
	int j, ndev, i, found;
	unsigned long flags;

#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	found = 0;
	if (!port[0]) {
		/*
		 * Autodetect all adapters.
		 */
		for (i = 0, j = 0; tauisa_porttab[i] && j < TAUISA_NBRD; i++) {
			if (request_region (tauisa_porttab[i], TAUISA_NPORT, "Cronyx Tau-ISA")) {
				if (tauisa_probe_board (tauisa_porttab[i], -1, -1))
					port[j++] = tauisa_porttab[i];
				release_region (tauisa_porttab[i], TAUISA_NPORT);
			}
		}
	}
	if (!dma[0]) {
		/*
		 * Find available 16-bit DRQs.
		 */
		for (i = 0, j = 0; tauisa_dmatab[i] && j < TAUISA_NBRD && port[j]; ++i)
			if (request_dma (tauisa_dmatab[i], "Cronyx Tau-ISA") == 0) {
				dma[j++] = tauisa_dmatab[i];
				free_dma (tauisa_dmatab[i]);
			}
	}
	ndev = 0;
	for (i = 0; i < TAUISA_NBRD; ++i) {
		int connected_irq = 0;

		if (!port[i])
			continue;
		if (request_region (port[i], TAUISA_NPORT, "Cronyx Tau-ISA") == 0) {
			printk (KERN_ERR "ct: Adapter #%d - port address busy at 0x%x\n", i, port[i]);
			continue;
		}
		local_irq_save (flags);
		if (!tauisa_probe_board (port[i], irq[i] ? irq[i] : -1, dma[i])) {
			printk (KERN_ERR "ct: Adapter #%d - no adapter at port 0x%x\n", i, port[i]);
			local_irq_restore (flags);
			release_region (port[i], TAUISA_NPORT);
			continue;
		}
		found = 1;
		local_irq_restore (flags);

		b = kzalloc (sizeof (tauisa_adapter_t), GFP_KERNEL);
		if (b == NULL) {
			printk (KERN_ERR "ct: Adapter #%d - out of memory\n", i);
			break;
		}
		spin_lock_init (&b->lock);

		b->ddk.dma = dma[i];
		if (request_dma (b->ddk.dma, "Cronyx Tau-ISA") != 0) {
			printk (KERN_ERR "ct: Adapter #%d - cannot get dma %d\n", i, b->ddk.dma);
			b->ddk.dma = 0;
			goto ballout;
		}
		disable_dma (b->ddk.dma);
		if (!tauisa_open_board (&b->ddk, i, port[i], -1, b->ddk.dma)) {
			printk (KERN_ERR "ct: Adapter #%d - initialization failed\n", i);
ballout:
			if (b->binder_data.id)
				cronyx_binder_unregister_item (b->binder_data.id);
			spin_lock_irqsave (&b->lock, flags);
			tauisa_close_board (&b->ddk);
			spin_unlock_irqrestore (&b->lock, flags);
			if (b->ddk.dma > 0)
				free_dma (b->ddk.dma);
			if (connected_irq > 0)
				free_irq (connected_irq, b);
			release_region (b->ddk.port, TAUISA_NPORT);
			for (j = 0; j < TAUISA_NCHAN; j++)
				if (b->buffers[j])
					kfree (b->buffers[j]);
			kfree (b);
			continue;
		}

		b->ddk.irq = irq[i];
		set_dma_mode (b->ddk.dma, DMA_MODE_CASCADE);
		if (b->ddk.irq > 0) {
			if (!cronyx_probe_irq (b->ddk.irq, b, (void (*)(void *, int)) tauisa_probe_irq))
				printk (KERN_ERR "ct: Adapter #%d - irq %d not functional\n", i, b->ddk.irq);
			tauisa_init_board (&b->ddk, b->ddk.num, b->ddk.port, b->ddk.irq, b->ddk.dma, b->ddk.type,
					   b->ddk.osc);
			tauisa_setup_board (&b->ddk, 0, 0, 0);
		} else {
			/*
			 * Find available IRQ.
			 */
			for (j = 0; tauisa_irqtab[j]; ++j) {
				if (!cronyx_probe_irq (tauisa_irqtab[j], b, (void (*)(void *, int)) tauisa_probe_irq))
					continue;
				b->ddk.irq = tauisa_irqtab[j];
				tauisa_init_board (&b->ddk, b->ddk.num, b->ddk.port, b->ddk.irq, b->ddk.dma,
						   b->ddk.type, b->ddk.osc);
				tauisa_setup_board (&b->ddk, 0, 0, 0);
				break;
			}
			if (b->ddk.irq <= 0) {
				printk (KERN_ERR "ct: Adapter #%d - no free irq for adapter at port 0x%x\n", i, port[i]);
				goto ballout;
			}
		}

		if (request_irq (b->ddk.irq, irq_handler, IRQF_DISABLED, "Cronyx Tau-ISA", b) != 0) {
			printk (KERN_ERR "ct: Adapter #%d - cannot get irq %d\n", i, b->ddk.irq);
			b->ddk.irq = 0;
			goto ballout;
		}
		connected_irq = b->ddk.irq;
		printk (KERN_INFO "ct: Adapter #%d <Cronyx %s> at 0x%03x irq %d dma %d\n", i, b->ddk.name, b->ddk.port,
			b->ddk.irq, b->ddk.dma);

		for (c = b->ddk.chan; c < b->ddk.chan + TAUISA_NCHAN; c++) {
			if (c->type == TAUISA_NONE)
				continue;
			b->buffers[c->num] = kmalloc (sizeof (tauisa_buf_t), GFP_KERNEL | GFP_DMA);
			if (b->buffers[c->num] == NULL) {
				printk (KERN_ERR "ct: Adapter #%d, channel #%d - out of memory\n", i, c->num);
				goto ballout;
			}
			if (isa_virt_to_bus (b->buffers[c->num]) >= 16ul * 1024 * 1024 - sizeof (tauisa_buf_t)) {
				printk (KERN_ERR "ct: Adapter #%d, channel #%d - out of dma-memory\n", i, c->num);
				goto ballout;
			}
		}

		enable_dma (b->ddk.dma);
		b->binder_data.dispatch = dispatch_tab;
		b->binder_data.type = CRONYX_ITEM_ADAPTER;
		b->binder_data.hw = b;
		b->binder_data.provider = THIS_MODULE;
		if (cronyx_binder_register_item (0, "tauisa", i, NULL, -1, &b->binder_data) < 0) {
			printk (KERN_ERR "ct: Adapter #%d - unable register on binder\n", i);
			goto ballout;
		}
		cronyx_led_init (&b->led_flasher, &b->lock, b, tauisa_led_set, tauisa_led_state);
		adapters[ndev++] = b;

		for (c = b->ddk.chan; c < b->ddk.chan + TAUISA_NCHAN; c++) {
			cronyx_binder_item_t *item;

			if (c->type == TAUISA_NONE)
				continue;

			item = &b->interfaces[c->num];
			item->dispatch = dispatch_tab;
			item->hw = c;
			c->sys = item;
			item->type = CRONYX_ITEM_INTERFACE;
			item->order = c->num;
			if (cronyx_binder_register_item
			    (b->binder_data.id, tauisa_iface_name (c->type), c->num, NULL, -1, item) < 0) {
				printk (KERN_ERR "ct: Adapter #%d - unable register interface #%d on binder\n", i, c->num);
			}

			item = &b->channels[c->num];
			item->dispatch = dispatch_tab;
			item->dispatch.second_timer = tauisa_second_timer;
			item->hw = c;
			c->sys = item;
			item->type = CRONYX_ITEM_CHANNEL;
			item->order = c->num;
			item->mtu = TAUISA_DMABUFSZ;
			item->fifosz = TAUISA_DMABUFSZ;
			if (cronyx_binder_register_item
			    (b->binder_data.id, NULL, c->num, "ct", b->ddk.num * TAUISA_NCHAN + c->num, item) < 0) {
				printk (KERN_ERR "ct: Adapter #%d - unable register channel #%d on binder\n", i, c->num);
			} else {
				spin_lock_irqsave (&b->lock, flags);
				tauisa_start_chan (c, b->buffers[c->num], isa_virt_to_bus (b->buffers[c->num]));
				tauisa_enable_receive (c, 0);
				tauisa_enable_transmit (c, 0);
				tauisa_register_transmit (c, &irq_transmit);
				tauisa_register_receive (c, &irq_receive);
				tauisa_register_error (c, &irq_error);
				tauisa_register_modem (c, &irq_modem);
				spin_unlock_irqrestore (&b->lock, flags);
			}
		}
	}
	if (!ndev)
		printk (KERN_ERR "ct: no %s <Cronyx Tau-ISA> adapters are available\n", found ? "usable" : "any");
	return 0;
}

void cleanup_module (void)
{
	tauisa_adapter_t *b;
	int i, j;
	unsigned long flags;

	for (i = 0; i < TAUISA_NBRD; i++) {
		b = adapters[i];
		if (b != NULL) {
			adapters[i] = NULL;
			cronyx_binder_unregister_item (b->binder_data.id);
			cronyx_led_destroy (&b->led_flasher);
			spin_lock_irqsave (&b->lock, flags);
			tauisa_close_board (&b->ddk);
			spin_unlock_irqrestore (&b->lock, flags);
			free_irq (b->ddk.irq, b);
			free_dma (b->ddk.dma);
			for (j = 0; j < TAUISA_NCHAN; ++j)
				if (b->buffers[j])
					kfree (b->buffers[j]);
			release_region (b->ddk.port, TAUISA_NPORT);
			kfree (b);
		}
	}
}

#if 0
int tauisa_match(struct device *dev, unsigned int num)
{
	
}

int tauisa_probe(struct device *dev, unsigned int num)
{
	
}

int tauisa_remove(struct device *dev, unsigned int num)
{
	
}

static struct isa_driver tauisa_driver = {
	.driver = {
		.name = "Cronyx Tau-ISA"
	},
	.probe = tauisa_probe,
	.remove = __devexit_p (tauisa_remove),
	.match = tauisa_match,
/*		void (*shutdown)(struct device *, unsigned int);
	int (*suspend)(struct device *, unsigned int, pm_message_t);
	int (*resume)(struct device *, unsigned int);*/
};
#endif
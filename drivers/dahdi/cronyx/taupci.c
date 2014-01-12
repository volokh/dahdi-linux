/*
 * Cronyx Tau-PCI adapter driver for Linux.
 *
 * This source was derived from:
 * Cronyx-Tau adapter driver for Linux.
 * by Dmitry Gorodchanin <begemot@bgm.rosprint.net> (C) 1997
 * It was rewritten using Cronyx-Tau-PCI DDK (Driver Developer's Toolkit)
 *
 * Copyright (C) 1999-2002 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *
 * Copyright (C) 2003-2005 Cronyx Engineering.
 * Author: Roman Kurakin <rik@cronyx.ru>
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
 * $Id: taupci.c,v 1.81 2009-09-18 12:19:47 ly Exp $
 */
#include "module.h"
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/string.h>
#include "cserial.h"

#ifndef CONFIG_PCI
#	error "Cronyx Tau-PCI module required PCI-bus support."
#endif

#ifndef DMA_32BIT_MASK
#	define DMA_32BIT_MASK 0x00000000FFFFFFFFull
#endif

#include "taupci-ddk.h"

/* Module information */
MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx driver for Tau-PCI adapters serie\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

typedef struct _taupci_adapter_t {
	taupci_board_t ddk;
	struct list_head entry;
	int irq_apic, irq_raw;
	dma_addr_t mem;
	spinlock_t lock;
	taupci_buf_t *txb[TAUPCI_NCHAN], *rxb[TAUPCI_NCHAN];
	dma_addr_t rxb_dma[TAUPCI_NCHAN], txb_dma[TAUPCI_NCHAN];
	taupci_iq_t *iq;
	dma_addr_t iq_dma;
	struct pci_dev *pdev;
	cronyx_binder_item_t channels[TAUPCI_NCHAN];
	cronyx_binder_item_t interfaces[TAUPCI_NCHAN];
	cronyx_binder_item_t binder_data;
	struct cronyx_led_flasher_t led_flasher;
} taupci_adapter_t;

static LIST_HEAD (adapters_list);
static struct timer_list taupci_timer;
static int dev_counter = -1, chan_counter;
static DEFINE_SPINLOCK (taupci_lock_driver);

#if LINUX_VERSION_CODE >= 0x020613
static irqreturn_t taupci_isr (int irq, void *dev);
#elif LINUX_VERSION_CODE >= 0x020600
static irqreturn_t taupci_isr (int irq, void *dev, struct pt_regs *regs);
#else
static void taupci_isr (int irq, void *dev, struct pt_regs *regs);
#endif

static void taupci_reset_buggy_infineon20534 (taupci_adapter_t * b)
{
	u32 stacmd, clsiz, base1, base2, intlin;

	pci_read_config_dword (b->pdev, PCI_COMMAND, &stacmd);
	pci_read_config_dword (b->pdev, PCI_CACHE_LINE_SIZE, &clsiz);
	pci_read_config_dword (b->pdev, PCI_BASE_ADDRESS_0, &base1);
	pci_read_config_dword (b->pdev, PCI_BASE_ADDRESS_1, &base2);
	pci_read_config_dword (b->pdev, PCI_INTERRUPT_LINE, &intlin);

	taupci_hard_reset (&b->ddk);
	udelay (12);

	pci_write_config_dword (b->pdev, PCI_COMMAND, stacmd);
	pci_write_config_dword (b->pdev, PCI_CACHE_LINE_SIZE, clsiz);
	pci_write_config_dword (b->pdev, PCI_BASE_ADDRESS_0, base1);
	pci_write_config_dword (b->pdev, PCI_BASE_ADDRESS_1, base2);
	pci_write_config_dword (b->pdev, PCI_INTERRUPT_LINE, intlin);
}

#if 0
static void taupci_reset (taupci_adapter_t * b, int hard)
{
	unsigned long flags;

	spin_lock (&taupci_lock_driver);

	spin_lock_irqsave (&b->lock, flags);
	taupci_enable_interrupt (&b->ddk, 0);
	b->ddk.dead = -1;
	spin_unlock_irqrestore (&b->lock, flags);

	spin_unlock (&taupci_lock_driver);

	taupci_halt (&b->ddk);
	if (b->irq_apic) {
		free_irq (b->pdev->irq, b);
		b->irq_apic = 0;
	}

	taupci_reset_buggy_infineon20534 (b);
	if (taupci_init (&b->ddk, b->ddk.num, b->ddk.base, b->iq, b->iq_dma))
		printk (KERN_ERR "cp: Adapter #%d - hardware broken, code: 0x%08x\n", b->binder_data.order, b->ddk.dead);
	else if (request_irq (b->pdev->irq, taupci_isr, IRQF_SHARED, "Cronyx Tau-PCI", b)) {
		printk (KERN_ERR "cp: Adapter #%d - can't get irq %s\n",
			b->binder_data.order, cronyx_format_irq (b->irq_raw, b->pdev->irq));
	} else {
		b->irq_apic = b->pdev->irq;
		spin_lock_irqsave (&b->lock, flags);
		taupci_enable_interrupt (&b->ddk, 1);
		spin_unlock_irqrestore (&b->lock, flags);
	}
}
#endif

static int taupci_ctl_get (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	int n, error;
	taupci_chan_t *c, *i;
	taupci_adapter_t *b;
	unsigned long flags;

	if (item->hw == NULL)
		return -ENODEV;

	i = NULL;
	c = NULL;
	if (item->type == CRONYX_ITEM_ADAPTER) {
		b = (taupci_adapter_t *) item->hw;
	} else if (item->type == CRONYX_ITEM_INTERFACE) {
		i = (taupci_chan_t *) item->hw;
		b = (taupci_adapter_t *) i->board;
	} else {
		c = (taupci_chan_t *) item->hw;
		b = (taupci_adapter_t *) c->board;
		if (ctl->param_id & cronyx_flag_channel2link) {
			i = c;
			if (b->ddk.mux)
				i = &b->ddk.chan[c->dir];
		}
	}

	if (b->ddk.dead || b->ddk.model == 0)
		return -ENODEV;

	error = 0;
	switch (ctl->param_id) {
		case cronyx_channel_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: get-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (! c->phony)
				ctl->u.param.value = CRONYX_MODE_HDLC;
			else
				ctl->u.param.value = (! c->unframed && c->voice) ? CRONYX_MODE_VOICE : CRONYX_MODE_PHONY;
			break;

		case cronyx_loop_mode:
		case cronyx_loop_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: get-loop\n");
			if (NULL == i)
				error = -EINVAL;
			else {
				ctl->u.param.value = CRONYX_LOOP_NONE;
				if (i->lloop)
					ctl->u.param.value = CRONYX_LOOP_INTERNAL;
				if (i->type == TAUPCI_G703 || i->type == TAUPCI_E3
				    || i->type == TAUPCI_T3 || i->type == TAUPCI_STS1) {
					spin_lock_irqsave (&b->lock, flags);
					if (taupci_get_rloop (i))
						ctl->u.param.value = CRONYX_LOOP_REMOTE;
					spin_unlock_irqrestore (&b->lock, flags);
				}
			}
			break;

		case cronyx_line_code:
		case cronyx_line_code | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: get-line\n");
			if (NULL == i)
				error = -EINVAL;
			else {
				if (i->type == TAUPCI_SERIAL)
					ctl->u.param.value = i->nrzi ? CRONYX_NRZI : CRONYX_NRZ;
				else if (i->type == TAUPCI_G703 || i->type == TAUPCI_E1)
					ctl->u.param.value = CRONYX_HDB3;
				else
					error = -EINVAL;
			}
			break;

		case cronyx_adapter_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: get-adapter\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else if (b->ddk.model != TAUPCI_MODEL_OE1 && b->ddk.model != TAUPCI_MODEL_2E1_4
				 && b->ddk.model != TAUPCI_MODEL_4E1)
				error = -EINVAL;
			else
				ctl->u.param.value = b->ddk.mux ? CRONYX_MODE_MUX : CRONYX_MODE_SEPARATE;
			break;

		case cronyx_cas_mode:
		case cronyx_cas_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: get-cas\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1)
				error = -EINVAL;
			else
				switch (i->cas) {
					default:
					case TAUPCI_CAS_OFF:
						ctl->u.param.value = CRONYX_CASMODE_OFF;
						break;
					case TAUPCI_CAS_SET:
						ctl->u.param.value = CRONYX_CASMODE_SET;
						break;
					case TAUPCI_CAS_PASS:
						ctl->u.param.value = CRONYX_CASMODE_PASS;
						break;
					case TAUPCI_CAS_CROSS:
						ctl->u.param.value = CRONYX_CASMODE_CROSS;
						break;
				}
			break;

		case cronyx_invclk_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: get-invclk\n");
			if (NULL == i || i->type != TAUPCI_SERIAL)
				error = -EINVAL;
			else {
				ctl->u.param.value = CRONYX_ICLK_NORMAL;
				if (i->invtxc)
					ctl->u.param.value = CRONYX_ICLK_TX;
				if (i->invrxc)
					ctl->u.param.value = CRONYX_ICLK_RX;
				if (i->invtxc && i->invrxc)
					ctl->u.param.value = CRONYX_ICLK;
			}
			break;

		case cronyx_port_or_cable_type:
			CRONYX_LOG_1 (item, "taupci.ctl: get-cable-type\n");
			if (NULL == i)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (taupci_get_cable (i)) {
					case TAUPCI_CABLE_RS232:
						ctl->u.param.value = CRONYX_RS232;
						break;
					case TAUPCI_CABLE_V35:
						ctl->u.param.value = CRONYX_V35;
						break;
					case TAUPCI_CABLE_RS530:
						ctl->u.param.value = CRONYX_RS530;
						break;
					case TAUPCI_CABLE_X21:
						ctl->u.param.value = CRONYX_X21;
						break;
					case TAUPCI_CABLE_RS485:
						ctl->u.param.value = CRONYX_RS485;
						break;
					case TAUPCI_CABLE_NOT_ATTACHED:
						ctl->u.param.value = CRONYX_NONE;
						break;
					case TAUPCI_CABLE_COAX:
						ctl->u.param.value = CRONYX_COAX;
						break;
					case TAUPCI_CABLE_TP:
						ctl->u.param.value = CRONYX_TP;
						break;
					default:
						ctl->u.param.value = CRONYX_UNKNOWN;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_sync_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: get-clock\n");
			if (NULL == i || i->type < TAUPCI_G703)
				error = -EINVAL;
			else {
				switch (i->gsyn) {
					default:
						ctl->u.param.value = CRONYX_E1CLK_INTERNAL;
						break;
					case TAUPCI_GSYN_RCV:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE;
						break;
					case TAUPCI_GSYN_RCV0:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE_CHAN0;
						break;
					case TAUPCI_GSYN_RCV1:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE_CHAN1;
						break;
					case TAUPCI_GSYN_RCV2:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE_CHAN2;
						break;
					case TAUPCI_GSYN_RCV3:
						ctl->u.param.value = CRONYX_E1CLK_RECEIVE_CHAN3;
						break;
				}
			}
			break;

		case cronyx_timeslots_use:
			CRONYX_LOG_1 (item, "taupci.ctl: get-timeslots\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if ((c->type != TAUPCI_E1 || c->unframed)
				 && c->type != TAUPCI_DATA)
				error = -EINVAL;
			else
				ctl->u.param.value = c->ts;
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (item, "taupci.ctl: get-baud\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				if ((c->type == TAUPCI_E1 || c->type == TAUPCI_DATA) && !c->unframed) {
					ctl->u.param.value = 0;
					for (n = 0; n < 32; n++)
						if (c->ts & (1ul << n))
							ctl->u.param.value += 64000;
				} else
					ctl->u.param.value = c->baud;
				if (ctl->u.param.value == 0) {
					if (c->type == TAUPCI_G703)
						ctl->u.param.value = 2048000;
					else if (c->type != TAUPCI_SERIAL)
						error = -EINVAL;
				}
			}
			break;

		case cronyx_iface_bind:
			CRONYX_LOG_1 (item, "taupci.ctl: get-iface-bind\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->type >= TAUPCI_E3)
				error = -EINVAL;
			else
				ctl->u.param.value = c->dir;
			break;

		case cronyx_crc4:
		case cronyx_crc4 | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: get-crc4\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1 || i->unframed)
				error = -EINVAL;
			else
				ctl->u.param.value = i->crc4;
			break;

		case cronyx_dpll:
			CRONYX_LOG_1 (item, "taupci.ctl: get-dpll\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_SERIAL)
				error = -EINVAL;
			else
				ctl->u.param.value = i->dpll;
			break;

		case cronyx_higain:
		case cronyx_higain | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: get-higain\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1)
				error = -EINVAL;
			else
				ctl->u.param.value = i->higain;
			break;

		case cronyx_unframed:
			CRONYX_LOG_1 (item, "taupci.ctl: get-unframed\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type == TAUPCI_G703)
				ctl->u.param.value = 1;
			else if (i->type != TAUPCI_E1)
				error = -EINVAL;
			else
				ctl->u.param.value = i->unframed;
			break;

		case cronyx_monitor:
			CRONYX_LOG_1 (item, "taupci.ctl: get-monitor\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1 && i->type != TAUPCI_E3 && i->type != TAUPCI_T3 &&
				 i->type != TAUPCI_STS1)
				error = -EINVAL;
			else
				ctl->u.param.value = i->monitor;
			break;

		case cronyx_scrambler:
			CRONYX_LOG_1 (item, "taupci.ctl: get-scrambler\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_G703 && !i->unframed)
				error = -EINVAL;
			else
				ctl->u.param.value = i->scrambler;
			break;

		case cronyx_t3_long:
			CRONYX_LOG_1 (item, "taupci.ctl: get-t3-long\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_T3 && i->type != TAUPCI_STS1)
				error = -EINVAL;
			else
				ctl->u.param.value = i->t3_long;
			break;

		case cronyx_inlevel_sdb:
			CRONYX_LOG_1 (item, "taupci.ctl: get-inlevel\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = taupci_get_lq (i);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_mtu:
			CRONYX_LOG_1 (item, "taupci.ctl: get-mtu\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else
				ctl->u.param.value = c->mios;
			break;

		case cronyx_qlen:
			CRONYX_LOG_1 (item, "taupci.ctl: get-qlen\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else
				ctl->u.param.value = c->qlen;
			break;

			//case cronyx_dxc:
			//      break;

		case cronyx_stat_channel:
			CRONYX_LOG_1 (item, "taupci.ctl: get-stat-channel\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_channel.rintr = c->rintr;
				ctl->u.stat_channel.tintr = c->tintr;
				ctl->u.stat_channel.ibytes = c->ibytes;
				ctl->u.stat_channel.obytes = c->obytes;
				ctl->u.stat_channel.ipkts = c->ipkts;
				ctl->u.stat_channel.opkts = c->opkts;
				ctl->u.stat_channel.ierrs = c->overrun + c->frame + c->crc;
				ctl->u.stat_channel.oerrs = c->underrun;
				ctl->u.stat_channel.mintr = 0;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_stat_e1:
		case cronyx_stat_e1 | cronyx_flag_channel2link:
			CRONYX_LOG_3 (item, "taupci.ctl: get-stat-e1\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1 && i->type != TAUPCI_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_e1.status = 0;
				if (!i->lloop) {
					if (i->status & TAUPCI_ESTS_LOS)
						ctl->u.stat_e1.status |= CRONYX_E1_LOS;
					if (i->status & TAUPCI_ESTS_LOF)
						ctl->u.stat_e1.status |= CRONYX_E1_LOF;
					if (i->status & TAUPCI_ESTS_AIS)
						ctl->u.stat_e1.status |= CRONYX_E1_AIS;
					if (i->status & TAUPCI_ESTS_LOMF)
						ctl->u.stat_e1.status |= CRONYX_E1_LOMF;
					if (i->status & TAUPCI_ESTS_AIS16)
						ctl->u.stat_e1.status |= CRONYX_E1_AIS16;
					if (i->status & TAUPCI_ESTS_RA)
						ctl->u.stat_e1.status |= CRONYX_E1_RA;
					if (i->status & TAUPCI_ESTS_RDMA)
						ctl->u.stat_e1.status |= CRONYX_E1_RDMA;
					if (i->status & TAUPCI_ESTS_TSTREQ)
						ctl->u.stat_e1.status |= CRONYX_E1_TSTREQ;
					if (i->status & TAUPCI_ESTS_TSTERR)
						ctl->u.stat_e1.status |= CRONYX_E1_TSTERR;
				}

				ctl->u.stat_e1.cursec = i->cursec;
				ctl->u.stat_e1.totsec = i->totsec + i->cursec;

				ctl->u.stat_e1.currnt.bpv = i->currnt.bpv;
				ctl->u.stat_e1.currnt.fse = i->currnt.fse;
				ctl->u.stat_e1.currnt.crce = i->currnt.crce;
				ctl->u.stat_e1.currnt.rcrce = i->currnt.rcrce;
				ctl->u.stat_e1.currnt.uas = i->currnt.uas;
				ctl->u.stat_e1.currnt.les = i->currnt.les;
				ctl->u.stat_e1.currnt.es = i->currnt.es;
				ctl->u.stat_e1.currnt.bes = i->currnt.bes;
				ctl->u.stat_e1.currnt.ses = i->currnt.ses;
				ctl->u.stat_e1.currnt.oofs = i->currnt.oofs;
				ctl->u.stat_e1.currnt.css = i->currnt.css;
				ctl->u.stat_e1.currnt.dm = i->currnt.dm;

				ctl->u.stat_e1.total.bpv = i->total.bpv + i->currnt.bpv;
				ctl->u.stat_e1.total.fse = i->total.fse + i->currnt.fse;
				ctl->u.stat_e1.total.crce = i->total.crce + i->currnt.crce;
				ctl->u.stat_e1.total.rcrce = i->total.rcrce + i->currnt.rcrce;
				ctl->u.stat_e1.total.uas = i->total.uas + i->currnt.uas;
				ctl->u.stat_e1.total.les = i->total.les + i->currnt.les;
				ctl->u.stat_e1.total.es = i->total.es + i->currnt.es;
				ctl->u.stat_e1.total.bes = i->total.bes + i->currnt.bes;
				ctl->u.stat_e1.total.ses = i->total.ses + i->currnt.ses;
				ctl->u.stat_e1.total.oofs = i->total.oofs + i->currnt.oofs;
				ctl->u.stat_e1.total.css = i->total.css + i->currnt.css;
				ctl->u.stat_e1.total.dm = i->total.dm + i->currnt.dm;
				for (n = 0; n < 48; ++n) {
					ctl->u.stat_e1.interval[n].bpv = i->interval[n].bpv;
					ctl->u.stat_e1.interval[n].fse = i->interval[n].fse;
					ctl->u.stat_e1.interval[n].crce = i->interval[n].crce;
					ctl->u.stat_e1.interval[n].rcrce = i->interval[n].rcrce;
					ctl->u.stat_e1.interval[n].uas = i->interval[n].uas;
					ctl->u.stat_e1.interval[n].les = i->interval[n].les;
					ctl->u.stat_e1.interval[n].es = i->interval[n].es;
					ctl->u.stat_e1.interval[n].bes = i->interval[n].bes;
					ctl->u.stat_e1.interval[n].ses = i->interval[n].ses;
					ctl->u.stat_e1.interval[n].oofs = i->interval[n].oofs;
					ctl->u.stat_e1.interval[n].css = i->interval[n].css;
					ctl->u.stat_e1.interval[n].dm = i->interval[n].dm;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_stat_e3:
			CRONYX_LOG_1 (item, "taupci.ctl: get-stat-e3\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type == TAUPCI_NONE || i->board->model != TAUPCI_MODEL_1E3)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_e3.status = 0;
				if (i->status & TAUPCI_ESTS_LOS)
					ctl->u.stat_e3.status |= CRONYX_E3_LOS;
				if (i->status & TAUPCI_ESTS_TXE)
					ctl->u.stat_e3.status |= CRONYX_E3_TXE;

				ctl->u.stat_e3.cursec = (i->e3csec_5 + i->e3csec_5 + 1) / 10;
				ctl->u.stat_e3.totsec = i->e3tsec + ctl->u.stat_e3.cursec;

				ctl->u.stat_e3.ccv = i->e3ccv;
				ctl->u.stat_e3.tcv = i->e3tcv + i->e3ccv;
				for (n = 0; n < 48; ++n)
					ctl->u.stat_e3.icv[n] = i->e3icv[n];
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: get-led\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_GET, ctl);
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: get-crc-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->phony)
				error = -EINVAL;
			else if (!c->hdlc_crclen)
				ctl->u.param.value = CRONYX_CRC_NONE;
			else if (c->hdlc_crclen & 2)
				ctl->u.param.value = CRONYX_CRC_32;
			else
				ctl->u.param.value = CRONYX_CRC_16;
			break;

		case cronyx_hdlc_flags:
			CRONYX_LOG_1 (item, "taupci.ctl: get-hdlc-flags\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->phony)
				error = -EINVAL;
			else if (c->hdlc_shareflags)
				ctl->u.param.value = CRONYX_HDLC_SHARE;
			else
				ctl->u.param.value = CRONYX_HDLC_2FLAGS;
			break;

		default:
			CRONYX_LOG_2 (item, "taupci.ctl: get-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}
	return error;
}

static int taupci_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	int n, error;
	taupci_chan_t *c, *i;
	taupci_adapter_t *b;
	unsigned long flags;

	if (item->hw == NULL)
		return -ENODEV;

	i = NULL;
	c = NULL;
	if (item->type == CRONYX_ITEM_ADAPTER) {
		b = (taupci_adapter_t *) item->hw;
	} else if (item->type == CRONYX_ITEM_INTERFACE) {
		i = (taupci_chan_t *) item->hw;
		b = (taupci_adapter_t *) i->board;
	} else {
		c = (taupci_chan_t *) item->hw;
		b = (taupci_adapter_t *) c->board;
		if (ctl->param_id & cronyx_flag_channel2link) {
			i = c;
			if (b->ddk.mux)
				i = &b->ddk.chan[c->dir];
		}
	}

	if (b->ddk.dead || b->ddk.model == 0)
		return -ENODEV;

	error = 0;
	switch (ctl->param_id) {
		case cronyx_channel_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: set-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_PHONY && ctl->u.param.value != CRONYX_MODE_VOICE
			&& ctl->u.param.value != CRONYX_MODE_HDLC)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_HDLC && (c->type != TAUPCI_E1 || c->unframed))
				error = -EINVAL;
			else if (ctl->u.param.value == CRONYX_MODE_VOICE && c->unframed)
				error = -EINVAL;
			else if (item->proto && item->proto->chanmode_lock)
				error = -EBUSY;
			else {
				spin_lock_irqsave (&b->lock, flags);
				c->voice = ctl->u.param.value == CRONYX_MODE_VOICE;
				taupci_set_phony (c, ctl->u.param.value == CRONYX_MODE_PHONY || ctl->u.param.value == CRONYX_MODE_VOICE);
				if (c->phony != (ctl->u.param.value == CRONYX_MODE_PHONY || ctl->u.param.value == CRONYX_MODE_VOICE))
					error = -EINVAL;
				else {
					item->mtu = c->mios;
					item->fifosz = c->qlen * item->mtu;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_loop_mode:
		case cronyx_loop_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: set-loop\n");
			if (NULL == i)
				error = -EINVAL;
			else
				switch (ctl->u.param.value) {
					case CRONYX_LOOP_NONE:
						spin_lock_irqsave (&b->lock, flags);
						taupci_set_lloop (i, 0);
						taupci_set_rloop (i, 0);
						spin_unlock_irqrestore (&b->lock, flags);
						break;

					case CRONYX_LOOP_INTERNAL:
						spin_lock_irqsave (&b->lock, flags);
						taupci_set_lloop (i, 1);
						taupci_set_rloop (i, 0);
						spin_unlock_irqrestore (&b->lock, flags);
						break;

					case CRONYX_LOOP_REMOTE:
						if (i->type == TAUPCI_G703
						    || i->type == TAUPCI_E3 || i->type == TAUPCI_T3 ||
						    i->type == TAUPCI_STS1) {
							spin_lock_irqsave (&b->lock, flags);
							taupci_set_lloop (i, 0);
							taupci_set_rloop (i, 1);
							spin_unlock_irqrestore (&b->lock, flags);
							break;
						}
						/*
						 * LY: no break here
						 */

					default:
						error = -EINVAL;
				}
			break;

		case cronyx_line_code:
		case cronyx_line_code | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: set-line\n");
			if (NULL == i)
				error = -EINVAL;
			else
				switch (ctl->u.param.value) {
					case CRONYX_NRZI:
					case CRONYX_NRZ:
						if (i->type == TAUPCI_SERIAL) {
							spin_lock_irqsave (&b->lock, flags);
							taupci_set_nrzi (i, ctl->u.param.value == CRONYX_NRZI);
							spin_unlock_irqrestore (&b->lock, flags);
						} else
							error = -EINVAL;
						break;

					case CRONYX_HDB3:
						if (i->type == TAUPCI_G703 || i->type == TAUPCI_E1)
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

		case cronyx_adapter_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: set-adapter\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else if (b->ddk.model != TAUPCI_MODEL_OE1 && b->ddk.model != TAUPCI_MODEL_2E1_4
				 && b->ddk.model != TAUPCI_MODEL_4E1)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_SEPARATE && ctl->u.param.value != CRONYX_MODE_MUX)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_mux (&b->ddk, ctl->u.param.value == CRONYX_MODE_MUX);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_cas_mode:
		case cronyx_cas_mode | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: set-cas\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_CASMODE_OFF:
						taupci_set_cas (i, TAUPCI_CAS_OFF);
						break;
					case CRONYX_CASMODE_SET:
						taupci_set_cas (i, TAUPCI_CAS_SET);
						break;
					case CRONYX_CASMODE_PASS:
						taupci_set_cas (i, TAUPCI_CAS_PASS);
						break;
					case CRONYX_CASMODE_CROSS:
						taupci_set_cas (i, TAUPCI_CAS_CROSS);
						break;
					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_invclk_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: set-invclk\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_SERIAL)
				error = -EINVAL;
			else
				switch (ctl->u.param.value) {
					case CRONYX_ICLK_NORMAL:
						spin_lock_irqsave (&b->lock, flags);
						taupci_set_invtxc (i, 0);
						taupci_set_invrxc (i, 0);
						spin_unlock_irqrestore (&b->lock, flags);
						break;
					case CRONYX_ICLK_RX:
						spin_lock_irqsave (&b->lock, flags);
						taupci_set_invtxc (i, 0);
						taupci_set_invrxc (i, 1);
						spin_unlock_irqrestore (&b->lock, flags);
						break;
					case CRONYX_ICLK_TX:
						spin_lock_irqsave (&b->lock, flags);
						taupci_set_invtxc (i, 1);
						taupci_set_invrxc (i, 0);
						spin_unlock_irqrestore (&b->lock, flags);
						break;
					case CRONYX_ICLK:
						spin_lock_irqsave (&b->lock, flags);
						taupci_set_invtxc (i, 1);
						taupci_set_invrxc (i, 1);
						spin_unlock_irqrestore (&b->lock, flags);
						break;
					default:
						error = -EINVAL;
				}
			break;

		case cronyx_sync_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: set-clock\n");
			if (NULL == i || i->type < TAUPCI_G703)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					default:
						error = -EINVAL;
						/*
						 * LY: no break here
						 */
					case CRONYX_E1CLK_INTERNAL:
						taupci_set_gsyn (i, TAUPCI_GSYN_INT);
						break;
					case CRONYX_E1CLK_RECEIVE:
						taupci_set_gsyn (i, TAUPCI_GSYN_RCV);
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN0:
						if (b->ddk.chan[0].type != TAUPCI_G703 &&
						    b->ddk.chan[0].type != TAUPCI_E1)
							error = -EINVAL;
						else
							taupci_set_gsyn (i, TAUPCI_GSYN_RCV0);
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN1:
						if (b->ddk.chan[1].type != TAUPCI_G703 &&
						    b->ddk.chan[1].type != TAUPCI_E1)
							error = -EINVAL;
						else
							taupci_set_gsyn (i, TAUPCI_GSYN_RCV1);
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN2:
						if (b->ddk.chan[2].type != TAUPCI_G703 &&
						    b->ddk.chan[2].type != TAUPCI_E1)
							error = -EINVAL;
						else
							taupci_set_gsyn (i, TAUPCI_GSYN_RCV2);
						break;
					case CRONYX_E1CLK_RECEIVE_CHAN3:
						if (b->ddk.chan[3].type != TAUPCI_G703 &&
						    b->ddk.chan[3].type != TAUPCI_E1)
							error = -EINVAL;
						else
							taupci_set_gsyn (i, TAUPCI_GSYN_RCV3);
						break;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_timeslots_use:
			CRONYX_LOG_1 (item, "taupci.ctl: set-timeslots\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if ((c->type != TAUPCI_E1 && c->type != TAUPCI_DATA) || c->unframed)
				error = -EINVAL;
			else if (ctl->u.param.value & 1)
				error = -EINVAL;
			else if (item->proto && item->proto->timeslots_lock)
				error = -EBUSY;
			else {
				taupci_chan_t *x;
				spin_lock_irqsave (&b->lock, flags);
				for (x = b->ddk.chan; x < b->ddk.chan + TAUPCI_NCHAN; x++) {
					if (x != c
					&& (x->type == TAUPCI_E1 || x->type == TAUPCI_DATA)
					&& x->dir == c->dir && !x->unframed
					&& (x->ts & ctl->u.param.value) != 0) {
						cronyx_binder_item_t *h;
						h = x->sys;
						if (h->proto && (h->proto->timeslots_lock || h->running)) {
							error = -EBUSY;
							break;
						}
						taupci_set_ts (x, x->ts & ~ctl->u.param.value);
					}

				}
				if (! error) {
					taupci_set_ts (c, ctl->u.param.value);
					if (ctl->u.param.value != 0 && c->ts == 0)
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (item, "taupci.ctl: set-baud\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (c->type == TAUPCI_E1 && !c->unframed) {
				if (c->baud != ctl->u.param.value)
					error = -EINVAL;
			} else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_baud (c, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_iface_bind:
			CRONYX_LOG_1 (item, "taupci.ctl: set-iface-bind\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_dir (c, ctl->u.param.value);
				if (c->dir != ctl->u.param.value)
					error = -EINVAL;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_crc4:
		case cronyx_crc4 | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: set-crc4\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1 || i->unframed)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_crc4 (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_dpll:
			CRONYX_LOG_1 (item, "taupci.ctl: set-dpll\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_SERIAL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_dpll (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_higain:
		case cronyx_higain | cronyx_flag_channel2link:
			CRONYX_LOG_1 (item, "taupci.ctl: set-higain\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_higain (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_unframed:
			CRONYX_LOG_1 (item, "taupci.ctl: set-unframed\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_unfram (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_monitor:
			CRONYX_LOG_1 (item, "taupci.ctl: set-monitor\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_E1 && i->type != TAUPCI_E3 && i->type != TAUPCI_T3 &&
				 i->type != TAUPCI_STS1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_monitor (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_scrambler:
			CRONYX_LOG_1 (item, "taupci.ctl: set-scrambler\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_G703 && !i->unframed)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_scrambler (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_t3_long:
			CRONYX_LOG_1 (item, "taupci.ctl: set-t3-long\n");
			if (NULL == i)
				error = -EINVAL;
			else if (i->type != TAUPCI_T3 && i->type != TAUPCI_STS1)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				taupci_set_t3_long (i, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_mtu:
			CRONYX_LOG_1 (item, "taupci.ctl: set-mtu\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (item->proto && item->proto->mtu_lock)
				error = -EBUSY;
			else {
				if (taupci_set_mru (c, ctl->u.param.value) < 0)
					error = -EINVAL;
				item->mtu = c->mios;
				item->fifosz = c->qlen * item->mtu;
			}
			break;

		case cronyx_qlen:
			CRONYX_LOG_1 (item, "taupci.ctl: set-qlen\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (ctl->u.param.value < 2 || ctl->u.param.value > TAUPCI_NBUF)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				c->qlen = ctl->u.param.value;
				item->fifosz = c->qlen * item->mtu;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_clear_stat:
			CRONYX_LOG_1 (item, "taupci.ctl: clear-stat\n");
			spin_lock_irqsave (&b->lock, flags);
			switch (item->type) {
				default:
					error = -EINVAL;
				case CRONYX_ITEM_CHANNEL:
					c->rintr = 0;
					c->tintr = 0;
					c->ibytes = 0;
					c->obytes = 0;
					c->ipkts = 0;
					c->opkts = 0;
					c->overrun = 0;
					c->frame = 0;
					c->crc = 0;
					c->underrun = 0;
					break;
				case CRONYX_ITEM_INTERFACE:
					i->cursec = 0;
					i->totsec = 0;
					i->e3ccv = 0;
					i->e3tcv = 0;
					i->e3csec_5 = 0;
					i->e3tsec = 0;
					memset (&i->currnt, 0, sizeof (i->currnt));
					memset (&i->total, 0, sizeof (i->total));
					memset (&i->interval, 0, sizeof (i->interval));
					memset (&i->e3icv, 0, sizeof (i->e3icv));
					break;
				case CRONYX_ITEM_ADAPTER:
					for (n = 0; n < TAUPCI_NCHAN; n++) {
						c = (taupci_chan_t *) b->channels[n].hw;
						if (c) {
							c->rintr = 0;
							c->tintr = 0;
							c->ibytes = 0;
							c->obytes = 0;
							c->ipkts = 0;
							c->opkts = 0;
							c->overrun = 0;
							c->frame = 0;
							c->crc = 0;
							c->underrun = 0;
							c->cursec = 0;
							c->totsec = 0;
							c->e3ccv = 0;
							c->e3tcv = 0;
							c->e3csec_5 = 0;
							c->e3tsec = 0;
							memset (&c->currnt, 0, sizeof (c->currnt));
							memset (&c->total, 0, sizeof (c->total));
							memset (&c->interval, 0, sizeof (c->interval));
							memset (&c->e3icv, 0, sizeof (c->e3icv));
						}
					}
					break;
			}
			spin_unlock_irqrestore (&b->lock, flags);
			break;

		case cronyx_led_mode:
			CRONYX_LOG_2 (item, "taupci.ctl: set-led\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_SET, ctl);
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (item, "taupci.ctl: set-crc-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->phony)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_CRC_NONE:
						taupci_set_hdlcmode (c, 0, c->hdlc_shareflags);
						break;
					case CRONYX_CRC_16:
						taupci_set_hdlcmode (c, 16, c->hdlc_shareflags);
						break;
					case CRONYX_CRC_32:
						taupci_set_hdlcmode (c, 32, c->hdlc_shareflags);
						break;
					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_hdlc_flags:
			CRONYX_LOG_1 (item, "taupci.ctl: set-hdlc-flags\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->phony)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				switch (ctl->u.param.value) {
					case CRONYX_HDLC_2FLAGS:
						taupci_set_hdlcmode (c, c->hdlc_crclen << 4, 0);
						break;
					case CRONYX_HDLC_SHARE:
						taupci_set_hdlcmode (c, c->hdlc_crclen << 4, 1);
						break;
					default:
						error = -EINVAL;
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		default:
			return -ENOSYS;
	}

	return error;
}

static int taupci_up (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;
	int error;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return -ENODEV;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	CRONYX_LOG_1 (item, "taupci.up\n");
#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	if (!try_module_get (THIS_MODULE))
		return -EAGAIN;
#endif
	spin_lock_irqsave (&b->lock, flags);

	if (item->running) {
		spin_unlock_irqrestore (&b->lock, flags);
#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put (THIS_MODULE);
#endif
		return 0;
	}

	error = -ENODEV;
	if (b->ddk.dead == 0 && b->ddk.model) {
		taupci_start_chan (c, b->txb[c->num], b->txb_dma[c->num], b->rxb[c->num], b->rxb_dma[c->num]);
		/*
		 * Raise DTR and RTS
		 */
		taupci_set_dtr (c, 1);
		taupci_set_rts (c, 1);
		item->running = 1;
		error = 0;
	}
	spin_unlock_irqrestore (&b->lock, flags);

	if (unlikely (error)) {
#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put (THIS_MODULE);
#endif
	}
	return error;
}

static void taupci_down (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	CRONYX_LOG_1 (item, "taupci.down\n");
	spin_lock_irqsave (&b->lock, flags);
	if (item->running) {
		item->running = 0;
		if (b->ddk.dead == 0 && b->ddk.model) {
			/*
			 * Clear DTR and RTS
			 */
			taupci_set_rts (c, 0);
			taupci_set_dtr (c, 0);
			taupci_stop_chan (c);

			/* LY: (mad) Infineon PEB20534 need a couple of TXC ticks to change modem-signals. */
			spin_unlock_irqrestore (&b->lock, flags);
			set_current_state (TASK_UNINTERRUPTIBLE);
			schedule_timeout (HZ / 100);
			spin_lock_irqsave (&b->lock, flags);

			taupci_poweroff_chan (c);
		}
		spin_unlock_irqrestore (&b->lock, flags);

#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put (THIS_MODULE);
#endif
	} else
		spin_unlock_irqrestore (&b->lock, flags);
}

static int taupci_transmit (cronyx_binder_item_t * item, struct sk_buff *skb)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;
	int error;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return -ENODEV;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	spin_lock_irqsave (&b->lock, flags);
	if (b->ddk.dead != 0) {
		spin_unlock_irqrestore (&b->lock, flags);
		return -EIO;	/* error */
	}

	switch (error = taupci_send_packet (c, skb->data, skb->len, NULL)) {
		case 0:
			spin_unlock_irqrestore (&b->lock, flags);
			CRONYX_LOG_2 (item, "cp: send %d bytes, tn:%d, te:%d\n", skb->len, c->tn, c->te);
			return skb->len;	/* success */
		case TAUPCI_SEND_NOSPACE:
			spin_unlock_irqrestore (&b->lock, flags);
			return 0;	/* no free buffers */
		default:
			spin_unlock_irqrestore (&b->lock, flags);
			CRONYX_LOG_1 (item, "cp: send error #%d\n", error);
			return -EIO;	/* error */
	}
}

static void taupci_dtr (cronyx_binder_item_t * item, int val)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	spin_lock_irqsave (&b->lock, flags);
	if (b->ddk.dead == 0)
		taupci_set_dtr (c, val);
	spin_unlock_irqrestore (&b->lock, flags);
}

static void taupci_rts (cronyx_binder_item_t * item, int val)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	spin_lock_irqsave (&b->lock, flags);
	if (b->ddk.dead == 0)
		taupci_set_rts (c, val);
	spin_unlock_irqrestore (&b->lock, flags);
}

static int taupci_query_dtr (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return 0;

	c = item->hw;
	return c->dtr;
}

static int taupci_query_rts (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return 0;

	c = item->hw;
	return c->dtr;
}

static int taupci_query_dsr (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;
	int val;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return 0;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	spin_lock_irqsave (&b->lock, flags);
	val = 0;
	if (b->ddk.dead == 0)
		val = taupci_get_dsr (c);
	spin_unlock_irqrestore (&b->lock, flags);
	return val;
}

static int taupci_query_cts (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;
	int val;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return 0;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	spin_lock_irqsave (&b->lock, flags);
	val = 0;
	if (b->ddk.dead == 0)
		val = taupci_get_cts (c);
	spin_unlock_irqrestore (&b->lock, flags);
	return val;
}

static int taupci_query_dcd (cronyx_binder_item_t * item)
{
	taupci_chan_t *c;
	taupci_adapter_t *b;
	unsigned long flags;
	int val;

	if (item->type != CRONYX_ITEM_CHANNEL || item->hw == NULL)
		return 0;

	c = item->hw;
	b = (taupci_adapter_t *) c->board;

	spin_lock_irqsave (&b->lock, flags);
	val = 0;
	if (b->ddk.dead == 0)
		val = taupci_get_cd (c);
	spin_unlock_irqrestore (&b->lock, flags);
	return val;
}

static void irq_receive (taupci_chan_t * c, unsigned char *data, int len)
{
	cronyx_binder_item_t *h = c->sys;
	taupci_adapter_t *b = (taupci_adapter_t *) c->board;
	struct sk_buff *skb;

	CRONYX_LOG_2 (h, "cp: irq-receive %d bytes, rn:%d\n", len, c->rn);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4RX);
	if (!h->proto)
		return;

	skb = dev_alloc_skb (len);
	if (!skb) {
		printk (KERN_ERR "cp: no memory for I/O buffer.");
		return;
	}

	skb_put (skb, len);
	memcpy (skb->data, data, len);
	spin_unlock (&b->lock);
	cronyx_receive (h, skb);
	spin_lock (&b->lock);
}

static void irq_transmit (taupci_chan_t * c, void *attachment, int len)
{
	cronyx_binder_item_t *h = c->sys;
	taupci_adapter_t *b = (taupci_adapter_t *) c->board;

	CRONYX_LOG_2 (h, "cp: irq-transmit %d bytes, tn:%d, te:%d\n", len, c->tn, c->te);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4TX);
	if (h->proto) {
		spin_unlock (&b->lock);
		cronyx_transmit_done (h);
		spin_lock (&b->lock);
	}
}

static void irq_error (taupci_chan_t * c, int data)
{
	cronyx_binder_item_t *h = c->sys;
	taupci_adapter_t *b = (taupci_adapter_t *) c->board;

	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4ERR);
	if (h->proto) {
		spin_unlock (&b->lock);
		switch (data) {
			case TAUPCI_FRAME:
				CRONYX_LOG_1 (h, "cp: framing error\n");
				cronyx_receive_error (h, CRONYX_ERR_FRAMING);
				break;
			case TAUPCI_CRC:
				CRONYX_LOG_1 (h, "cp: crc error\n");
				cronyx_receive_error (h, CRONYX_ERR_CHECKSUM);
				break;
			case TAUPCI_UNDERRUN:
				CRONYX_LOG_1 (h, "cp: transmit underrun\n");
				cronyx_receive_error (h, CRONYX_ERR_UNDERRUN);
				break;
			case TAUPCI_OVERRUN:
				CRONYX_LOG_1 (h, "cp: receive overrun\n");
				cronyx_receive_error (h, CRONYX_ERR_OVERRUN);
				break;
			case TAUPCI_OVERFLOW:
				CRONYX_LOG_1 (h, "cp: receive overflow\n");
				cronyx_receive_error (h, CRONYX_ERR_OVERFLOW);
				break;
			default:
				CRONYX_LOG_1 (h, "cp: error #%d\n", data);
				break;
		}
		spin_lock (&b->lock);
	}
}

static void taupci_led_set (void *tag, int on)
{
	taupci_adapter_t *b = (taupci_adapter_t *) tag;

	if (b->ddk.dead == 0)
		taupci_led (&b->ddk, on);
}

static int taupci_led_state (void *tag)
{
	taupci_adapter_t *b = (taupci_adapter_t *) tag;
	taupci_chan_t *c;
	int result = CRONYX_LEDSTATE_OFF;

	if (b && b->ddk.dead == 0) {
		for (c = b->ddk.chan; c < b->ddk.chan + TAUPCI_NCHAN; c++) {
			cronyx_binder_item_t *item;

			if (c->type == TAUPCI_NONE)
				continue;

			item = c->sys;
			if (item && item->running) {
				if (c->type >= TAUPCI_G703 || c->type >= TAUPCI_E3) {
					if (c->status & (TAUPCI_ESTS_LOF | TAUPCI_ESTS_LOS | TAUPCI_ESTS_LOMF))
						return CRONYX_LEDSTATE_FAILED;
					if (c->status & TAUPCI_ESTS_TSTERR)
						result = CRONYX_LEDSTATE_ALARM;
					if ((c->status & (TAUPCI_ESTS_RA | TAUPCI_ESTS_AIS | TAUPCI_ESTS_AIS16 |
						       TAUPCI_ESTS_RDMA)) != 0 && result < CRONYX_LEDSTATE_WARN)
						result = CRONYX_LEDSTATE_WARN;
					if ((c->status & TAUPCI_ESTS_NOALARM) && (result < CRONYX_LEDSTATE_OK))
						result = CRONYX_LEDSTATE_OK;
				} else {
					if (!taupci_get_cd (c) || taupci_get_txcerr (c) || taupci_get_rxcerr (c))
						return CRONYX_LEDSTATE_FAILED;
					if (!taupci_get_cts (c))
						result = CRONYX_LEDSTATE_ALARM;
					if (!taupci_get_dsr (c))
						result = CRONYX_LEDSTATE_WARN;
					if (result < CRONYX_LEDSTATE_OK)
						result = CRONYX_LEDSTATE_OK;
				}
			}
		}
	}
	return result;
}

static struct device *taupci_get_device (cronyx_binder_item_t *h)
{
	if (h && h->type==CRONYX_ITEM_ADAPTER)
		return &((taupci_adapter_t *)h->hw)->pdev->dev;
	return NULL;
}

#if LINUX_VERSION_CODE >= 0x020613
static irqreturn_t taupci_isr (int irq, void *dev)
#elif LINUX_VERSION_CODE >= 0x020600
static irqreturn_t taupci_isr (int irq, void *dev, struct pt_regs *regs)
#else
static void taupci_isr (int irq, void *dev, struct pt_regs *regs)
#endif
{
	taupci_adapter_t *b = dev;

	if (b->ddk.dead != 0)
#if LINUX_VERSION_CODE >= 0x020600
		return IRQ_NONE;
#else
		return;
#endif

	if (!taupci_is_interrupt_pending (&b->ddk)) {
#if LINUX_VERSION_CODE >= 0x020600
		return IRQ_NONE;
#else
		return;
#endif
	}

	spin_lock (&b->lock);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4IRQ);
	if (taupci_handle_interrupt (&b->ddk) < 0) {
		printk (KERN_EMERG "cp: Adapter #%d - interrupt storm on '%s' at irq %d\n",
			b->binder_data.order, b->ddk.name, b->pdev->irq);
		taupci_enable_interrupt (&b->ddk, 0);
	}

	spin_unlock (&b->lock);
#if LINUX_VERSION_CODE >= 0x020600
	return IRQ_HANDLED;
#endif
}

static void taupci_second_timer (cronyx_binder_item_t * item)
{
	unsigned long flags;
	taupci_chan_t *c = (taupci_chan_t *) item->hw;

	spin_lock_irqsave (&((taupci_adapter_t *) c->board)->lock, flags);
	if (((taupci_adapter_t *) c->board)->ddk.dead == 0) {
		if (c->type == TAUPCI_G703)
			taupci_g703_timer (c);
		if (c->type == TAUPCI_E1)
			taupci_e1_timer (c);
		if (c->prev_status != c->status) {
			c->prev_status = c->status;
			cronyx_modem_event (item);
		}
	}
	spin_unlock_irqrestore (&((taupci_adapter_t *) c->board)->lock, flags);
}

static void taupci_timer5_proc (unsigned long arg)
{
	int n;
	taupci_adapter_t *b;
	unsigned long flags;

	spin_lock (&taupci_lock_driver);
	n = 0;
	list_for_each_entry (b, &adapters_list, entry) {
		if (b->ddk.dead || b->ddk.model != TAUPCI_MODEL_1E3)
			continue;
		spin_lock_irqsave (&b->lock, flags);
		taupci_e3_timer (b->ddk.chan);
		spin_unlock_irqrestore (&b->lock, flags);
		n++;
	}

	if (n) {
		taupci_timer.function = &taupci_timer5_proc;
		taupci_timer.expires = jiffies + (HZ + 1) / 5;
		add_timer (&taupci_timer);
	}
	spin_unlock (&taupci_lock_driver);
}

//static int taupci_get_e1ts_map (cronyx_binder_item_t * item, struct cronyx_e1ts_map_t *e1ts_map)
//{
//      if (item->type != CRONYX_ITEM_CHANNEL)
//              return -EINVAL;
//      else {
//              taupci_chan_t *c = item->hw;
//              taupci_adapter_t *b = (taupci_adapter_t *) c->board;
//              unsigned long flags;
//              int i;
//
//              spin_lock_irqsave (&b->lock, flags);
//              if (c->phony && c->ds21x54) {
//                      e1ts_map->length = 32;
//                      for (i = 0; i < 32; i++)
//                              e1ts_map->ts_index[i] = (c->ts & (1ul << i)) ? i : -1;
//              } else {
//                      e1ts_map->length = 0;
//                      for (i = 0; i < 32; i++)
//                              if (c->ts & (1ul << i))
//                                      e1ts_map->ts_index[e1ts_map->length++] = i;
//              }
//              spin_unlock_irqrestore (&b->lock, flags);
//              return 0;
//      }
//}

//static int taupci_phony_dacs (cronyx_binder_item_t * a, int a_ts, cronyx_binder_item_t * b, int b_ts)
//{
//      return -EINVAL;
//}

static cronyx_dispatch_t dispatch_tab = {
	.link_up = taupci_up,
	.link_down = taupci_down,
	.transmit = taupci_transmit,
	.set_dtr = taupci_dtr,
	.set_rts = taupci_rts,
	.query_dtr = taupci_query_dtr,
	.query_rts = taupci_query_rts,
	.query_dsr = taupci_query_dsr,
	.query_cts = taupci_query_cts,
	.query_dcd = taupci_query_dcd,
	.ctl_set = taupci_ctl_set,
	.ctl_get = taupci_ctl_get,
	.get_device = taupci_get_device,
	//.phony_get_e1ts_map = taupci_get_e1ts_map
};

static char *taupci_iface_name (int type)
{
	switch (type) {
		default:
		case TAUPCI_SERIAL:
			return "s";
		case TAUPCI_G703:
			return "g703";
		case TAUPCI_E1:
			return "e1";
		case TAUPCI_E3:
			return "e3";
		case TAUPCI_HSSI:
			return "hssi";
		case TAUPCI_T3:
			return "t3";
		case TAUPCI_STS1:
			return "sts1";
	}
}

static void taucpi_freedma (taupci_adapter_t *b)
{
	int i;

	for (i = 0; i < TAUPCI_NCHAN; ++i) {
		if (b->txb[i]) {
			if (b->txb_dma[i])
				pci_unmap_single (b->pdev, b->txb_dma[i], sizeof (taupci_buf_t), PCI_DMA_TODEVICE);
			kfree (b->txb[i]);
		}
		if (b->rxb[i]) {
			if (b->rxb_dma[i])
				pci_unmap_single (b->pdev, b->rxb_dma[i], sizeof (taupci_buf_t), PCI_DMA_FROMDEVICE);
			kfree (b->rxb[i]);
		}
	}
	if (b->iq)
		pci_free_consistent (b->pdev, sizeof (taupci_iq_t), b->iq, b->iq_dma);
}

static int __init taupci_probe (struct pci_dev *dev, const struct pci_device_id *ident)
{
	taupci_adapter_t *b;
	taupci_chan_t *c;
	cronyx_binder_item_t *chan;
	void *vbase = NULL;
	unsigned long flags;

	dev_counter++;
	b = kzalloc (sizeof (*b), GFP_KERNEL);
	if (b == NULL) {
		printk (KERN_ERR "cp: Adapter #%d - out of memory\n", dev_counter);
		return -ENOMEM;
	}

	b->mem = pci_resource_start (dev, 0);
	if (!request_mem_region (b->mem, TAUPCI_BAR1_SIZE, "Cronyx Tau-PCI")) {
		printk (KERN_ERR "cp: Adapter #%d - io-region busy at 0x%lx, other driver (dscc4) loaded?\n",
			b->binder_data.order, (unsigned long) b->mem);
		b->mem = 0;
		goto ballout;
	}

	spin_lock_init (&b->lock);
	INIT_LIST_HEAD (&b->entry);
	b->irq_raw = dev->irq;
	b->pdev = dev;
	b->ddk.dead = -1;
	b->binder_data.order = dev_counter;
	vbase = NULL;

	if (pci_enable_device (dev)) {
		printk (KERN_ERR "cp: Adapter #%d - unable to enable pci-device\n", b->binder_data.order);
		goto ballout;
	}

	vbase = ioremap_nocache (b->mem, TAUPCI_BAR1_SIZE);
	if (vbase == NULL) {
		printk (KERN_ERR "cp: Adapter #%d - can't ioremap_nocache pci-memory 0x%lx\n",
			b->binder_data.order, (unsigned long) b->mem);
		goto ballout;
	}

	if (pci_set_dma_mask (dev, DMA_32BIT_MASK)
#if LINUX_VERSION_CODE >= 0x020600
	|| pci_set_consistent_dma_mask (dev, DMA_32BIT_MASK)
#endif
	) {
		printk (KERN_ERR "cp: Adapter #%d - no suitable DMA available\n", b->binder_data.order);
		goto ballout;
	}

	b->iq = pci_alloc_consistent (dev, sizeof (taupci_iq_t), &b->iq_dma);
	if (b->iq == 0) {
		printk (KERN_ERR "cp: Adapter #%d - can't alloc IQ-buffers\n", b->binder_data.order);
		goto ballout;
	}

	/*
	 * LY: We should download a firmware and reset buggy Infineon PEB20534.
	 Then initialize adapter again.
	 */
	taupci_init (&b->ddk, dev_counter, vbase, b->iq, b->iq_dma);
	taupci_halt (&b->ddk);
	taupci_reset_buggy_infineon20534 (b);
	pci_set_master (b->pdev);
	if (taupci_init (&b->ddk, dev_counter, vbase, b->iq, b->iq_dma)) {
		printk (KERN_ERR "cp: Adapter #%d - hardware broken, code: 0x%08x\n", b->binder_data.order, b->ddk.dead);
		goto ballout;
	}

	if (request_irq (b->pdev->irq, taupci_isr, IRQF_SHARED, "Cronyx Tau-PCI", b)) {
		printk (KERN_ERR "cp: Adapter #%d - can't get irq %s\n", b->binder_data.order,
			cronyx_format_irq (b->irq_raw, b->pdev->irq));
		goto ballout;
	}
	b->irq_apic = b->pdev->irq;

	pci_set_drvdata (dev, b);
	printk (KERN_INFO "cp: Adapter #%d <Cronyx %s> at 0x%lx irq %s\n", b->binder_data.order, b->ddk.name,
		(unsigned long) b->mem, cronyx_format_irq (b->irq_raw, b->irq_apic));
	if (b->ddk.model == 0) {
		printk (KERN_ERR "cp: Adapter #%d - unknown device model\n", b->binder_data.order);
		goto ballout;
	}

	for (c = b->ddk.chan; c < b->ddk.chan + TAUPCI_NCHAN; ++c) {
		if (c->type <= 0)
			continue;
		b->txb[c->num] = kmalloc (sizeof (taupci_buf_t), GFP_KERNEL | GFP_DMA);
		b->rxb[c->num] = kmalloc (sizeof (taupci_buf_t), GFP_KERNEL | GFP_DMA);
		if (b->txb[c->num] == 0 || b->rxb[c->num] == 0) {
			printk (KERN_ERR "cp: Adapter #%d - no memory for DMA-buffers\n", b->binder_data.order);
			goto ballout;
		}
		b->txb_dma[c->num] = pci_map_single (b->pdev, b->txb[c->num], sizeof (taupci_buf_t), PCI_DMA_TODEVICE);
		b->rxb_dma[c->num] = pci_map_single (b->pdev, b->rxb[c->num], sizeof (taupci_buf_t), PCI_DMA_FROMDEVICE);
		if (b->txb_dma[c->num] == 0 || b->rxb_dma[c->num] == 0) {
			printk (KERN_ERR "cp: Adapter #%d - can't map DMA-buffers to PCI\n", b->binder_data.order);
			goto ballout;
		}
	}

	spin_lock_irqsave (&b->lock, flags);
	taupci_set_mux (&b->ddk, 0);
	spin_unlock_irqrestore (&b->lock, flags);

	b->binder_data.dispatch = dispatch_tab;
	b->binder_data.type = CRONYX_ITEM_ADAPTER;
	b->binder_data.hw = b;
	b->binder_data.provider = THIS_MODULE;
	if (cronyx_binder_register_item (0, "taupci", dev_counter, NULL, -1, &b->binder_data) < 0) {
		printk (KERN_ERR "cp: Adapter #%d - unable register on binder\n", b->binder_data.order);
		goto ballout;
	}

	cronyx_led_init (&b->led_flasher, &b->lock, b, taupci_led_set, taupci_led_state);
	for (c = b->ddk.chan; c < b->ddk.chan + TAUPCI_NCHAN; ++c) {
		if (c->type <= 0)
			continue;
		if (c->type >= TAUPCI_SERIAL) {
			chan = &b->interfaces[c->num];
			chan->dispatch = dispatch_tab;
			chan->hw = c;
			c->sys = chan;
			chan->type = CRONYX_ITEM_INTERFACE;
			chan->order = c->num;
			if (b->ddk.model <= TAUPCI_MODEL_LITE)
				chan->order = -1;
			if (cronyx_binder_register_item
			    (b->binder_data.id, taupci_iface_name (c->type), c->num, NULL, -1, chan) < 0) {
				printk (KERN_ERR "cp: Adapter #%d - unable register interface #%d on binder\n",
					b->binder_data.order, c->num);
			}
		}

		chan = &b->channels[c->num];
		chan->dispatch = dispatch_tab;
		chan->dispatch.second_timer = taupci_second_timer;
		chan->hw = c;
		c->sys = chan;
		chan->type = CRONYX_ITEM_CHANNEL;
		chan->order = c->num;
		if (b->ddk.model <= TAUPCI_MODEL_LITE)
			chan->order = -1;
		chan->mtu = TAUPCI_MTU;
		chan->fifosz = TAUPCI_NBUF * TAUPCI_MTU;
		if (cronyx_binder_register_item (b->binder_data.id, NULL, c->num, "cp", chan_counter, chan) < 0) {
			printk (KERN_ERR "cp: Adapter #%d - unable register channel #%d on binder\n",
				b->binder_data.order, c->num);
		} else {
			taupci_register_transmit (c, &irq_transmit);
			taupci_register_receive (c, &irq_receive);
			taupci_register_error (c, &irq_error);

			spin_lock_irqsave (&b->lock, flags);
			taupci_set_phony (c, 0);
			taupci_set_cas (c, TAUPCI_CAS_OFF);
			taupci_start_e1 (c);
			spin_unlock_irqrestore (&b->lock, flags);
		}
		chan_counter++;
	}

	spin_lock_irqsave (&taupci_lock_driver, flags);
	list_add_tail (&b->entry, &adapters_list);
	spin_lock (&b->lock);
	taupci_enable_interrupt (&b->ddk, 1);
	spin_unlock (&b->lock);
	spin_unlock_irqrestore (&taupci_lock_driver, flags);

	taupci_timer5_proc (0);
	return 0;

ballout:
	if (b->mem && b->ddk.dead == 0)
		taupci_halt (&b->ddk);
	if (vbase)
		iounmap (vbase);
	if (b->mem) {
		release_mem_region (b->mem, TAUPCI_BAR1_SIZE);
		pci_disable_device (b->pdev);
		if (b->irq_apic)
			free_irq (b->pdev->irq, b);
		b->pdev->irq = b->irq_raw;
	}
	taucpi_freedma (b);
	kfree (b);
	return -EIO;
}

static void __exit taupci_remove (struct pci_dev *dev)
{
	int i;
	taupci_adapter_t *b;
	cronyx_binder_item_t *h;
	unsigned long flags;

	b = pci_get_drvdata (dev);
	if (b->ddk.dead == 0 && b->ddk.model != 0) {
		spin_lock_irqsave (&b->lock, flags);
		for (i = 0; i < TAUPCI_NCHAN; ++i) {
			h = &b->channels[i];
			if (h->hw) {
				taupci_stop_chan (h->hw);
				taupci_stop_e1 (h->hw);
			}
		}
		spin_unlock_irqrestore (&b->lock, flags);
	}
	if (b->binder_data.id > 0 && cronyx_binder_unregister_item (b->binder_data.id) < 0)
		printk (KERN_EMERG "cp: Adapter #%d - unable unregister from binder\n", b->ddk.num);

	cronyx_led_destroy (&b->led_flasher);
	spin_lock_irqsave (&taupci_lock_driver, flags);
	spin_lock (&b->lock);
	taupci_enable_interrupt (&b->ddk, 0);
	taupci_handle_interrupt (&b->ddk);
	b->ddk.dead = -1;
	list_del_init (&b->entry);
	spin_unlock (&b->lock);
	spin_unlock_irqrestore (&taupci_lock_driver, flags);

	if (b->irq_apic)
		free_irq (b->pdev->irq, b);
	taupci_halt (&b->ddk);
	pci_disable_device (b->pdev);
	b->pdev->irq = b->irq_raw;

	iounmap ((void *) b->ddk.base);
	release_mem_region (b->mem, TAUPCI_BAR1_SIZE);
	taucpi_freedma (b);
	pci_set_drvdata (dev, NULL);
	kfree (b);
}

static struct pci_device_id taupci_ids[] = {
	{TAUPCI_PCI_VENDOR_ID, TAUPCI_PCI_DEVICE_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0,
	 (unsigned long) "Infineon 20534 / DSCC4"},
	{0,}
};

static struct pci_driver taupci_driver = {
	.name = "Cronyx Tau-PCI",
	.probe = taupci_probe,
#if LINUX_VERSION_CODE < 0x020600
	.remove = taupci_remove,
#else
	.remove = __exit_p (taupci_remove),
#endif
	.id_table = taupci_ids
};

int __init init_module (void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif

	init_timer (&taupci_timer);
#if LINUX_VERSION_CODE < 0x02060A
	return pci_module_init (&taupci_driver);
#else
	return pci_register_driver (&taupci_driver);
#endif
}

void __exit cleanup_module (void)
{
	pci_unregister_driver (&taupci_driver);
	del_timer_sync (&taupci_timer);
}

MODULE_DEVICE_TABLE (pci, taupci_ids);

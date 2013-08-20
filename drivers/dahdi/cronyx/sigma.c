/*
 * Cronyx-Sigma adapter driver.
 *
 * Copyright (C) 1997-2005 Cronyx Engineering.
 * Author: Kurakin Roman <rik@cronyx.ru>
 * (C)1998 Alexander Kvitchenko <aak@cronyx.ru>
 *
 * Copyright (C) 2006-2008 Cronyx Engineering.
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: sigma.c,v 1.50 2009-10-08 12:33:40 ly Exp $
 */
#include "module.h"
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/tty.h>
#include <asm/io.h>
#include <asm/dma.h>
#include "sigma-ddk.h"
#include "cserial.h"

#if !defined(CONFIG_ISA) && !defined(CONFIG_GENERIC_ISA_DMA) && !defined(CONFIG_ISA_DMA_API)
#	error "Cronyx Sigma module required ISA-bus support."
#endif

/* Module information */
MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx driver Sigma-ISA adapters serie\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

typedef struct _sigma_adapter_t {
	sigma_board_t ddk;
	spinlock_t lock;
	sigma_buf_t *buffers[SIGMA_NCHAN];
	cronyx_binder_item_t channels[SIGMA_NCHAN];
	cronyx_binder_item_t interfaces[SIGMA_NCHAN];
	cronyx_binder_item_t binder_data;
	int modem_status[SIGMA_NCHAN];
	unsigned long modem_intr[SIGMA_NCHAN];
	struct cronyx_led_flasher_t led_flasher;
} sigma_adapter_t;

/* Module parameters */
int port[SIGMA_NBRD], irq[SIGMA_NBRD], dma[SIGMA_NBRD], busmode;

#ifdef MODULE_PARM
MODULE_PARM (port, "1-" __MODULE_STRING (SIGMA_NBRD) "i");
MODULE_PARM (irq, "1-" __MODULE_STRING (SIGMA_NBRD) "i");
MODULE_PARM (dma, "1-" __MODULE_STRING (SIGMA_NBRD) "i");
MODULE_PARAM (busmode, "i");
#else
module_param_array (port, int, NULL, 0644);
module_param_array (irq, int, NULL, 0644);
module_param_array (dma, int, NULL, 0644);
module_param (busmode, int, 0644);
#endif
MODULE_PARM_DESC (port, "I/O base port for each board");
MODULE_PARM_DESC (irq, "IRQ for each board");
MODULE_PARM_DESC (dma, "DMA for each board");
MODULE_PARM_DESC (busmode, "ISA-BUS speedup mode (0=normal - 3=fastest)");

static sigma_adapter_t *adapters[SIGMA_NBRD];

static int sigma_ctl_get (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	int i, error;
	sigma_chan_t *c;
	sigma_adapter_t *b;
	unsigned long flags;

	if (item->hw == NULL)
		return -ENODEV;

	if (item->type == CRONYX_ITEM_ADAPTER) {
		c = NULL;
		b = (sigma_adapter_t *) item->hw;
	} else {
		c = (sigma_chan_t *) item->hw;
		b = (sigma_adapter_t *) c->board;
	}

	error = 0;
	switch (ctl->param_id) {
		case cronyx_port_or_cable_type:
			CRONYX_LOG_1 (item, "sigma.ctl: get-port-type\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				i = sigma_get_port (c);
				spin_unlock_irqrestore (&b->lock, flags);
				switch (i) {
					default:
						ctl->u.param.value = CRONYX_UNKNOWN;
						break;
					case SIGMA_PORT_AUTO:
						ctl->u.param.value = CRONYX_AUTO;
						break;
					case SIGMA_PORT_RS232:
						ctl->u.param.value = CRONYX_RS232;
						break;
					case SIGMA_PORT_V35:
						ctl->u.param.value = CRONYX_V35;
						break;
					case SIGMA_PORT_RS449:
						ctl->u.param.value = CRONYX_RS449;
						break;
				}
			}
			break;

		case cronyx_channel_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: get-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else
				ctl->u.param.value =
					(c->mode == SIGMA_MODE_ASYNC) ? CRONYX_MODE_ASYNC : CRONYX_MODE_HDLC;
			break;

		case cronyx_stat_channel:
			CRONYX_LOG_1 (item, "sigma.ctl: get-stat-channel\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.stat_channel.rintr = c->rintr;
				ctl->u.stat_channel.tintr = c->tintr;
				ctl->u.stat_channel.mintr = b->modem_intr[c->num];
				ctl->u.stat_channel.ibytes = c->ibytes;
				ctl->u.stat_channel.ipkts = c->ipkts;
				ctl->u.stat_channel.ierrs = c->ierrs;
				ctl->u.stat_channel.obytes = c->obytes;
				ctl->u.stat_channel.opkts = c->opkts;
				ctl->u.stat_channel.oerrs = c->oerrs;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (item, "sigma.ctl: get-baud\n");
			if (item->type != CRONYX_ITEM_CHANNEL /* || c->mode != SIGMA_MODE_HDLC */)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = sigma_get_baud (c);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_loop_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: get-loop\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = sigma_get_loop (c) ? CRONYX_LOOP_INTERNAL : CRONYX_LOOP_NONE;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_dpll:
			CRONYX_LOG_1 (item, "sigma.ctl: get-dpll\n");
			if (item->type != CRONYX_ITEM_INTERFACE || c->mode != SIGMA_MODE_HDLC)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = sigma_get_dpll (c);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_line_code:
			CRONYX_LOG_1 (item, "sigma.ctl: get-line\n");
			if (item->type != CRONYX_ITEM_INTERFACE || c->mode != SIGMA_MODE_HDLC)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				ctl->u.param.value = sigma_get_nrzi (c) ? CRONYX_NRZI : CRONYX_NRZ;
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: get-led\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_GET, ctl);
			break;

		case cronyx_crc_mode:
			ctl->u.param.value = CRONYX_CRC_16;
			CRONYX_LOG_1 (item, "sigma.ctl: get-crc-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->mode != SIGMA_MODE_HDLC)
				error = -EINVAL;
			break;

		default:
			CRONYX_LOG_2 (item, "sigma.ctl: get-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}

	return error;
}

static int sigma_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	int i, error;
	sigma_chan_t *c;
	sigma_adapter_t *b;
	unsigned long flags;

	if (item->hw == NULL)
		return -ENODEV;

	if (item->type == CRONYX_ITEM_ADAPTER) {
		c = NULL;
		b = (sigma_adapter_t *) item->hw;
	} else {
		c = (sigma_chan_t *) item->hw;
		b = (sigma_adapter_t *) c->board;
	}

	error = 0;
	switch (ctl->param_id) {
		case cronyx_port_or_cable_type:
			CRONYX_LOG_1 (item, "sigma.ctl: set-port-type\n");
			if (item->type != CRONYX_ITEM_INTERFACE)
				error = -EINVAL;
			else {
				switch (ctl->u.param.value) {
					default:
						error = -EINVAL;
						/*
						 * LY: no break here
						 */ ;
					case CRONYX_UNKNOWN:
						i = SIGMA_PORT_AUTO;
						break;
					case CRONYX_RS232:
						i = SIGMA_PORT_RS232;
						break;
					case CRONYX_V35:
						i = SIGMA_PORT_V35;
						break;
					case CRONYX_RS449:
						i = SIGMA_PORT_RS449;
						break;
				}
				if (error == 0) {
					spin_lock_irqsave (&b->lock, flags);
					sigma_set_port (c, i);
					spin_unlock_irqrestore (&b->lock, flags);
				}
			}
			break;

		case cronyx_channel_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: set-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_MODE_ASYNC && ctl->u.param.value != CRONYX_MODE_HDLC)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				if (sigma_set_mode
				    (c,
				     (ctl->u.param.value == CRONYX_MODE_ASYNC) ? SIGMA_MODE_ASYNC : SIGMA_MODE_HDLC) !=
				    0)
					error = -EINVAL;
				else if (item->running && c->mode == SIGMA_MODE_HDLC) {
					sigma_set_dtr (c, 1);
					sigma_set_rts (c, 1);
				}
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_clear_stat:
			CRONYX_LOG_1 (item, "sigma.ctl: clear-stat\n");
			spin_lock_irqsave (&b->lock, flags);
			switch (item->type) {
				default:
					error = -EINVAL;
				case CRONYX_ITEM_CHANNEL:
				case CRONYX_ITEM_INTERFACE:
					c->rintr = 0;
					c->tintr = 0;
					b->modem_intr[c->num] = 0;
					c->ibytes = 0;
					c->ipkts = 0;
					c->ierrs = 0;
					c->obytes = 0;
					c->opkts = 0;
					c->oerrs = 0;
					break;
				case CRONYX_ITEM_ADAPTER:
					for (i = 0; i < SIGMA_NCHAN; i++) {
						c = (sigma_chan_t *) b->channels[i].hw;
						if (c) {
							c->rintr = 0;
							c->tintr = 0;
							b->modem_intr[c->num] = 0;
							c->ibytes = 0;
							c->ipkts = 0;
							c->ierrs = 0;
							c->obytes = 0;
							c->opkts = 0;
							c->oerrs = 0;
						}
					}
					break;
			}
			spin_unlock_irqrestore (&b->lock, flags);
			break;

		case cronyx_baud:
			CRONYX_LOG_1 (item, "sigma.ctl: set-baud\n");
			if (item->type != CRONYX_ITEM_CHANNEL /* || c->mode != SIGMA_MODE_HDLC */)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				sigma_set_baud (c, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_loop_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: set-loop\n");
			if (item->type != CRONYX_ITEM_INTERFACE || c->mode != SIGMA_MODE_HDLC)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_LOOP_INTERNAL && ctl->u.param.value != CRONYX_LOOP_NONE)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				sigma_set_loop (c, ctl->u.param.value == CRONYX_LOOP_INTERNAL);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_dpll:
			CRONYX_LOG_1 (item, "sigma.ctl: set-dpll\n");
			if (item->type != CRONYX_ITEM_INTERFACE || c->mode != SIGMA_MODE_HDLC)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				sigma_set_dpll (c, ctl->u.param.value);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_line_code:
			CRONYX_LOG_1 (item, "sigma.ctl: set-line\n");
			if (item->type != CRONYX_ITEM_INTERFACE || c->mode != SIGMA_MODE_HDLC)
				error = -EINVAL;
			else if (ctl->u.param.value != CRONYX_NRZI && ctl->u.param.value != CRONYX_NRZ)
				error = -EINVAL;
			else {
				spin_lock_irqsave (&b->lock, flags);
				sigma_set_nrzi (c, ctl->u.param.value == CRONYX_NRZI);
				spin_unlock_irqrestore (&b->lock, flags);
			}
			break;

		case cronyx_led_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: set-led\n");
			if (item->type != CRONYX_ITEM_ADAPTER)
				error = -EINVAL;
			else
				error = cronyx_led_ctl (&b->led_flasher, CRONYX_SET, ctl);
			break;

		case cronyx_crc_mode:
			CRONYX_LOG_1 (item, "sigma.ctl: set-crc-mode\n");
			if (item->type != CRONYX_ITEM_CHANNEL || c->mode != SIGMA_MODE_HDLC
			    || ctl->u.param.value != CRONYX_CRC_16)
				error = -EINVAL;
			break;

		default:
			CRONYX_LOG_2 (item, "sigma.ctl: set-unsupported (%d)\n", ctl->param_id);
			error = -ENOSYS;
	}

	return error;
}

static int sigma_up (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_1 (h, "sigma.up\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	if (c->mode == SIGMA_MODE_HDLC) {
		sigma_set_dtr (c, 1);
		sigma_set_rts (c, 1);
	}
	sigma_enable_receive (c, 1);
	sigma_enable_transmit (c, 1);
	if (h->running) {
		spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
		return 0;
	}
	h->running = 1;
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);

#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif
	return 0;
}

static void sigma_down (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;
	int was_running;

	CRONYX_LOG_1 (h, "sigma.down\n");

	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	was_running = h->running;
	h->running = 0;
	sigma_enable_receive (c, 0);
	if (was_running && sigma_is_transmit (c) && ! signal_pending (current)) {
		int wait = HZ / 10;
		if (c->txbaud)
			wait = HZ * h->mtu * 25ul / c->txbaud;
		spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
		set_current_state (TASK_INTERRUPTIBLE);
		schedule_timeout (wait);
		spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	}
	sigma_enable_transmit (c, 0);
	if (c->mode == SIGMA_MODE_HDLC) {
		sigma_set_rts (c, 0);
		sigma_set_dtr (c, 0);
	}
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);

	if (was_running) {
#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put (THIS_MODULE);
#endif
	}
}

static int sigma_transmit (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	sigma_chan_t *c = h->hw;
	int res;
	unsigned long flags;

	CRONYX_LOG_2 (h, "sigma.transmit(%d)\n", skb->len);
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	res = sigma_send_packet (c, skb->data, skb->len, 0);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
	switch (res) {
		case 0:
			return skb->len;	/* success */
		case -1:
			return 0;	/* no free buffers */
		default:
			CRONYX_LOG_1 (h, "sigma.transmit(%d) error #%d\n", skb->len, res);
			return -EIO;	/* error */
	}
}

static void sigma_dtr (cronyx_binder_item_t * h, int val)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_1 (h, "sigma.set_dtr(%d)\n", val);
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_set_dtr (c, val);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void sigma_rts (cronyx_binder_item_t * h, int val)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_1 (h, "sigma.set_rts(%d)\n", val);
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_set_rts (c, val);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static int sigma_query_dtr (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;

	return c->dtr;
}

static int sigma_query_rts (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;

	return c->rts;
}

static int sigma_query_dsr (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;
	int val;

	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	val = sigma_get_dsr (c);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
	return val;
}

static int sigma_query_cts (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;
	int val;

	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	val = sigma_get_cts (c);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
	return val;
}

static int sigma_query_dcd (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;
	int val;

	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	val = sigma_get_cd (c);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
	return val;
}

static void sigma_second_timer (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	sigma_adapter_t *b = (sigma_adapter_t *) c->board;
	int modem_status;
	unsigned long flags;

	modem_status = cronyx_get_modem_status (h);
	spin_lock_irqsave (&b->lock, flags);
	if ((modem_status ^ b->modem_status[c->num]) & (TIOCM_CD | TIOCM_CTS | TIOCM_DSR)) {
		CRONYX_LOG_2 (h, "sigma.modem_event (0x%X -> 0x%X)\n", b->modem_status[c->num], modem_status);
		b->modem_status[c->num] = modem_status;
		b->modem_intr[c->num]++;
		spin_unlock_irqrestore (&b->lock, flags);
		cronyx_modem_event (h);
	} else
		spin_unlock_irqrestore (&b->lock, flags);
}

static void irq_receive (sigma_chan_t * c, char *data, int len)
{
	cronyx_binder_item_t *h = c->sys;
	sigma_adapter_t *b = (sigma_adapter_t *) c->board;
	struct sk_buff *skb;

	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4RX);
	CRONYX_LOG_2 (h, "sigma.irq_receive(%d)\n", len);
	if (!h->proto)
		return;
	skb = dev_alloc_skb (len);
	if (!skb) {
		++c->ierrs;
		CRONYX_LOG_1 (h, "cx: no memory for receive buffer\n");
		return;
	}
	skb_put (skb, len);
	memcpy (skb->data, data, len);
	spin_unlock (&b->lock);
	cronyx_receive (h, skb);
	spin_lock (&b->lock);
}

static void irq_transmit (sigma_chan_t * c, void *attachment, int len)
{
	cronyx_binder_item_t *h = c->sys;
	sigma_adapter_t *b = (sigma_adapter_t *) c->board;

	CRONYX_LOG_2 (h, "sigma.irq_transmit(%d)\n", len);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4TX);
	spin_unlock (&b->lock);
	cronyx_transmit_done (h);
	spin_lock (&b->lock);
}

static void irq_error (sigma_chan_t * c, int data)
{
	cronyx_binder_item_t *h = c->sys;
	sigma_adapter_t *b = (sigma_adapter_t *) c->board;

	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4ERR);
	if (h->proto) {
		spin_unlock (&b->lock);
		switch (data) {
			case SIGMA_ERR_FRAME:
				CRONYX_LOG_1 (h, "cx: framing error\n");
				cronyx_receive_error (h, CRONYX_ERR_FRAMING);
				break;
			case SIGMA_ERR_CRC:
				CRONYX_LOG_1 (h, "cx: CRC error\n");
				cronyx_receive_error (h, CRONYX_ERR_CHECKSUM);
				break;
			case SIGMA_ERR_OVERRUN:
				CRONYX_LOG_1 (h, "cx: overrun error\n");
				cronyx_receive_error (h, CRONYX_ERR_OVERRUN);
				break;
			case SIGMA_ERR_OVERFLOW:
				CRONYX_LOG_1 (h, "cx: overflow error\n");
				cronyx_receive_error (h, CRONYX_ERR_OVERFLOW);
				break;
			case SIGMA_ERR_UNDERRUN:
				CRONYX_LOG_1 (h, "cx: underrun error\n");
				cronyx_receive_error (h, CRONYX_ERR_UNDERRUN);
				break;
			case SIGMA_ERR_BREAK:
				CRONYX_LOG_2 (h, "cx: break-condition\n");
				cronyx_receive_error (h, CRONYX_ERR_BREAK);
				break;
			default:
				CRONYX_LOG_1 (h, "cx: receive error %d\n", data);
		}
		spin_lock (&b->lock);
	}
}

static void sigma_led_set (void *tag, int on)
{
	sigma_led (&((sigma_adapter_t *) tag)->ddk, on);
}

static int sigma_led_state (void *tag)
{
	sigma_adapter_t *b = (sigma_adapter_t *) tag;
	sigma_chan_t *c;
	int result = CRONYX_LEDSTATE_OFF;

	for (c = b->ddk.chan; c < b->ddk.chan + SIGMA_NCHAN; c++) {
		cronyx_binder_item_t *item;

		if (c->type == SIGMA_TYPE_NONE)
			continue;

		item = c->sys;
		if (item->running) {
			if (!sigma_get_cd (c))
				return CRONYX_LEDSTATE_FAILED;
			if (!sigma_get_cts (c))
				result = CRONYX_LEDSTATE_ALARM;
			if (!sigma_get_dsr (c) && result < CRONYX_LEDSTATE_WARN)
				result = CRONYX_LEDSTATE_WARN;
			if (result < CRONYX_LEDSTATE_OK)
				result = CRONYX_LEDSTATE_OK;
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
	sigma_adapter_t *b = dev;

	spin_lock (&b->lock);
	cronyx_led_kick (&b->led_flasher, CRONYX_LEDMODE_4IRQ);
	sigma_int_handler (&b->ddk);
	spin_unlock (&b->lock);
#if LINUX_VERSION_CODE >= 0x020600
	return IRQ_HANDLED;
#endif
}

static void sigma_start_transmitter (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_1 (h, "sigma.start_transmit\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_transmitter_ctl (c, 1);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void sigma_stop_transmitter (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_1 (h, "sigma.stop_transmit\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_purge_transmit (c);
	sigma_transmitter_ctl (c, 0);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void sigma_flush_tx_buffer (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags, fuse = jiffies;

	CRONYX_LOG_1 (h, "sigma.flush_tx_buffer\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	while (sigma_is_transmit (c) && jiffies - fuse < HZ) {
		spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
		cpu_relax ();
		spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	}
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void sigma_send_xon (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_2 (h, "sigma.send_xon\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_xflow_ctl (c, 1);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void sigma_send_xoff (cronyx_binder_item_t * h)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_2 (h, "sigma.send_xoff\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_xflow_ctl (c, 0);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void sigma_send_brk (cronyx_binder_item_t * h, int msec)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_2 (h, "sigma.send_brk\n");
	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_send_break (c, msec);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static void
sigma_set_async_options (cronyx_binder_item_t * h, int baud, int bits, int parity,
			 int stop2, int ignpar, int rtscts, int ixon, int ixany, int symstart, int symstop)
{
	sigma_chan_t *c = h->hw;
	unsigned long flags;

	CRONYX_LOG_2 (h, "sigma.set_async_options\n");
	switch (parity) {
		case CRONYX_PARITY_NONE:
		default:
			parity = SIGMA_PAR_NONE;
			break;
		case CRONYX_PARITY_EVEN:
			parity = SIGMA_PAR_EVEN;
			break;
		case CRONYX_PARITY_ODD:
			parity = SIGMA_PAR_ODD;
			break;
		case CRONYX_PARITY_0:
			parity = SIGMA_PAR_0;
			break;
		case CRONYX_PARITY_1:
			parity = SIGMA_PAR_1;
			break;
	}

	spin_lock_irqsave (&((sigma_adapter_t *) c->board)->lock, flags);
	sigma_set_async_param (c, baud, bits, parity, stop2, ignpar, rtscts, ixon, ixany, symstart, symstop);
	spin_unlock_irqrestore (&((sigma_adapter_t *) c->board)->lock, flags);
}

static cronyx_dispatch_t dispatch_tab = {
	.link_up = sigma_up,
	.link_down = sigma_down,
	.transmit = sigma_transmit,
	.set_dtr = sigma_dtr,
	.set_rts = sigma_rts,
	.query_dtr = sigma_query_dtr,
	.query_rts = sigma_query_rts,
	.query_dsr = sigma_query_dsr,
	.query_cts = sigma_query_cts,
	.query_dcd = sigma_query_dcd,
	.set_async_param = sigma_set_async_options,
	.send_break = sigma_send_brk,
	.send_xon = sigma_send_xon,
	.send_xoff = sigma_send_xoff,
	.start_transmitter = sigma_start_transmitter,
	.stop_transmitter = sigma_stop_transmitter,
	.flush_transmit_buffer = sigma_flush_tx_buffer,
	.ctl_get = sigma_ctl_get,
	.ctl_set = sigma_ctl_set,
};

static const char *sigma_iface_name (int type)
{
	switch (type) {
		case SIGMA_TYPE_ASYNC:
			return "a";
		case SIGMA_TYPE_SYNC_RS232:
		case SIGMA_TYPE_UNIV_RS232:
			return "rs232";
		case SIGMA_TYPE_SYNC_V35:
		case SIGMA_TYPE_UNIV_V35:
			return "v35";
		case SIGMA_TYPE_SYNC_RS449:
		case SIGMA_TYPE_UNIV_RS449:
			return "rs449";
		default:
		case SIGMA_TYPE_UNIV_UNKNOWN:
			return "u";
	}
}

int init_module (void)
{
	sigma_adapter_t *b;
	sigma_chan_t *c;
	int j, ndev, i, found;
	unsigned long flags;

#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	if (busmode >= SIGMA_BUS_NORMAL && busmode <= SIGMA_BUS_FAST3)
		board_opt_dflt.fast = busmode;
	else
		printk (KERN_ERR "cx: Invalid bus mode %d\n", busmode);

	found = 0;
	if (!port[0]) {
		/*
		 * Autodetect all adapters.
		 */
		for (i = 0, j = 0; sigma_porttab[i] && j < SIGMA_NBRD; i++) {
			if (request_region (sigma_porttab[i], SIGMA_NPORT, "Cronyx Sigma-ISA")) {
				if (sigma_probe_board (sigma_porttab[i], -1, -1))
					port[j++] = sigma_porttab[i];
				release_region (sigma_porttab[i], SIGMA_NPORT);
			}
		}
	}
	if (!dma[0]) {
		/*
		 * Find available 16-bit DRQs.
		 */
		for (i = 0, j = 0; sigma_dmatab[i] && j < SIGMA_NBRD && port[j]; ++i)
			if (request_dma (sigma_dmatab[i], "Cronyx Sigma-ISA") == 0) {
				dma[j++] = sigma_dmatab[i];
				free_dma (sigma_dmatab[i]);
			}
	}
	ndev = 0;
	for (i = 0; i < SIGMA_NBRD; ++i) {
		int connected_irq = 0;

		if (!port[i])
			continue;

		if (request_region (port[i], SIGMA_NPORT, "Cronyx Sigma-ISA") == 0) {
			printk (KERN_ERR "cx: Adapter #%d - port address conflict at 0x%x\n", i, port[i]);
			continue;
		}
		local_irq_save (flags);
		if (!sigma_probe_board (port[i], -1, -1)) {
			printk (KERN_ERR "cx: Adapter #%d - no adapter at port 0x%x\n", i, port[i]);
			local_irq_restore (flags);
			release_region (port[i], SIGMA_NPORT);
			continue;
		}
		found = 1;
		local_irq_restore (flags);

		b = kzalloc (sizeof (sigma_adapter_t), GFP_KERNEL);
		if (b == NULL) {
			printk (KERN_ERR "cx: Adapter #%d - out of memory\n", i);
			break;
		}
		spin_lock_init (&b->lock);

		b->ddk.dma = dma[i];
		if (request_dma (b->ddk.dma, "Cronyx Sigma-ISA") != 0) {
			printk (KERN_ERR "cx: Adapter #%d - cannot get dma %d\n", i, b->ddk.dma);
			b->ddk.dma = 0;
			goto ballout;
		}
		disable_dma (b->ddk.dma);

		if (!sigma_open_board (&b->ddk, i, port[i], -1, b->ddk.dma)) {
			printk (KERN_ERR "cx: Adapter #%d - initialization failed\n", i);
ballout:
			if (b->binder_data.id)
				cronyx_binder_unregister_item (b->binder_data.id);
			spin_lock_irqsave (&b->lock, flags);
			sigma_close_board (&b->ddk);
			spin_unlock_irqrestore (&b->lock, flags);
			if (b->ddk.dma > 0)
				free_dma (b->ddk.dma);
			if (connected_irq > 0)
				free_irq (connected_irq, b);
			release_region (b->ddk.port, SIGMA_NPORT);
			for (j = 0; j < SIGMA_NCHAN; j++)
				if (b->buffers[j])
					kfree (b->buffers[j]);
			kfree (b);
			continue;
		}

		set_dma_mode (b->ddk.dma, DMA_MODE_CASCADE);
		b->ddk.irq = irq[i];
		if (b->ddk.irq > 0) {
			if (!cronyx_probe_irq (b->ddk.irq, b, (void (*)(void *, int)) sigma_probe_irq)) {
				printk (KERN_ERR "cx: Adapter #%d - irq %d not functional\n", i, b->ddk.irq);
				goto ballout;
			}
			sigma_init (&b->ddk, b->ddk.num, b->ddk.port, b->ddk.irq, b->ddk.dma);
			sigma_setup_board (&b->ddk, 0, 0, 0);
		} else {
			/*
			 * Find available IRQ.
			 */
			for (j = 0; sigma_irqtab[j]; ++j) {
				if (!cronyx_probe_irq (sigma_irqtab[j], b, (void (*)(void *, int)) sigma_probe_irq))
					continue;
				b->ddk.irq = sigma_irqtab[j];
				sigma_init (&b->ddk, b->ddk.num, b->ddk.port, b->ddk.irq, b->ddk.dma);
				sigma_setup_board (&b->ddk, 0, 0, 0);
				break;
			}
			if (b->ddk.irq <= 0) {
				printk (KERN_ERR "cx: Adapter #%d - no free irq for adapter at port 0x%x\n", i, port[i]);
				goto ballout;
			}
		}

		if (request_irq (b->ddk.irq, irq_handler, IRQF_DISABLED, "Cronyx Sigma-ISA", b) != 0) {
			printk (KERN_ERR "cx: Adapter #%d - cannot get irq %d\n", i, b->ddk.irq);
			b->ddk.irq = 0;
			goto ballout;
		}
		connected_irq = b->ddk.irq;
		printk (KERN_INFO "cx: Adapter #%d <Cronyx Sigma-%s> at 0x%03x irq %d dma %d\n", i, b->ddk.name, b->ddk.port,
			b->ddk.irq, b->ddk.dma);

		for (c = b->ddk.chan; c < b->ddk.chan + SIGMA_NCHAN; c++) {
			if (c->type == SIGMA_TYPE_NONE)
				continue;
			b->buffers[c->num] = kmalloc (sizeof (sigma_buf_t), GFP_KERNEL | GFP_DMA);
			if (b->buffers[c->num] == NULL) {
				printk (KERN_ERR "cx: Adapter #%d, channel #%d - out of memory\n", i, c->num);
				goto ballout;
			}
			if (isa_virt_to_bus (b->buffers[c->num]) >= 16ul * 1024 * 1024 - sizeof (sigma_buf_t)) {
				printk (KERN_ERR "cx: Adapter #%d, channel #%d - out of ISA-DMA-memory\n", i, c->num);
				goto ballout;
			}
		}

		enable_dma (b->ddk.dma);
		b->binder_data.dispatch = dispatch_tab;
		b->binder_data.type = CRONYX_ITEM_ADAPTER;
		b->binder_data.hw = b;
		b->binder_data.provider = THIS_MODULE;
		if (cronyx_binder_register_item (0, "sigma", i, NULL, -1, &b->binder_data) < 0) {
			printk (KERN_ERR "cx: unable register adapter #%d on binder\n", i);
			goto ballout;
		}
		cronyx_led_init (&b->led_flasher, &b->lock, b, sigma_led_set, sigma_led_state);
		adapters[ndev++] = b;

		for (c = b->ddk.chan; c < b->ddk.chan + SIGMA_NCHAN; c++) {
			cronyx_binder_item_t *item;

			if (c->type == SIGMA_TYPE_NONE)
				continue;

			item = &b->interfaces[c->num];
			item->dispatch = dispatch_tab;
			item->hw = c;
			c->sys = item;
			item->type = CRONYX_ITEM_INTERFACE;
			item->order = c->num;
			if (cronyx_binder_register_item
			    (b->binder_data.id, sigma_iface_name (c->type), c->num, NULL, -1, item) < 0) {
				printk (KERN_ERR "cx: unable register interface #%d adapter #%d on binder\n", c->num,
					i);
			}

			item = &b->channels[c->num];
			item->dispatch = dispatch_tab;
			item->dispatch.second_timer = sigma_second_timer;
			item->hw = c;
			c->sys = item;
			item->type = CRONYX_ITEM_CHANNEL;
			item->order = c->num;
			item->mtu = SIGMA_DMABUFSZ;
			item->fifosz = SIGMA_DMABUFSZ;
			if (cronyx_binder_register_item
			    (b->binder_data.id, NULL, c->num, "cx", b->ddk.num * SIGMA_NCHAN + c->num, item) < 0) {
				printk (KERN_ERR "cx: unable register channel #%d adapter #%d on binder\n", c->num, i);
			} else {
				spin_lock_irqsave (&b->lock, flags);
				sigma_register_transmit (c, &irq_transmit);
				sigma_register_receive (c, &irq_receive);
				sigma_register_error (c, &irq_error);
				sigma_start_chan (c, b->buffers[c->num], isa_virt_to_bus (b->buffers[c->num]));
				spin_unlock_irqrestore (&b->lock, flags);
			}
		}
	}
	if (!ndev)
		printk (KERN_ERR "cx: no %s <Cronyx Sigma-ISA> adapters are available\n", found ? "usable" : "any");
	return 0;
}

void cleanup_module (void)
{
	sigma_adapter_t *b;
	int i, j;
	unsigned long flags;

	for (i = 0; i < SIGMA_NBRD; i++) {
		b = adapters[i];
		if (b != NULL) {
			adapters[i] = NULL;
			cronyx_binder_unregister_item (b->binder_data.id);
			cronyx_led_destroy (&b->led_flasher);
			spin_lock_irqsave (&b->lock, flags);
			sigma_close_board (&b->ddk);
			spin_unlock_irqrestore (&b->lock, flags);
			free_irq (b->ddk.irq, b);
			free_dma (b->ddk.dma);
			for (j = 0; j < SIGMA_NCHAN; ++j)
				if (b->buffers[j])
					kfree (b->buffers[j]);
			release_region (b->ddk.port, SIGMA_NPORT);
			kfree (b);
		}
	}
}

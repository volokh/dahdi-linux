/*
 * DAHDI protocol layer for Cronyx serial (E1) adapters.
 *
 * Copyright (C) 2005 Cronyx Engineering.
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
 * $Id: cdahdi.c, v 1.10 2009-09-09 11:54:56 ly Exp $
 */
#include "module.h"
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/timer.h>
#include <linux/pkt_sched.h>
#include <asm/bitops.h>
#include "cserial.h"

#include <dahdi/kernel.h>
#ifndef DAHDI_ECHO_MODE_DEFINED
	#include "dahdi-echostate-def.h"
#endif

/* Module information */
MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx support for dahdi/asterisk.org protocol\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

int spareparts_mz = 0xff;

#ifdef MODULE_PARM
	MODULE_PARM (spareparts_mz, "i");
#else
	module_param (spareparts_mz, int, 0644);
#endif
MODULE_PARM_DESC(spareparts_mz, "Assume that spare bits of CAS multiframe may be zeroed");

typedef struct _ec_buf_t {
	unsigned head, offset, mask;
	char *buf[31];
} ec_buf_t;

typedef struct _dahdi_t {
	cronyx_binder_item_t *chan;
	unsigned long usemask;
	struct dahdi_chan *chans[31];

	u8 *tx;
	short io_chunk;
	short s2p_pos, p2s_pos, cas_fb, ts16_fb, fb_count;
	s8 map_ts2chan[32], map_fb2chan[32];

	struct {
		u8 abcd[32];
		u32 force;
		short lock, pos, long_lost;
	} cas_s2p;

	struct {
		short pos;
		u8 abcd[32];
	} cas_p2s;

	struct sk_buff *skb;
	struct dahdi_span span;
	struct dahdi_chan chans_data[31];
	int start_countdown, cas_countdown, recovery_countdown, error;
	int io_count, io_count_chunk, qlen_limit;
	ec_buf_t *ec;

#define RX_BACKLOG_SIZE	4
	struct sk_buff *backlog_rx[RX_BACKLOG_SIZE];
	unsigned backlog_head, backlog_tail;
	unsigned backlog_report_countdown;
} dahdi_t;

static unsigned ec_auto_qlen(dahdi_t * p, unsigned qlen)
{
	return qlen * p->io_chunk / p->fb_count + DAHDI_CHUNKSIZE;
}

static void ec_free (ec_buf_t * ec)
{
	int i;

	for (i = 0; i < 31; i++) {
		if (ec->buf[i])
			kfree(ec->buf[i]);
	}
	kfree(ec);
}

static void ec_kick (dahdi_t * p)
{
	int i;
	unsigned long flags;

	for (i = 0; i < p->span.channels; i++) {
		struct dahdi_chan *chan = p->chans[i];
		u32 pretrain = 32 + (p->ec ? p->ec->offset : 0);

		spin_lock_irqsave (&chan->lock, flags);
#ifdef DAHDI_ECHO_MODE_DEFINED
		if (p->ec && chan->chanpos != 16 && chan->ec_state && chan->ec_state->status.mode != ECHO_MODE_IDLE) {
			chan->ec_state->status.mode = ECHO_MODE_PRETRAINING;
			if (chan->ec_state->status.pretrain_timer < pretrain)
				chan->ec_state->status.pretrain_timer = pretrain;
		}
#else
		if (p->ec && chan->chanpos != 16 && chan->ec_state && chan->echostate != ECHO_STATE_IDLE) {
			chan->echostate = ECHO_STATE_PRETRAINING;
			if (chan->echotimer < pretrain)
				chan->echotimer = pretrain;
		}
#endif
		spin_unlock_irqrestore (&chan->lock, flags);
	}
}

static void ec_update (dahdi_t * p, ec_buf_t * ec)
{
	unsigned long flags;
	ec_buf_t *old_ec;

	spin_lock_irqsave (&p->span.lock, flags);
	old_ec = p->ec;
	p->ec = ec;
	spin_unlock_irqrestore (&p->span.lock, flags);

	ec_kick (p);

	if (old_ec) {
		/* LY-TODO: rcu-like, all running paths with current ec-buffers must be done. */
		set_current_state (TASK_INTERRUPTIBLE);
		schedule_timeout(DAHDI_CHUNKSIZE * 5 * HZ / 8000 + 1);
		ec_free (old_ec);
	}
}

static int ec_offset(dahdi_t * p, int offset)
{
	int i, buflen;
	ec_buf_t *ec;

	buflen = 0;

	if (offset > 0) {
		buflen = 16;
		while(buflen <= offset + DAHDI_CHUNKSIZE * 4) {
			buflen += buflen;
			if (buflen > 4096)
				return -EINVAL;
		}
	}

	if (buflen == 0) {
		ec_update (p, NULL);
		return 0;
	}

	if (p->ec != NULL && p->ec->mask == buflen - 1) {
		p->ec->offset = offset;
		return 0;
	}

	ec = kzalloc(sizeof(ec_buf_t), GFP_KERNEL);

	if (!ec)
		return -ENOMEM;

	for (i = 0; i < p->span.channels; i++) {
		if (p->chans[i]->chanpos != 16) {
			ec->buf[i] = kmalloc (buflen, GFP_KERNEL);
			if (!ec->buf[i]) {
				ec_free (ec);
				return -ENOMEM;
			}
			memset(ec->buf[i], 0xD5, buflen);
		}
	}

	ec->mask = buflen - 1;
	ec->offset = offset;
	ec_update (p, ec);
	return 0;
}

static unsigned cas_get(dahdi_t * p)
{
	unsigned byte = p->cas_s2p.lock ? 11 : 15 /* LY: remote mfas alarm */ ;

	if (p->cas_p2s.pos)
		byte = (p->cas_p2s.abcd[p->cas_p2s.pos] << 4) + p->cas_p2s.abcd[p->cas_p2s.pos + 16];
	p->cas_p2s.pos = (p->cas_p2s.pos + 1) & 15;
	return byte;
}

static inline int is_valid_mfas(u8 byte)
{
	return byte < 16 && (spareparts_mz | byte);
}

static void cas_put_abcd (dahdi_t * p, int pos, u8 abcd)
{
	u8 old_abcd = p->cas_s2p.abcd[pos];

	if (abcd == 0 && !(pos == 0 || (pos == 16 && spareparts_mz))) {
		abcd = p->cas_s2p.abcd[pos];
		if (p->cas_countdown < 3)
			p->cas_countdown++;
		if (p->cas_s2p.lock && --p->cas_s2p.lock == 0) {
			CRONYX_LOG_1(p->chan, "cdahdi: CAS sync-lost\n");
			cronyx_modem_event(p->chan);
		}
	}

	if (old_abcd != abcd || (p->cas_s2p.force & (1ul << pos))) {
		int n = p->map_ts2chan[pos];

		if (n >= 0 && p->cas_countdown == 0) {
			p->cas_s2p.force &= ~(1ul << pos);
			if (!(p->chans[n]->sig & DAHDI_SIG_CLEAR)) {
				CRONYX_LOG_1(p->chan, "cdahdi: CAS abcd-change %X->%X, ts = %d, ch = %d\n", old_abcd,
					abcd, pos, n + 1);
				if (! abcd || p->cas_s2p.long_lost > 1)
					abcd = 0xF;
				dahdi_rbsbits(p->chans[n], abcd);
			}
		}
		p->cas_s2p.abcd[pos] = abcd;
	}
}

static void cas_put(dahdi_t * p, u8 byte)
{
	u8 abcd_rebuild[32];
	int i, lock_target_pos;

#ifdef LY_DEEP_DEBUG
	static unsigned char buf[16], index;

	buf[index & 15] = byte;

	if (++index == 0) {
		printk (KERN_DEBUG "cas:");
		for (i = 0; i < 16; i++)
			printk (" %02X", buf[i]);
		printk ("\n");
	}
#endif

	if (likely(p->cas_s2p.lock)) {
		if (p->cas_s2p.pos == 0) {
			if (unlikely(!is_valid_mfas(byte))) {
				p->cas_countdown = 3;
				if (--p->cas_s2p.lock == 1) {
					/*
					 * LY: slip fast-recovery logic
					 * may be drop-slip case, looking for cas-sync in previsions cell
					 */
					if ( /*(fas_demux_state & 1) == 0 && */ p->cas_s2p.abcd[15] == 0
						&& p->cas_s2p.abcd[31] != 0) {
						lock_target_pos = 1;
						CRONYX_LOG_1(p->chan, "cdahdi: CAS slip-drop\n");
						goto cas_lock;
					}
				} else {
					CRONYX_LOG_1(p->chan, "cdahdi: CAS sync-lost\n");
					cronyx_modem_event(p->chan);
				}
			} else if (unlikely(p->cas_s2p.lock != 2)) {
				p->cas_countdown = 3;
				p->cas_s2p.lock = 2;
				p->cas_s2p.force = 0xFFFFFFFFul;
				cronyx_modem_event(p->chan);
			}
		} else if (p->cas_s2p.pos == 1 && unlikely(p->cas_s2p.lock == p->cas_s2p.pos)) {
			/*
			 * LY: slip fast-recovery logic
			 * may be repeat-slip case, looking for cas-sync in current cell
			 */
			if ( /*(fas_demux_state & 1) == 0 && */ is_valid_mfas(byte)) {
				lock_target_pos = 0;
				CRONYX_LOG_1(p->chan, "cdahdi: CAS slip-repeat\n");
				goto cas_lock;
			}
		}
	} else if (is_valid_mfas(byte) /*&& (fas_demux_state & 1) == 0 */ ) {
		if ((p->cas_s2p.abcd[p->cas_s2p.pos] | p->cas_s2p.abcd[p->cas_s2p.pos + 16]) & 15) {
			lock_target_pos = 0;
			CRONYX_LOG_1(p->chan, "cdahdi: CAS sync-lock\n");
			if (p->cas_s2p.pos != lock_target_pos) {
cas_lock:
				i = 0;
				do {
					unsigned from = (p->cas_s2p.pos + i) & 15;
					unsigned to = (lock_target_pos + i) & 15;

					abcd_rebuild[to] = p->cas_s2p.abcd[from];
					abcd_rebuild[to + 16] = p->cas_s2p.abcd[from + 16];
				} while(++i < 16);
				memcpy(p->cas_s2p.abcd, abcd_rebuild, 32);
				p->cas_s2p.pos = lock_target_pos;
			}
			p->cas_s2p.lock = 2;
			p->cas_s2p.force = 0xFFFFFFFFul;
			p->cas_countdown = 3;
			cronyx_modem_event(p->chan);
		}
	} else if (byte != 0) {
		/*
		 * LY: put AIS16 if sync loss
		 */
		byte = 0xFF;
	}

	cas_put_abcd (p, p->cas_s2p.pos, byte >> 4);
	cas_put_abcd (p, p->cas_s2p.pos + 16, byte & 15);
	p->cas_s2p.pos = (p->cas_s2p.pos + 1) & 15;

	if (p->cas_countdown > 0)
		p->cas_countdown--;
}

static void cdahdi_error (cronyx_binder_item_t *h, int errcode)
{
	dahdi_t *p = h->sw;

	if (p->start_countdown == 0) {
		p->span.parent->irqmisses++;
		CRONYX_LOG_1(h, "cdahdi: rx/tx error %x\n", errcode);
		if (p->error < errcode && (errcode == CRONYX_ERR_OVERFLOW
					|| errcode == CRONYX_ERR_OVERRUN || errcode == CRONYX_ERR_UNDERRUN))
			p->error = errcode;
	}
}

static void cdahdi_put_received (dahdi_t * p, struct sk_buff *skb)
{
	int i, fb;

#ifdef LY_DEEP_DEBUG
	static char index;

	if (++index == 0) {
		printk (KERN_DEBUG "data-rcv:");
		for (i = 0; i < skb->len; i++)
			printk (" %02X", skb->data[i]);
		printk ("\n");
	}
#endif

	i = fb = 0;
	do {
		if (fb == p->cas_fb)
			cas_put(p, skb->data[i]);
		else if (p->map_fb2chan[fb] >= 0)
			p->chans[p->map_fb2chan[fb]]->readchunk[p->s2p_pos] = skb->data[i];
		if (++fb == p->fb_count) {
			fb = 0;
			if (p->recovery_countdown > 0 && --p->recovery_countdown == 0)
				cronyx_modem_event(p->chan);

			if (++p->s2p_pos == DAHDI_CHUNKSIZE) {
				p->s2p_pos = 0;
				if (p->span.flags & DAHDI_FLAG_RUNNING) {
					int n = 0;

					do {
						struct dahdi_chan *chan = p->chans[n];

						if (chan->chanpos != 16 && chan->ec_current) {
							char ec_buf_local[DAHDI_CHUNKSIZE], *ec_buf = chan->writechunk;

							if (p->ec) {
								unsigned tail =	(p->ec->head - p->ec->offset) & p->ec->mask;
								unsigned gap = p->ec->mask + 1 - tail;

								ec_buf = p->ec->buf[n] + tail;
								if (gap < DAHDI_CHUNKSIZE) {
									memcpy(ec_buf_local, ec_buf, gap);
									memcpy(ec_buf_local + gap, p->ec->buf[n],
										DAHDI_CHUNKSIZE - gap);
									ec_buf = ec_buf_local;
								}
							}
							dahdi_ec_chunk (chan, chan->readchunk, ec_buf);
						}
					} while(++n < p->span.channels);
					dahdi_receive (&p->span);
				}
			}
		}
	} while(++i < skb->len);

	dev_kfree_skb_any(skb);
}

static void cdahdi_transmit(cronyx_binder_item_t *h)
{
	unsigned long flags;
	int i, fb;
	dahdi_t *p = h->sw;

	spin_lock_irqsave (&p->span.lock, flags);
	i = fb = 0;
	do {
		u8 byte = 0xFF;

		if (fb == p->cas_fb)
			byte = cas_get(p);
		else if (p->map_fb2chan[fb] >= 0)
			byte = p->chans[p->map_fb2chan[fb]]->writechunk[p->p2s_pos];
		p->tx[i] = byte;
		if (++fb == p->fb_count) {
			fb = 0;
			if (++p->p2s_pos == DAHDI_CHUNKSIZE) {
				p->p2s_pos = 0;
				if (p->span.maintstat != DAHDI_MAINT_LOCALLOOP &&
				(p->span.flags & DAHDI_FLAG_RUNNING)) {
					dahdi_transmit(&p->span);
					if (p->ec) {
						unsigned n = 0, gap = p->ec->mask + 1 - p->ec->head;

						if (gap > DAHDI_CHUNKSIZE)
							gap = DAHDI_CHUNKSIZE;
						do {
							struct dahdi_chan *chan = p->chans[n];

							if (chan->chanpos != 16 && chan->ec_current) {
								memcpy(p->ec->buf[n] + p->ec->head, chan->writechunk, gap);
								if (gap < DAHDI_CHUNKSIZE)
									memcpy(p->ec->buf[n], chan->writechunk + gap, DAHDI_CHUNKSIZE - gap);
							}
						} while(++n < p->span.channels);
						p->ec->head = (p->ec->head + DAHDI_CHUNKSIZE) & p->ec->mask;
					}
				}
			}
		}
	} while(++i < p->io_chunk);
	spin_unlock_irqrestore (&p->span.lock, flags);

	if (h->dispatch.transmit(h, p->skb) <= 0 && p->start_countdown == 0)
		p->span.parent->irqmisses++;

	if (likely(p->backlog_head != p->backlog_tail)) {
		cdahdi_put_received (p, p->backlog_rx[p->backlog_tail]);
		p->backlog_tail = (p->backlog_tail + 1) & (RX_BACKLOG_SIZE - 1);
	} else if (p->start_countdown == 0) {
		if (! p->backlog_report_countdown) {
			p->backlog_report_countdown = 8 * 5;
			printk (KERN_DEBUG "cdahdi: rx-backlog underflow on span %s\n", p->chan->name);
		}
		p->span.parent->irqmisses++;
	}

	if (unlikely(p->start_countdown > 0)) {
		if (--p->start_countdown == 0)
			ec_kick (p);
	} else if (--p->io_count == 0) {
		/* LY: 8Hz modem-event for update E1 statistics. */
		if (p->backlog_report_countdown)
			p->backlog_report_countdown--;
		if (p->cas_fb >= 0) {
			if (p->cas_s2p.lock < 2) {
				if (p->cas_s2p.long_lost < 2)
					p->cas_s2p.long_lost++;
				if (p->cas_s2p.long_lost == 2)
					memset(p->cas_s2p.abcd, -1, sizeof(p->cas_s2p.abcd));
			} else {
				if (p->cas_s2p.long_lost)
					--p->cas_s2p.long_lost;
			}
		}
		p->io_count = p->io_count_chunk;
		cronyx_modem_event(p->chan);
	}
}

static void cdahdi_receive (cronyx_binder_item_t *h, struct sk_buff *skb)
{
	dahdi_t *p = h->sw;

	if (skb->len != p->io_chunk && skb->len % p->fb_count != 0) {
		if (p->start_countdown == 0)
			printk (KERN_ERR "cdahdi: invalid rx-data length (%d)\n", skb->len);
		dev_kfree_skb_any(skb);
		return;
	}

	p->backlog_rx[p->backlog_head] = skb;
	p->backlog_head = (p->backlog_head + 1) & (RX_BACKLOG_SIZE - 1);

	if (unlikely(p->backlog_head == p->backlog_tail)) {
		printk (KERN_NOTICE "cdahdi: rx-backlog overflow on span %s\n", h->name);
		dev_kfree_skb_any(p->backlog_rx[p->backlog_tail]);
		p->backlog_tail = (p->backlog_tail + 1) & (RX_BACKLOG_SIZE - 1);
	}
}

/*-----------------------------------------------------------------------------*/

static void cdahdi_link_down(dahdi_t * p)
{
	p->chan->dispatch.link_down(p->chan);
	while(p->backlog_tail != p->backlog_head) {
		dev_kfree_skb_any(p->backlog_rx[p->backlog_tail]);
		p->backlog_tail = (p->backlog_tail + 1) & (RX_BACKLOG_SIZE - 1);
	}
	memset(&p->cas_s2p, 0, sizeof(p->cas_s2p));
	p->recovery_countdown = 256;
}

static int cdahdi_link_up(dahdi_t * p)
{
	long value;
	int i, error;
	unsigned delay_samples;

	p->backlog_tail = p->backlog_head = 0;

	if (cronyx_param_get(p->chan, cronyx_qlen, &value) >= 0 && value < 2)
		cronyx_param_set(p->chan, cronyx_qlen, 2);
	error = p->chan->dispatch.link_up(p->chan);

	if (error < 0)
		CRONYX_LOG_1(p->chan, "cdahdi: link-up failed %d\n", error);
	else {
		if (cronyx_param_get(p->chan, cronyx_qlen, &value) < 0)
			value = 2;
		p->start_countdown = value << 2;
		for (i = 0; i < value; i++)
			if (p->chan->dispatch.transmit(p->chan, p->skb) < 0)
				break;
		CRONYX_LOG_1(p->chan, "cdahdi: link-up auto tx-qlen %d\n", i);

		memset(&p->cas_s2p, 0, sizeof(p->cas_s2p));
		p->recovery_countdown = 256;

		if (p->start_countdown < 64)
			p->start_countdown = 64;
		p->span.alarms = 0;
		cronyx_modem_event(p->chan);

		delay_samples = ec_auto_qlen(p, i);
		if (p->ec == NULL || p->ec->offset + DAHDI_CHUNKSIZE < delay_samples / 2) {
			if (ec_offset(p, delay_samples) < 0)
				CRONYX_LOG_1(p->chan, "cdahdi: link-up update ec-delay failed\n");
			else
				CRONYX_LOG_1(p->chan, "cdahdi: link-up auto ec-delay %d samples\n", p->ec->offset);
		}
	}
	return error;
}

static int cdahdi_open(struct dahdi_chan *chan)
{
	dahdi_t *p = container_of(chan->span, dahdi_t, span);

	CRONYX_LOG_1(p->chan, "cdahdi: open(ts %d)\n", chan->chanpos);
	set_bit(chan->chanpos, &p->usemask);
	return 0;
}

static int cdahdi_close(struct dahdi_chan *chan)
{
	dahdi_t *p = container_of(chan->span, dahdi_t, span);

	CRONYX_LOG_1(p->chan, "cdahdi: close (ts %d)\n", chan->chanpos);
	clear_bit(chan->chanpos, &p->usemask);
	return 0;
}

static int cdahdi_rbsbits(struct dahdi_chan *chan, int bits)
{
	dahdi_t *p = container_of(chan->span, dahdi_t, span);

	if (p->cas_fb < 0)
		return -EINVAL;

	bits &= 15;

	if (chan->chanpos > 0 && chan->chanpos != 16 && chan->chanpos < 32
	&& (p->span.lineconfig & DAHDI_CONFIG_CCS) == 0) {
		if (bits == 0)
			bits = 1;
		p->cas_p2s.abcd[chan->chanpos] = bits;
		return 0;
	}
	return -EINVAL;
}

static int cdahdi_startup(struct file *file, struct dahdi_span *dspan)
{
	dahdi_t *p = container_of(dspan, dahdi_t, span);

	CRONYX_LOG_1(p->chan, "cdahdi: startup, flags is 0x%lx\n", dspan->flags);
	might_sleep();

	if (!p->chan->running)
		return cdahdi_link_up(p);

	return 0;
}

static int cdahdi_shutdown(struct dahdi_span *dspan)
{
	dahdi_t *p = container_of(dspan, dahdi_t, span);

	CRONYX_LOG_1(p->chan, "cdahdi: shutdown\n");
	might_sleep();

	if (p->chan->running)
		cdahdi_link_down(p);

	return 0;
}

static int cdahdi_maint(struct dahdi_span *dspan, int cmd)
{
	dahdi_t *p = container_of(dspan, dahdi_t, span);

	might_sleep();

	switch(cmd) {
	case DAHDI_MAINT_NONE:
		return cronyx_param_set(p->chan, cronyx_loop_mode | cronyx_flag_channel2link,
			CRONYX_LOOP_NONE);
	case DAHDI_MAINT_LOCALLOOP:
		return cronyx_param_set(p->chan, cronyx_loop_mode | cronyx_flag_channel2link,
			CRONYX_LOOP_LINEMIRROR);
	case DAHDI_MAINT_REMOTELOOP:
		return cronyx_param_set(p->chan, cronyx_loop_mode | cronyx_flag_channel2link,
			CRONYX_LOOP_REMOTE);
	case DAHDI_MAINT_LOOPUP:
	case DAHDI_MAINT_LOOPDOWN:
	//!		case DAHDI_MAINT_LOOPSTOP:
		return -ENOSYS;
	default:
		CRONYX_LOG_2(p->chan, "cdahdi: unknown maint command: %d\n", cmd);
		return -EINVAL;
	}
}

static int cdahdi_spanconfig(struct file *file, struct dahdi_span *dspan, struct dahdi_lineconfig *lc)
{
	dahdi_t *p = container_of(dspan, dahdi_t, span);
	long value;
	int error;

	if (cronyx_param_get(p->chan, cronyx_cas_mode | cronyx_flag_channel2link, &value) < 0)
		value = CRONYX_CASMODE_OFF;

	if (lc->lineconfig & DAHDI_CONFIG_CCS) {
		if (value > CRONYX_CASMODE_OFF || p->cas_fb >= 0 || p->ts16_fb < 0) {
			CRONYX_LOG_1(p->chan,
				"cdahdi: hardware channel CAS-mode is not compatible with span config\n");
			return -EIO;
		}
	}

	switch(lc->lineconfig & (DAHDI_CONFIG_D4 | DAHDI_CONFIG_ESF | DAHDI_CONFIG_AMI | DAHDI_CONFIG_B8ZS | DAHDI_CONFIG_HDB3)) {
	default:
		error = -EINVAL;
		break;
	case DAHDI_CONFIG_HDB3:
		error = cronyx_param_set(p->chan, cronyx_line_code | cronyx_flag_channel2link, CRONYX_HDB3);
		break;
	case DAHDI_CONFIG_AMI:
		error = cronyx_param_set(p->chan, cronyx_line_code | cronyx_flag_channel2link, CRONYX_AMI);
		break;
	}

	if (error < 0) {
		CRONYX_LOG_1(p->chan, "cdahdi: unsupported line\n");
		return error;
	}

	error = cronyx_param_set(p->chan, cronyx_crc4 | cronyx_flag_channel2link,
		(lc->lineconfig & DAHDI_CONFIG_CRC4) != 0);

	if (error < 0) {
		CRONYX_LOG_1(p->chan, "cdahdi: unable set crc4-mode\n");
		return error;
	}

	dspan->lineconfig = lc->lineconfig;
	dspan->txlevel = lc->lbo;
	dspan->rxlevel = 0;

	return 0;
}

static int cdahdi_dacs(struct dahdi_chan *chan1, struct dahdi_chan *chan2)
{
	dahdi_t *p1 = chan1->pvt;
	cronyx_binder_item_t *h1 = p1->chan;

	if (h1->dispatch.phony_dacs) {
		if (NULL == chan2) {
			h1->dispatch.phony_dacs(h1, chan1->chanpos, NULL, -1);
			return 0;
		} else {
			dahdi_t *p2 = chan2->pvt;
			cronyx_binder_item_t *h2 = p2->chan;

			if (h1->dispatch.phony_dacs == h2->dispatch.phony_dacs)
				return h1->dispatch.phony_dacs(h1, chan1->chanpos, h2, chan2->chanpos);
		}
	}
	return -EINVAL;
}

static struct dahdi_span_ops binder_span_ops =
{
		.owner = THIS_MODULE
	, .spanconfig = cdahdi_spanconfig
	, .startup = cdahdi_startup
	, .shutdown = cdahdi_shutdown
	, .maint = cdahdi_maint
	, .open = cdahdi_open
	, .close = cdahdi_close
	, .dacs = cdahdi_dacs
	, .rbsbits = cdahdi_rbsbits
};

static void InitSpanE1(struct dahdi_span *span)
{
	span->spantype = SPANTYPE_DIGITAL_E1;
	span->channels = 31;
	span->deflaw = DAHDI_LAW_ALAW;
	span->linecompat = DAHDI_CONFIG_HDB3 | DAHDI_CONFIG_AMI | DAHDI_CONFIG_CRC4 | DAHDI_CONFIG_NOTOPEN/* | DAHDI_CONFIG_CCS*/;
}

static int cdahdi_init_span(cronyx_binder_item_t *h, struct dahdi_device *ddev)
{
	dahdi_t *p;
	int i, j;
	struct cronyx_e1ts_map_t e1ts_map;
	long value;
	const char *desc;

	if (cronyx_param_set(h, cronyx_channel_mode, CRONYX_MODE_VOICE) < 0) {
		CRONYX_LOG_1(h, "cdahdi: unable set channel voice-mode\n");
		return -EIO;
	}

	if (h->dispatch.phony_get_e1ts_map) {
		if (h->dispatch.phony_get_e1ts_map(h, &e1ts_map) < 0) {
			CRONYX_LOG_1(h, "cdahdi: unable get ts-map\n");
			return -EIO;
		}
		if (h->debug > 1) {
			CRONYX_LOG_2(h, "cdahdi: e1ts_map =");
			for (i = 0; i < e1ts_map.length; i++)
				printk (" %d", e1ts_map.ts_index[i]);
			printk ("\n");
		}
		if (e1ts_map.length < 1 || e1ts_map.length > 32) {
			CRONYX_LOG_1(h, "cdahdi: invalid ts-map\n");
			return -EIO;
		}
		for (i = 0; i < e1ts_map.length; i++) {
			if (e1ts_map.ts_index[i] < 0)
				continue;
			if (e1ts_map.ts_index[i] >= 32) {
				CRONYX_LOG_1(h, "cdahdi: invalid ts-map\n");
				return -EIO;
			}
			for (j = 0; j < i; j++) {
				if (e1ts_map.ts_index[i] == e1ts_map.ts_index[j]) {
					CRONYX_LOG_1(h, "cdahdi: invalid ts-map\n");
					return -EIO;
				}
			}
		}
	} else {
		unsigned long timeslots;

		if (cronyx_param_get(h, cronyx_timeslots_use, &timeslots) < 0) {
			CRONYX_LOG_1(h, "cdahdi: unable get ts-set\n");
			return -EIO;
		}
		CRONYX_LOG_1(h, "cdahdi: timeslots = 0x%lX\n", timeslots);
		if ((timeslots & 0xFFFEFFFEul) == 0 || timeslots > 0xFFFFFFFFul) {
			CRONYX_LOG_1(h, "cdahdi: invalid ts-set\n");
			return -EIO;
		}
		i = e1ts_map.length = 0;
		do {
			if (timeslots & 1) {
				e1ts_map.ts_index[e1ts_map.length] = i;
				e1ts_map.length++;
			}
			i++;
		} while(timeslots >>= 1);
	}

	p = kzalloc(sizeof(dahdi_t), GFP_KERNEL);

	if (NULL == p)
		return -ENOMEM;

	memset(&p->cas_p2s, 15, sizeof(p->cas_p2s));
	memset(p->map_ts2chan, -1, sizeof(p->map_ts2chan));
	memset(p->map_fb2chan, -1, sizeof(p->map_fb2chan));
	p->start_countdown = -1;
	p->cas_countdown = -1;
	p->recovery_countdown = -1;
	p->ts16_fb = -1;

	for (i = 0; i < e1ts_map.length; i++) {
		if (e1ts_map.ts_index[i] == 16) {
			p->ts16_fb = i;
			break;
		}
	}

	p->cas_fb = -1;
	value = CRONYX_CASMODE_OFF;

	if (cronyx_param_get(h, cronyx_cas_mode | cronyx_flag_channel2link, &value) >= 0 && value > CRONYX_CASMODE_OFF)
		p->cas_fb = p->ts16_fb;
	CRONYX_LOG_2(h, "cdahdi: cas-mode = %ld\n", value);

	p->fb_count = e1ts_map.length;

	for (i = 0; i < e1ts_map.length; i++) {
		struct dahdi_chan *chan = &p->chans_data[p->span.channels];
		int n = e1ts_map.ts_index[i];

		if (n <= 0 || i == p->cas_fb)
			continue;

		p->chans[p->span.channels] = chan;
		if (i == p->ts16_fb) {
			chan->sigcap = DAHDI_SIG_HDLCFCS | DAHDI_SIG_CLEAR;
			snprintf(chan->name, sizeof(chan->name), "%s/CCS", h->name);
		} else {
			chan->sigcap = DAHDI_SIG_SF | DAHDI_SIG_CLEAR /* | DAHDI_SIG_DACS */;
			if (p->cas_fb >= 0)
				chan->sigcap |= DAHDI_SIG_CAS
			| DAHDI_SIG_EM_E1 | DAHDI_SIG_EM
			| DAHDI_SIG_FXSLS | DAHDI_SIG_FXSGS | DAHDI_SIG_FXSKS
			| DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_FXOKS;
			snprintf(chan->name, sizeof(chan->name), "%s/%d", h->name, n);
		}

		chan->pvt = p;
		chan->chanpos = n;
		p->map_ts2chan[n] = p->span.channels;
		p->map_fb2chan[i] = p->span.channels;
		p->span.channels++;
	}

	/* LY: select appropriate io-chunk size. */
	p->io_chunk = DAHDI_CHUNKSIZE * p->fb_count;
	while(p->io_chunk > 512 && (p->io_chunk & 1) == 0 && ((p->io_chunk >> 1) % p->fb_count) == 0)
		p->io_chunk >>= 1;
	while(cronyx_param_set(h, cronyx_mtu, p->io_chunk) < 0) {
		if (p->io_chunk < DAHDI_CHUNKSIZE * p->fb_count)
			p->io_chunk <<= 1;
		else
			p->io_chunk += DAHDI_CHUNKSIZE * p->fb_count;
		if (p->io_chunk > 1024) {
			kfree(p);
			CRONYX_LOG_1(h, "cdahdi: unable set appropriate mtu/io-chunk (%d*n)\n",
				DAHDI_CHUNKSIZE * p->fb_count);
			return -EIO;
		}
	}

	if (p->io_chunk != DAHDI_CHUNKSIZE * p->fb_count)
		printk (KERN_WARNING "cronyx-%s: cdahdi: hardware does not support DAHDI_CHUNKSIZE = %d\n", h->name,
			DAHDI_CHUNKSIZE);

	/* LY: calculate count of io-chunks for 8Hz timer. */
	p->io_count_chunk = 8000 / 8 * p->fb_count / p->io_chunk;
	p->io_count = p->io_count_chunk;

	CRONYX_LOG_2(h, "cdahdi: ts16_fb = %d, cas_fb = %d\n", p->ts16_fb, p->cas_fb);
	CRONYX_LOG_2(h, "cdahdi: fb_count = %d, io_chunk = %d, io_count_chunk = %d\n", p->fb_count, p->io_chunk,
		p->io_count_chunk);
	CRONYX_LOG_2(h, "cdahdi: span.channels = %d\n", p->span.channels);

	if (h->debug > 1) {
		CRONYX_LOG_2(h, "cdahdi: map_ts2chan =");
		for (i = 0; i < 32; i++)
			printk (" %d", p->map_ts2chan[i]);
		printk ("\n");
		CRONYX_LOG_2(h, "cdahdi: map_fb2chan =");
		for (i = 0; i < p->fb_count; i++)
			printk (" %d", p->map_fb2chan[i]);
		printk ("\n");
	}

	p->skb = dev_alloc_skb(p->io_chunk);
	CRONYX_LOG_2(h, "cdahdi: Alloc skb");
	if (!p->skb) {
		kfree(p);
		return -ENOMEM;
	}

	skb_put(p->skb, p->io_chunk);
	p->tx = p->skb->data;
	memset(p->tx, 0xFF, p->skb->len);

	strncpy(p->span.name, h->alias[0] ? h->alias : h->name, sizeof(p->span.name) - 1);
	desc = h->name;

	if (strncmp(h->alias, "ce", 2) == 0)
		desc = "Tau-PCI/32";
	else if (strncmp(h->alias, "cp", 2) == 0)
		desc = "Tau-PCI/xE1";

	CRONYX_LOG_2(h, "cdahdi: Find dev:%s",desc);

	snprintf(p->span.desc, sizeof(p->span.desc) - 1, "%s: Cronyx %s ch# %d", h->name, desc, h->order);

	CRONYX_LOG_2(h, "cdahdi: Create Dahdi span ");
	h->sw = p;
	p->chan = h;
	p->span.ops = &binder_span_ops;

/*!	if (h->dispatch.phony_dacs)
		p->span.dacs = cdahdi_dacs; */
	InitSpanE1(&p->span);
	p->span.chans = p->chans;
	p->span.lineconfig = DAHDI_CONFIG_NOTOPEN;

	if (p->cas_fb < 0) {
		if (p->ts16_fb >= 0)
			p->span.linecompat |= DAHDI_CONFIG_CCS;
	} else {
		p->span.flags = DAHDI_FLAG_RBS;
		//!		p->span.rbsbits = cdahdi_rbsbits;
	}

#if 0 /* LY: uncomment this for svn-trunk version of DAHDI. */
	p->span.owner = THIS_MODULE;
#endif

	dahdi_init_span(&p->span);

	return 0;
}

void cdahdi_uninit_span(cronyx_binder_item_t *h)
{
	dahdi_t *p = h->sw;

	if (p) {
	/* LY-TODO: might be race with span->open(). */

//!if ((p->span.flags & DAHDI_FLAG_REGISTERED) && dahdi_unregister (&p->span) < 0) {
//dahdi_unregister_device(p->span.span_device);
/*	if ((p->span.flags & DAHDI_FLAG_REGISTERED) && dahdi_unassign_span(p->span) < 0) {
		CRONYX_LOG_1(h, "cdahdi: unable detach, reject from dahdi_unregister()\n");
		return -EBUSY;
	}*/

#if 0
		/* LY: seems it is a bug in dahdi.c, our usemask may be != 0. */
		if (p->usemask) {
			CRONYX_LOG_1(h, "cdahdi: unable detach, span is used\n");
			return -EBUSY;
		}
#endif

		p->span.flags &= ~DAHDI_FLAG_RUNNING;

		h->sw = 0;
		cdahdi_link_down(p);
		if (p->skb)
			kfree_skb (p->skb);

		if (p->ec)
			ec_free (p->ec);
		kfree(p);
#if LINUX_VERSION_CODE < 0x020600
		MOD_DEC_USE_COUNT;
#else
		module_put(THIS_MODULE);
#endif
	}
}

int crdahdi_init_device(struct dahdi_device *ddev)
{
	/*!
	strncpy(p->span.parent->devicetype, desc, sizeof(p->span.parent->devicetype));
	strncpy(p->span.parent->location, "TODO: location", sizeof(p->span.parent->location));*/
	ddev->manufacturer = "Cronyx Engineering, Moscow, Russia";
//	ddev->devicetype = kasprintf(GFP_KERNEL, "FALC56 based span FPGA v%d.%d", Card->fFpgaVersion.sHigh, Card->fFpgaVersion.sLow);
//	ddev->location = kasprintf(GFP_KERNEL, "PCI Bus %02d Slot %02d", Card->fPciDev->bus->number, PCI_SLOT(Card->fPciDev->devfn));

	return 0;
}

void crdahdi_uninit_device(struct dahdi_device **ddev)
{
	if(ddev && *ddev)
	{
		dahdi_free_device(*ddev);
		*ddev=NULL;
	}
}

int cdahdi_assign_span(cronyx_binder_item_t *h, struct dahdi_device *ddev)
{
	dahdi_t *p = h->sw;
	int res = -EINVAL;

	if (p)
	{
		p->span.parent = ddev;
		p->span.spanno = 0;
		dahdi_init_span(&p->span);
		res = dahdi_assign_span(&p->span, 0, 0, 1);
		if (!res)
			list_add_tail(&p->span.device_node, &ddev->spans);
	}

	return res;
}

int cdahdi_unassign_span(cronyx_binder_item_t *h)//, struct dahdi_device *ddev)
{
	dahdi_t *p = h->sw;
	int res = -EINVAL;

	if (p) {
		res = dahdi_unassign_span(&p->span);
		if (!res)
			list_del_init(&p->span.device_node);
	}

	return res;
}

static int cdahdi_attach(cronyx_binder_item_t *h)
{
	cronyx_binder_item_t *parent = cronyx_binder_item_get_parent(h, CRONYX_ITEM_ADAPTER);

	if (h->sw != NULL)
	{
		CRONYX_LOG_2(h, "cdahdi: Already have registered protocol");
		return -EIO;
	}

	if (parent) {
		struct device *dev = parent->dispatch.get_device ? parent->dispatch.get_device(parent) : NULL;
		struct dahdi_device *ddev=parent->sw ?: dahdi_create_device();

		if (dev)
		{
		  struct pci_dev *pdev = container_of(dev, struct pci_dev, dev);
			if (pdev) {
				CRONYX_LOG_2(h, "cdahdi: PDEV Name=%d",pdev->irq);

			}
		}

		if (!parent->sw)
		{
			crdahdi_init_device(ddev);
		}

		if (cdahdi_init_span(h, ddev)) {
			CRONYX_LOG_2(h, "cdahdi: FAILED init span");
			cdahdi_uninit_span(h);
		}

		if (!parent->sw) {
			if (dahdi_register_device(ddev, dev)) {
				dev_err(dev, "Failed crdahdi to register with DAHDI.\n");
				crdahdi_uninit_device(&ddev);
			}else {
				CRONYX_LOG_2(h, "cdahdi: DAHDI Register Device");
			}
		}

		if (cdahdi_assign_span(h, ddev))
			CRONYX_LOG_2(h, "cdahdi: Can`t Assign Span");

		parent->sw=ddev;
	}

	if (h->sw != NULL) {
#if LINUX_VERSION_CODE < 0x020600
		MOD_INC_USE_COUNT;
#else
		try_module_get(THIS_MODULE);
#endif
	}

	return 0;
}

static int cdahdi_detach(cronyx_binder_item_t *h)
{
	cronyx_binder_item_t *parent = cronyx_binder_item_get_parent(h, CRONYX_ITEM_ADAPTER);

	if (h->sw == NULL)
	{
		CRONYX_LOG_2(h, "cdahdi: Already have detached protocol\n");
		return -EIO;
	}

	if (cdahdi_unassign_span(h))
		CRONYX_LOG_2(h, "cdahdi: Can`t unassign span\n");

	cdahdi_uninit_span(h);

	if (parent && parent->sw) {
		struct dahdi_device *ddev = parent->sw;

		if (list_empty(&ddev->spans)) {
			dahdi_unregister_device(ddev);
			crdahdi_uninit_device(&ddev);
			CRONYX_LOG_2(h, "cdahdi: DAHDI UnRegister Device\n");
			parent->sw = NULL;
		}else {
			struct dahdi_span *span;
			list_for_each_entry(span, &ddev->spans, device_node)
				CRONYX_LOG_2(h, "cdahdi: DAHDI Device contains %d[%s]\n", span->spanno, span->name);
		}
	}
	return 0;
}

static void __cdahdi_modem_event(cronyx_binder_item_t *h)
{
	dahdi_t *p = h->sw;

	if ((p->span.flags & DAHDI_FLAG_REGISTERED) && p->start_countdown == 0) {
		int cdahdi_alarm = 0;

		if (p->start_countdown == 0) {
			struct cronyx_ctl_t *ctl = kzalloc(sizeof(struct cronyx_ctl_t), GFP_KERNEL);

			if (p->error) {
				printk (KERN_ERR "cdahdi: buffer-slip/irq-miss condition on %s\n", h->name);
				ctl->param_id = cronyx_qlen;
				if (cronyx_ctl_get(h, ctl) >= 0 &&
					(p->qlen_limit == 0 || ctl->u.param.value < p->qlen_limit)) {
					ctl->u.param.value++;
					if (cronyx_ctl_set(h, ctl) < 0) {
						printk (KERN_WARNING "cdahdi: %s auto-grow qlen to %d on %s\n",
							"failed", (int) ctl->u.param.value, h->name);
					} else {
						printk (KERN_WARNING "cdahdi: %s auto-grow qlen to %d on %s\n",
							"success", (int) ctl->u.param.value, h->name);
						if (p->ec)
							ec_offset(p, p->ec->offset + p->io_chunk / p->fb_count);
					}
				}
				if (p->error == CRONYX_ERR_UNDERRUN)
					h->dispatch.transmit(h, p->skb);
				p->error = 0;
			}

			if (p->cas_fb >= 0 && p->cas_s2p.lock < 2) {
				if (p->cas_s2p.lock == 0 || (p->span.alarms & DAHDI_ALARM_RED))
					cdahdi_alarm |= DAHDI_ALARM_RED;
			}
			if (ctl) {
				ctl->param_id = cronyx_stat_e1 | cronyx_flag_channel2link;
				if (cronyx_ctl_get(h, ctl) >= 0) {
					if (ctl->u.stat_e1.status & (CRONYX_E1_CRC4E | CRONYX_E1_LOF | CRONYX_E1_LOMF | CRONYX_E1_LOS))
						cdahdi_alarm |= DAHDI_ALARM_RED;
					if (ctl->u.stat_e1.status & CRONYX_E1_AIS)
						cdahdi_alarm |= DAHDI_ALARM_BLUE;

					if (p->span.lineconfig & DAHDI_CONFIG_NOTOPEN) {
						int i;

						cdahdi_alarm |= DAHDI_ALARM_NOTOPEN;
						for (i = 0; i < 31; i++) {
							if (p->chans[i]->flags & (DAHDI_FLAG_OPEN/*! | DAHDI_FLAG_NETDEV*/)) {
								cdahdi_alarm &= ~DAHDI_ALARM_NOTOPEN;
								break;
							}
						}
					}
					if (cdahdi_alarm == 0) {
						if (p->span.alarms & ~DAHDI_ALARM_RECOVER)
							p->recovery_countdown = DAHDI_ALARMSETTLE_TIME * 8;
						if (p->recovery_countdown)
							cdahdi_alarm |= DAHDI_ALARM_RECOVER;
					}

					if (ctl->u.stat_e1.status & (CRONYX_E1_RA | CRONYX_E1_RDMA))
						cdahdi_alarm |= DAHDI_ALARM_YELLOW;
					p->span.count.bpv = ctl->u.stat_e1.total.bpv;
					p->span.count.crc4 = ctl->u.stat_e1.total.crce;
					p->span.count.ebit = ctl->u.stat_e1.total.rcrce;
					p->span.count.fas = ctl->u.stat_e1.total.fse;
				}
				kfree(ctl);
			}
		}
		if (p->span.alarms != cdahdi_alarm) {
			CRONYX_LOG_1(h, "cdahdi: notify alarm changes(0x%X -> 0x%X)\n", p->span.alarms, cdahdi_alarm);
			p->span.alarms = cdahdi_alarm;
			dahdi_alarm_notify(&p->span);
		}
	}
}

static void cdahdi_modem_event(cronyx_binder_item_t *h)
{
	if (h->proto->dispatch_priority)
		binder_deffered_queue1 (h, "cdahdi", &__cdahdi_modem_event);
	else
		__cdahdi_modem_event(h);
}

static int cdahdi_ctl_get(cronyx_binder_item_t *item, struct cronyx_ctl_t *ctl)
{
	if (item->type == CRONYX_ITEM_CHANNEL) {
		dahdi_t *p = item->sw;

		switch(ctl->param_id & ~cronyx_flag_channel2link) {
		case cronyx_ec_delay:
			ctl->u.param.value = p->ec ? p->ec->offset : 0;
			return 0;

		case cronyx_qlen_limit:
			if (p->qlen_limit > 0) {
				ctl->u.param.value = p->qlen_limit;
				return 0;
			}
			break;
		}
	}
	return -ENOSYS;
}

static int cdahdi_ctl_set(cronyx_binder_item_t *item, struct cronyx_ctl_t *ctl)
{
	if (item->type == CRONYX_ITEM_CHANNEL) {
		dahdi_t *p = item->sw;

		switch(ctl->param_id & ~cronyx_flag_channel2link) {
		case cronyx_ec_delay:
			CRONYX_LOG_1(item, "cdahdi: set ec-delay %ld\n", ctl->u.param.value);
			if (ctl->u.param.value < 0) {
				long qlen;

				if (cronyx_param_get(item, cronyx_qlen, &qlen) < 0)
					qlen = 2;
				ctl->u.param.value = ec_auto_qlen(p, qlen);
			}
			return ec_offset(p, ctl->u.param.value);

		case cronyx_qlen_limit:
			if (ctl->u.param.value < 0) {
				long qlen;

				if (cronyx_param_get(item, cronyx_qlen, &qlen) < 0)
					qlen = 2;
				ctl->u.param.value = qlen << 1;
			}
			if (ctl->u.param.value > 256)
				return -EINVAL;
			p->qlen_limit = ctl->u.param.value;
			return 0;

		case cronyx_loop_mode:
		case cronyx_line_code:
		case cronyx_cas_mode:
		case cronyx_timeslots_use:
		case cronyx_crc4:
		case cronyx_mtu:
			CRONYX_LOG_1(item,
				"cdahdi: can't change configuration, reconfigure dahdi/asterisk.org instead of using sconfig\n");
			return -EBUSY;
		}
	}
	return -ENOSYS;
}

static int cronyx_register_dahdi(void)
{
	//!TODO: For each binder_root find _ADAPTER && register it with spans
	return -EINVAL;
}

static int cronyx_unregister_dahdi(void)
{
	return -EINVAL;
}

static cronyx_proto_t cdahdi_tab = {
	.name = "dahdi",
	.dispatch_priority = 1,
	.chanmode_lock = 1,
	.timeslots_lock = 1,
	.mtu_lock = 1,
	.notify_receive = cdahdi_receive,
	.notify_receive_error = cdahdi_error,
	.notify_transmit_error = cdahdi_error,
	.notify_transmit_done = cdahdi_transmit,
	.notify_modem_event = cdahdi_modem_event,
	.attach = cdahdi_attach,
	.detach = cdahdi_detach,
	.ctl_set = cdahdi_ctl_set,
	.ctl_get = cdahdi_ctl_get
};

int init_module(void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	cronyx_binder_register_protocol(&cdahdi_tab);
	printk (KERN_DEBUG "cdahdi: Cronyx DAHDI/asterisk.org protocol loaded\n");
#ifdef ECHO_STATE_MAYBE_INCORRECT
	printk (KERN_NOTICE "cdahdi: The files 'dahdi.c' or 'dahdi-base.c' was not found at make-stage, \n"
		"cdahdi: definitions for ECHO_STATE_... may be incorrect!\n");
#endif
	cronyx_register_dahdi();
	return 0;
}

void cleanup_module(void)
{
	cronyx_binder_unregister_protocol (&cdahdi_tab);
	printk (KERN_DEBUG "cdahdi: Cronyx DAHDI/asterisk.org protocol unloaded\n");
	cronyx_unregister_dahdi();
}

/*
 * Packetized HDLC protocol layer for Cronyx serial adapters.
 *
 * Copyright (C) 1997-2002 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *         Victor Cherkashin <vich@cronyx.ru>: add transmit queue
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
 * $Id: packet.c,v 1.31 2009-09-04 17:10:37 ly Exp $
 */
#include "module.h"
#include <linux/timer.h>
#include <linux/poll.h>
#include "cserial.h"

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx packet-bundled HDLC protocol\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

#define MAX_READ_QUEUE	16
#define MAX_WRITE_QUEUE	16
#define MAX_TX_SIZE	512

typedef struct {
	int opened;
	struct timer_list timer;

	struct sk_buff_head rxdata;
	spinlock_t rx_queue_lock;

	struct sk_buff *txbuf;
	struct sk_buff_head txdata;
	spinlock_t tx_queue_lock;

	wait_queue_head_t rxq;
	wait_queue_head_t txq;
} packet_t;

/*
 * Put the packet into the transmit queue.
 * Optionally wait, when the transmit queue is full.
 */
static int packet_enqueue (cronyx_binder_item_t * h, int space_needed, unsigned flg)
{
	packet_t *p = h->sw;
	int error = 0;
	unsigned qlen;

	if (!p->txbuf || p->txbuf->len + space_needed <= MAX_TX_SIZE)
		return 0;

	while ((qlen = skb_queue_len (&p->txdata)) >= MAX_WRITE_QUEUE) {
		if (flg & O_NONBLOCK)
			return -EWOULDBLOCK;

		spin_unlock (&p->tx_queue_lock);
		wait_event_interruptible (p->txq, p->opened <= 0 || skb_queue_len (&p->txdata) < MAX_WRITE_QUEUE);
		spin_lock (&p->tx_queue_lock);
		if (p->opened <= 0 || signal_pending (current))
			return -EINTR;

		if (!p->txbuf || p->txbuf->len + space_needed <= MAX_TX_SIZE)
			return 0;
	}

	if (qlen == 0) {
		error = h->dispatch.transmit (h, p->txbuf);
		if (error > 0) {
			dev_kfree_skb_any (p->txbuf);
			p->txbuf = 0;
			return error;
		}
		if (error < 0)
			return error;
	}

	skb_queue_tail (&p->txdata, p->txbuf);
	p->txbuf = 0;
	return 0;
}

static void packet_timer (unsigned long arg)
{
	cronyx_binder_item_t *h = (cronyx_binder_item_t *) arg;
	packet_t *p = h->sw;

	if (p->opened) {
		/*
		 * Try to flush accumulated data.
		 */
		spin_lock (&p->tx_queue_lock);
		packet_enqueue (h, MAX_TX_SIZE, 1);
		spin_unlock (&p->tx_queue_lock);

		if (p->opened > 0) {
			/*
			 * Repeat it every tick (10 msec).
			 */
			p->timer.expires = jiffies + HZ / 100;
			p->timer.function = &packet_timer;
			p->timer.data = arg;
			add_timer (&p->timer);
		}
	}
}

/*
 * Char device implementation
 */
static int packet_open (cronyx_binder_item_t * h)
{
	packet_t *p = h->sw;
	int error;

	if (p->opened)
		return -EBUSY;

	spin_lock_init (&p->rx_queue_lock);
	spin_lock_init (&p->tx_queue_lock);
	skb_queue_head_init (&p->rxdata);
	skb_queue_head_init (&p->txdata);
	init_timer (&p->timer);
	p->txbuf = 0;

	error = h->dispatch.link_up (h);
	if (error < 0)
		return error;

	p->opened = 1;
	packet_timer ((unsigned long) h);
	return 0;
}

static void packet_release (cronyx_binder_item_t * h)
{
	packet_t *p = h->sw;
	struct sk_buff *skb;

	p->opened = -1;
	h->dispatch.link_down (h);
	del_timer_sync (&p->timer);

	while ((skb = skb_dequeue (&p->rxdata)))
		dev_kfree_skb_any (skb);

	while ((skb = skb_dequeue (&p->txdata)))
		dev_kfree_skb_any (skb);

	if (p->txbuf) {
		dev_kfree_skb_any (p->txbuf);
		p->txbuf = NULL;
	}
	p->opened = 0;
}

static int packet_read (cronyx_binder_item_t * h, unsigned flg, char *buf, int len)
{
	packet_t *p = h->sw;
	struct sk_buff *skb;

	for (;;) {
		if (p->opened <= 0)
			return -EINTR;
		spin_lock (&p->rx_queue_lock);
		skb = skb_dequeue (&p->rxdata);
		spin_unlock (&p->rx_queue_lock);

		if (skb) {
			if (len > skb->len)
				len = skb->len;
			if (copy_to_user (buf, skb->data, len) != 0) {
				dev_kfree_skb_any (skb);
				return -EFAULT;
			}
			dev_kfree_skb_any (skb);
			return len;
		}
		/*
		 * No data available
		 */
		if (flg & O_NONBLOCK)
			return -EWOULDBLOCK;
		if (wait_event_interruptible (p->rxq, p->opened <= 0 || !skb_queue_empty (&p->rxdata)))
			return -EINTR;
	}
}

static int packet_write (cronyx_binder_item_t * h, unsigned flg, const char *data, int data_len)
{
	packet_t *p = h->sw;
	struct sk_buff *skb;
	int error;

	if (data_len > h->mtu)
		data_len = h->mtu;
	if (data_len > MAX_TX_SIZE - 2)
		data_len = MAX_TX_SIZE - 2;

	/*
	 * Put user data into skb block.
	 * * Allocate buffer of max size, to allow appending additional data.
	 */
	skb = alloc_skb (MAX_TX_SIZE + 16, GFP_ATOMIC);
	if (!skb)
		return -EAGAIN;
	skb_reserve (skb, 16);
	skb->protocol = htons (ETH_P_WAN_PPP);
	if (copy_from_user (skb_put (skb, data_len), data, data_len) != 0) {
		dev_kfree_skb_any (skb);
		return -EFAULT;
	}
	skb_push (skb, 2);
	((unsigned char *) skb->data)[0] = (unsigned char) (data_len >> 8);
	((unsigned char *) skb->data)[1] = (unsigned char) data_len;

	/*
	 * Flush accumulated data.
	 */
	spin_lock (&p->tx_queue_lock);
	error = packet_enqueue (h, skb->len, flg);
	if (error < 0) {
		spin_unlock (&p->tx_queue_lock);
		dev_kfree_skb_any (skb);
		return error;
	}

	/*
	 * Append user data to transmit buffer.
	 */
	if (!p->txbuf)
		p->txbuf = skb;
	else {
		/*
		 * Append skb to txbuf.
		 */
		memcpy (skb_put (p->txbuf, skb->len), skb->data, skb->len);
		dev_kfree_skb_any (skb);
	}
	spin_unlock (&p->tx_queue_lock);
	return data_len;
}

static int packet_select (cronyx_binder_item_t * h, struct poll_table_struct *st, struct file *filp)
{
	packet_t *p = h->sw;
	int mask = 0;

	poll_wait (filp, &p->rxq, (poll_table *) st);
	poll_wait (filp, &p->txq, (poll_table *) st);
	if (!skb_queue_empty (&p->rxdata))
		mask |= POLLIN | POLLRDNORM;

	if (skb_queue_len (&p->txdata) < MAX_WRITE_QUEUE)
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

/*
 * Protocol interface implementation
 */
static void packet_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	packet_t *p = h->sw;
	struct sk_buff *pkt;
	unsigned short pkt_len;

	if (p->opened <= 0) {
		dev_kfree_skb_any (skb);
		return;
	}

	spin_lock (&p->rx_queue_lock);

	/*
	 * Unpack the data packet into separate frames.
	 */
	while (skb->len > 2) {
		/*
		 * Extract packet length.
		 */
		pkt_len = ((unsigned char *) skb->data)[1] | (((unsigned char *) skb->data)[0] << 8);
		skb_pull (skb, 2);

		/*
		 * Check packet size.
		 */
		if (pkt_len == 0 || pkt_len > skb->len)
			break;

		/*
		 * Create new packet.
		 */
		pkt = dev_alloc_skb (pkt_len);
		if (pkt) {
			pkt->protocol = htons (ETH_P_WAN_PPP);
			memcpy (skb_put (pkt, pkt_len), skb->data, pkt_len);

			/*
			 * Put it into receive queue.
			 */
			if (skb_queue_len (&p->rxdata) >= MAX_READ_QUEUE) {
				/* LY-TODO: overrun error counter. */
				dev_kfree_skb_any (skb_dequeue (&p->rxdata));
			}
			skb_queue_tail (&p->rxdata, pkt);
		}
		skb_pull (skb, pkt_len);
	}
	spin_unlock (&p->rx_queue_lock);
	dev_kfree_skb_any (skb);

	if (h->proto->fasync)
		kill_fasync (&h->proto->fasync, SIGIO, POLL_IN);
	wake_up_interruptible (&p->rxq);
}

static void packet_transmit_done (cronyx_binder_item_t * h)
{
	packet_t *p = h->sw;
	struct sk_buff *skb;

	spin_lock (&p->tx_queue_lock);
	skb = skb_dequeue (&p->txdata);
	spin_unlock (&p->tx_queue_lock);
	if (skb) {
		h->dispatch.transmit (h, skb);
		dev_kfree_skb_any (skb);
	}

	wake_up_interruptible (&p->txq);
}

static int packet_attach (cronyx_binder_item_t * h)
{
	packet_t *p;
	int error;

	error = cronyx_binder_minor_get (h, CRONYX_SVC_DIRECT);
	if (error < 0)
		return error;

	p = kzalloc (sizeof (packet_t), GFP_KERNEL);
	if (!p) {
		cronyx_binder_minor_put (h);
		return -ENOMEM;
	}
	h->sw = p;
	init_waitqueue_head (&p->rxq);
	init_waitqueue_head (&p->txq);
#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif
	return 0;
}

static int packet_detach (cronyx_binder_item_t * h)
{
	packet_t *p = h->sw;

	if (p->opened)
		return -EBUSY;
	cronyx_binder_minor_put (h);
	kfree (p);
	h->sw = 0;
	h->dispatch.link_down (h);
#if LINUX_VERSION_CODE < 0x020600
	MOD_DEC_USE_COUNT;
#else
	module_put (THIS_MODULE);
#endif
	return 0;
}

static cronyx_proto_t dispatch_tab = {
	.name = "packet",
	.chanmode_lock = 1,
	.notify_receive = packet_receive,
	.notify_transmit_done = packet_transmit_done,
	.open = packet_open,
	.release = packet_release,
	.read = packet_read,
	.write = packet_write,
	.select = packet_select,
	.attach = packet_attach,
	.detach = packet_detach
};

int init_module (void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx packetized HDLC protocol module loaded\n");
	return 0;
}

void cleanup_module (void)
{
	cronyx_binder_unregister_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx packetized HDLC protocol module unloaded\n");
}

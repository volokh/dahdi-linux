/*
 * Raw HDLC protocol layer for Cronyx serial adapters.
 *
 * Copyright (C) 1997-2002 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *         Victor Cherkashin <vich@cronyx.ru>: add transmit queue
 *
 * Copyright (C) 2001-2005 Cronyx Engineering.
 * Author: Roman Kurakin <rik@cronyx.ru>
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
 * $Id: raw.c,v 1.39 2009-09-04 17:10:37 ly Exp $
 */
#include "module.h"
#include <linux/poll.h>
#include "cserial.h"

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx raw protocol\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

#define MAX_READ_QUEUE	16
#define MAX_WRITE_QUEUE	16

typedef struct {
	int opened;
	struct sk_buff_head rxdata;
	spinlock_t rx_queue_lock;

	struct sk_buff_head txdata;
	spinlock_t tx_queue_lock;
	atomic_t tx_done;

	wait_queue_head_t rxq;
	wait_queue_head_t txq;
} raw_t;

static void raw_unlock_tx (cronyx_binder_item_t * h, unsigned long flags)
{
	raw_t *p = h->sw;
	struct sk_buff *skb;
	int flip;

	if ((flip = atomic_read (&p->tx_done)) != 0) for (;;) {
		skb = skb_dequeue (&p->txdata);
		if (! skb) {
			atomic_set (&p->tx_done, 1);
			break;
		}

		if (! h->dispatch.transmit (h, skb)) {
			atomic_sub (flip, &p->tx_done);
			skb_queue_head (&p->txdata, skb);
			break;
		}
		dev_kfree_skb_any (skb);
	}

	spin_unlock_irqrestore (&p->tx_queue_lock, flags);
}

/*
 * Put the packet into the transmit queue.
 * Optionally wait, when the transmit queue is full.
 */
static int raw_enqueue (cronyx_binder_item_t * h, struct sk_buff *skb, unsigned flg)
{
	raw_t *p = h->sw;
	int error = 0;
	unsigned qlen;
	unsigned long flags;

	spin_lock_irqsave (&p->tx_queue_lock, flags);
	while ((qlen = skb_queue_len (&p->txdata)) >= MAX_WRITE_QUEUE) {
		spin_unlock_irqrestore (&p->tx_queue_lock, flags);
		if (flg & O_NONBLOCK)
			return -EWOULDBLOCK;
		if (wait_event_interruptible (p->txq, p->opened <= 0 || skb_queue_len (&p->txdata) < MAX_WRITE_QUEUE))
			return -EINTR;
		if (p->opened <= 0)
			return -EINTR;
		spin_lock_irqsave (&p->tx_queue_lock, flags);
	}

	if (qlen == 0)
		error = h->dispatch.transmit (h, skb);

	if (error == 0)
		skb_queue_tail (&p->txdata, skb);

	raw_unlock_tx (h, flags);
	return error;
}

/*
 * Char device implementation
 */
static int raw_open (cronyx_binder_item_t * h)
{
	raw_t *p = h->sw;
	int error;

	if (p->opened)
		return -EBUSY;

	spin_lock_init (&p->rx_queue_lock);
	spin_lock_init (&p->tx_queue_lock);
	skb_queue_head_init (&p->rxdata);
	skb_queue_head_init (&p->txdata);
	atomic_set (&p->tx_done, 1);

	error = h->dispatch.link_up (h);
	if (error < 0)
		return error;

	p->opened = 1;
	return 0;
}

static void raw_release (cronyx_binder_item_t * h)
{
	raw_t *p = h->sw;
	struct sk_buff *skb;

	p->opened = -1;
	h->dispatch.link_down (h);
	wake_up_interruptible (&p->rxq);
	wake_up_interruptible (&p->txq);

	while ((skb = skb_dequeue (&p->rxdata)))
		dev_kfree_skb_any (skb);

	while ((skb = skb_dequeue (&p->txdata)))
		dev_kfree_skb_any (skb);

	p->opened = 0;
}

static int raw_read (cronyx_binder_item_t * h, unsigned flg, char *buf, int len)
{
	raw_t *p = h->sw;
	struct sk_buff *skb;
	unsigned long flags;

	for (;;) {
		if (p->opened <= 0)
			return -EINTR;
		spin_lock_irqsave (&p->rx_queue_lock, flags);
		skb = skb_dequeue (&p->rxdata);
		spin_unlock_irqrestore (&p->rx_queue_lock, flags);

		if (skb) {
			if (len > skb->len)
				len = skb->len;
			if (copy_to_user (buf, skb->data, len) != 0) {
				dev_kfree_skb_any (skb);
				return -EFAULT;
			}
			if (len < skb->len)
				len = -EFBIG;
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

static int raw_write (cronyx_binder_item_t * h, unsigned flg, const char *data, int data_len)
{
	struct sk_buff *skb;
	int error;

	if (data_len > h->mtu)
		return -EFBIG;

	/*
	 * Put user data into skb block.
	 */
	skb = dev_alloc_skb (data_len);
	if (!skb)
		return -EAGAIN;

	/* LY: dummy protocol id. */
	skb->protocol = htons (ETH_P_WAN_PPP);

	if (copy_from_user (skb_put (skb, data_len), data, data_len) != 0) {
		dev_kfree_skb_any (skb);
		return -EFAULT;
	}

	error = raw_enqueue (h, skb, flg);
	if (error) {
		dev_kfree_skb_any (skb);
		return error;
	}
	/* LY: skb is in tx-queue. */
	return data_len;
}

static int raw_select (cronyx_binder_item_t * h, struct poll_table_struct *st, struct file *filp)
{
	raw_t *p = h->sw;
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
static void raw_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	raw_t *p = h->sw;
	unsigned long flags;

	if (p->opened <= 0) {
		dev_kfree_skb_any (skb);
		return;
	}

	spin_lock_irqsave (&p->rx_queue_lock, flags);
	if (skb_queue_len (&p->rxdata) >= MAX_READ_QUEUE) {
		/* LY-TODO: overrun error counter. */
		dev_kfree_skb_any (skb_dequeue (&p->rxdata));
		CRONYX_LOG_1 (h, "raw: rxq-oveflow\n");
	}
	skb_queue_tail (&p->rxdata, skb);
	spin_unlock_irqrestore (&p->rx_queue_lock, flags);

	if (h->proto->fasync)
		kill_fasync (&h->proto->fasync, SIGIO, POLL_IN);
	wake_up_interruptible (&p->rxq);
}

static void raw_transmit_done (cronyx_binder_item_t * h)
{
	raw_t *p = h->sw;
	unsigned long flags;

	atomic_inc (&p->tx_done);
	if (likely (spin_trylock_irqsave (&p->tx_queue_lock, flags)))
		raw_unlock_tx (h, flags);

	wake_up_interruptible (&p->txq);
}

static int raw_attach (cronyx_binder_item_t * h)
{
	raw_t *p;
	int error;

	error = cronyx_binder_minor_get (h, CRONYX_SVC_DIRECT);
	if (error < 0)
		return error;

	p = kzalloc (sizeof (raw_t), GFP_KERNEL);
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

static int raw_detach (cronyx_binder_item_t * h)
{
	raw_t *p = h->sw;

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
	.name = "raw",
	.dispatch_priority = 1,
	.notify_receive = raw_receive,
	.notify_transmit_done = raw_transmit_done,
	.open = raw_open,
	.release = raw_release,
	.read = raw_read,
	.write = raw_write,
	.select = raw_select,
	.attach = raw_attach,
	.detach = raw_detach
};

int init_module (void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx raw protocol module loaded\n");
	return 0;
}

void cleanup_module (void)
{
	cronyx_binder_unregister_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx raw protocol module unloaded\n");
}

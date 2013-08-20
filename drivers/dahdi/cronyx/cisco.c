/*
 * Cisco/HDLC protocol layer for Cronyx serial adapters.
 *
 * Copyright (C) 1997-2001 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *
 * Copyright (C) 2001-2004 Cronyx Engineering.
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
 * $Id: cisco.c,v 1.33 2009-09-04 17:10:37 ly Exp $
 */
#include "module.h"
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/timer.h>
#include <linux/pkt_sched.h>
#include "cserial.h"

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx CISCO/HDLC protocol\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

typedef struct {
	struct net_device *dev;
	cronyx_binder_item_t *h;
	struct net_device_stats stats;
	struct timer_list keepalive_timer;
	u32 remote_sequence;
	u32 local_sequence;
	u8 keepalive_countdown, some_received;
} cisco_t;

/*
 * Some cisco HDLC protocol-specific defintions
 */
#define CISCO_ADDR_UNICAST	0x0ful
#define CISCO_ADDR_BCAST	0x8ful
#define CISCO_CTL		0x00ul
#define CISCO_SLARP		0x8035ul
#define CISCO_CDP		0x2000ul

/*
 * Cisco SLARP control packet structure
 */
struct slarp_pkt {
	u32 type;		/* code */
	u32 opt1, opt2;
	u16 rel;		/* reliability */
	u16 time_hi, time_lo;	/* time alive in ms */
} __attribute__ ((packed));

#define	SLARP_SIZE		14	/* LY: without timestamp */
#define	SLARP_SIZE_FULL		18	/* LY: with timestamp */
#define CISCO_HEADER_SIZE	4
#define	CISCO_EXTRA_2		2

/*
 * SLARP control packet codes
 */
#define SLARP_REQUEST		0
#define SLARP_REPLY		1
#define SLARP_KEEPALIVE		2

#define SLARP_KEEPALIVE_TIME	10	/* 10 sec */

#define CISCO_QUEUE_LEN		32	/* Length of transmit queue */

static cronyx_proto_t dispatch_tab;
static long startup;

static int cisco_dev_transmit (struct sk_buff *skb, struct net_device *dev);

/*
 * Slarp implementation
 */
static struct sk_buff *cisco_slarp_packet (struct net_device *dev, u32 type, u32 opt1, u32 opt2)
{
	struct sk_buff *skb;
	struct slarp_pkt *pkt;
	u32 uptime_ms;

	skb = dev_alloc_skb (SLARP_SIZE_FULL + CISCO_HEADER_SIZE);
	if (skb) {
		skb->priority = TC_PRIO_CONTROL;
		skb->protocol = htons (CISCO_SLARP);
		skb_reserve (skb, CISCO_HEADER_SIZE);
		*((u32*) skb_push (skb, CISCO_HEADER_SIZE)) = htonl (
			(((type == SLARP_KEEPALIVE) ? CISCO_ADDR_BCAST : CISCO_ADDR_UNICAST) << 24)
			+ (CISCO_CTL << 16) + CISCO_SLARP);
		skb_reset_mac_header (skb);

		pkt = (struct slarp_pkt *) skb_put (skb, SLARP_SIZE_FULL);
		pkt->type = htonl (type);
		pkt->opt1 = opt1;
		pkt->opt2 = opt2;
		pkt->rel = 0xFFFF;
		uptime_ms = jiffies_to_msecs (jiffies - startup);
		pkt->time_hi = htons (uptime_ms >> 16);
		pkt->time_lo = htons (uptime_ms & 0xFFFF);
		skb->dev = dev;
		skb_reset_network_header (skb);
		dev_queue_xmit (skb);
		netif_wake_queue (dev);
	}

	return skb;
}

static void cisco_slarp_reply (cronyx_binder_item_t *h)
{
	cisco_t *p = h->sw;
	struct in_device *idev = p->dev->ip_ptr;
	struct in_ifaddr *ifaddr = idev->ifa_list;

	if (cisco_slarp_packet (p->dev, SLARP_REPLY, ifaddr->ifa_address, ifaddr->ifa_mask))
		CRONYX_LOG_1 (h, "ccisco: send slarp-reply\n");
}

static void cisco_keepalive_timer (unsigned long arg)
{
	cronyx_binder_item_t *h = (cronyx_binder_item_t *) arg;
	cisco_t *p = h->sw;
	int delay = HZ;

	if (!h->running)
		return;
	if (cisco_slarp_packet (p->dev, SLARP_KEEPALIVE, htonl (++p->local_sequence), htonl (p->remote_sequence))) {
		CRONYX_LOG_1 (h, "ccisco: send slarp-keepalive\n");
		delay = SLARP_KEEPALIVE_TIME * HZ;
		if (p->some_received && p->keepalive_countdown < 0xFF)
			--p->keepalive_countdown;
	}
	if (p->keepalive_countdown) {
		p->keepalive_timer.function = &cisco_keepalive_timer;
		p->keepalive_timer.data = arg;
		p->keepalive_timer.expires = jiffies + delay;
		add_timer (&p->keepalive_timer);
	}
}

static void cisco_dump (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	int i;

	if (h->debug > 2) {
		printk (KERN_DEBUG "ccisco: %s, packet-dump: ", h->name);
		for (i = 0; i < skb->len; i++) {
			if (!(i % 4))
				printk (" ");
			if (!(i % 16))
				printk ("\n%s: ", h->name);
			printk ("%02x", skb->data[i]);
		}
		printk ("\n");
	}
}

static void cisco_slarp_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	cisco_t *p = h->sw;

	CRONYX_LOG_2 (h, "ccisco: slarp-receive\n");
	if (skb->len != SLARP_SIZE + CISCO_HEADER_SIZE /* LY: uptime is optional */
	&& skb->len != SLARP_SIZE_FULL + CISCO_HEADER_SIZE
	&& skb->len != SLARP_SIZE_FULL + CISCO_HEADER_SIZE + CISCO_EXTRA_2) {
		CRONYX_LOG_1 (h, "ccisco: got strange slarp-packet, len %d\n", (int) skb->len);
		cisco_dump (h, skb);
		p->stats.rx_dropped++;
	} else if (skb->data[1] != CISCO_CTL) {
		CRONYX_LOG_1 (h, "ccisco: got strange slarp-packet, ctrl 0x%02X\n", skb->data[1]);
		cisco_dump (h, skb);
		p->stats.rx_dropped++;
	} else {
		struct slarp_pkt *slp = (struct slarp_pkt *) (skb->data + CISCO_HEADER_SIZE);
		switch (ntohl (slp->type)) {
			case SLARP_REQUEST:
				CRONYX_LOG_1 (h, "ccisco: got slarp-request\n");
				cisco_slarp_reply (h);
				p->some_received = 1;
				p->stats.rx_bytes += skb->len;
				p->stats.rx_packets++;
				break;

			case SLARP_REPLY:
				CRONYX_LOG_1 (h, "ccisco: got unexpected slarp-reply\n");
				p->stats.rx_dropped++;
				break;

			case SLARP_KEEPALIVE:
				CRONYX_LOG_1 (h, "ccisco: got slarp-keepalive\n");
				p->keepalive_countdown = 0xFF;
				p->remote_sequence = ntohl (slp->opt1);
				if (p->remote_sequence == p->local_sequence) {
					printk (KERN_NOTICE "ccisco: %s loopback?\n", h->name);
					p->local_sequence = jiffies;
				}
				if (! timer_pending (&p->keepalive_timer))
					cisco_keepalive_timer ((unsigned long) h);
				p->stats.rx_bytes += skb->len;
				p->stats.rx_packets++;
				break;

			default:
				CRONYX_LOG_1 (h, "ccisco: got strange slarp-packet, type 0x%lX\n",
					(unsigned long) ntohl (slp->type));
				cisco_dump (h, skb);
				p->stats.rx_dropped++;
				break;
		}
	}
	dev_kfree_skb_any (skb);
}

static int cisco_dev_transmit (struct sk_buff *skb, struct net_device *dev)
{
	cisco_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;

	CRONYX_LOG_2 (h, "ccisco: transmit\n");
	/*
	 * Net layer thinks we're broken. Restart the channel
	 */
	if (netif_queue_stopped (dev)) {
		if (time_before (jiffies, dev->trans_start + 10 * HZ))
			return 1;
		printk (KERN_ERR "ccisco: %s transmitter timeout\n", dev->name);
		p->stats.tx_dropped++;
		netif_start_queue (dev);
		dev->trans_start = jiffies;
	}

	/*
	 * if card is full, return unsuccessfuly, device is busy.
	 */
	if (h->dispatch.transmit (h, skb) <= 0) {
		netif_stop_queue (dev);
		return 1;
	}

	/*
	 * Packet was queued onto card.
	 */
	p->stats.tx_bytes += skb->len;
	p->stats.tx_packets++;
	dev->trans_start = jiffies;
	dev_kfree_skb_any (skb);
	return 0;
}

static struct net_device_stats *cisco_dev_getstats (struct net_device *dev)
{
	cisco_t *p = netdev_priv (dev);

	return &p->stats;
}

static int cisco_dev_up (struct net_device *dev)
{
	cisco_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;
	int error;

	CRONYX_LOG_2 (h, "ccisco: up\n");
	error = h->dispatch.link_up (h);
	if (error)
		return error;

	memset (&p->stats, 0, sizeof (p->stats));
	netif_start_queue (dev);
	p->remote_sequence = 0;
	p->local_sequence = jiffies;
	init_timer (&p->keepalive_timer);
	p->some_received = 0;
	p->keepalive_countdown = 5; /* LY: send no more than 5 keepalives w/o receive ones from remote. */
	cisco_keepalive_timer ((unsigned long) h);
	return 0;
}

static int cisco_dev_down (struct net_device *dev)
{
	cisco_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;

	CRONYX_LOG_2 (h, "ccisco: down\n");
	netif_stop_queue (dev);
	h->dispatch.link_down (h);
	del_timer_sync (&p->keepalive_timer);
	return 0;
}

static void cisco_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	cisco_t *p = h->sw;
	int proto;

	CRONYX_LOG_2 (h, "ccisco: receive\n");
	if (skb->len < CISCO_HEADER_SIZE) {
		CRONYX_LOG_1 (h, "ccisco: received short packet\n");
		cisco_dump (h, skb);
		p->stats.rx_dropped++;
		dev_kfree_skb_any (skb);
	} else if (skb->data[0] != CISCO_ADDR_UNICAST && skb->data[0] != CISCO_ADDR_BCAST) {
		CRONYX_LOG_1 (h, "ccisco: received packet with unknown addr 0x%02X\n", skb->data[0]);
		cisco_dump (h, skb);
		p->stats.rx_dropped++;
		dev_kfree_skb_any (skb);
	} else {
		p->dev->last_rx = jiffies;
		proto = ntohs (*((u16 *) &skb->data[2]));
		switch (proto) {
 			default:
				p->some_received = 1;
				p->stats.rx_bytes += skb->len;
				p->stats.rx_packets++;
				skb->dev = p->dev;
				skb->protocol = htons (proto);
				skb_reset_mac_header (skb);
				cisco_dump (h, skb);
				skb_pull (skb, CISCO_HEADER_SIZE);
				netif_rx (skb);
				break;
			case CISCO_SLARP:
				cisco_slarp_receive (h, skb);
				break;
			case CISCO_CDP:
				CRONYX_LOG_2 (h, "ccisco: received CDP packet, use \"no cdp enable\" on cisco-router.\n");
				cisco_dump (h, skb);
				p->stats.rx_dropped++;
				dev_kfree_skb_any (skb);
				break;
		}
	}
}

static void cisco_receive_error (cronyx_binder_item_t * h, int errcode)
{
	cisco_t *p = h->sw;

	p->stats.rx_errors++;
}

static void cisco_transmit_done (cronyx_binder_item_t * h)
{
	cisco_t *p = h->sw;

	CRONYX_LOG_2 (h, "ccisco: transmit-done\n");
	netif_wake_queue (p->dev);
}

static int cisco_dev_change_mtu (struct net_device *dev, int new_mtu)
{
	cisco_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;

	CRONYX_LOG_2 (h, "ccisco: change mtu\n");
	if (new_mtu < 64 || new_mtu + CISCO_HEADER_SIZE > h->mtu)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static int cisco_dev_header (struct sk_buff *skb, struct net_device *dev,
#if LINUX_VERSION_CODE < 0x020618
	unsigned short type, void *daddr, void *saddr,
#else
	unsigned short type, const void *daddr, const void *saddr,
#endif
	unsigned len)
{
	*((u32*) skb_push (skb, CISCO_HEADER_SIZE)) = htonl (
		(((type == SLARP_KEEPALIVE) ? CISCO_ADDR_BCAST : CISCO_ADDR_UNICAST) << 24)
		+ (CISCO_CTL << 16) + type);
	return CISCO_HEADER_SIZE;
}

#if LINUX_VERSION_CODE < 0x020618

static int cisco_dev_rebuild_header (struct sk_buff *skb)
{
	return 0;
}

#else

static const struct header_ops cisco_header_ops = {
	.create = cisco_dev_header
};

#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
static const struct net_device_ops cisco_netdev_ops =
{
	.ndo_open = cisco_dev_up,
	.ndo_stop = cisco_dev_down,
	.ndo_start_xmit = cisco_dev_transmit,
	.ndo_get_stats = cisco_dev_getstats,
	.ndo_change_mtu = cisco_dev_change_mtu,
};
#endif

static void cisco_dev_setup (struct net_device *dev)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	dev->open = cisco_dev_up;
	dev->stop = cisco_dev_down;
	dev->hard_start_xmit = cisco_dev_transmit;
	dev->get_stats = cisco_dev_getstats;
	dev->change_mtu = cisco_dev_change_mtu;
#else
	dev->netdev_ops = &cisco_netdev_ops;
#endif
#if LINUX_VERSION_CODE < 0x020618
	dev->hard_header = cisco_dev_header;
	dev->rebuild_header = cisco_dev_rebuild_header;
#else
	dev->header_ops = &cisco_header_ops;
#endif
	dev->type = ARPHRD_HDLC;
	dev->hard_header_len = CISCO_HEADER_SIZE;
	dev->mtu = 1500;
	dev->addr_len = 0;
	dev->tx_queue_len = CISCO_QUEUE_LEN;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->destructor = free_netdev;
	dev_init_buffers (dev);
}

static int cisco_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	if (ctl->param_id == cronyx_channel_mode) {
		if (ctl->u.param.value == CRONYX_MODE_HDLC)
			return 0;
		return -EBUSY;
	}
	return -ENOSYS;
}

static int cisco_attach (cronyx_binder_item_t * h)
{
	cisco_t *p;
	struct net_device *dev;

	CRONYX_LOG_2 (h, "ccisco: attach\n");
	if (cronyx_param_set (h, cronyx_channel_mode, CRONYX_MODE_HDLC) < 0) {
		CRONYX_LOG_1 (h, "ccisco: unable set channel hdlc-mode");
		return -EIO;
	}

	dev = alloc_netdev (sizeof (cisco_t), h->alias[0] ? h->alias : h->name, cisco_dev_setup);
	if (!dev)
		return -ENOMEM;

	p = netdev_priv (dev);
	p->dev = dev;
	p->h = h;
	h->sw = p;
	dev->mtu = h->mtu - CISCO_HEADER_SIZE;
	if (register_netdev (dev) != 0) {
		printk (KERN_ERR "ccisco: unable to register net device %s/%s\n", h->name, h->alias);
		h->sw = 0;
		free_netdev (dev);
		return -EINVAL;
	}
#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif
	return 0;
}

static int cisco_detach (cronyx_binder_item_t * h)
{
	cisco_t *p = h->sw;

	CRONYX_LOG_2 (h, "ccisco: detach\n");
	if (netif_running (p->dev)) {
		printk (KERN_ERR "ccisco: device %s is busy\n", h->name);
		return -EBUSY;
	}
	unregister_netdev (p->dev);
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
	.name = "cisco",
	.chanmode_lock = 1,
	.mtu_lock = 1,
	.notify_receive = cisco_receive,
	.notify_receive_error = cisco_receive_error,
	.notify_transmit_done = cisco_transmit_done,
	.attach = cisco_attach,
	.detach = cisco_detach,
	.ctl_set = cisco_ctl_set
};

int init_module (void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	startup = jiffies;
	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx CISCO/HDLC protocol module loaded\n");
	return 0;
}

void cleanup_module (void)
{
	cronyx_binder_unregister_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx CISCO/HDLC protocol module unloaded\n");
}

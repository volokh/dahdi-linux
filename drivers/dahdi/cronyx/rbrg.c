/*
* Remote bridge for Cronyx serial adapters.
*
* Copyright (C) 2000-2002 Cronyx Engineering.
* Author: Victor Cherkashin <vich@cronyx.ru>
*
* Copyright (C) 2006-2009 Cronyx Engineering.
* Author: Leo Yuriev <ly@cronyx.ru>
*
* This source is derived from
* Cisco/HDLC protocol layer for Cronyx serial adapters
* by Serge Vakulenko <vak@cronyx.ru>
*
* This software is distributed with NO WARRANTIES, not even the implied
* warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* Authors grant any other persons or organisations permission to use
* or modify this software as long as this message is kept with the software,
* all derivative works or modified versions.
*
* $Id: rbrg.c,v 1.24 2009-09-04 17:10:37 ly Exp $
*/

#include "module.h"
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/pkt_sched.h>
#include <linux/etherdevice.h>
#include "cserial.h"

/*
* Module information
*/
MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx remote bridge\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

typedef struct {
	struct net_device *dev;
	cronyx_binder_item_t *h;
	struct net_device_stats stats;
} rbridge_t;

#define RBRIDGE_QUEUE_LEN           32	/* lenght of transmit queue */

/*
* default ethernet MAC address from Cronyx Enginneering "INDIVIDUAL" LAN
* MAC Address Block
*/
unsigned char default_mac_address[ETH_ALEN] = { 0x00, 0x50, 0xC2, 0x06, 0x28, 0x00 };

/*
* change device mac address (change only 11 bits)
*/
static int rbridge_mac_addr (struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (netif_running (dev))
		return -EBUSY;

	dev->dev_addr[ETH_ALEN - 2] &= 0xF8;
	dev->dev_addr[ETH_ALEN - 2] |= addr->sa_data[ETH_ALEN - 2] & 0x07;
	dev->dev_addr[ETH_ALEN - 1] = addr->sa_data[ETH_ALEN - 1];

	return 0;
}

/*
* change device MTU
*/
static int rbridge_change_mtu (struct net_device *dev, int mtu)
{
	rbridge_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;

	if (mtu < 68 || mtu > h->mtu - ETH_HLEN)
		return -EINVAL;

	dev->mtu = mtu;
	return 0;
}

/*
* Network device implementation
*/
static int rbridge_dev_transmit (struct sk_buff *skb, struct net_device *dev)
{
	rbridge_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;

	/*
	 * Net layer thinks we're broken. Restart the channel
	 */
	if (netif_queue_stopped (dev)) {
		if (time_before (jiffies, dev->trans_start + 10 * HZ))
			return 1;
		printk (KERN_ERR "crbrg: %s transmitter timeout\n", dev->name);
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

static struct net_device_stats *rbridge_dev_getstats (struct net_device *dev)
{
	rbridge_t *p = netdev_priv (dev);

	return &p->stats;
}

static int rbridge_dev_up (struct net_device *dev)
{
	rbridge_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;
	int error;

	error = h->dispatch.link_up (h);
	if (error)
		return error;

	memset (&p->stats, 0, sizeof (p->stats));
	netif_start_queue (dev);
	return 0;
}

static int rbridge_dev_down (struct net_device *dev)
{
	rbridge_t *p = netdev_priv (dev);
	cronyx_binder_item_t *h = p->h;

	netif_stop_queue (dev);
	h->dispatch.link_down (h);
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
static const struct net_device_ops rbridge_netdev_ops =
{
	.ndo_open = rbridge_dev_up,
	.ndo_stop = rbridge_dev_down,
	.ndo_start_xmit = rbridge_dev_transmit,
	.ndo_get_stats = rbridge_dev_getstats,
	.ndo_change_mtu = rbridge_change_mtu,
	.ndo_set_mac_address = rbridge_mac_addr,
};
#endif

static void rbridge_dev_setup (struct net_device *dev)
{
	ether_setup (dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	dev->open = rbridge_dev_up;
	dev->stop = rbridge_dev_down;
	dev->change_mtu = rbridge_change_mtu;
	dev->set_mac_address = rbridge_mac_addr;
	dev->hard_start_xmit = rbridge_dev_transmit;
	dev->get_stats = rbridge_dev_getstats;
#else
	dev->netdev_ops = &rbridge_netdev_ops;
#endif
	dev->tx_queue_len = RBRIDGE_QUEUE_LEN;
	dev->destructor = free_netdev;
	dev_init_buffers (dev);
}

/*
* Protocol interface implementation
*/
static void rbridge_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	rbridge_t *p = h->sw;

	skb->dev = p->dev;
	skb->protocol = eth_type_trans (skb, p->dev);
	p->stats.rx_bytes += skb->len;
	p->stats.rx_packets++;
	p->dev->last_rx = jiffies;
	netif_rx (skb);
}

static void rbridge_receive_error (cronyx_binder_item_t * h, int errcode)
{
	rbridge_t *p = h->sw;

	p->stats.rx_errors++;
}

static void rbridge_transmit_done (cronyx_binder_item_t * h)
{
	rbridge_t *p = h->sw;

	netif_wake_queue (p->dev);
}

static int rbridge_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	if (ctl->param_id == cronyx_channel_mode) {
		if (ctl->u.param.value == CRONYX_MODE_HDLC)
			return 0;
		return -EBUSY;
	}
	return -ENOSYS;
}

static int rbridge_attach (cronyx_binder_item_t * h)
{
	rbridge_t *p;
	struct net_device *dev;

	CRONYX_LOG_2 (h, "crbrg.attach\n");
	if (cronyx_param_set (h, cronyx_channel_mode, CRONYX_MODE_HDLC) < 0) {
		CRONYX_LOG_1 (h, "crbrg: unable set channel hdlc-mode");
		return -EIO;
	}

	dev = alloc_netdev (sizeof (rbridge_t), h->alias[0] ? h->alias : h->name, rbridge_dev_setup);
	if (!dev)
		return -ENOMEM;

	/*
	 * set ethernet MAC address
	 */
	memcpy (dev->dev_addr, default_mac_address, ETH_ALEN - 1);
	dev->dev_addr[ETH_ALEN - 1] = (unsigned char) h->id;
	dev->dev_addr[ETH_ALEN - 2] = (unsigned char) (h->id >> 8);
	dev->dev_addr[ETH_ALEN - 3] = (unsigned char) (h->id >> 16);

	p = netdev_priv (dev);
	p->dev = dev;
	p->h = h;
	h->sw = p;
	dev->mtu = h->mtu - ETH_HLEN;
	if (register_netdev (dev) != 0) {
		printk (KERN_ERR "crbrg: unable to register net device %s/%s\n", h->name, h->alias);
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

static int rbridge_detach (cronyx_binder_item_t * h)
{
	rbridge_t *p = h->sw;

	if (netif_running (p->dev)) {
		printk (KERN_ERR "crbrg: device %s is busy\n", h->name);
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
	.name = "rbrg",
	.chanmode_lock = 1,
	.mtu_lock = 1,
	.notify_receive = rbridge_receive,
	.notify_receive_error = rbridge_receive_error,
	.notify_transmit_done = rbridge_transmit_done,
	.attach = rbridge_attach,
	.detach = rbridge_detach,
	.ctl_set = rbridge_ctl_set
};

int init_module (void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx remote bridge protocol module loaded\n");
	return 0;
}

void cleanup_module (void)
{
	cronyx_binder_unregister_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx remote bridge protocol module unloaded\n");
}

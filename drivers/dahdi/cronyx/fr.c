/*
 * Frame Relay protocol layer for Cronyx serial adapters.
 *
 * Copyright (C) 1997-2001 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *
 * Copyright (C) 2001-2005 Cronyx Engineering.
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
 * $Id: fr.c,v 1.41 2009-09-04 17:10:37 ly Exp $
 */
#include "module.h"
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/timer.h>
#include <linux/pkt_sched.h>

#include "cserial.h"

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx Frame Relay protocol driver " CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

#define FR_QUEUE_LEN 64

typedef struct _dlci_t {
	struct net_device *dev;
	struct net_device_stats stats;
	struct _fr_t *proto;
	unsigned id;
	unsigned in_seq;
	struct sk_buff *in_frf12;
	unsigned out_seq, out_done;
	struct sk_buff *out_frf12;
	u8 status;
} dlci_t;

typedef struct _fr_t {
	cronyx_binder_item_t *chan;
	struct timer_list timer;
	u8 myseq;
	u8 yourseen;
	u8 lmi_dlci1023;
	u8 last_keepalive;
	unsigned long tbusy;
	int ndev_active;
	dlci_t *pdlci[1023 - 16];
} fr_t;

#define FR_UI		0x03
#define FR_IP		0xCC
#define FR_PADDING	0x00
#define FR_SIGNALING	0x08
#define FR_SNAP		0x80
#define FR_FRF12	0xB1

#define NLPID(high,low)	(((high)<<8) + (low))
#define NLPID_CDP	0x2000
#define NLPID_CISCO_BRIDGE	0x6558
#define NLPID_FRF12	NLPID (FR_UI, FR_FRF12)
#define NLPID_IP	NLPID (FR_UI, FR_IP)

/*
 * Signaling message types.
 */
#define FR_MSG_ENQUIRY  0x75	/* status enquiry */
#define FR_MSG_STATUS   0x7d	/* status */

/*
 * Message field types.
 */
#define FR_FLD_RTYPE    0x01	/* report type */
#define FR_FLD_VERIFY   0x03	/* link verification */
#define FR_FLD_PVC      0x07	/* PVC status */
#define FR_FLD_LSHIFT5  0x95	/* locking shift 5 */

/*
 * Report types.
 */
#define FR_RTYPE_FULL   0	/* full status */
#define FR_RTYPE_SHORT  1	/* link verification only */
#define FR_RTYPE_SINGLE 2	/* single PVC status */

/*
 * PVC status field.
 */
#define FR_DLCI_DELETE	0x04	/* PVC is deleted */
#define FR_DLCI_ACTIVE	0x02	/* PVC is operational */
#define FR_DLCI_NEW	0x08	/* PVC is new */

#define STATUS_ENQUIRY_SIZE 14
#define ARPOP_INVREQ    8
#define ARPOP_INVREPLY  9

struct arp_req {
	u16 htype;		/* hardware type = ARPHRD_DLCI */
	u16 ptype;		/* protocol type = ETH_P_IP */
	u8 halen;		/* hardware address length = 2 */
	u8 palen;		/* protocol address length = 4 */
	u16 op;			/* ARP/RARP/InARP request/reply */
	u16 hsource;		/* hardware source address */
	u16 psource1;		/* protocol source */
	u16 psource2;
	u16 htarget;		/* hardware target address */
	u16 ptarget1;		/* protocol target */
	u16 ptarget2;
};

static int fr_dev_transmit (struct sk_buff *skb, struct net_device *dev);
static void fr_receive (cronyx_binder_item_t * h, struct sk_buff *skb);
static void fr_frf12_transmit (dlci_t * dlci);
static void fr_frf12_receive (dlci_t * dlci, struct sk_buff *skb);

static void dump (const char *title, u8 * p, int l)
{
	printk ("%s: %02x", title, *p++);
	while (--l > 0)
		printk ("-%02x", *p++);
	printk ("\n");
}

static dlci_t *fr_find_dlci (fr_t * p, int id)
{
	if (id < 16 || id >= 1023)
		return 0;
	return p->pdlci[id - 16];
}

static dlci_t *fr_find_first_dlci (fr_t * p)
{
	int id;

	for (id = 16; id < 1023; ++id)
		if (p->pdlci[id - 16])
			return p->pdlci[id - 16];
	return 0;
}

/*
 * Arp implementation
 */
static void fr_arp (dlci_t * dlci, struct net_device *dev, struct arp_req *req, u16 his_hardware_address)
{
	struct sk_buff *skb;
	struct arp_req *reply;
	u8 *hdr;
	u16 my_hardware_address, reply_op;
	u32 his_ip_address, my_ip_address;
	struct in_device *idev;
	cronyx_binder_item_t *h = dlci->proto->chan;

	if ((ntohs (req->htype) != ARPHRD_DLCI && ntohs (req->htype) != 16) ||	/* for BayNetworks routers */
	    ntohs (req->ptype) != ETH_P_IP) {
		CRONYX_LOG_1 (h, "cfr: invalid ARP hardware/protocol type = 0x%x/0x%x\n", ntohs (req->htype),
			      ntohs (req->ptype));
		return;
	}
	if (req->halen != 2 || req->palen != 4) {
		CRONYX_LOG_1 (h, "cfr: invalid ARP hardware/protocol address length = %d/%d\n", req->halen, req->palen);
		return;
	}
	switch (ntohs (req->op)) {
		default:
			CRONYX_LOG_1 (h, "cfr: invalid ARP op = 0x%x\n", ntohs (req->op));
			return;

		case ARPOP_INVREPLY:
		case ARPOP_REPLY:
			/*
			 * Ignore.
			 */
			return;

		case ARPOP_INVREQ:
			reply_op = ARPOP_INVREPLY;
			break;

		case ARPOP_REQUEST:
			reply_op = ARPOP_REPLY;
			break;
	}
	my_hardware_address = ntohs (req->htarget);
	his_ip_address = ntohs (req->psource1) << 16 | ntohs (req->psource2);
	my_ip_address = ntohs (req->ptarget1) << 16 | ntohs (req->ptarget2);

	CRONYX_LOG_1 (h,
		      "cfr: got ARP request, source=0x%04x/%d.%d.%d.%d, target=0x%04x/%d.%d.%d.%d\n",
		      ntohs (req->hsource), (u8) (his_ip_address >> 24),
		      (u8) (his_ip_address >> 16),
		      (u8) (his_ip_address >> 8), (u8) his_ip_address,
		      my_hardware_address, (u8) (my_ip_address >> 24),
		      (u8) (my_ip_address >> 16), (u8) (my_ip_address >> 8), (u8) my_ip_address);

	idev = dev->ip_ptr;
	my_ip_address = ntohl (idev->ifa_list->ifa_address);

	if (!my_ip_address)
		return;		/* nothing to reply */

	/*
	 * Send the Inverse ARP reply.
	 */
	skb = dev_alloc_skb (sizeof (struct arp_req) + 10);
	if (!skb)
		return;
	skb->dev = dev;
	skb_reset_mac_header (skb);

	hdr = skb_put (skb, 10);
	hdr[0] = his_hardware_address >> 8;
	hdr[1] = his_hardware_address;
	hdr[2] = FR_UI;
	hdr[3] = FR_PADDING;
	hdr[4] = FR_SNAP;
	hdr[5] = 0;
	hdr[6] = 0;
	hdr[7] = 0;
	*(u16 *) (hdr + 8) = htons (ETH_P_ARP);

	reply = (struct arp_req *) skb_put (skb, sizeof (struct arp_req));
	reply->htype = htons (ARPHRD_DLCI);
	reply->ptype = htons (ETH_P_IP);
	reply->halen = 2;
	reply->palen = 4;
	reply->op = htons (reply_op);
	reply->hsource = htons (my_hardware_address);
	reply->psource1 = htonl (my_ip_address);
	reply->psource2 = htonl (my_ip_address) >> 16;
	reply->htarget = htons (his_hardware_address);
	reply->ptarget1 = htonl (his_ip_address);
	reply->ptarget2 = htonl (his_ip_address) >> 16;

	CRONYX_LOG_1 (h,
		      "cfr: send ARP reply, source=0x%04x/%d.%d.%d.%d, target=0x%04x/%d.%d.%d.%d\n",
		      my_hardware_address, (u8) (my_ip_address >> 24),
		      (u8) (my_ip_address >> 16),
		      (u8) (my_ip_address >> 8), (u8) my_ip_address,
		      his_hardware_address, (u8) (his_ip_address >> 24),
		      (u8) (his_ip_address >> 16), (u8) (his_ip_address >> 8), (u8) his_ip_address);

	if (h->debug > 2)
		dump ("cfr-snd", hdr, skb->len);
	skb->priority = TC_PRIO_CONTROL;
	skb_reset_network_header (skb);
	dev_queue_xmit (skb);
	netif_wake_queue (dev);
}

/*
 * Frame relay signaling implementation
 * Process the input signaling packet (DLCI 0).
 * The implemented protocol is ANSI T1.617 Annex D.
 */
static void fr_signal (cronyx_binder_item_t * h, u8 * hdr, int len)
{
	fr_t *p = h->sw;
	dlci_t *dlci, *dlci0;
	u8 *s;
	int id;
	struct sk_buff *skb;
	struct net_device *dev;

	/*
	 * Find the first configured DLCI.
	 * * When no DLCIs configured - ignore all
	 * * incoming signaling packets.
	 */
	dlci0 = fr_find_first_dlci (p);
	if (!dlci0)
		return;

#if LINUX_VERSION_CODE > 0x20139
	dlci0->stats.rx_bytes += len;
#endif
	dlci0->stats.rx_packets++;
	dev = dlci0->dev;

	if (hdr[2] != FR_UI || hdr[4] != 0
	|| (hdr[3] != FR_SIGNALING && hdr[3] != 0x09)) {
		CRONYX_LOG_1 (h, "cfr: invalid signaling header\n");
	      ballout:
		dlci0->stats.rx_errors++;
		return;
	}
	if (hdr[5] == FR_MSG_ENQUIRY) {
		if (len != STATUS_ENQUIRY_SIZE - 1) {
			CRONYX_LOG_1 (h, "cfr: invalid status-enquiry message\n");
			return;
		}
		p->yourseen = hdr[12];
		if (p->yourseen == p->myseq) {
			printk (KERN_NOTICE "cfr: %s loopback detected\n", h->name);
			p->myseq += jiffies;
		}

		skb = dev_alloc_skb (STATUS_ENQUIRY_SIZE);
		if (!skb)
			return;
		/*
		 * Prepare the link verification signaling packet,
		 * * according to T1.617 Annex D..
		 */
		skb->dev = dev;
		skb_reset_mac_header (skb);
		s = skb_put (skb, STATUS_ENQUIRY_SIZE);

		if (!p->lmi_dlci1023) {
			s[0] = 0;	/* DLCI = 0 */
			s[1] = 1;
		} else {
			s[0] = 0xFC;	/* LY: DLCI = 1023 */
			s[1] = 0xF1;
		}
		s[2] = FR_UI;
		s[3] = FR_SIGNALING;	/* NLPID = UNI call control */

		s[4] = 0;	/* call reference length = 0 */
		s[5] = FR_MSG_STATUS;	/* message type = status */

		s[6] = FR_FLD_LSHIFT5;	/* locking shift 5 */

		s[7] = FR_FLD_RTYPE;	/* report type field */
		s[8] = 1;	/* report type length = 1 */
		if (p->myseq % 6)
			s[9] = FR_RTYPE_SHORT;	/* link verification only */
		else
			s[9] = FR_RTYPE_FULL;	/* full status needed */

		s[10] = FR_FLD_VERIFY;	/* link verification type field */
		s[11] = 2;	/* link verification field length = 2 */
		s[12] = ++p->myseq;	/* our sequence number */
		s[13] = p->yourseen;	/* last received sequence number */

		CRONYX_LOG_1 (p->chan, "cfr: send lmi-status packet, myseq=%d, yourseen=%d\n", p->myseq, p->yourseen);

		if (h->debug > 2)
			dump ("cfr-snd", hdr, skb->len);
		skb->priority = TC_PRIO_CONTROL;
		skb_reset_network_header (skb);
		dev_queue_xmit (skb);
		netif_wake_queue (dev);
		return;
	}

	if (hdr[5] != FR_MSG_STATUS) {
		CRONYX_LOG_1 (h, "cfr: unknown signaling message 0x%02x\n", hdr[5]);
		goto ballout;
	}

	/*
	 * Parse message fields.
	 */
	for (s = hdr + 6; s < hdr + len;) {
		switch (*s) {
			default:
				CRONYX_LOG_1 (h, "cfr: unknown signaling-status field 0x%x\n", *s);
				break;
			case FR_FLD_LSHIFT5:
			case FR_FLD_RTYPE:
				/*
				 * Ignore.
				 */
				break;
			case FR_FLD_VERIFY:
				if (s[1] != 2) {
					CRONYX_LOG_1 (h, "cfr: invalid signaling verify field length %d\n", s[1]);
					break;
				}
				p->yourseen = s[2];
				CRONYX_LOG_1 (h, "cfr: link-verify, yourseen %d, myseq %d (really %d)\n", s[2], s[3],
					      p->myseq);
				break;
			case FR_FLD_PVC:
				if (s[1] < 3) {
					CRONYX_LOG_1 (h, "cfr: invalid PVC status length %d\n", s[1]);
					break;
				}
				id = (s[2] << 4 & 0x3f0) | (s[3] >> 3 & 0x0f);
				dlci = fr_find_dlci (p, id);
				if (!dlci) {
					CRONYX_LOG_1 (h, "cfr: unknown DLCI %d\n", id);
					break;	/* Ignore extra DLCIs. */
				}
				if (dlci->status != s[4]) {
					printk (KERN_INFO "cfr: %s, DLCI %d, %s%s\n", dlci->dev->name, id,
						s[4] & FR_DLCI_DELETE ? "deleted" :
						s[4] & FR_DLCI_ACTIVE ? "active" : "passive",
						s[4] & FR_DLCI_NEW ? ", new" : "");
					dlci->status = s[4];
				}
				break;
		}
		if (*s & 0x80)
			++s;
		else if (s < hdr + len + 1 && s[1])
			s += 2 + s[1];
		else {
			CRONYX_LOG_1 (h, "cfr: invalid signaling field 0x%x\n", *s);
			goto ballout;
		}
	}
	CRONYX_LOG_1 (h, "cfr: signaling-status message parsed\n");
}

/*
 * Send periodical link verification messages via DLCI 0.
 * Called every 10 seconds (default value of T391 timer is 10 sec).
 * Every 6-th message is a full status request
 * (default value of N391 counter is 6).
 */
static void fr_keepalive (unsigned long arg)
{
	fr_t *p = (fr_t *) arg;
	dlci_t *dlci0;
	struct net_device *dev;
	u8 *s;
	struct sk_buff *skb;

	/*
	 * Find the first configured DLCI.
	 * * When no DLCIs configured - skip it all.
	 */
	dlci0 = fr_find_first_dlci (p);
	/*
	 * Couldn't find any? We are going to be unloaded.
	 */
	if (!dlci0)
		return;
	dev = dlci0->dev;

	if (p->last_keepalive == p->myseq) {
		/*
		 * Allocate the packet.
		 */
		skb = dev_alloc_skb (STATUS_ENQUIRY_SIZE);
		if (skb) {
			/*
			 * Prepare the link verification signaling packet,
			 * * according to T1.617 Annex D..
			 */
			skb->dev = dev;
			skb_reset_mac_header (skb);
			s = skb_put (skb, STATUS_ENQUIRY_SIZE);

			if (!p->lmi_dlci1023) {
				s[0] = 0;	/* DLCI = 0 */
				s[1] = 1;
			} else {
				s[0] = 0xFC;	/* LY: DLCI = 1023 */
				s[1] = 0xF1;
			}
			s[2] = FR_UI;
			s[3] = FR_SIGNALING;	/* NLPID = UNI call control */

			s[4] = 0;	/* call reference length = 0 */
			s[5] = FR_MSG_ENQUIRY;	/* message type = status enquiry */

			s[6] = FR_FLD_LSHIFT5;	/* locking shift 5 */

			s[7] = FR_FLD_RTYPE;	/* report type field */
			s[8] = 1;	/* report type length = 1 */
			if (p->myseq % 6)
				s[9] = FR_RTYPE_SHORT;	/* link verification only */
			else
				s[9] = FR_RTYPE_FULL;	/* full status needed */

			s[10] = FR_FLD_VERIFY;	/* link verification type field */
			s[11] = 2;	/* link verification field length = 2 */
			s[12] = ++p->myseq;	/* our sequence number */
			s[13] = p->yourseen;	/* last received sequence number */
			p->last_keepalive = p->myseq;

			CRONYX_LOG_1 (p->chan, "cfr: send lmi-enquiry packet, myseq=%d, yourseen=%d\n", p->myseq,
				      p->yourseen);
			if (p->chan->debug > 2)
				dump ("cfr-snd", s, skb->len);
			skb->priority = TC_PRIO_CONTROL;
			skb_reset_network_header (skb);
			dev_queue_xmit (skb);
			netif_wake_queue (dev);
		}
	}

	if (!timer_pending (&p->timer)) {
		p->timer.expires = jiffies + 10 * HZ;	/* 10 sec */
		p->timer.data = (unsigned long) p;
		p->timer.function = &fr_keepalive;
		add_timer (&p->timer);
	}
}

/*
 * Network device implementation
 */
static int fr_dev_transmit (struct sk_buff *skb, struct net_device *dev)
{
	dlci_t *dlci = netdev_priv (dev);
	cronyx_binder_item_t *h = dlci->proto->chan;

	netif_stop_queue (dev);
	/*
	 * Mark the card busy.
	 */
	if (test_and_set_bit (0, &dlci->proto->tbusy))
		return 1;

	if (skb->len > h->mtu) {
		dlci->out_frf12 = skb;
		dlci->out_done = 2;
		fr_frf12_transmit (dlci);
		return 0;
	}

	skb_reset_network_header (skb);
	/*
	 * if card is full, return unsuccessfuly, device is busy.
	 */
	if (h->dispatch.transmit (h, skb) == 0)
		return 1;

	/*
	 * Packet was queued onto card, device not busy.
	 */
	clear_bit (0, &dlci->proto->tbusy);
	netif_wake_queue (dev);

	dlci->stats.tx_bytes += skb->len;
	dlci->stats.tx_packets++;
	dev_kfree_skb_any (skb);
	return 0;
}

static struct net_device_stats *fr_dev_stats (struct net_device *dev)
{
	dlci_t *dlci = netdev_priv (dev);

	return &dlci->stats;
}

static int fr_dev_up (struct net_device *dev)
{
	dlci_t *dlci = netdev_priv (dev);

	if (!dlci->proto->ndev_active) {
		/*
		 * This is the first active dlci device.
		 * * Start the channel.
		 */
		cronyx_binder_item_t *h = dlci->proto->chan;
		int error = h->dispatch.link_up (h);

		if (error)
			return error;
		clear_bit (0, &dlci->proto->tbusy);
	}
	memset (&dlci->stats, 0, sizeof (dlci->stats));
	netif_start_queue (dev);
	dlci->status = 0xff;
	dlci->proto->ndev_active++;
	return 0;
}

static int fr_dev_down (struct net_device *dev)
{
	dlci_t *dlci = netdev_priv (dev);
	cronyx_binder_item_t *h = dlci->proto->chan;

	netif_stop_queue (dev);
	dlci->proto->ndev_active--;
	if (!dlci->proto->ndev_active) {
		/*
		 * No active dlci devices remaining, stop the channel.
		 */
		h->dispatch.link_down (h);
		set_bit (0, &dlci->proto->tbusy);
	}
	return 0;
}

static void fr_frf12_transmit (dlci_t * dlci)
{
	cronyx_binder_item_t *h = dlci->proto->chan;
	struct sk_buff *skb;
	u8 *fragment;
	int chunk;

	chunk = dlci->out_frf12->len - dlci->out_done;
	while (chunk + 6 > h->mtu)
		chunk >>= 1;

	skb = dev_alloc_skb (chunk + 6);
	if (!skb) {
		dlci->stats.tx_dropped++;
		goto done;
	}
	skb->dev = dlci->dev;
	skb_reset_mac_header (skb);

	fragment = skb_put (skb, chunk + 6);
	*((u16 *) fragment) = *((u16 *) dlci->out_frf12->data);
	fragment[2] = FR_UI;
	fragment[3] = FR_FRF12;
	fragment[4] = (dlci->out_seq >> 8) << 1;
	fragment[5] = (u8) dlci->out_seq;
	if (dlci->out_done == 2)
		fragment[4] |= 0x80;	// LY: first fragment
	if (dlci->out_done + chunk == dlci->out_frf12->len)
		fragment[4] |= 0x40;	// LY: last fragment

	memcpy (fragment + 6, dlci->out_frf12->data + dlci->out_done, chunk);
	skb_reset_network_header (skb);
	if (h->dispatch.transmit (h, skb) == 0) {
		// LY: tx-queue is full.
		dev_kfree_skb_any (skb);
		return;
	}
	dev_kfree_skb_any (skb);

	CRONYX_LOG_2 (h, "cfr: DLCI %d, fragment transmit, data %d+%d, sequence %d\n", dlci->id, dlci->out_done, chunk,
		      dlci->out_seq);
	dlci->out_seq = (dlci->out_seq + 1) & 0x0fff;
	dlci->out_done += chunk;
	if (dlci->out_done == dlci->out_frf12->len) {
	      done:
		dlci->stats.tx_bytes += dlci->out_frf12->len;
		dlci->stats.tx_packets++;
		dev_kfree_skb_any (dlci->out_frf12);
		dlci->out_frf12 = NULL;
		clear_bit (0, &dlci->proto->tbusy);
		netif_wake_queue (dlci->dev);
	}
}

static void fr_frf12_receive (dlci_t * dlci, struct sk_buff *skb)
{
	u8 *hdr = (u8 *) skb->data;
	u8 *re_hdr;
	u16 seq;
	cronyx_binder_item_t *h = dlci->proto->chan;

	if (skb->len < 6) {
		CRONYX_LOG_1 (h, "cfr: DLCI %d, too short frf.12-frame (%d bytes), drop packet\n", dlci->id,
			      (int) skb->len);
		dev_kfree_skb_any (skb);
		return;
	}

	seq = ((hdr[4] & 0x1e) << 7) | hdr[5];
	CRONYX_LOG_2 (h, "cfr: DLCI %d, fragment received, sequence %d->%d\n", dlci->id, dlci->in_seq, seq);
	if (hdr[4] & 0x20) {
		CRONYX_LOG_1 (h, "cfr: DLCI %d, unsupported control bit detected, drop packet\n", dlci->id);
		dlci->stats.rx_errors++;
		dev_kfree_skb_any (skb);
		if (dlci->in_frf12) {
			dev_kfree_skb_any (dlci->in_frf12);
			dlci->in_frf12 = NULL;
		}
		return;
	}
	dlci->in_seq = (dlci->in_seq + 1) & 0x0fff;
	if (dlci->in_seq != seq) {
		if (dlci->in_frf12) {
			CRONYX_LOG_1 (h, "cfr: DLCI %d, sequence missmatch, drop packet\n", dlci->id);
			dlci->stats.rx_errors++;
			dev_kfree_skb_any (dlci->in_frf12);
			dlci->in_frf12 = NULL;
		}
	}
	dlci->in_seq = seq;
	if (dlci->in_frf12 == NULL && !(hdr[4] & 0x80)) {
		CRONYX_LOG_1 (h, "cfr: DCLI %d, fragment loss detected, drop packet\n", dlci->id);
		dlci->stats.rx_errors++;
		dev_kfree_skb_any (skb);
		return;
	}
	/*
	 * Start packet reassambling
	 */
	if (dlci->in_frf12 == NULL) {
		if ((hdr[4] & 0xC0) == 0xC0) {
			memmove (skb->data + 2, skb->data + 6, skb->len - 6);
			skb_trim (skb, skb->len - 4);
			CRONYX_LOG_1 (h, "cfr: DLCI %d, packet reassembled (one fragment, %d bytes)\n", dlci->id,
				      skb->len);
			fr_receive (h, skb);
			return;
		}

		if (skb->len > h->mtu || skb->len - 4 > dlci->dev->mtu + 10) {
			CRONYX_LOG_1 (h, "cfr: DLCI %d, fragment too big (%d bytes), drop packet\n", dlci->id,
				      skb->len);
			dlci->stats.rx_errors++;
			dev_kfree_skb_any (skb);
			return;
		}

		dlci->in_frf12 = dev_alloc_skb (dlci->dev->mtu + 10);
		dlci->in_frf12->data[0] = hdr[0];
		dlci->in_frf12->data[1] = hdr[1];
		memcpy (skb_put (dlci->in_frf12, skb->len - 4) + 2, skb->data + 6, skb->len - 6);
		dev_kfree_skb_any (skb);
		return;
	}
	if (skb_tailroom (dlci->in_frf12) < skb->len - 6) {
		CRONYX_LOG_1 (h, "cfr: DLCI %d, packet too big (%d bytes), drop packet\n", dlci->id,
			      skb->len - 6 + dlci->in_frf12->len);
		dlci->stats.rx_errors++;
		dev_kfree_skb_any (skb);
		dev_kfree_skb_any (dlci->in_frf12);
		dlci->in_frf12 = NULL;
		return;
	}
	re_hdr = (u8 *) dlci->in_frf12->data;
	re_hdr[0] |= hdr[0];
	re_hdr[1] |= hdr[1];
	memcpy (skb_put (dlci->in_frf12, skb->len - 6), skb->data + 6, skb->len - 6);
	dev_kfree_skb_any (skb);

	if (hdr[4] & 0x40) {
		skb = dlci->in_frf12;
		dlci->in_frf12 = NULL;
		CRONYX_LOG_1 (h, "cfr: DLCI %d, packet reassembled (%d bytes)\n", dlci->id, skb->len);
		fr_receive (h, skb);
	}
}

/*
 * Protocol interface implementation
 */
static void fr_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	fr_t *p = h->sw;
	dlci_t *dlci, *dlci0;
	u8 *hdr = (u8 *) skb->data;
	int id, hlen;
	u16 nlpid;

	if (skb->len < 4) {
		CRONYX_LOG_1 (h, "cfr: too short frame (%d bytes), dropped\n", (int) skb->len);
		dev_kfree_skb_any (skb);
		return;
	}

	/*
	 * Find the first configured DLCI.
	 * * When no DLCIs configured - ignore all incoming packets.
	 */
	dlci0 = fr_find_first_dlci (p);
	if (!dlci0) {
		skb->dev = NULL;
		dev_kfree_skb_any (skb);
		return;
	}

	/*
	 * Get the DLCI number.
	 */
	skb->dev = dlci0->dev;
	id = (hdr[0] << 2 & 0x3f0) | (hdr[1] >> 4 & 0x0f);

	CRONYX_LOG_1 (h, "cfr: DLCI %d, received %d bytes\n", id, (int) skb->len);
	if (h->debug > 2)
		dump ("cfr-rcv", hdr, skb->len);

	/*
	 * Process signaling packets.
	 */
	if (id == 0 || id == 1023) {
		p->lmi_dlci1023 = (id == 1023);
		fr_signal (h, skb->data, skb->len);
		dev_kfree_skb_any (skb);
		return;
	}

	dlci = fr_find_dlci (p, id);
	if (!dlci) {
		CRONYX_LOG_1 (h, "cfr: received packet from invalid DLCI %d, ignoring\n", id);
		dlci = dlci0;
	      ballout:
		dlci->stats.rx_errors++;
		dev_kfree_skb_any (skb);
		return;
	}
	skb->dev = dlci->dev;

	/*
	 * Process the packet.
	 */
	hlen = 4;
	nlpid = hdr[2] << 8;
	if (hdr[3] == 0) {
		/* LY: skip optional pad. */
		if (skb->len < 5) {
			CRONYX_LOG_1 (h, "cfr: too short frame (%d bytes), dropped\n", (int) skb->len);
			goto ballout;
		}
		hlen++;
		hdr++;
	}
	nlpid += hdr[3];

	switch (nlpid) {
		default:
			CRONYX_LOG_1 (h, "cfr: unsupported NLPID 0x%04X.\n", nlpid);
			goto ballout;
		case NLPID_CDP:
			/* LY: CDP (Cisco Discovery Protocol). */
			CRONYX_LOG_1 (h, "cfr: CDP not supported\n");
			goto ballout;
		case NLPID_CISCO_BRIDGE:
			if (skb->len < hlen + 14) {
				CRONYX_LOG_1 (h, "cfr: too short bridge-802.3-frame (%d bytes), DLCI %d, dropped\n",
					      (int) skb->len, id);
				goto ballout;
			}
			CRONYX_LOG_1 (h, "cfr: Cisco-BRIDGE not yet supported\n");
			goto ballout;
		case NLPID (FR_UI, FR_SNAP):
			hlen += 3 /* OID */ + 2 /* PROTO */;
			if (skb->len < hlen + ETH_HLEN) {
				CRONYX_LOG_1 (h, "cfr: too short 802.3-frame (%d bytes), DLCI %d, dropped\n",
					      (int) skb->len, id);
				goto ballout;
			}
			if (hdr[4] | hdr[5] | hdr[6]) {
				CRONYX_LOG_1 (h, "cfr: unsupported OUID 0x%02X-%02X-%02X.\n", hdr[4], hdr[5], hdr[6]);
				goto ballout;
			}
			skb->protocol = *(u16 *) (hdr + 4 /* FR */  + 3 /* OID */ );
			if (skb->protocol == htons (ETH_P_ARP)) {
				/*
				 * Process the ARP request.
				 */
				if (skb->len < hlen + sizeof (struct arp_req)) {
					CRONYX_LOG_1 (h, "cfr: ballout ARP request size = %d bytes\n", (int) skb->len);
					goto ballout;
				}
				dlci->stats.rx_bytes += skb->len;
				dlci->stats.rx_packets++;
				fr_arp (dlci, skb->dev, (struct arp_req *) (hdr + hlen), hdr[0] << 8 | hdr[1]);
				dev_kfree_skb_any (skb);
				return;
			}
			break;

		case ETH_P_IP:
			/*
			 * Prehistoric IP framing?
			 */
		case NLPID_IP:
			if (skb->len < hlen + 20) {
				CRONYX_LOG_1 (h, "cfr: too short IP-data-frame (%d bytes), DLCI %d, dropped\n",
					      (int) skb->len, id);
				goto ballout;
			}
			skb->protocol = htons (ETH_P_IP);
			break;
		case NLPID_FRF12:
			fr_frf12_receive (dlci, skb);
			return;
	}

	dlci->stats.rx_bytes += skb->len;
	dlci->stats.rx_packets++;

	/*
	 * Discard the header.
	 */
	skb_reset_mac_header (skb);
	skb_pull (skb, hlen);
	dlci->dev->last_rx = jiffies;
	netif_rx (skb);
}

static void fr_receive_error (cronyx_binder_item_t * h, int errcode)
{
	fr_t *p = h->sw;
	dlci_t *dlci0 = fr_find_first_dlci (p);

	if (dlci0)
		dlci0->stats.rx_errors++;
}

static void fr_transmit_done (cronyx_binder_item_t * h)
{
	fr_t *p = h->sw;
	int id;

	for (id = 16; id < 1023; ++id)
		if (p->pdlci[id - 16] && p->pdlci[id - 16]->out_frf12) {
			fr_frf12_transmit (p->pdlci[id - 16]);
			return;
		}

	clear_bit (0, &p->tbusy);
	for (id = 16; id < 1023; ++id)
		if (p->pdlci[id - 16])
			netif_wake_queue (p->pdlci[id - 16]->dev);
}

static int fr_attach (cronyx_binder_item_t * h)
{
	fr_t *p;

	if (cronyx_param_set (h, cronyx_channel_mode, CRONYX_MODE_HDLC) < 0) {
		CRONYX_LOG_1 (h, "cfr: unable set channel hdlc-mode");
		return -EIO;
	}

	p = kzalloc (sizeof (fr_t), GFP_KERNEL);
	if (!p)
		return -ENOMEM;
	h->sw = p;
	p->chan = h;
	init_timer (&p->timer);
#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif
	return 0;
}

static int fr_detach (cronyx_binder_item_t * h)
{
	fr_t *p = h->sw;
	int i;

	for (i = 0; i < 1023 - 16; ++i)
		if (p->pdlci[i] && netif_running (p->pdlci[i]->dev))
			return -EBUSY;
	h->sw = NULL;
	del_timer_sync (&p->timer);
	h->dispatch.link_down (h);
	for (i = 0; i < 1023 - 16; ++i)
		if (p->pdlci[i]) {
			if (p->pdlci[i]->in_frf12)
				dev_kfree_skb_any (p->pdlci[i]->in_frf12);
			if (p->pdlci[i]->out_frf12)
				dev_kfree_skb_any (p->pdlci[i]->out_frf12);
			unregister_netdev (p->pdlci[i]->dev);
		}
	kfree (p);
#if LINUX_VERSION_CODE < 0x020600
	MOD_DEC_USE_COUNT;
#else
	module_put (THIS_MODULE);
#endif
	return 0;
}

static int fr_dev_header (struct sk_buff *skb, struct net_device *dev,
#if LINUX_VERSION_CODE < 0x020618
			  unsigned short type, void *daddr, void *saddr,
#else
			  unsigned short type, const void *daddr, const void *saddr,
#endif
			  unsigned len)
{
	dlci_t *dlci = netdev_priv (dev);
	u8 *hdr;
	unsigned hlen;

	/*
	 * Is the DLCI number already known?
	 */
	if (!dlci->id)
		return 0;

	/*
	 * Compute the additional header length.
	 */
	switch (type) {
		case ETH_P_IP:
			hlen = 4;
			break;
		default:
			hlen = 9;
			break;
	}

	/*
	 * Add the space for Frame Relay header.
	 */
	hdr = skb_push (skb, hlen);
	if (!hdr)
		return 0;

	/*
	 * Fill the header.
	 */
	hdr[0] = dlci->id >> 2 & 0xfc;
	hdr[1] = dlci->id << 4 | 1;
	hdr[2] = FR_UI;
	switch (type) {
		case ETH_P_IP:
			hdr[3] = FR_IP;
			break;
		default:
			hdr[3] = FR_SNAP;
			hdr[4] = 0;
			hdr[5] = 0;
			hdr[6] = 0;
			*(short *) (hdr + 7) = htons (type);
			break;
	}
	return hlen;
}

#if LINUX_VERSION_CODE < 0x020618

static int fr_dev_rebuild_header (struct sk_buff *skb)
{
	/* LY-TODO: in bridge mode rebuild like ethernet. */
	return 0;
}

#else

static const struct header_ops fr_header_ops = {
	.create = fr_dev_header
};

#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
static const struct net_device_ops fr_netdev_ops =
{
	.ndo_open = fr_dev_up,
	.ndo_stop = fr_dev_down,
	.ndo_start_xmit = fr_dev_transmit,
	.ndo_get_stats = fr_dev_stats,
};
#endif

static void fr_dev_setup (struct net_device *dev)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,31)
	dev->open = fr_dev_up;
	dev->stop = fr_dev_down;
	dev->hard_start_xmit = fr_dev_transmit;
	dev->get_stats = fr_dev_stats;
#else
	dev->netdev_ops = &fr_netdev_ops;
#endif
#if LINUX_VERSION_CODE < 0x020618
	dev->hard_header = fr_dev_header;
	dev->rebuild_header = fr_dev_rebuild_header;
#else
	dev->header_ops = &fr_header_ops;
#endif
	dev->type = ARPHRD_DLCI;
	dev->hard_header_len = 10;
	dev->mtu = 1500;
	dev->addr_len = 2;
	dev->flags = IFF_POINTOPOINT;
	dev->tx_queue_len = FR_QUEUE_LEN;
	dev->destructor = free_netdev;

	dev_init_buffers (dev);
}

static int fr_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	fr_t *p = item->sw;
	dlci_t *dlci;
	struct net_device *dev;
	char dlci_name[IFNAMSIZ];

	if (ctl->param_id == cronyx_channel_mode) {
		if (ctl->u.param.value == CRONYX_MODE_HDLC)
			return 0;
		return -EBUSY;
	}

	if (ctl->param_id != cronyx_add_dlci)
		return -ENOSYS;

	CRONYX_LOG_1 (item, "cfr: add DLCI %ld\n", ctl->u.param.value);

	if (ctl->u.param.value < 16 || ctl->u.param.value >= 1023)
		return -EINVAL;

	if (p->pdlci[ctl->u.param.value - 16])
		return -EEXIST;

	snprintf (dlci_name, sizeof (dlci_name), "%sd%d", item->alias[0] ? item->alias : item->name,
		  (int) ctl->u.param.value);
	dlci_name[sizeof (dlci_name) - 1] = 0;

	dev = alloc_netdev (sizeof (dlci_t), dlci_name, fr_dev_setup);
	if (!dev)
		return -ENOMEM;

	dlci = netdev_priv (dev);
	dlci->dev = dev;
	dlci->proto = p;
	dlci->id = ctl->u.param.value;
	dev->mtu = item->mtu - 6;

	if (register_netdev (dev) != 0) {
		printk (KERN_ERR "cfr: unable to register net device %s\n", dev->name);
		free_netdev (dev);
		return -EINVAL;
	}
	p->pdlci[dlci->id - 16] = dlci;
	fr_keepalive ((unsigned long) p);
	return 0;
}

static cronyx_proto_t dispatch_tab = {
	.name = "fr",
	.chanmode_lock = 1,
	.mtu_lock = 1,
	.notify_receive = fr_receive,
	.notify_receive_error = fr_receive_error,
	.notify_transmit_done = fr_transmit_done,
	.attach = fr_attach,
	.detach = fr_detach,
	.ctl_set = fr_ctl_set
};

int init_module (void)
{
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif
	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx Frame Relay protocol module loaded\n");
	return 0;
}

void cleanup_module (void)
{
	cronyx_binder_unregister_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx Frame Relay protocol module unloaded\n");
}

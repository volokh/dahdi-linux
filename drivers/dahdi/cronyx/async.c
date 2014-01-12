/*
 * Async mode layer for Cronyx serial adapters.
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
 * $Id: async.c,v 1.53 2009-09-04 17:10:37 ly Exp $
 */
#include "module.h"
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>

#if LINUX_VERSION_CODE < 0x020600
#	include <linux/devfs_fs_kernel.h>
#endif
#include "cserial.h"

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx asyncronous mode layer\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

#ifndef SERIAL_TYPE_NORMAL
#	define SERIAL_TYPE_NORMAL	1
#endif

#ifndef SERIAL_TYPE_CALLOUT
#	define SERIAL_TYPE_CALLOUT	2
#endif

#ifndef ASYNC_CALLOUT_ACTIVE
#	define ASYNC_CALLOUT_ACTIVE 0
#endif

#ifndef TTY_DRIVER_NO_DEVFS
#	define TTY_DRIVER_NO_DEVFS 0
#endif

#ifndef TTY_DRIVER_DYNAMIC_DEV
#	define TTY_DRIVER_DYNAMIC_DEV 0
#endif

typedef struct {
#if LINUX_VERSION_CODE < 0x020600
	int istate;
#else
	unsigned long istate;
#endif
#define	ASYI_TXBUSY  1
#define	ASYI_TXLOW   2
#define	ASYI_DCDLOST 4
	int flags;		/* ASYNC_ flags */
	int baud_base;
	int custom_divisor;
	int close_delay;
	int closing_wait;
	int refcount;
	int openwaitcnt;
	pid_t session;
	pid_t pgrp;
	int carrier;		/* DCD modem signal */
	int mtu;
	struct tty_struct *tty;
	wait_queue_head_t open_wait;
	wait_queue_head_t close_wait;
	cronyx_termios_t normaltermios;
	cronyx_termios_t callouttermios;
#if LINUX_VERSION_CODE < 0x020600
	struct tq_struct tqueue;
#else
	struct work_struct tqueue;
#endif
	struct sk_buff *skb;
	struct semaphore sem;
} asy_t;


#if LINUX_VERSION_CODE < 0x020600
static struct tty_driver __asy_serial, __asy_callout;
#define asy_serial (&__asy_serial)
#define asy_callout (&__asy_callout)
static struct tty_struct *asy_ttys[CRONYX_MINOR_MAX];
static int asy_refcount;
#else
static struct tty_driver *asy_serial, *asy_callout;
#endif
static cronyx_termios_t *asy_termios[CRONYX_MINOR_MAX];
#if LINUX_VERSION_CODE < 0x030409
static cronyx_termios_t *asy_termioslocked[CRONYX_MINOR_MAX];
#endif
static asy_t asy_data[CRONYX_MINOR_MAX];

#if LINUX_VERSION_CODE < 0x020614
static void asy_offintr (void *private);
#else
static void asy_offintr (struct work_struct *work);
#endif /* if LINUX_VERSION_CODE < 0x020614 */
static cronyx_proto_t dispatch_tab;

/*
 * Receive character interrupt handler. Determine if we have good chars
 * or bad chars and then process appropriately. Good chars are easy
 * just shove the lot into the RX buffer and set all status byte to 0.
 * If a bad RX char then process as required. This routine needs to be
 * fast!  In practice it is possible that we get an interrupt on a port
 * that is closed. This can happen on hangups - since they completely
 * shutdown a port not in user context. Need to handle this case.
 */
static void asy_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	asy_t *p = h->sw;
	struct tty_struct *tty = p->tty;

#if LINUX_VERSION_CODE > 0x02060F
#if LINUX_VERSION_CODE >= 0x030B0A
	struct tty_port *tty_p = tty->port;
#else
	struct tty_struct *tty_p = tty;
#endif

	if (tty && tty_insert_flip_string (tty_p, skb->data, skb->len) > 0)
		tty_flip_buffer_push (tty_p);
#else
	unsigned buflen;

	if (tty && tty->flip.char_buf_ptr && (buflen = TTY_FLIPBUF_SIZE - tty->flip.count)) {
		unsigned len = skb->len;

		if (len > buflen)
			len = buflen;
		if (len > 0) {
			memcpy (tty->flip.char_buf_ptr, skb->data, len);
			memset (tty->flip.flag_buf_ptr, 0, len);
			tty->flip.flag_buf_ptr += len;
			tty->flip.char_buf_ptr += len;
			tty->flip.count += len;
			tty_schedule_flip (tty);
		}
	}
#endif
	CRONYX_LOG_1 (h, "casync.receive\n");
	dev_kfree_skb_any (skb);
}

static void asy_receive_error (cronyx_binder_item_t * h, int errcode)
{
	asy_t *p = h->sw;
	struct tty_struct *tty = p->tty;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;
	int status;

	if (!tty || !termios)
		return;

	CRONYX_LOG_1 (h, "casync.receive-error %d\n", errcode);
	switch (errcode) {
		default:
			return;

		case CRONYX_ERR_FRAMING:
			if (termios->c_iflag & IGNPAR)
				return;
			status = TTY_FRAME;
			break;

		case CRONYX_ERR_CHECKSUM:
			if ((termios->c_iflag & IGNPAR)
			    || !(termios->c_iflag & INPCK))
				return;
			status = TTY_PARITY;
			break;

		case CRONYX_ERR_BREAK:
			if (termios->c_iflag & IGNBRK)
				return;
			status = TTY_BREAK;
			break;

		case CRONYX_ERR_OVERFLOW:
		case CRONYX_ERR_OVERRUN:
			if (termios->c_iflag & IGNPAR)
				return;
			status = TTY_OVERRUN;
			break;
	}
#if LINUX_VERSION_CODE > 0x02060F
	{
#if LINUX_VERSION_CODE >= 0x030B0A
		struct tty_port *tty_p = tty->port;
#else
		struct tty_struct *tty_p = tty;
#endif

		if (tty_insert_flip_char (tty_p, 0, status))
			tty_flip_buffer_push (tty_p);
	}
#else
	if (tty->flip.char_buf_ptr) {
		if (tty->flip.count < TTY_FLIPBUF_SIZE) {
			*tty->flip.flag_buf_ptr++ = status;
			*tty->flip.char_buf_ptr++ = 0;
			tty->flip.count++;
		}
		tty_schedule_flip (tty);
	}
#endif
}

/*
 * Transmit interrupt handler. This has gotta be fast!  Handling TX
 * chars is pretty simple, stuff as many as possible from the TX buffer
 * into the controller FIFO. Must also handle TX breaks here, since they
 * are embedded as commands in the data stream.
 * In practice it is possible that interrupts are enabled but that the
 * port has been hung up.
 */
static void asy_transmit_done (cronyx_binder_item_t * h)
{
	asy_t *p = h->sw;

	if (p->skb->len > 0 && h->dispatch.transmit (h, p->skb))
		p->skb->len = 0;
	else
		clear_bit (ASYI_TXBUSY, &p->istate);
	if (!p->skb->len && !test_and_set_bit (ASYI_TXLOW, &p->istate))
		schedule_work (&p->tqueue);
}

/*
 * Modem interrupt handler. The is called when the modem signal line
 * (DCD) has changed state. Leave most of the work to the off-level
 * processing routine.
 */
static void asy_modem_event (cronyx_binder_item_t * h)
{
	asy_t *p = h->sw;
	unsigned int old_carrier;

	old_carrier = p->carrier;
	p->carrier = (h->dispatch.query_dcd) ? h->dispatch.query_dcd (h) : 1;
	if (p->carrier && !old_carrier) {
		/*
		 * Carrier detected.
		 */
		CRONYX_LOG_1 (h, "casync.carrier-detect\n");
		wake_up_interruptible (&p->open_wait);
	}

	if (old_carrier && !p->carrier && (p->flags & ASYNC_CHECK_CD) &&
	    !((p->flags & ASYNC_CALLOUT_ACTIVE) && (p->flags & ASYNC_CALLOUT_NOHUP))) {
		/*
		 * Carrier lost.
		 */
		set_bit (ASYI_DCDLOST, &p->istate);
		schedule_work (&p->tqueue);
		CRONYX_LOG_1 (h, "casync.carrier-lost\n");
	}
}

static int asy_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	if (ctl->param_id == cronyx_channel_mode) {
		if (ctl->u.param.value == CRONYX_MODE_ASYNC)
			return 0;
		return -EBUSY;
	}
	return -ENOSYS;
}

static int asy_attach (cronyx_binder_item_t * h)
{
	int error;
	asy_t *p;

	if (h->dispatch.set_async_param == NULL
	    || h->dispatch.send_break == NULL
	    || h->dispatch.send_xon == NULL
	    || h->dispatch.send_xoff == NULL
	    || h->dispatch.start_transmitter == NULL
	    || h->dispatch.stop_transmitter == NULL || h->dispatch.flush_transmit_buffer == NULL)
		return -ENODEV;

	if (cronyx_param_set (h, cronyx_channel_mode, CRONYX_MODE_ASYNC) < 0) {
		CRONYX_LOG_1 (h, "casync: unable set channel hdlc-mode");
		return -EIO;
	}

	error = cronyx_binder_minor_get (h, CRONYX_SVC_TTY_ASYNC);
	if (error < 0)
		return error;

	p = asy_data + h->minor;
	memset (p, 0, sizeof (*p));
	p->baud_base = 115200;
	p->close_delay = 5 * HZ / 10;
	p->closing_wait = 30 * HZ;
	p->normaltermios = asy_serial->init_termios;
	p->callouttermios = asy_callout->init_termios;
#if LINUX_VERSION_CODE < 0x020600
	p->tqueue.routine = asy_offintr;
	p->tqueue.data = p;
#else
	CRONYX_INIT_WORK (&p->tqueue, asy_offintr, p);
#endif
	sema_init (&p->sem, 1);

	h->sw = p;
#if LINUX_VERSION_CODE >= 0x020400
	init_waitqueue_head (&p->open_wait);
	init_waitqueue_head (&p->close_wait);
#endif
	p->mtu = h->mtu;
	CRONYX_LOG_1 (h, "casync.open(mtu=%d)\n", p->mtu);
	p->skb = dev_alloc_skb (p->mtu);
	if (p->skb == NULL) {
		cronyx_binder_minor_put (h);
		return -ENOMEM;
	}
#if LINUX_VERSION_CODE < 0x020600
	tty_register_devfs (asy_serial, DEVFS_FL_NONE, h->minor);
	tty_register_devfs (asy_callout, DEVFS_FL_NONE, h->minor);
#else
	tty_register_device (asy_serial, h->minor, NULL);
	tty_register_device (asy_callout, h->minor, NULL);
#endif

#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif
	return 0;
}

static int asy_detach (cronyx_binder_item_t * h)
{
	asy_t *p = h->sw;

	down (&p->sem);
	if (p->refcount > 0 || p->openwaitcnt > 0) {
		up (&p->sem);
		return -EBUSY;
	}
#if LINUX_VERSION_CODE < 0x020600
	tty_unregister_devfs (asy_serial, h->minor);
	tty_unregister_devfs (asy_callout, h->minor);
#else
	tty_unregister_device (asy_serial, h->minor);
	tty_unregister_device (asy_callout, h->minor);
#endif

	cronyx_binder_minor_put (h);
	dev_kfree_skb_any (p->skb);
	p->skb = NULL;
	h->sw = 0;
	h->dispatch.link_down (h);
#if LINUX_VERSION_CODE < 0x020600
	MOD_DEC_USE_COUNT;
#else
	module_put (THIS_MODULE);
#endif
	up (&p->sem);
	return 0;
}

static cronyx_proto_t dispatch_tab = {
	.name = "async",
	.chanmode_lock = 1,
	.mtu_lock = 1,
	.notify_receive = asy_receive,
	.notify_receive_error = asy_receive_error,
	.notify_transmit_done = asy_transmit_done,
	.notify_modem_event = asy_modem_event,
	.attach = asy_attach,
	.detach = asy_detach,
	.ctl_set = asy_ctl_set
};

static int asy_open (struct tty_struct *tty, struct file *filp);
static void asy_close (struct tty_struct *tty, struct file *filp);
static void asy_hangup (struct tty_struct *tty);

#if LINUX_VERSION_CODE >= 0x020544
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int asy_tiocmget (struct tty_struct *tty);
static int asy_tiocmset (struct tty_struct *tty, unsigned int set, unsigned int clear);
#else
static int asy_tiocmget (struct tty_struct *tty, struct file *file);
static int asy_tiocmset (struct tty_struct *tty, struct file *file, unsigned int set, unsigned int clear);
#endif
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int asy_ioctl (struct tty_struct *tty, unsigned int cmd, unsigned long arg);
#else
static int asy_ioctl (struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
#endif

#if LINUX_VERSION_CODE < 0x02060a
static int asy_write (struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
#else
static int asy_write (struct tty_struct *tty, const unsigned char *buf, int count);
#endif
#if LINUX_VERSION_CODE >= 0x02061a
static int asy_put_char (struct tty_struct *tty, unsigned char ch);
#else
static void asy_put_char (struct tty_struct *tty, unsigned char ch);
#endif
static void asy_flush_chars (struct tty_struct *tty);
static int asy_write_room (struct tty_struct *tty);
static int asy_chars_in_buffer (struct tty_struct *tty);
static void asy_flush_buffer (struct tty_struct *tty);
static void asy_throttle (struct tty_struct *tty);
static void asy_unthrottle (struct tty_struct *tty);
static void asy_stop (struct tty_struct *tty);
static void asy_start (struct tty_struct *tty);
static void asy_set_termios (struct tty_struct *tty, cronyx_termios_t * old);

#if LINUX_VERSION_CODE >= 0x02061a
static const struct tty_operations asy_ops = {
	.open = asy_open,
	.close = asy_close,
	.write = asy_write,
	.put_char = asy_put_char,
	.flush_chars = asy_flush_chars,
	.write_room = asy_write_room,
	.chars_in_buffer = asy_chars_in_buffer,
	.ioctl = asy_ioctl,
	.set_termios = asy_set_termios,
	.throttle = asy_throttle,
	.unthrottle = asy_unthrottle,
	.stop = asy_stop,
	.start = asy_start,
	.hangup = asy_hangup,
	.flush_buffer = asy_flush_buffer,
	.tiocmget = asy_tiocmget,
	.tiocmset = asy_tiocmset
};
#endif

/*
 * Set up the tty driver structure and register tty and callout devices.
 */
int init_module (void)
{
	int err;
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif

#if LINUX_VERSION_CODE < 0x020600
	__asy_serial.magic = TTY_DRIVER_MAGIC;
	__asy_serial.num = CRONYX_MINOR_MAX;
	__asy_callout.magic = TTY_DRIVER_MAGIC;
	__asy_callout.num = CRONYX_MINOR_MAX;
#else
	asy_serial = alloc_tty_driver (CRONYX_MINOR_MAX);
	if (! asy_serial)
		return -ENOMEM;

	asy_callout = alloc_tty_driver (CRONYX_MINOR_MAX);
	if (! asy_callout) {
		put_tty_driver (asy_serial);
		return -ENOMEM;
	}
#endif
#if LINUX_VERSION_CODE >= 0x02053b
	asy_serial->owner = THIS_MODULE;
#endif
	asy_serial->driver_name = "/dev/cronyx/async";
	asy_serial->name = "ttyQ";
	asy_serial->major = CRONYX_MJR_ASYNC_SERIAL;
	asy_serial->type = TTY_DRIVER_TYPE_SERIAL;
	asy_serial->subtype = SERIAL_TYPE_NORMAL;
	asy_serial->init_termios = tty_std_termios;
	asy_serial->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS | TTY_DRIVER_DYNAMIC_DEV;
#if LINUX_VERSION_CODE < 0x020547
	asy_serial->refcount = &asy_refcount;
	asy_serial->table = asy_ttys;
#elif TTY_DRIVER_NO_DEVFS
	asy_serial->devfs_name = "ttyQ";
#endif
	asy_serial->termios = asy_termios;
#if LINUX_VERSION_CODE < 0x030409
	asy_serial->termios_locked = asy_termioslocked;
#endif

#if LINUX_VERSION_CODE >= 0x02061a
	asy_serial->ops = &asy_ops;
#else
	asy_serial->open = asy_open;
	asy_serial->close = asy_close;
	asy_serial->write = asy_write;
	asy_serial->put_char = asy_put_char;
	asy_serial->flush_chars = asy_flush_chars;
	asy_serial->write_room = asy_write_room;
	asy_serial->chars_in_buffer = asy_chars_in_buffer;
	asy_serial->ioctl = asy_ioctl;
	asy_serial->set_termios = asy_set_termios;
	asy_serial->throttle = asy_throttle;
	asy_serial->unthrottle = asy_unthrottle;
	asy_serial->stop = asy_stop;
	asy_serial->start = asy_start;
	asy_serial->hangup = asy_hangup;
	asy_serial->flush_buffer = asy_flush_buffer;
#if LINUX_VERSION_CODE >= 0x020544
	asy_serial->tiocmget = asy_tiocmget;
	asy_serial->tiocmset = asy_tiocmset;
#endif
#endif

#if LINUX_VERSION_CODE >= 0x02053b
	asy_callout->owner = THIS_MODULE;
#endif
	asy_callout->driver_name = "/dev/cronyx/callout";
	asy_callout->name = "cuq";
	asy_callout->major = CRONYX_MJR_ASYNC_CALLOUT;
	asy_callout->type = TTY_DRIVER_TYPE_SERIAL;
	asy_callout->subtype = SERIAL_TYPE_CALLOUT;
	asy_callout->subtype = SERIAL_TYPE_NORMAL;
	asy_callout->init_termios = tty_std_termios;
	asy_callout->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS | TTY_DRIVER_DYNAMIC_DEV;
#if LINUX_VERSION_CODE < 0x020547
	asy_callout->refcount = &asy_refcount;
	asy_callout->table = asy_ttys;
#elif TTY_DRIVER_NO_DEVFS
	asy_callout->devfs_name = "cuq";
#endif
	asy_callout->termios = asy_termios;
#if LINUX_VERSION_CODE < 0x030409
	asy_callout->termios_locked = asy_termioslocked;
#endif

#if LINUX_VERSION_CODE >= 0x02061a
	asy_callout->ops = &asy_ops;
#else
	asy_callout->open = asy_open;
	asy_callout->close = asy_close;
	asy_callout->write = asy_write;
	asy_callout->put_char = asy_put_char;
	asy_callout->flush_chars = asy_flush_chars;
	asy_callout->write_room = asy_write_room;
	asy_callout->chars_in_buffer = asy_chars_in_buffer;
	asy_callout->ioctl = asy_ioctl;
	asy_callout->set_termios = asy_set_termios;
	asy_callout->throttle = asy_throttle;
	asy_callout->unthrottle = asy_unthrottle;
	asy_callout->stop = asy_stop;
	asy_callout->start = asy_start;
	asy_callout->hangup = asy_hangup;
	asy_callout->flush_buffer = asy_flush_buffer;
#if LINUX_VERSION_CODE >= 0x020544
	asy_callout->tiocmget = asy_tiocmget;
	asy_callout->tiocmset = asy_tiocmset;
#endif
#endif

	err = tty_register_driver (asy_serial);
	if (err < 0) {
		printk (KERN_ERR "casync: failed to register serial driver\n");
#if LINUX_VERSION_CODE >= 0x020600
		put_tty_driver (asy_serial);
		put_tty_driver (asy_callout);
#endif
		return err;
	}

	err = tty_register_driver (asy_callout);
	if (err < 0) {
		printk (KERN_ERR "casync: failed to register callout driver\n");
		tty_unregister_driver (asy_serial);
#if LINUX_VERSION_CODE >= 0x020600
		put_tty_driver (asy_serial);
		put_tty_driver (asy_callout);
#endif
		return err;
	}

	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx async protocol module loaded\n");
	return 0;
}

/*
 * Unregister tty and callout devices.
 */
void cleanup_module (void)
{
	tty_unregister_driver (asy_serial);
	tty_unregister_driver (asy_callout);

	cronyx_binder_unregister_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx async protocol module unloaded\n");
}

/*
 * Service an off-level request for some channel.
 */
#if LINUX_VERSION_CODE < 0x020614
static void asy_offintr (void *private)
{
	asy_t *p = private;
#else
static void asy_offintr (struct work_struct *work)
{
	asy_t *p = container_of (work, asy_t, tqueue);
#endif /* LINUX_VERSION_CODE < 0x020614 */
	struct tty_struct *tty;

	if (!p || !(tty = p->tty))
		return;

	if (test_and_clear_bit (ASYI_TXLOW, &p->istate)) {
		/*
		 * Transmitter empty, resume writing.
		 */
		if (test_bit (TTY_DO_WRITE_WAKEUP, &tty->flags)) {
			struct tty_ldisc *ld = tty_ldisc_ref (tty);
			if (ld) {
				if (tty_ldisc_ops (ld)->write_wakeup)
					tty_ldisc_ops (ld)->write_wakeup (tty);
				tty_ldisc_deref (ld);
			}
		}
		wake_up_interruptible (&tty->write_wait);
	}

	if (test_and_clear_bit (ASYI_DCDLOST, &p->istate))
		/*
		 * Carrier lost, hang up.
		 */
		tty_hangup (tty);
}

/*
 * Wait for carrier (DCD signal) to come high.
 * But if we are clocal then we don't need to wait...
 */
static int asy_waitcarrier (cronyx_binder_item_t * h, struct file *filp)
{
	asy_t *p = h->sw;
	int error = 0, doclocal = 0;
	struct ktermios *termios = p && p->tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		p->tty->termios
#else
		&p->tty->termios
#endif
		: NULL;

	if (p->flags & ASYNC_CALLOUT_ACTIVE) {
		if (p->normaltermios.c_cflag & CLOCAL)
			doclocal = 1;
	} else {
		if (termios->c_cflag & CLOCAL)
			doclocal = 1;
	}

	p->openwaitcnt++;
	if (p->refcount > 0)
		p->refcount--;

	if (wait_event_interruptible(p->open_wait, ({
		int done = 0;
		if (!(p->flags & ASYNC_CALLOUT_ACTIVE)) {
			if (h->dispatch.set_dtr)
				h->dispatch.set_dtr (h, 1);
			if (h->dispatch.set_rts)
				h->dispatch.set_rts (h, 1);
		}
		if (tty_hung_up_p (filp) || !(p->flags & ASYNC_INITIALIZED)) {
			error = -EBUSY;
			if (!(p->flags & ASYNC_HUP_NOTIFY))
				error = -ERESTARTSYS;
			done = 1;
		} else if (!(p->flags & ASYNC_CALLOUT_ACTIVE) && !(p->flags & ASYNC_CLOSING) && (doclocal || p->carrier))
			done = 1;
		done;
	})))
		error = -EINTR;

	if (!tty_hung_up_p (filp))
		p->refcount++;
	p->openwaitcnt--;
	CRONYX_LOG_2 (h, "casync.wait-carrier (%d)\n", error);
	return error;
}

/*
 * Set up the channel modes based on the termios port settings.
 */
static void asy_setup_parameters (cronyx_binder_item_t * h)
{
	asy_t *p = h->sw;
	cronyx_termios_t *t = p && p->tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		p->tty->termios
#else
		&p->tty->termios
#endif
		: NULL;
	int baud, bits, parity, stop2, ignpar;
	int rtscts, ixon, ixany, symstop, symstart;

	switch (t->c_cflag & CBAUD) {
		default:
			baud = 0;
			break;
		case B50:
			baud = 50;
			break;
		case B75:
			baud = 75;
			break;
		case B110:
			baud = 110;
			break;
		case B134:
			baud = 134;
			break;
		case B150:
			baud = 150;
			break;
		case B200:
			baud = 200;
			break;
		case B300:
			baud = 300;
			break;
		case B600:
			baud = 600;
			break;
		case B1200:
			baud = 1200;
			break;
		case B1800:
			baud = 1800;
			break;
		case B2400:
			baud = 2400;
			break;
		case B4800:
			baud = 4800;
			break;
		case B9600:
			baud = 9600;
			break;
		case B19200:
			baud = 19200;
			break;
		case B38400:
			baud = 38400;
			break;
		case B57600:
			baud = 57600;
			break;
		case B115200:
			baud = 115200;
			break;
		case B230400:
			baud = 230400;
			break;
		case B460800:
			baud = 460800;
			break;
		case B500000:
			baud = 500000;
			break;
		case B576000:
			baud = 576000;
			break;
		case B921600:
			baud = 921600;
			break;
		case B1000000:
			baud = 1000000;
			break;
		case B1152000:
			baud = 1152000;
			break;
		case B1500000:
			baud = 1500000;
			break;
		case B2000000:
			baud = 2000000;
			break;
		case B2500000:
			baud = 2500000;
			break;
		case B3000000:
			baud = 3000000;
			break;
		case B3500000:
			baud = 3500000;
			break;
		case B4000000:
			baud = 4000000;
			break;
	}

	switch (t->c_cflag & CSIZE) {
		case CS5:
			bits = 5;
			break;
		case CS6:
			bits = 6;
			break;
		case CS7:
			bits = 7;
			break;
		default:
			bits = 8;
			break;
	}

	parity = CRONYX_PARITY_NONE;
	if (t->c_cflag & PARENB) {
		parity = CRONYX_PARITY_EVEN;
		if (t->c_cflag & PARODD)
			parity = CRONYX_PARITY_ODD;
	}

	ignpar = parity == 0 || (t->c_iflag & IGNPAR) != 0;
	stop2 = (t->c_cflag & CSTOPB) != 0;
	rtscts = (t->c_cflag & CRTSCTS) != 0;
	ixon = (t->c_iflag & IXON) != 0;
	ixany = (t->c_iflag & IXANY) != 0;
	symstart = t->c_cc[VSTART];
	symstop = t->c_cc[VSTOP];

	h->dispatch.set_async_param (h, baud, bits, parity, stop2, ignpar, rtscts, ixon, ixany, symstart, symstop);

	if (t->c_cflag & CLOCAL)
		p->flags &= ~ASYNC_CHECK_CD;
	else
		p->flags |= ASYNC_CHECK_CD;
}

static int asy_open (struct tty_struct *tty, struct file *filp)
{
#if LINUX_VERSION_CODE < 0x020545
	cronyx_binder_item_t *h = cronyx_minor2item (MINOR (tty->device));
#else
	cronyx_binder_item_t *h = cronyx_minor2item (tty->index);
#endif
	asy_t *p;
	int error;

	if (!h)
		return -ENODEV;
	p = h->sw;

	/*
	 * On the first open of the device setup the port hardware, and
	 * initialize the per port data structure.
	 */
	down (&p->sem);
	tty->driver_data = h;
	p->tty = tty;
	p->refcount++;

	if (!(p->flags & ASYNC_INITIALIZED)) {
		asy_setup_parameters (h);
		p->carrier = (h->dispatch.query_dcd) ? h->dispatch.query_dcd (h) : 1;
		h->dispatch.link_up (h);
		clear_bit (TTY_IO_ERROR, &tty->flags);
		p->flags |= ASYNC_INITIALIZED;
	}

	if (p->flags & ASYNC_CLOSING) {
		up (&p->sem);
		wait_event_interruptible (p->close_wait, !(p->flags & ASYNC_CLOSING));
		down (&p->sem);
		error = -ERESTARTSYS;
		if (p->flags & ASYNC_HUP_NOTIFY)
			error = -EAGAIN;
		p->refcount--;
		up (&p->sem);
		return error;
	}

	/*
	 * Based on type of open being done check if it can overlap with any
	 * previous opens still in effect. If we are a normal serial device
	 * then also we might have to wait for carrier.
	 */
#if LINUX_VERSION_CODE < 0x020545
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
#else
	if (tty->driver->subtype == SERIAL_TYPE_CALLOUT) {
#endif
		if (p->flags & ASYNC_NORMAL_ACTIVE) {
			up (&p->sem);
			return -EBUSY;
		}
		if (p->flags & ASYNC_CALLOUT_ACTIVE) {
			if ((p->flags & ASYNC_SESSION_LOCKOUT) &&
			    p->session != cronyx_task_session (current)) {
				up (&p->sem);
				return -EBUSY;
			}
			if ((p->flags & ASYNC_PGRP_LOCKOUT) &&
				p->pgrp != cronyx_task_pgrp (current)) {
				up (&p->sem);
				return -EBUSY;
			}
		}
		p->flags |= ASYNC_CALLOUT_ACTIVE;
	} else {
		if (filp->f_flags & O_NONBLOCK) {
			if (p->flags & ASYNC_CALLOUT_ACTIVE) {
				up (&p->sem);
				return -EBUSY;
			}
		} else {
			error = asy_waitcarrier (h, filp);
			if (error != 0) {
				p->refcount--;
				up (&p->sem);
				return error;
			}
		}
		p->flags |= ASYNC_NORMAL_ACTIVE;
	}

	if (p->refcount == 1 && (p->flags & ASYNC_SPLIT_TERMIOS)) {
#if LINUX_VERSION_CODE < 0x020545
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
#else
		if (tty->driver->subtype == SERIAL_TYPE_NORMAL)
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
			*tty->termios = p->normaltermios;
		else
			*tty->termios = p->callouttermios;
#else
			tty->termios = p->normaltermios;
		else
			tty->termios = p->callouttermios;
#endif
		asy_setup_parameters (h);
	}
	p->session = cronyx_task_session (current);
	p->pgrp = cronyx_task_pgrp (current);
	up (&p->sem);
	return 0;
}

static void asy_close (struct tty_struct *tty, struct file *filp)
{
	cronyx_binder_item_t *h = tty->driver_data;
	asy_t *p;
	struct tty_ldisc *ld;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!h)
		return;
	p = h->sw;

	down (&p->sem);
	if (tty_hung_up_p (filp) || p->refcount-- > 1) {
		up (&p->sem);
		return;
	}
	p->refcount = 0;
	p->flags |= ASYNC_CLOSING;

	if (p->flags & ASYNC_NORMAL_ACTIVE)
		p->normaltermios = *termios;
	if (p->flags & ASYNC_CALLOUT_ACTIVE)
		p->callouttermios = *termios;

	/*
	 * May want to wait for any data to drain before closing. The BUSY
	 * flag keeps track of whether we are still sending or not.
	 */
	tty->closing = 1;
	if (! signal_pending (current)) {
		if (test_bit (ASYI_TXBUSY, &p->istate) && p->closing_wait != ASYNC_CLOSING_WAIT_NONE)
			tty_wait_until_sent (tty, p->closing_wait);
		asy_flush_buffer (tty);
	}

	p->flags &= ~ASYNC_INITIALIZED;
	p->istate = 0;
	set_bit (TTY_IO_ERROR, &tty->flags);
	if (! signal_pending (current)) {
		ld = tty_ldisc_ref (tty);
		if (ld) {
			if (tty_ldisc_ops (ld)->flush_buffer)
				tty_ldisc_ops (ld)->flush_buffer (tty);
			tty_ldisc_deref (ld);
		}
		asy_flush_buffer (tty);
	}
	h->dispatch.link_down (h);
	if (termios->c_cflag & HUPCL) {
		if (h->dispatch.set_dtr)
			h->dispatch.set_dtr (h, 0);
		if (h->dispatch.set_rts)
			h->dispatch.set_rts (h, 0);
	}

	tty->closing = 0;
	tty->driver_data = 0;
	p->tty = 0;

	if (p->openwaitcnt) {
		if (p->close_delay) {
			set_current_state (TASK_INTERRUPTIBLE);
			schedule_timeout (p->close_delay);
		}
		wake_up_interruptible (&p->open_wait);
	}

	p->flags &= ~(ASYNC_CALLOUT_ACTIVE | ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	wake_up_interruptible (&p->close_wait);
	up (&p->sem);
}

/*
 * Hangup this port. This is pretty much like closing the port, only
 * a little more brutal. No waiting for data to drain. Shutdown the
 * port and maybe drop signals.
 */
static void asy_hangup (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!tty || !(h = tty->driver_data))
		return;
	p = h->sw;

	p->flags &= ~ASYNC_INITIALIZED;
	h->dispatch.link_down (h);
	if (termios->c_cflag & HUPCL) {
		if (h->dispatch.set_dtr)
			h->dispatch.set_dtr (h, 0);
		if (h->dispatch.set_rts)
			h->dispatch.set_rts (h, 0);
	}
	p->istate = 0;
	set_bit (TTY_IO_ERROR, &tty->flags);
	tty->driver_data = 0;
	p->tty = 0;
	p->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE);
	p->refcount = 0;
	wake_up_interruptible (&p->open_wait);
	CRONYX_LOG_1 (h, "casync.hangup\n");
}

#if LINUX_VERSION_CODE >= 0x020544
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int asy_tiocmget (struct tty_struct *tty)
#else
static int asy_tiocmget (struct tty_struct *tty, struct file *file)
#endif
{
	cronyx_binder_item_t *h;
	asy_t *p;
	int val;

	if (!tty || !(h = tty->driver_data))
		return -ENODEV;
	p = h->sw;

	/*
	 * Query the modem signals.
	 */
	val = cronyx_get_modem_status (h);
	return val;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int asy_tiocmset (struct tty_struct *tty, unsigned int set, unsigned int clear)
#else
static int asy_tiocmset (struct tty_struct *tty, struct file *file, unsigned int set, unsigned int clear)
#endif
{
	cronyx_binder_item_t *h;
	asy_t *p;

	if (!tty || !(h = tty->driver_data))
		return -ENODEV;
	p = h->sw;

	/*
	 * Set&clear the modem signals.
	 */
	if (h->dispatch.set_dtr && (set & TIOCM_DTR))
		h->dispatch.set_dtr (h, 1);
	if (h->dispatch.set_rts && (set & TIOCM_RTS))
		h->dispatch.set_rts (h, 1);
	if (h->dispatch.set_dtr && (clear & TIOCM_DTR))
		h->dispatch.set_dtr (h, 0);
	if (h->dispatch.set_rts && (clear & TIOCM_RTS))
		h->dispatch.set_rts (h, 0);

	return 0;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int asy_ioctl (struct tty_struct *tty, unsigned int cmd, unsigned long arg)
#else
static int asy_ioctl (struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
#endif
{
	cronyx_binder_item_t *h;
	asy_t *p;
	struct serial_struct sio;
	int val, sigs, error = 0;
	long lval;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!tty || !(h = tty->driver_data))
		return -ENODEV;
	p = h->sw;

	switch (cmd) {
		case TIOCMGET:
			if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof (int))) {
				error = -EFAULT;
				break;
			}

			/*
			 * Query the modem signals.
			 */
			val = cronyx_get_modem_status (h);
			put_user (val, (int *) arg);
			break;

		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			if (!access_ok (VERIFY_READ, (void *) arg, sizeof (int))) {
				error = -EFAULT;
				break;
			}

			/*
			 * Set the modem signals.
			 */
			get_user (val, (int *) arg);
			sigs = (cmd == TIOCMSET) ? ~0 : val;
			if (cmd == TIOCMBIC)
				val = ~val;

			if (h->dispatch.set_dtr && (sigs & TIOCM_DTR))
				h->dispatch.set_dtr (h, (val & TIOCM_DTR) ? 1 : 0);
			if (h->dispatch.set_rts && (sigs & TIOCM_RTS))
				h->dispatch.set_rts (h, (val & TIOCM_RTS) ? 1 : 0);
			break;

		case TCSBRK:
			error = tty_check_change (tty);
			if (error)
				break;
			tty_wait_until_sent (tty, 0);
			if (!arg)
				h->dispatch.send_break (h, 250);
			break;

		case TCSBRKP:
			error = tty_check_change (tty);
			if (error)
				break;
			tty_wait_until_sent (tty, 0);
			h->dispatch.send_break (h, arg ? arg * 100 : 250);
			break;

		case TIOCGSOFTCAR:
			if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof (long))) {
				error = -EFAULT;
				break;
			}

			lval = (termios->c_cflag & CLOCAL) ? 1 : 0;
			if (copy_to_user ((unsigned long *) arg, &lval, sizeof (lval)) != 0) {
				error = -EFAULT;
				break;
			}
			break;

		case TIOCSSOFTCAR:
			if (!access_ok (VERIFY_READ, (void *) arg, sizeof (long))) {
				error = -EFAULT;
				break;
			}

			if (copy_from_user (&lval, (unsigned long *) arg, sizeof (lval)) != 0) {
				error = -EFAULT;
				break;
			}

			if (lval)
				termios->c_cflag |= CLOCAL;
			else
				termios->c_cflag &= ~CLOCAL;
			break;

		case TIOCGSERIAL:
			if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof (struct serial_struct))) {
				error = -EFAULT;
				break;
			}

			memset (&sio, 0, sizeof (sio));
			sio.type = PORT_UNKNOWN;
			sio.line = h->minor;
			sio.flags = p->flags;
			sio.baud_base = p->baud_base;
			sio.close_delay = p->close_delay;
			sio.closing_wait = p->closing_wait;
			sio.custom_divisor = p->custom_divisor;
			sio.xmit_fifo_size = h->fifosz;
			sio.hub6 = 0;
			if (copy_to_user ((void *) arg, &sio, sizeof (sio)) != 0) {
				error = -EFAULT;
				break;
			}
			break;

		case TIOCSSERIAL:
			if (!access_ok (VERIFY_READ, (void *) arg, sizeof (struct serial_struct))) {
				error = -EFAULT;
				break;
			}

			if (copy_from_user (&sio, (void *) arg, sizeof (sio)) != 0) {
				error = -EFAULT;
				break;
			}
#if LINUX_VERSION_CODE < 0x020600
			if (!suser ()
#else
			if (!capable (CAP_SYS_ADMIN)
#endif
			&& (sio.baud_base != p->baud_base || sio.close_delay != p->close_delay ||
			(sio.flags & ~ASYNC_USR_MASK) != (p->flags & ~ASYNC_USR_MASK))) {
				error = -EPERM;
				break;
			}
			p->flags = (p->flags & ~ASYNC_USR_MASK) | (sio.flags & ASYNC_USR_MASK);
			p->baud_base = sio.baud_base;
			p->close_delay = sio.close_delay;
			p->closing_wait = sio.closing_wait;
			p->custom_divisor = sio.custom_divisor;
			asy_setup_parameters (h);
			break;

		default:
			error = -ENOIOCTLCMD;
			break;
	}
	return error;
}

static void asy_set_termios (struct tty_struct *tty, cronyx_termios_t * old)
{
	cronyx_binder_item_t *h;
	cronyx_termios_t *t = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;
	asy_t *p;

	if (!tty || !(h = tty->driver_data))
		return;
	p = h->sw;

	if ((t->c_cflag == old->c_cflag) && (t->c_iflag == old->c_iflag))
		return;

	asy_setup_parameters (h);
	if (h->dispatch.set_dtr)
		h->dispatch.set_dtr (h, (t->c_cflag & (CBAUD & ~CBAUDEX)) ? 1 : 0);
	if ((old->c_cflag & CRTSCTS) && !(t->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		asy_start (tty);
	}
	if (!(old->c_cflag & CLOCAL) && (t->c_cflag & CLOCAL))
		wake_up_interruptible (&p->open_wait);
}

#if LINUX_VERSION_CODE < 0x02060a
static int asy_write (struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
#else
static int asy_write (struct tty_struct *tty, const unsigned char *buf, int count)
{
	int from_user = 0;	/* false */
#endif
	cronyx_binder_item_t *h;
	asy_t *p;
	int lot, done;

	if (!tty || !(h = tty->driver_data))
		return 0;
	p = h->sw;
	done = 0;
	while (done < count) {
		set_bit (ASYI_TXBUSY, &p->istate);
		if (p->skb->len >= p->mtu / 2)
			asy_flush_chars (tty);
		/*
		 * Use up to MTU/2 of the buffer data,
		 * * leaving enough space for the async preprocessing.
		 */
		lot = count - done;
		if (lot > p->mtu / 2 - p->skb->len)
			lot = p->mtu / 2 - p->skb->len;
		if (unlikely (lot <= 0))
			break;
		if (from_user) {
			if (copy_from_user (p->skb->data + p->skb->len, buf + done, lot) != 0)
				return -EFAULT;
		} else
			memcpy (p->skb->data + p->skb->len, buf + done, lot);
		p->skb->len += lot;
		BUG_ON (p->skb->len > p->mtu);
		done += lot;
		if (h->dispatch.transmit (h, p->skb) == 0)
			break;
		p->skb->len = 0;
	}
	CRONYX_LOG_1 (h, "casync.write (%d,%d)\n", count, done);
	return done;
}

#if LINUX_VERSION_CODE >= 0x02061a
static int asy_put_char (struct tty_struct *tty, unsigned char ch)
#else
static void asy_put_char (struct tty_struct *tty, unsigned char ch)
#endif
{
	cronyx_binder_item_t *h;
	asy_t *p;

	if (tty && (h = tty->driver_data) != 0) {
		p = h->sw;
		if (p->skb->len >= p->mtu / 2)
			asy_flush_chars (tty);
		p->skb->data[p->skb->len++] = ch;
	}
#if LINUX_VERSION_CODE >= 0x02061a
	return 1;
#else
	return;
#endif
}

/*
 * If there are any characters in the buffer then make sure that TX
 * interrupts are on and get'em out. Normally used after the putchar
 * routine has been called.
 */
static void asy_flush_chars (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;
	int err;

	if (!tty || !(h = tty->driver_data))
		return;
	p = h->sw;
	if (p->skb->len) {
		err = h->dispatch.transmit (h, p->skb);
		if (err >= 0)
			set_bit (ASYI_TXBUSY, &p->istate);
		if (err != 0)
			p->skb->len = 0;
	}
}

static int asy_write_room (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;

	if (!tty || !(h = tty->driver_data))
		return 0;
	p = h->sw;
	return p->mtu / 2 - p->skb->len;
}

/*
 * Return number of chars in the TX buffer.
 */
static int asy_chars_in_buffer (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;

	if (!tty || !(h = tty->driver_data))
		return 0;
	p = h->sw;
	return p->skb->len;
}

static void asy_flush_buffer (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;
	unsigned long fuse;

	if (!tty || !(h = tty->driver_data))
		return;

	p = h->sw;
	fuse = jiffies;
	while (test_bit (ASYI_TXBUSY, &p->istate) && jiffies - fuse < HZ) {
		if (in_atomic () || irqs_disabled ()) {
			cpu_relax ();
			if (jiffies - fuse > 1)
				break;
		} else
			schedule ();
	}
	p->skb->len = 0;

	if (h->dispatch.flush_transmit_buffer)
		h->dispatch.flush_transmit_buffer (h);
	wake_up_interruptible (&tty->write_wait);

	if (test_bit (TTY_DO_WRITE_WAKEUP, &tty->flags)) {
		struct tty_ldisc *ld = tty_ldisc_ref (tty);
		if (ld) {
			if (tty_ldisc_ops (ld)->write_wakeup)
				tty_ldisc_ops (ld)->write_wakeup (tty);
			tty_ldisc_deref (ld);
		}
	}
}

static void asy_throttle (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!tty || !(h = tty->driver_data))
		return;

	CRONYX_LOG_2 (h, "casync.throttle\n");
	p = h->sw;
	if (termios->c_iflag & IXOFF)
		h->dispatch.send_xoff (h);
	if (h->dispatch.set_rts)
		h->dispatch.set_rts (h, 0);
}

static void asy_unthrottle (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!tty || !(h = tty->driver_data))
		return;

	CRONYX_LOG_2 (h, "casync.unthrottle\n");
	p = h->sw;
	if (termios->c_iflag & IXOFF)
		h->dispatch.send_xon (h);
	if (h->dispatch.set_rts)
		h->dispatch.set_rts (h, 1);
}

/*
 * Stop the transmitter.
 */
static void asy_stop (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;

	if (!tty || !(h = tty->driver_data))
		return;

	p = h->sw;
	h->dispatch.stop_transmitter (h);
}

/*
 * Start the transmitter.
 */
static void asy_start (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	asy_t *p;

	if (!tty || !(h = tty->driver_data))
		return;

	p = h->sw;
	h->dispatch.start_transmitter (h);
}

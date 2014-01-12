/*
 * Sync mode layer for Cronyx serial adapters.
 *
 * Copyright (C) 2001-2005 Cronyx Engineering.
 * Author: Roman Kurakin <rik@cronyx.ru>
 *
 * This source is derived from
 * Async protocol layer for Cronyx serial adapters
 * by Serge Vakulenko <vak@cronyx.ru>
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
 * $Id: sync.c,v 1.58 2009-09-04 17:10:37 ly Exp $
 */
#include "module.h"
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/tty_driver.h>
#include <linux/serial.h>
#if LINUX_VERSION_CODE < 0x020600
#	include <linux/devfs_fs_kernel.h>
#endif
#include "cserial.h"

MODULE_AUTHOR ("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION ("Cronyx syncronous mode layer\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE ("Dual BSD/GPL");

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
	int refcount;
	int openwaitcnt;
	int closing_wait;
	char carrier;		/* DCD modem signal */
	volatile unsigned rx_error;
	struct tty_struct *tty;
	wait_queue_head_t open_wait;
	wait_queue_head_t close_wait;
	cronyx_termios_t normaltermios;
#if LINUX_VERSION_CODE < 0x020600
	struct tq_struct tqueue;
#else
	struct work_struct tqueue;
#endif
	struct sk_buff * volatile skb;
	volatile unsigned skb_len;
	struct semaphore sem;
	struct timer_list timer;
} syn_t;

#if LINUX_VERSION_CODE < 0x020600
static struct tty_driver __syn_serial;
#define syn_serial (&__syn_serial)
static struct tty_struct *syn_ttys[CRONYX_MINOR_MAX];
static int syn_refcount;
#else
static struct tty_driver *syn_serial;
#endif
static cronyx_termios_t *syn_termios[CRONYX_MINOR_MAX];
#if LINUX_VERSION_CODE < 0x030409
static cronyx_termios_t *syn_termioslocked[CRONYX_MINOR_MAX];
#endif
static syn_t syn_data[CRONYX_MINOR_MAX];

#if LINUX_VERSION_CODE < 0x020614
static void syn_offintr (void *private);
#else
static void syn_offintr (struct work_struct *work);
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

static void syn_receive (cronyx_binder_item_t * h, struct sk_buff *skb)
{
	syn_t *p = h->sw;
	struct tty_struct *tty;
	unsigned char error;

	CRONYX_LOG_2 (h, "csync.receive\n");
	tty = p->tty;
	del_timer (&p->timer);
	error = xchg (&p->rx_error, TTY_NORMAL);

	if (tty) {
		struct tty_ldisc *ld = tty_ldisc_ref (tty);
		if (ld) {
			if (tty_ldisc_ops (ld)->receive_buf)
				tty_ldisc_ops (ld)->receive_buf (tty, skb->data, &error, skb->len);
			tty_ldisc_deref (ld);
		}
	}
	dev_kfree_skb_any (skb);
}

static void syn_report_error (struct tty_struct *tty, unsigned char error)
{
	char dummy = -1;

	if (error != TTY_NORMAL && tty) {
		struct tty_ldisc *ld = tty_ldisc_ref (tty);
		if (ld) {
			if (tty_ldisc_ops (ld)->receive_buf)
				tty_ldisc_ops (ld)->receive_buf (tty, &dummy, &error, 1);
			tty_ldisc_deref (ld);
		}
	}
}

static void syn_report_error_timer (unsigned long arg)
{
	syn_t *p = (syn_t *) arg;
	unsigned char error;

	error = xchg (&p->rx_error, TTY_NORMAL);
	syn_report_error (p->tty, error);
}

static void syn_push_error (syn_t *p, unsigned error)
{
	unsigned char prev_error;

	prev_error = xchg (&p->rx_error, error);
	if (prev_error != error) {
		syn_report_error (p->tty, prev_error);
		p->timer.data = (unsigned long) p;
		p->timer.function = &syn_report_error_timer;
		mod_timer (&p->timer, jiffies + HZ / 4);
	}
}

static void syn_receive_error (cronyx_binder_item_t * h, int errcode)
{
	syn_t *p = h->sw;
	struct tty_struct *tty = p->tty;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!tty || !termios)
		return;

	CRONYX_LOG_2 (h, "csync.receive-error %d\n", errcode);
	switch (errcode) {
		case CRONYX_ERR_FRAMING:
			if (!(termios->c_iflag & IGNPAR))
				syn_push_error (p, TTY_FRAME);
			break;

		case CRONYX_ERR_CHECKSUM:
			if (!(termios->c_iflag & IGNPAR) && (termios->c_iflag & INPCK))
				syn_push_error (p, TTY_PARITY);
			break;

		case CRONYX_ERR_BREAK:
			if (!(termios->c_iflag & IGNBRK))
				syn_push_error (p, TTY_BREAK);
			break;

		case CRONYX_ERR_OVERFLOW:
		case CRONYX_ERR_OVERRUN:
			syn_push_error (p, TTY_OVERRUN);
			break;
	}
}

/*
 * Transmit interrupt handler. This has gotta be fast!  Handling TX
 * chars is pretty simple, stuff as many as possible from the TX buffer
 * into the controller FIFO. Must also handle TX breaks here, since they
 * are embedded as commands in the data stream.
 * In practice it is possible that interrupts are enabled but that the
 * port has been hung up.
 */
static void syn_transmit_done (cronyx_binder_item_t * h)
{
	syn_t *p = h->sw;
	struct sk_buff *skb;

	CRONYX_LOG_2 (h, "csync.transmit-done\n");
	skb = xchg (&p->skb, 0);

	if (skb) {
		if (h->dispatch.transmit (h, skb) == 0)
			CRONYX_LOG_1 (h, "csync.transmit-overflow\n");
		dev_kfree_skb_any (skb);
	} else
		clear_bit (ASYI_TXBUSY, &p->istate);

	if (!test_and_set_bit (ASYI_TXLOW, &p->istate))
		schedule_work (&p->tqueue);
}

/*
 * Modem interrupt handler. The is called when the modem signal line
 * (DCD) has changed state. Leave most of the work to the off-level
 * processing routine.
 */
static void syn_modem_event (cronyx_binder_item_t * h)
{
	syn_t *p = h->sw;
	unsigned int old_carrier;

	old_carrier = p->carrier;
	p->carrier = (h->dispatch.query_dcd) ? h->dispatch.query_dcd (h) : 1;
	if (p->carrier && !old_carrier) {
		/*
		 * Carrier detected.
		 */
		CRONYX_LOG_1 (h, "csync.carrier-detect\n");
		wake_up_interruptible (&p->open_wait);
	}

	if (old_carrier && !p->carrier && (p->flags & ASYNC_CHECK_CD)) {
		/*
		 * Carrier lost.
		 */
		set_bit (ASYI_DCDLOST, &p->istate);
		schedule_work (&p->tqueue);
		CRONYX_LOG_1 (h, "csync.carrier-lost\n");
	}
}

static int syn_ctl_set (cronyx_binder_item_t * item, struct cronyx_ctl_t *ctl)
{
	if (ctl->param_id == cronyx_channel_mode) {
		if (ctl->u.param.value == CRONYX_MODE_HDLC)
			return 0;
		return -EBUSY;
	}
	return -ENOSYS;
}

static int syn_attach (cronyx_binder_item_t * h)
{
	int error;
	syn_t *p;

	CRONYX_LOG_2 (h, "csync.attach\n");
	if (cronyx_param_set (h, cronyx_channel_mode, CRONYX_MODE_HDLC) < 0) {
		CRONYX_LOG_1 (h, "csync: unable set channel hdlc-mode");
		return -EIO;
	}

	error = cronyx_binder_minor_get (h, CRONYX_SVC_TTY_SYNC);
	if (error < 0)
		return error;

	p = syn_data + h->minor;
	memset (p, 0, sizeof (*p));
	p->baud_base = 115200;
	p->close_delay = (5 * HZ + 9) / 10;
	p->closing_wait = 30 * HZ;
	p->normaltermios = syn_serial->init_termios;
#if LINUX_VERSION_CODE < 0x020600
	p->tqueue.routine = syn_offintr;
	p->tqueue.data = p;
#else
	CRONYX_INIT_WORK (&p->tqueue, syn_offintr, p);
#endif
	sema_init (&p->sem, 1);
	init_timer (&p->timer);

	h->sw = p;
	init_waitqueue_head (&p->open_wait);
	init_waitqueue_head (&p->close_wait);

#if LINUX_VERSION_CODE < 0x020600
	tty_register_devfs (syn_serial, DEVFS_FL_NONE, h->minor);
#else
	tty_register_device (syn_serial, h->minor, NULL);
#endif


#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get (THIS_MODULE);
#endif
	return 0;
}

static int syn_detach (cronyx_binder_item_t * h)
{
	syn_t *p = h->sw;

	down (&p->sem);
	if (p->refcount > 0 || p->openwaitcnt > 0) {
		up (&p->sem);
		return -EBUSY;
	}
	CRONYX_LOG_2 (h, "csync.detach\n");

#if LINUX_VERSION_CODE < 0x020600
	tty_unregister_devfs (syn_serial, h->minor);
#else
	tty_unregister_device (syn_serial, h->minor);
#endif

	cronyx_binder_minor_put (h);
	h->sw = 0;
	h->dispatch.link_down (h);
	up (&p->sem);
#if LINUX_VERSION_CODE < 0x020600
	MOD_DEC_USE_COUNT;
#else
	module_put (THIS_MODULE);
#endif
	return 0;
}

static cronyx_proto_t dispatch_tab = {
	.name = "sync",
	.chanmode_lock = 1,
	.notify_receive = syn_receive,
	.notify_receive_error = syn_receive_error,
	.notify_transmit_done = syn_transmit_done,
	.notify_modem_event = syn_modem_event,
	.attach = syn_attach,
	.detach = syn_detach,
	.ctl_set = syn_ctl_set
};

static int syn_open (struct tty_struct *tty, struct file *filp);
static void syn_close (struct tty_struct *tty, struct file *filp);
static void syn_hangup (struct tty_struct *tty);

#if LINUX_VERSION_CODE >= 0x020544
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int syn_tiocmget (struct tty_struct *tty);
static int syn_tiocmset (struct tty_struct *tty, unsigned int set, unsigned int clear);
#else
static int syn_tiocmget (struct tty_struct *tty, struct file *file);
static int syn_tiocmset (struct tty_struct *tty, struct file *file, unsigned int set, unsigned int clear);
#endif
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int syn_ioctl (struct tty_struct *tty, unsigned int cmd, unsigned long arg);
#else
static int syn_ioctl (struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
#endif

#if LINUX_VERSION_CODE < 0x02060a
static int syn_write (struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
#else
static int syn_write (struct tty_struct *tty, const unsigned char *buf, int count);
#endif
static int syn_write_room (struct tty_struct *tty);
static int syn_chars_in_buffer (struct tty_struct *tty);
static void syn_flush_buffer (struct tty_struct *tty);
static void syn_set_termios (struct tty_struct *tty, cronyx_termios_t * old);

#if LINUX_VERSION_CODE >= 0x02061a
static const struct tty_operations syn_ops = {
	.open = syn_open,
	.close = syn_close,
	.write = syn_write,
	.write_room = syn_write_room,
	.chars_in_buffer = syn_chars_in_buffer,
	.ioctl = syn_ioctl,
	.set_termios = syn_set_termios,
	.hangup = syn_hangup,
	.flush_buffer = syn_flush_buffer,
	.tiocmget = syn_tiocmget,
	.tiocmset = syn_tiocmset
};
#endif

/*
 * Set up the tty driver structure and register tty device.
 */
int init_module (void)
{
	int err;
#if LINUX_VERSION_CODE < 0x020600
	EXPORT_NO_SYMBOLS;
#endif

#if LINUX_VERSION_CODE < 0x020600
	__syn_serial.magic = TTY_DRIVER_MAGIC;
	__syn_serial.num = CRONYX_MINOR_MAX;
#else
	syn_serial = alloc_tty_driver (CRONYX_MINOR_MAX);
	if (! syn_serial)
		return -ENOMEM;
#endif

#if LINUX_VERSION_CODE >= 0x02053b
	syn_serial->owner = THIS_MODULE;
#endif
	syn_serial->driver_name = "/dev/cronyx/sync";
	syn_serial->name = "ttyZ";
	syn_serial->major = CRONYX_MJR_SYNC_SERIAL;
	syn_serial->type = TTY_DRIVER_TYPE_SERIAL;
	syn_serial->subtype = SERIAL_TYPE_NORMAL;
	syn_serial->init_termios = tty_std_termios;
	syn_serial->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS | TTY_DRIVER_DYNAMIC_DEV;
#if LINUX_VERSION_CODE < 0x020547
	syn_serial->refcount = &syn_refcount;
	syn_serial->table = syn_ttys;
#elif TTY_DRIVER_NO_DEVFS
	syn_serial->devfs_name = "ttyZ";
#endif
	syn_serial->termios = syn_termios;
#if LINUX_VERSION_CODE < 0x030409
	syn_serial->termios_locked = syn_termioslocked;
#endif

#if LINUX_VERSION_CODE >= 0x02061a
	syn_serial->ops = &syn_ops;
#else
	syn_serial->open = syn_open;
	syn_serial->close = syn_close;
	syn_serial->write = syn_write;
	syn_serial->put_char = NULL;
	syn_serial->write_room = syn_write_room;
	syn_serial->chars_in_buffer = syn_chars_in_buffer;
	syn_serial->ioctl = syn_ioctl;
	syn_serial->set_termios = syn_set_termios;
	syn_serial->hangup = syn_hangup;
	syn_serial->flush_buffer = syn_flush_buffer;
#if LINUX_VERSION_CODE >= 0x020544
	syn_serial->tiocmget = syn_tiocmget;
	syn_serial->tiocmset = syn_tiocmset;
#endif
#endif

	err = tty_register_driver (syn_serial);
	if (err < 0) {
		printk (KERN_ERR "csync: failed to register serial driver\n");
#if LINUX_VERSION_CODE >= 0x020600
		put_tty_driver (syn_serial);
#endif
		return err;
	}

	cronyx_binder_register_protocol (&dispatch_tab);
	printk (KERN_DEBUG "Cronyx sync protocol module loaded\n");
	return 0;
}

void cleanup_module (void)
{
	tty_unregister_driver (syn_serial);
	cronyx_binder_unregister_protocol (&dispatch_tab);
#if LINUX_VERSION_CODE >= 0x020600
	put_tty_driver (syn_serial);
#endif
	printk (KERN_DEBUG "Cronyx sync protocol module unloaded\n");
}

#if LINUX_VERSION_CODE < 0x020614
static void syn_offintr (void *private)
{
	syn_t *p = private;
#else
static void syn_offintr (struct work_struct *work)
{
	syn_t *p = container_of (work, syn_t, tqueue);
#endif /* LINUX_VERSION_CODE < 0x020614 */
	struct tty_struct *tty;

	tty = p->tty;
	if (tty == NULL)
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
		tty_hangup (tty);
}

/*
 * Wait for carrier (DCD signal) to come high.
 * But if we are clocal then we don't need to wait...
 */
static int syn_waitcarrier (cronyx_binder_item_t * h, struct file *filp)
{
	syn_t *p = h->sw;
	int error = 0, doclocal = 0;
	struct ktermios *termios = p && p->tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		p->tty->termios
#else
		&p->tty->termios
#endif
		: NULL;

	CRONYX_LOG_2 (h, "csync.wait-carrier\n");
	if (termios->c_cflag & CLOCAL)
		doclocal = 1;

	p->openwaitcnt++;
	if (p->refcount > 0)
		p->refcount--;

	if (wait_event_interruptible(p->open_wait, ({
		int done = 0;
		if (h->dispatch.set_dtr)
			h->dispatch.set_dtr (h, 1);
		if (h->dispatch.set_rts)
			h->dispatch.set_rts (h, 1);
		if (tty_hung_up_p (filp) || !(p->flags & ASYNC_INITIALIZED)) {
			error = -EBUSY;
			if (! (p->flags & ASYNC_HUP_NOTIFY))
				error = -ERESTARTSYS;
			done = 1;
		} else if (!(p->flags & ASYNC_CLOSING) && (doclocal || p->carrier))
			done = 1;
		done;
	})))
		error = -EINTR;

	if (!tty_hung_up_p (filp))
		p->refcount++;
	p->openwaitcnt--;

	return error;
}

/*
 * Set up the channel modes based on the termios port settings.
 */
static void syn_setup_parameters (cronyx_binder_item_t * h)
{
	syn_t *p = h->sw;
	cronyx_termios_t *t = p && p->tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		p->tty->termios
#else
		&p->tty->termios
#endif
		: NULL;

	CRONYX_LOG_2 (h, "csync.set_async_param\n");
	if (t->c_cflag & CLOCAL)
		p->flags &= ~ASYNC_CHECK_CD;
	else
		p->flags |= ASYNC_CHECK_CD;
}

static int syn_open (struct tty_struct *tty, struct file *filp)
{
#if LINUX_VERSION_CODE < 0x020545
	cronyx_binder_item_t *h = cronyx_minor2item (MINOR (tty->device));
#else
	cronyx_binder_item_t *h = cronyx_minor2item (tty->index);
#endif
	syn_t *p;
	int error;

	if (!h)
		return -ENODEV;
	CRONYX_LOG_2 (h, ">> sync.open\n");
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
		syn_setup_parameters (h);
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
	if (!(filp->f_flags & O_NONBLOCK)) {
		error = syn_waitcarrier (h, filp);
		if (error != 0) {
			p->refcount--;
			up (&p->sem);
			return error;
		}
	}
	p->flags |= ASYNC_NORMAL_ACTIVE;

	if (p->refcount == 1 && (p->flags & ASYNC_SPLIT_TERMIOS)) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		*tty->termios = p->normaltermios;
#else
		tty->termios = p->normaltermios;
#endif
		syn_setup_parameters (h);
	}
	up (&p->sem);
	CRONYX_LOG_2 (h, "<< sync.open\n");

	return 0;
}

static void syn_close (struct tty_struct *tty, struct file *filp)
{
	cronyx_binder_item_t *h = tty->driver_data;
	syn_t *p;
	struct tty_ldisc *ld;
	struct sk_buff *skb;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!h)
		return;
	CRONYX_LOG_2 (h, ">> sync.close\n");
	p = h->sw;

	down (&p->sem);
	if (tty_hung_up_p (filp) || p->refcount-- > 1) {
		up (&p->sem);
		CRONYX_LOG_2 (h, "<< sync.close (1)\n");
		return;
	}
	p->refcount = 0;
	p->flags |= ASYNC_CLOSING;

	if (p->flags & ASYNC_NORMAL_ACTIVE)
		p->normaltermios = *termios;

	/*
	 * May want to wait for any data to drain before closing. The BUSY
	 * flag keeps track of whether we are still sending or not.
	 */
	tty->closing = 1;
	if (! signal_pending (current)) {
		if (test_bit (ASYI_TXBUSY, &p->istate) && p->closing_wait != ASYNC_CLOSING_WAIT_NONE)
			tty_wait_until_sent (tty, p->closing_wait);
		syn_flush_buffer (tty);
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
		syn_flush_buffer (tty);
	}

	h->dispatch.link_down (h);
	if (termios->c_cflag & HUPCL) {
		if (h->dispatch.set_dtr)
			h->dispatch.set_dtr (h, 0);
		if (h->dispatch.set_rts)
			h->dispatch.set_rts (h, 0);
	}

	del_timer_sync (&p->timer);
	tty->closing = 0;
	tty->driver_data = 0;
	p->tty = 0;

	if (p->openwaitcnt) {
		if (p->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout (p->close_delay);
		}
		wake_up_interruptible (&p->open_wait);
	}
	p->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	skb = xchg (&p->skb, 0);
	if (skb)
		dev_kfree_skb_any (skb);
	wake_up_interruptible (&p->close_wait);
	up (&p->sem);
	CRONYX_LOG_2 (h, "<< sync.close (2)\n");
}

/*
 * Hangup this port. This is pretty much like closing the port, only
 * a little more brutal. No waiting for data to drain. Shutdown the
 * port and maybe drop signals.
 */
static void syn_hangup (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	syn_t *p;
	struct sk_buff *skb;
	struct ktermios *termios = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;

	if (!tty || !(h = tty->driver_data))
		return;

	CRONYX_LOG_2 (h, ">> sync.hangup\n");
	p = h->sw;

	down (&p->sem);
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
	p->flags &= ~(ASYNC_NORMAL_ACTIVE);
	p->refcount = 0;
	skb = xchg (&p->skb, 0);
	if (skb)
		dev_kfree_skb_any (skb);
	wake_up_interruptible (&p->open_wait);
	up (&p->sem);
	CRONYX_LOG_2 (h, "<< sync.hangup\n");
}

#if LINUX_VERSION_CODE >= 0x020544
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 10)
static int syn_tiocmget (struct tty_struct *tty)
#else
static int syn_tiocmget (struct tty_struct *tty, struct file *file)
#endif
{
	cronyx_binder_item_t *h;
	syn_t *p;
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
static int syn_tiocmset (struct tty_struct *tty, unsigned int set, unsigned int clear)
#else
static int syn_tiocmset (struct tty_struct *tty, struct file *file, unsigned int set, unsigned int clear)
#endif
{
	cronyx_binder_item_t *h;
	syn_t *p;

	if (!tty || !(h = tty->driver_data))
		return -ENODEV;
	p = h->sw;

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
static int syn_ioctl (struct tty_struct *tty, unsigned int cmd, unsigned long arg)
#else
static int syn_ioctl (struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
#endif
{
	cronyx_binder_item_t *h;
	syn_t *p;
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

	CRONYX_LOG_2 (h, "csync.ioctl: %c nr:%d size:%d \n", _IOC_TYPE (cmd), _IOC_NR (cmd), _IOC_SIZE (cmd));
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

			get_user (val, (int *) arg);
			if (cmd == TIOCMSET)
				sigs = ~0;
			else
				sigs = val;
			if (cmd == TIOCMBIC)
				val = ~val;

			if (h->dispatch.set_dtr && (sigs & TIOCM_DTR))
				h->dispatch.set_dtr (h, (val & TIOCM_DTR) ? 1 : 0);
			if (h->dispatch.set_rts && (sigs & TIOCM_RTS))
				h->dispatch.set_rts (h, (val & TIOCM_RTS) ? 1 : 0);
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
			&& (sio.baud_base != p->baud_base ||  sio.close_delay != p->close_delay ||
			(sio.flags & ~ASYNC_USR_MASK) != (p->flags & ~ASYNC_USR_MASK))) {
				error = -EPERM;
				break;
			}
			p->flags = (p->flags & ~ASYNC_USR_MASK) | (sio.flags & ASYNC_USR_MASK);
			p->baud_base = sio.baud_base;
			p->close_delay = sio.close_delay;
			p->closing_wait = sio.closing_wait;
			p->custom_divisor = sio.custom_divisor;
			syn_setup_parameters (h);
			break;

		default:
			error = -ENOIOCTLCMD;
			break;
	}
	return error;
}

static void syn_set_termios (struct tty_struct *tty, cronyx_termios_t * old)
{
	cronyx_binder_item_t *h;
	cronyx_termios_t *t = tty ?
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		tty->termios
#else
		&tty->termios
#endif
		: NULL;
	syn_t *p;

	if (!tty || !(h = tty->driver_data))
		return;
	CRONYX_LOG_2 (h, "csync.set_termios\n");
	p = h->sw;

	if ((t->c_cflag == old->c_cflag) && (t->c_iflag == old->c_iflag))
		return;

	syn_setup_parameters (h);
	if (h->dispatch.set_dtr)
		h->dispatch.set_dtr (h, (t->c_cflag & (CBAUD & ~CBAUDEX)) ? 1 : 0);
	if ((old->c_cflag & CRTSCTS) && !(t->c_cflag & CRTSCTS))
		tty->hw_stopped = 0;
	if (!(old->c_cflag & CLOCAL) && (t->c_cflag & CLOCAL))
		wake_up_interruptible (&p->open_wait);
}

#if LINUX_VERSION_CODE < 0x02060a
static int syn_write (struct tty_struct *tty, int from_user, const unsigned char *buf, int len)
{
#else
static int syn_write (struct tty_struct *tty, const unsigned char *buf, int len)
{
	int from_user = 0;	/* false */
#endif
	cronyx_binder_item_t *h;
	struct sk_buff *skb;
	syn_t *p;
	int err;

	if (!tty || !(h = tty->driver_data))
		return 0;
	CRONYX_LOG_2 (h, "csync.write\n");
	p = h->sw;

	if (p->skb || !len)
		return 0;

	if (len > h->mtu)
		return -ENOMEM;

	skb = dev_alloc_skb (len);
	if (skb == NULL)
		return -ENOMEM;

	if (from_user) {
		if (copy_from_user (skb->data, buf, len) != 0) {
			dev_kfree_skb_any (skb);
			return -EFAULT;
		}
	} else
		memcpy (skb->data, buf, len);
	skb_put (skb, len);

	err = h->dispatch.transmit (h, skb);
	if (err >= 0)
		set_bit (ASYI_TXBUSY, &p->istate);
	if (err != 0)
		dev_kfree_skb_any (skb);
	else {
		p->skb_len = len;
		skb = xchg (&p->skb, skb);
		if (unlikely (skb != 0)) {
			CRONYX_LOG_1 (h, "csync.write-overrun");
			dev_kfree_skb_any (skb);
		}
	}
	return (err >= 0) ? len : -EIO;
}

static int syn_write_room (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	syn_t *p;
	int len;

	if (!tty || !(h = tty->driver_data))
		return 0;
	CRONYX_LOG_2 (h, "csync.write_room\n");
	p = h->sw;
	len = p->skb ? 0 : h->mtu;
	return len;
}

/*
 * Return number of chars in the TX buffer.
 */
static int syn_chars_in_buffer (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	syn_t *p;
	int len;

	if (!tty || !(h = tty->driver_data))
		return 0;
	CRONYX_LOG_2 (h, "csync.chars_in_buffer\n");
	p = h->sw;
	len = p->skb ? p->skb_len : 0;
	return len;
}

static void syn_flush_buffer (struct tty_struct *tty)
{
	cronyx_binder_item_t *h;
	syn_t *p;
	unsigned long fuse;

	if (!tty || !(h = tty->driver_data))
		return;

	CRONYX_LOG_2 (h, "csync.flush\n");
	p = h->sw;
	fuse = jiffies;
	while (test_bit (ASYI_TXBUSY, &p->istate) && jiffies - fuse < HZ) {
		if (in_atomic () || irqs_disabled ()) {
			/* LY-TODO: BUG_ON (in_atomic () || irqs_disabled ()); */
			cpu_relax ();
			if (jiffies - fuse > 1)
				break;
		} else
			schedule ();
	}

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

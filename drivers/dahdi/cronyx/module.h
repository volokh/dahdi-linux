/*
 * This file contains defines and includes, common for all modules,
 * and version dependent things.
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
 * $Id: module.h,v 1.112 2009-10-29 13:44:03 ly Exp $
 */
#define CRONYX_VERSION_INFO \
	"Cronyx Linux drivers 6.1.11-20091029"

#if !defined (__GNUC__) || (__GNUC__ < 2)
#	error The GNU C-compiler version 2 or higer is required.
#endif

/* Common includes */
#include <linux/version.h>
#if defined (RHEL_MAJOR) || defined (RHEL_MINOR) \
	|| defined (RHEL_RELEASE_CODE) || defined (RHEL_RELEASE_VERSION)
#	/* warning "Cronyx don't fully support for RHEL, currently only 5.3 release was tested." */
#	define RHEL_VERSION_HELL		1
#else
#	define RHEL_RELEASE_VERSION(a,b)	0
#	define RHEL_RELEASE_CODE		0
#endif /* RHEL */

#if (LINUX_VERSION_CODE < 0x020613) && (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION (5,0))
#	include <linux/config.h>
#endif
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/netdevice.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/if.h>
#include <linux/inetdevice.h>
#include <asm/uaccess.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#ifndef IRQF_DISABLED
#	define IRQF_DISABLED SA_INTERRUPT
#endif

#ifndef IRQF_SAMPLE_RANDOM
#	define IRQF_SAMPLE_RANDOM SA_SAMPLE_RANDOM
#endif

#ifndef IRQF_SHARED
#	define IRQF_SHARED SA_SHIRQ
#endif

#if LINUX_VERSION_CODE < 0x020417
#	error Your kernel is too old for Cronyx drivers, at least 2.4.23 is required.
#endif

#if LINUX_VERSION_CODE >= 0x020600
#	include <linux/mempool.h>
#	define dev_init_buffers(dev) do{}while(0)

#	if LINUX_VERSION_CODE < 0x020614
#		define 	CRONYX_INIT_WORK(_work, _func, _data) INIT_WORK (_work, _func, _data)
#		define cronyx_termios_t struct termios
#	else
#		define 	CRONYX_INIT_WORK(_work, _func, _data) INIT_WORK (_work, _func)
#		define cronyx_termios_t struct ktermios
#	endif /* LINUX_VERSION_CODE < 0x020614 */

#	if LINUX_VERSION_CODE < 0x020611
		void *cronyx_mempool_kmalloc (int gfp_mask, void *pool_data);
		void cronyx_mempool_kfree (void *element, void *pool_data);
		static __inline mempool_t *cronyx_mempool_create_kmalloc_pool (int min_nr, size_t size) {
			return mempool_create (min_nr, cronyx_mempool_kmalloc, cronyx_mempool_kfree, (void *) size);
		}
#		define mempool_create_kmalloc_pool cronyx_mempool_create_kmalloc_pool
#	endif

#else

#	ifndef isa_virt_to_bus
#		define isa_virt_to_bus virt_to_phys
#	endif
#	ifndef schedule_work
#		define schedule_work(a)	schedule_task (a)
#	endif
#	ifndef might_sleep
#		define might_sleep() do{}while(0)
#	endif
#	ifndef prepare_to_wait
#		define prepare_to_wait(head, queue, state) do {	\
			set_current_state (state);		\
			add_wait_queue (head, queue);		\
		} while (0)
#	endif
#	ifndef finish_wait
#		define finish_wait(head, queue)	do {		\
			remove_wait_queue (head, queue);	\
			set_current_state (TASK_RUNNING);	\
		} while (0)
#	endif

#	ifndef DEFINE_WAIT
#		define DEFINE_WAIT(queue) DECLARE_WAITQUEUE (queue, current)
#	endif
#	ifndef in_atomic
#		define in_atomic() 0
#	endif
#	ifndef irqs_disabled
#		define irqs_disabled() in_interrupt()
#	endif
#	define cronyx_termios_t struct termios

#endif

#if (LINUX_VERSION_CODE < 0x02060B) && !defined (DEFINED_SPINLOCK)
#	define DEFINE_SPINLOCK(x) spinlock_t x = SPIN_LOCK_UNLOCKED
#endif

#if !defined (unlikely) || !defined (likely)
#	undef unlikely
#	undef likely
#	if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#		define	__builtin_expect(x, expected_value) (x)
#	endif
#	define likely(x)	__builtin_expect ((x),1)
#	define unlikely(x)	__builtin_expect ((x),0)
#endif

#if LINUX_VERSION_CODE < 0x02060E && !defined (kzalloc)
	extern void *cronyx_kzalloc (size_t size, unsigned flags);
#	define kzalloc(size, flags) cronyx_kzalloc (size, flags)
#endif

#if (LINUX_VERSION_CODE < 0x020616) && (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION (5,0))
static __inline unsigned char *skb_transport_header (const struct sk_buff *skb)
{
	return skb->h.raw;
}

static __inline void skb_reset_transport_header (struct sk_buff *skb)
{
	skb->h.raw = skb->data;
}

static __inline void skb_set_transport_header (struct sk_buff *skb, const int offset)
{
	skb->h.raw = skb->data + offset;
}

static __inline unsigned char *skb_network_header (const struct sk_buff *skb)
{
	return skb->nh.raw;
}

static __inline void skb_reset_network_header (struct sk_buff *skb)
{
	skb->nh.raw = skb->data;
}

static __inline void skb_set_network_header (struct sk_buff *skb, const int offset)
{
	skb->nh.raw = skb->data + offset;
}

static __inline unsigned char *skb_mac_header (const struct sk_buff *skb)
{
	return skb->mac.raw;
}

static __inline int skb_mac_header_was_set (const struct sk_buff *skb)
{
	return skb->mac.raw != NULL;
}

static __inline void skb_reset_mac_header (struct sk_buff *skb)
{
	skb->mac.raw = skb->data;
}

static __inline void skb_set_mac_header (struct sk_buff *skb, const int offset)
{
	skb->mac.raw = skb->data + offset;
}

static __inline int skb_transport_offset (const struct sk_buff *skb)
{
	return skb_transport_header (skb) - skb->data;
}

static __inline u32 skb_network_header_len (const struct sk_buff *skb)
{
	return skb->h.raw - skb->nh.raw;
}

static __inline int skb_network_offset (const struct sk_buff *skb)
{
	return skb_network_header (skb) - skb->data;
}
#else
#define HAVE_NETDEV_PRIV
#endif /* LINUX_VERSION_CODE < 0x020616 */

#ifdef CONFIG_KDB
#	include <linux/kdb.h>
#else
#	define KDB_ENTER() do{}while(0)
#endif

#define CRONYX_TRACE(break) \
	do { \
		printk (KERN_EMERG "file %s, line %d\n", __FILE__, __LINE__); \
		if (break) KDB_ENTER (); \
	} while (0)

#if __GNUC__ < 3
	static __inline void CRONYX_LOG (int level, ...) {
	}
#else
#	define CRONYX_LOG(level,h,f,a...) \
		do { \
			if ((h)->debug >= (level)) \
				printk (KERN_NOTICE "cronyx-%s: " f, (h)->name, ##a); \
		} while (0)
#endif

#define UP(x) \
	do { \
		printk ("up %s.%d\n", __FILE__, __LINE__); \
		up (x); \
	} while (0)

#define DOWN(x) \
	do { \
		printk (">> down %s.%d\n", __FILE__, __LINE__); \
		down (x); \
		printk ("<< down %s.%d\n", __FILE__, __LINE__); \
	} while (0)

#define CRONYX_LOG_1(h,f,a...) CRONYX_LOG (1,h,f,##a)
#define CRONYX_LOG_2(h,f,a...) CRONYX_LOG (2,h,f,##a)
#define CRONYX_LOG_3(h,f,a...) CRONYX_LOG (3,h,f,##a)

#ifdef CONFIG_DEBUG_KERNEL
#	define may_static
#else
#	define may_static static
#endif

static __inline pid_t cronyx_task_session (struct task_struct *tsk)
{
#if LINUX_VERSION_CODE < 0x020606
	return tsk->session;
#elif LINUX_VERSION_CODE < 0x020614
	return tsk->signal->session;
#elif LINUX_VERSION_CODE < 0x020618
	return process_session (tsk);
#elif LINUX_VERSION_CODE < 0x02061E
	return task_session_nr (tsk);
#else
	return task_session_vnr (tsk);
#endif
}

static __inline pid_t cronyx_task_pgrp (struct task_struct *tsk)
{
#if LINUX_VERSION_CODE < 0x020500
	return tsk->pgrp;
#elif LINUX_VERSION_CODE < 0x020606
	return process_group (tsk);
#elif LINUX_VERSION_CODE < 0x020614
	return tsk->signal->pgrp;
#elif LINUX_VERSION_CODE < 0x020618
	return process_group (tsk);
#elif LINUX_VERSION_CODE < 0x02061E
	return task_pgrp_nr (tsk);
#else
	return task_pgrp_vnr (tsk);
#endif
}

#ifndef HAVE_NETDEV_PRIV
static inline void *netdev_priv (const struct net_device *dev)
{
	return dev->priv;
}
#endif /* HAVE_NETDEV_PRIV */

#if LINUX_VERSION_CODE < 0x02041D || (LINUX_VERSION_CODE > 0x020500 && LINUX_VERSION_CODE < 0x020607)
	static inline unsigned int jiffies_to_msecs (const unsigned long j)
	{
#	if HZ <= 1000 && !(1000 % HZ)
		return (1000 / HZ) * j;
#	elif HZ > 1000 && !(HZ % 1000)
		return (j + (HZ / 1000) - 1)/(HZ / 1000);
#	else
		return (j * 1000) / HZ;
#	endif
	}
#endif /* jiffies_to_msecs */

#if !defined (spin_trylock_irqsave) && LINUX_VERSION_CODE < 0x020610
#	define spin_trylock_irqsave(lock, flags) ({	\
		local_irq_save(flags);			\
		spin_trylock(lock) ?			\
		1 : ({local_irq_restore(flags); 0;});	\
	})
#endif /* spin_trylock_irqsave */

#if LINUX_VERSION_CODE < 0x02061B
#	define tty_ldisc_ops(ld) (ld)
#else
#	define tty_ldisc_ops(ld) (ld->ops)
#endif

#if LINUX_VERSION_CODE < 0x020609
#	define tty_ldisc_ref(tty) (&(tty)->ldisc)
#	define tty_ldisc_deref(tty) do{}while(0)
#endif

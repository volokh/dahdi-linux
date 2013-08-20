/*
 * Dynamic protocol binder for Cronyx serial adapters.
 *
 * Copyright (C) 1997-2001 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *
 * Copyright (C) 2001-2005 Cronyx Engineering.
 * Author: Roman Kurakin <rik@cronyx.ru>
 *
 * Copyright (C) 2005-2009 Cronyx Engineering.
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: binder.c,v 1.77 2009-09-09 11:54:56 ly Exp $
 */
#include "module.h"
#include <linux/tty.h>
#include <linux/if_ether.h>
#include <linux/fs.h>
#if LINUX_VERSION_CODE < 0x020600
#include <linux/tqueue.h>
#else
#include <linux/sched.h>
#include <linux/workqueue.h>
#endif
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/semaphore.h>
#include "cserial.h"

MODULE_AUTHOR("Cronyx Engineering, Moscow, Russia.");
MODULE_DESCRIPTION("Cronyx dynamic binder\n" CRONYX_VERSION_INFO "\n");
MODULE_LICENSE("Dual BSD/GPL");

static ushort Debug = 0x00;
module_param(Debug, ushort, 0600);
MODULE_PARM_DESC(Debug, "Default Debug Level: (0..9), 0 - debugging disabled,"
									"9 - maximum debug information (for developers)");

static char *DefaultBindProto = "dahdi";
module_param(DefaultBindProto, charp, S_IRUGO);
MODULE_PARM_DESC(DefaultBindProto, "Set Default Protocol Mode: \"idle\","
								" \"async\", \"sync\", \"cisco\", \"rbrg\", \"fr\", \"raw\","
								" \"packet\", \"dahdi\"(default).");

may_static cronyx_proto_t *proto_list;

typedef struct _binder_node_t {
	cronyx_binder_item_t *item;
	struct _binder_node_t *parent;
	struct _binder_node_t *child;
	struct _binder_node_t *next;
} binder_node_t;

may_static binder_node_t binder_root;

#if LINUX_VERSION_CODE < 0x30409
  may_static DECLARE_MUTEX (binder_sema);
#else
  may_static DEFINE_SEMAPHORE (binder_sema);
#endif

#if LINUX_VERSION_CODE < 0x30409
  may_static DECLARE_MUTEX (binder_topology);
#else
  may_static DEFINE_SEMAPHORE (binder_topology);
#endif

may_static struct task_struct *binder_locker, *binder_deffered_task;
may_static int binder_lockdepth;
may_static binder_node_t *minor2item[CRONYX_MINOR_MAX] = { &binder_root };
may_static int binder_item_id, binder_evolution, binder_total_items,
								binder_push_evolution = -1;
may_static unsigned long binder_last_nominor;

may_static struct timer_list second_timer, led_timer;
may_static LIST_HEAD (leds_list);
may_static LIST_HEAD (deffered_list);

#if LINUX_VERSION_CODE < 0x020600
	may_static struct tq_struct binder_work;
#else /* LINUX_VERSION_CODE < 0x020600 */
	may_static struct work_struct binder_work;
#	if defined (CONFIG_SMP)
	struct workqueue_struct *binder_queue;
#	if !defined (create_singlethread_workqueue) && LINUX_VERSION_CODE < 0x02061C
		may_static DECLARE_MUTEX (binder_workqueue_mutex);
#		define CBINDER_NEED_WQM
#		define create_singlethread_workqueue(name) create_workqueue(name)
#	endif
#	endif /* defined (CONFIG_SMP) */
	may_static mempool_t *pool_deffered_item;
#endif /* LINUX_VERSION_CODE < 0x020600 */

#if defined (CONFIG_PREEMPT_RT)
DEFINE_RAW_SPINLOCK (deffered_lock);
#else
DEFINE_SPINLOCK (deffered_lock);
#endif /* CONFIG_PREEMPT_RT */

struct enum_scope_t {
	int counter;
	int limit;
	int *ids;
};

may_static void binder_get(void)
{
#if LINUX_VERSION_CODE < 0x020600
	MOD_INC_USE_COUNT;
#else
	try_module_get(THIS_MODULE);
#endif
}

may_static void binder_put(void)
{
#if LINUX_VERSION_CODE < 0x020600
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif
}

may_static void binder_lock(void)
{
	might_sleep ();
	if (binder_locker != current) {
		down (&binder_sema);
		binder_locker = current;
	} else {
		BUG_ON (binder_lockdepth < 1);
	}
	++binder_lockdepth;
	BUG_ON (binder_locker != current || binder_lockdepth < 1);
}

may_static void binder_unlock(void)
{
	BUG_ON (binder_locker != current || binder_lockdepth < 1);
	if (--binder_lockdepth == 0) {
		binder_locker = NULL;
		up (&binder_sema);
	}
}

may_static void binder_provider_get(cronyx_binder_item_t* h)
{
	if (h->provider) {
#if LINUX_VERSION_CODE < 0x020600
		__MOD_INC_USE_COUNT (h->provider);
#else
		try_module_get(h->provider);
#endif
	}
}

may_static void binder_provider_put(cronyx_binder_item_t* h)
{
	if (h->provider) {
#if LINUX_VERSION_CODE < 0x020600
		__MOD_DEC_USE_COUNT (h->provider);
#else
		module_put(h->provider);
#endif
	}
}

//-----------------------------------------------------------------------------

struct binder_deffered_t {
	struct list_head entry;
	void *param_1, *param_2;
	union {
		void (*_1) (void *);
		void (*_2) (void *, void *);
	} call;
};

static inline struct binder_deffered_t *binder_alloc_deffered(char *reason)
{
	struct binder_deffered_t *d;

#if LINUX_VERSION_CODE < 0x020600
	d = kmalloc (sizeof(struct binder_deffered_t), GFP_ATOMIC);
#else
	d = mempool_alloc (pool_deffered_item, GFP_ATOMIC);
#endif
	if (unlikely (!d))
		printk (KERN_ERR "cbinder: unable allocate deffered workitem for %s\n", reason);
	return d;
}

static inline void binder_free_deffered(struct binder_deffered_t *d)
{
#if LINUX_VERSION_CODE < 0x020600
	kfree(d);
#else
	mempool_free(d, pool_deffered_item);
#endif
}

#if LINUX_VERSION_CODE < 0x020614
may_static void binder_deffered(void *unused)
#else
may_static void binder_deffered(struct work_struct *work)
#endif /* LINUX_VERSION_CODE < 0x020614 */
{
	unsigned long flags;

#ifdef CBINDER_NEED_WQM
	down (&binder_workqueue_mutex);
#endif

	spin_lock_irqsave(&deffered_lock, flags);
	binder_deffered_task = current;
	while (!list_empty (&deffered_list)) {
		struct binder_deffered_t *d;

		d = list_entry (deffered_list.next, struct binder_deffered_t, entry);
		list_del (&d->entry);
		spin_unlock_irqrestore(&deffered_lock, flags);

		if (d->param_2 == d)
			d->call._1 (d->param_1);
		else
			d->call._2 (d->param_1, d->param_2);

		binder_free_deffered(d);
		spin_lock_irqsave(&deffered_lock, flags);
	}
	binder_deffered_task = NULL;
	spin_unlock_irqrestore(&deffered_lock, flags);

#ifdef CBINDER_NEED_WQM
	up (&binder_workqueue_mutex);
#endif
}

may_static void binder_deffered_queue(struct binder_deffered_t *d)
{
	unsigned long flags;

	spin_lock_irqsave(&deffered_lock, flags);
	list_add_tail (&d->entry, &deffered_list);
	if (binder_deffered_task != current && d->entry.next == &deffered_list && d->entry.prev == &deffered_list) {
#if LINUX_VERSION_CODE < 0x020600
		schedule_task (&binder_work);
#elif defined (CONFIG_SMP)
		queue_work (binder_queue, &binder_work);
#else /* defined (CONFIG_SMP) */
		schedule_work (&binder_work);
#endif /* LINUX_VERSION_CODE < 0x020600 */
	}
	spin_unlock_irqrestore(&deffered_lock, flags);
}

may_static void binder_deffered_wake(void *p1, void *p2)
{
	struct completion *wake_phase = p1;
	struct completion *lock_phase = p2;

	complete(wake_phase);
	if (lock_phase) {
		BUG_ON (binder_locker == current);
		wait_for_completion (lock_phase);
		complete(wake_phase);
	}
}

may_static void binder_deffered_flush_ex (struct completion *lock_phase)
{
	BUG_ON (binder_locker == current);
	might_sleep ();
	if (current != binder_deffered_task) {
		struct binder_deffered_t *d;
		struct completion wake_phase;

		for (;;) {
			d = binder_alloc_deffered("deffered_flush_queue()");
			if (d != NULL)
				break;
			schedule();
		}

		d->param_1 = &wake_phase;
		d->param_2 = lock_phase;
		d->call._2 = binder_deffered_wake;

		init_completion (&wake_phase);
		binder_deffered_queue(d);
		wait_for_completion (&wake_phase);

		if (unlikely (lock_phase != 0)) {
			INIT_COMPLETION (wake_phase);
			binder_lock();
			complete(lock_phase);
			wait_for_completion (&wake_phase);
		}
	}
}

may_static void binder_deffered_flush_and_lock(void)
{
	if (current != binder_deffered_task && current != binder_locker) {
		struct completion lock_phase;

		init_completion (&lock_phase);
		binder_deffered_flush_ex (&lock_phase);
	} else
		binder_lock();
}

void binder_deffered_flush (void)
{
	if (current != binder_deffered_task && current != binder_locker)
		binder_deffered_flush_ex (NULL);
}

EXPORT_SYMBOL(binder_deffered_flush);

//-----------------------------------------------------------------------------

may_static void binder_set_flash_timer (struct cronyx_led_flasher_t *f);
may_static void binder_flash_off (unsigned long arg);
may_static void binder_kick_timers (void);

#ifdef CONFIG_PREEMPT_RT
#	define led_lock(p, flags) \
		do { \
			if (likely (p->use_rawlock)) \
				spin_lock_irqsave(p->lock.raw, flags); \
			else \
				spin_lock_irqsave(p->lock.normal, flags); \
		} while (0)
#	define led_unlock(p, flags) \
		do { \
			if (likely (p->use_rawlock)) \
				spin_unlock_irqrestore(p->lock.raw, flags); \
			else \
				spin_unlock_irqrestore(p->lock.normal, flags); \
		} while (0)
#else
#	define led_lock(p, flags) \
		spin_lock_irqsave(p->lock, flags)
#	define led_unlock(p, flags) \
		spin_unlock_irqrestore(p->lock, flags)
#endif /* CONFIG_PREEMPT_RT */

may_static void binder_flash_off (unsigned long arg)
{
	unsigned long flags;
	struct cronyx_led_flasher_t *f = (struct cronyx_led_flasher_t *) arg;

	if (!f->tag)
		return;
	led_lock(f, flags);
	f->kick = 0;
	f->led_set(f->tag, f->last = cronyx_led_getblink (f));
	led_unlock(f, flags);
}

may_static void binder_set_flash_timer (struct cronyx_led_flasher_t *f)
{
	f->time_off = jiffies + (HZ + 25) / 50;	/* LY: about 20 msec */
	if (!timer_pending (&f->timer)) {
		f->timer.function = binder_flash_off;
		f->timer.data = (unsigned long) f;
		f->timer.expires = f->time_off;
		add_timer (&f->timer);
	}
}

#ifdef CONFIG_PREEMPT_RT
void cronyx_led_init_ex (struct cronyx_led_flasher_t *f, int use_rawlock, void *spinlock, void *tag,
			 void (*led_set) (void *, int on), int(*get_state) (void *))
{
	f->use_rawlock = use_rawlock;
	f->lock.ptr = spinlock;
#else

void cronyx_led_init(struct cronyx_led_flasher_t *f, spinlock_t *spinlock, void *tag,
		      void (*led_set) (void *, int on), int(*get_state) (void *))
{
	f->lock = spinlock;
#endif /* CONFIG_PREEMPT_RT */
	f->tag = tag;
	f->led_set = led_set;
	f->get_state = get_state;
	f->cadence = 0;
	f->time_off = 0;
	f->last = 0;
	f->counter = (((unsigned long) f) >> 2) & 31;
	f->mode = CRONYX_LEDMODE_DEFAULT;
	init_timer (&f->timer);
	binder_deffered_flush_and_lock();
	list_add_tail (&f->entry, &leds_list);
	binder_kick_timers ();
	binder_unlock();
}

#ifdef CONFIG_PREEMPT_RT
EXPORT_SYMBOL(cronyx_led_init_ex);
#else
EXPORT_SYMBOL(cronyx_led_init);
#endif /* CONFIG_PREEMPT_RT */

void cronyx_led_destroy(struct cronyx_led_flasher_t *f)
{
	unsigned long flags;

	binder_deffered_flush_and_lock();
	list_del_init(&f->entry);
	f->mode = CRONYX_LEDMODE_OFF;
	del_timer_sync (&f->timer);

	led_lock(f, flags);
	f->last = 0;
	f->kick = 0;
	f->led_set(f->tag, 0);
	led_unlock(f, flags);
#ifdef CONFIG_PREEMPT_RT
	f->lock.ptr = NULL;
#else
	f->lock = NULL;
#endif /* CONFIG_PREEMPT_RT */

	binder_unlock();
}

EXPORT_SYMBOL(cronyx_led_destroy);

void cronyx_led_kick (struct cronyx_led_flasher_t *f, unsigned kick)
{
	if (!f->tag)
		return;
	if (f->mode & kick) {
		if (f->kick == 0) {
			f->kick = 1;
			f->led_set(f->tag, f->last ^ f->kick);
			binder_set_flash_timer (f);
		}
	}
}

EXPORT_SYMBOL(cronyx_led_kick);

int cronyx_led_ctl (struct cronyx_led_flasher_t *f, unsigned cmd, struct cronyx_ctl_t *ctl)
{
	if (ctl->param_id == cronyx_led_mode) {
		if (cmd == CRONYX_GET) {
			ctl->u.param.value = f->mode;
			ctl->u.param.extra = f->cadence;
			return 0;
		} else if (cmd == CRONYX_SET) {
			if (ctl->u.param.value < 0 || ctl->u.param.value > 0xFF)
				return -EINVAL;
			f->mode = ctl->u.param.value;
			f->cadence = ctl->u.param.extra;
			return 0;
		}
	}
	return -ENOSYS;
}

EXPORT_SYMBOL(cronyx_led_ctl);

int cronyx_led_getblink (struct cronyx_led_flasher_t *f)
{
	static const unsigned long state2cadence[CRONYX_LEDSTATE_MAX] = {
		0	     /* led must be off */,
		0xFFFFFFFEul /* normal state */,
		0x05500550ul /* line-mirror indication */,
		0x00010001ul /* remote alarm, ais, etc, (short flashes) */,
		0x33333333ul /* jitter limit, slips, etc, (fast blinking) */,
		0x007F007Ful /* los, lof, crc-errors, etc, (slow blinking) */
	};
	unsigned actual_cadence;

	switch (f->mode & 0xF0) {
		case CRONYX_LEDMODE_ON:
			return 1;
		case CRONYX_LEDMODE_OFF:
			return 0;
		case CRONYX_LEDMODE_CADENCE:
			actual_cadence = f->cadence;
			break;
		case CRONYX_LEDMODE_DEFAULT:
		default:
			if (f->get_state == NULL)
				return 0;
			actual_cadence = state2cadence[f->get_state(f->tag)];
	}

	return (actual_cadence >> f->counter) & 1;
}

EXPORT_SYMBOL(cronyx_led_getblink);

//-----------------------------------------------------------------------------

may_static void binder_timer2deffer (unsigned long arg);

may_static int __binder_second_work (binder_node_t *node)
{
	int result = 0;

	do {
		if (node->item && node->item->dispatch.second_timer) {
			node->item->dispatch.second_timer (node->item);
			result++;
		}
		if (node->child)
			result += __binder_second_work (node->child);
		node = node->next;
	} while (node);
	return result;
}

may_static void binder_second_work (void *unused)
{
	if (__binder_second_work (&binder_root)) {
		second_timer.function = &binder_timer2deffer;
		mod_timer (&second_timer, jiffies + HZ);
	}
}

may_static void binder_timer2deffer (unsigned long p)
{
	struct binder_deffered_t *d = binder_alloc_deffered("timer");

	if (d != NULL) {
		d->param_2 = d;
		d->param_1 = NULL;
		d->call._1 = (void (*)(void *)) p;
		binder_deffered_queue(d);
	}
}

may_static void binder_led_work (void *unused)
{
	if (!list_empty (&leds_list)) {
		struct cronyx_led_flasher_t *f;
		unsigned long flags;

		list_for_each_entry (f, &leds_list, entry) {
			led_lock(f, flags);
			f->counter = (f->counter + 1) & 31;
			f->last = cronyx_led_getblink (f);
			f->led_set(f->tag, f->last ^ f->kick);
			led_unlock(f, flags);
		}
		led_timer.function = binder_timer2deffer;
		mod_timer (&led_timer, jiffies + (HZ + 11) / 12);
	}
}

may_static void binder_kick_timers (void)
{
	if (!timer_pending (&second_timer) && __binder_second_work (&binder_root)) {
		second_timer.function = &binder_timer2deffer;
		second_timer.data = (unsigned long) &binder_second_work;
		second_timer.expires = jiffies + HZ;
		add_timer (&second_timer);
	}
	if (!timer_pending (&led_timer) && !list_empty (&leds_list)) {
		led_timer.function = &binder_timer2deffer;
		led_timer.data = (unsigned long) &binder_led_work;
		led_timer.expires = jiffies + (HZ + 11) / 12;
		add_timer (&led_timer);
	}
}

//-----------------------------------------------------------------------------

may_static binder_node_t *binder_lookup_item (binder_node_t *from, cronyx_binder_item_t *h);

cronyx_binder_item_t *cronyx_binder_item_get_parent(cronyx_binder_item_t *h,
																										int type)
{
	binder_node_t *node = binder_lookup_item(&binder_root, h);

	while (node && node->item)
	{
		if (node->item->type == type)
			return node->item;

		node = node->parent;
	}

	return NULL;
}
EXPORT_SYMBOL(cronyx_binder_item_get_parent);

cronyx_binder_item_t *cronyx_minor2item(int minor)
{
	binder_node_t *node;

	if (minor < 0 || minor >= sizeof(minor2item) / sizeof(minor2item[0]))
		return NULL;

	node = minor2item[minor];
	if (node == NULL || node->item == NULL || node->item->type != CRONYX_ITEM_CHANNEL)
		return NULL;
	return node->item;
}

EXPORT_SYMBOL(cronyx_minor2item);

may_static binder_node_t *inode2node(struct inode *inode)
{
	int minor;

	minor = MINOR (inode->i_rdev);
	if (minor < 0 || minor >= sizeof(minor2item) / sizeof(minor2item[0]))
		return NULL;
	return minor2item[minor];
}

may_static cronyx_binder_item_t *inode2chan (struct inode *inode)
{
	binder_node_t *node;

	node = inode2node(inode);
	if (node == NULL || node->item == NULL || node->item->type != CRONYX_ITEM_CHANNEL)
		return NULL;
	return node->item;
}

may_static void __binder_enum (binder_node_t *walk, struct enum_scope_t *scope)
{
	do {
		if (scope->counter >= scope->limit)
			break;
		if (walk->item) {
			if (scope->counter >= 0)
				*scope->ids++ = walk->item->id;
			scope->counter++;
		}
		if (walk->child)
			__binder_enum (walk->child, scope);
		walk = walk->next;
	} while (walk);
}

may_static void binder_enum (int from, int *ids, int length)
{
	struct enum_scope_t scope = {
		.counter = -from,
		.ids = ids,
		.limit = length
	};
	__binder_enum (&binder_root, &scope);
	while (scope.ids < ids + length)
		*scope.ids++ = -1;
}

may_static binder_node_t *binder_lookup_name(binder_node_t *from, char *name)
{
	binder_node_t *scan = from;

	do {
		if (scan->item) {
			if (strncmp (name, scan->item->name, CRONYX_ITEM_MAXNAME) == 0
			    || strncmp (name, scan->item->alias, CRONYX_ITEM_MAXNAME) == 0)
				return scan;
		}
		if (scan->child) {
			binder_node_t *result = binder_lookup_name(scan->child, name);

			if (result)
				return result;
		}
		scan = scan->next;
	} while (scan);

	return NULL;
}

may_static binder_node_t *__binder_lookup_partial (binder_node_t *from, char *name, int *marker)
{
	binder_node_t *scan = from;
	binder_node_t *result = NULL;

	do {
		if (scan->item) {
			char *partial = scan->item->name;

			for (;;) {
				partial = strchr (partial, '.');
				if (partial == NULL)
					break;
				partial++;
				if (strcmp (partial, name) == 0) {
					marker[0]++;
					result = scan;
				}
			}
		}
		if (scan->child) {
			binder_node_t *child = __binder_lookup_partial (scan->child, name, marker);

			if (child)
				result = child;
		}
		scan = scan->next;
	} while (scan);

	return result;
}

may_static binder_node_t *binder_lookup_partial (char *name, int *error)
{
	int marker = 0;
	binder_node_t *result = __binder_lookup_partial (&binder_root, name, &marker);

	if (marker == 1)
		*error = 0;
	else if (marker == 0)
		*error = -ENOENT;
	else
		*error = -EINVAL;
	return result;
}

may_static binder_node_t *binder_lookup_id (binder_node_t *from, int id)
{
	binder_node_t *scan = from;

	do {
		if (scan->item && id == scan->item->id)
			return scan;
		if (scan->child) {
			binder_node_t *result = binder_lookup_id (scan->child, id);

			if (result)
				return result;
		}
		scan = scan->next;
	} while (scan);

	return NULL;
}

may_static binder_node_t *binder_lookup_item (binder_node_t *from, cronyx_binder_item_t *h)
{
	binder_node_t *scan = from;

	do {
		if (scan->item == h)
			return scan;

		if (scan->child) {
			binder_node_t *result = binder_lookup_item (scan->child, h);

			if (result)
				return result;
		}
		scan = scan->next;
	} while (scan);

	return NULL;
}

may_static binder_node_t *binder_add_node(int parent, const char *prefix, const char *alias, cronyx_binder_item_t *h,
				    int order_total)
{
	binder_node_t *node, *walk;
	char *traversal[CRONYX_ITEM_MAXDEEP];
	int i, l, id;

	if (!parent && h->provider == NULL)
		return NULL;

	if (binder_lookup_item (&binder_root, h) != 0)
		return NULL;

	node = kzalloc (sizeof(*node), GFP_KERNEL);
	if (node == NULL)
		return NULL;

	node->parent = &binder_root;
	if (parent) {
		node->parent = binder_lookup_id (&binder_root, parent);
		if (node->parent == NULL)
			goto ballout;
		h->provider = node->parent->item->provider;
	}
	node->item = h;

	if (alias) {
		if (snprintf (h->alias, CRONYX_ITEM_MAXNAME,
			      (order_total >= 0) ? "%s%d" : "%s", alias, order_total) >= CRONYX_ITEM_MAXNAME)
			goto ballout;
		if (binder_lookup_name(&binder_root, h->alias))
			goto ballout;
	}

	for (walk = node->parent, l = 0, i = 0; walk; walk = walk->parent) {
		if (walk->item) {
			if (walk->item->name[0] == 0)
				continue;
			if (i >= CRONYX_ITEM_MAXDEEP)
				goto ballout;
			traversal[i++] = walk->item->name;
		}
	}
	while (--i >= 0) {
		l += 1 + strlen (traversal[i]);
		if (l >= CRONYX_ITEM_MAXNAME)
			goto ballout;
		strcat(h->name, traversal[i]);
		strcat(h->name, ".");
	}

	if (prefix) {
		if (snprintf (h->name + l, CRONYX_ITEM_MAXNAME - l, (h->order >= 0) ? "%s_%d" : "%s",
			      prefix, h->order) >= CRONYX_ITEM_MAXNAME)
			goto ballout;
	} else {
		if (snprintf (h->name + l, CRONYX_ITEM_MAXNAME - l, "%d", h->order) >= CRONYX_ITEM_MAXNAME)
			goto ballout;
	}

	if (binder_lookup_name(&binder_root, h->name))
		goto ballout;

	do
		id = ++binder_item_id;
	while (id == 0 || binder_lookup_id (&binder_root, id));

	h->minor = -1;
	h->id = id;

	node->next = node->parent->child;
	node->parent->child = node;
	binder_evolution++;
	binder_total_items++;

	return node;

ballout:
	kfree(node);
	return NULL;
}

may_static int __binder_remove_node(binder_node_t *node)
{
	if (node->item->minor > 0)
		minor2item[node->item->minor] = 0;
	node->item->type = CRONYX_ITEM_REMOVED;

	while (node->child) {
		binder_node_t *child = node->child;

		node->child = child->next;
		__binder_remove_node(child);
	}
	binder_total_items--;
	BUG_ON (binder_total_items < 0);
	node->item->id = 0;
	kfree(node);
	binder_put();
	return 0;
}

may_static int binder_remove_node(binder_node_t *node)
{
	binder_node_t **walk;
	int error;

	for (walk = &node->parent->child; *walk; walk = &((*walk)->next))
		if (*walk == node) {
			*walk = node->next;
			break;
		}

	error = __binder_remove_node(node);
	binder_evolution++;
	return error;
}

void cronyx_binder_register_protocol(cronyx_proto_t *t)
{
	down (&binder_topology);
	binder_deffered_flush_and_lock();
	t->next = proto_list;
	proto_list = t;
	binder_get();
	binder_kick_timers ();
	binder_unlock();
	up (&binder_topology);
	printk (KERN_DEBUG "cbinder: protocol `%s' added\n", t->name);
}

EXPORT_SYMBOL(cronyx_binder_register_protocol);

void cronyx_binder_unregister_protocol (cronyx_proto_t *t)
{
	cronyx_proto_t *h, *p;

	down (&binder_topology);
	binder_deffered_flush_and_lock();
	for (h = proto_list, p = 0; h; p = h, h = h->next)
		if (h == t) {
			binder_put();
			if (p == 0)
				proto_list = h->next;
			else
				p->next = h->next;
			break;
		}
	binder_unlock();
	up (&binder_topology);

	printk (KERN_DEBUG "cbinder: protocol `%s' removed\n", t->name);
}

EXPORT_SYMBOL(cronyx_binder_unregister_protocol);

int cronyx_binder_register_item(int parent, const char *prefix,
																int order_parent,
																const char *alias,
																int order_total, cronyx_binder_item_t *h)
{
	int error;
	binder_node_t *node;

	if (h->type < CRONYX_ITEM_ADAPTER || h->type > CRONYX_ITEM_CHANNEL)
		return -EINVAL;

	down (&binder_topology);
	binder_deffered_flush_and_lock();
	h->debug = Debug;
	h->order = order_parent;
	node = binder_add_node(parent, prefix, alias, h, order_total);
	if (node) {
		binder_get();
		binder_kick_timers ();
		error = 0;
	} else
		error = -EINVAL;
	binder_unlock();
	up (&binder_topology);
	return error;
}

EXPORT_SYMBOL(cronyx_binder_register_item);

may_static int binder_try_free(binder_node_t *node)
{
	if (node->item) {
		int running = node->item->running;
		cronyx_proto_t *proto = node->item->proto;

		if (running)
			node->item->dispatch.link_down (node->item);
		if (proto) {
			node->item->proto = NULL;
			if (node->item->dispatch.notify_proto_changed)
				node->item->dispatch.notify_proto_changed (node->item);
			binder_unlock();
			schedule();
			binder_deffered_flush_and_lock();
			if (proto->detach (node->item) < 0) {
				node->item->proto = proto;
				if (node->item->dispatch.notify_proto_changed)
					node->item->dispatch.notify_proto_changed (node->item);
				if (running)
					node->item->dispatch.link_up (node->item);
				return 1;
			} else
				binder_provider_put(node->item);
		}
	}

	for (node = node->child; node != 0; node = node->next)
		if (binder_try_free(node))
			return 1;

	return 0;
}

int cronyx_binder_unregister_item (int id)
{
	int error;
	binder_node_t *node;

	down (&binder_topology);
	binder_deffered_flush_and_lock();

	node = binder_lookup_id (&binder_root, id);
	if (node == NULL || node->parent == NULL)
		error = -ENOENT;
	else if (binder_try_free(node))
		error = -EBUSY;
	else
		error = binder_remove_node(node);

	binder_unlock();
	up (&binder_topology);

	return error;
}

EXPORT_SYMBOL(cronyx_binder_unregister_item);

int cronyx_get_modem_status (cronyx_binder_item_t *h)
{
	int result = TIOCM_CD | TIOCM_CTS | TIOCM_DSR | TIOCM_DTR | TIOCM_RTS;

	if (h->dispatch.query_dcd && !h->dispatch.query_dcd (h))
		result &= ~TIOCM_CD;
	if (h->dispatch.query_cts && !h->dispatch.query_cts (h))
		result &= ~TIOCM_CTS;
	if (h->dispatch.query_dsr && !h->dispatch.query_dsr (h))
		result &= ~TIOCM_DSR;
	if (h->dispatch.query_rts && !h->dispatch.query_rts (h))
		result &= ~TIOCM_RTS;
	if (h->dispatch.query_dtr && !h->dispatch.query_dtr (h))
		result &= ~TIOCM_DTR;
	if (h->running == 1)
		result |= TIOCM_LE;
	return result;
}

EXPORT_SYMBOL(cronyx_get_modem_status);

may_static int binder_channel_ioctl (cronyx_binder_item_t *h, unsigned cmd, unsigned long arg)
{
	int val, sigs;

	switch (cmd) {
		case TIOCMGET:
			if (unlikely (!access_ok (VERIFY_WRITE, (void *) arg, sizeof(int))))
				return -EFAULT;

			put_user (cronyx_get_modem_status (h), (int *) arg);
			return 0;

		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			if (unlikely (!access_ok (VERIFY_READ, (void *) arg, sizeof(int))))
				return -EFAULT;

			/*
			 * update the modem signals.
			 */
			get_user (val, (int *) arg);
			sigs = (cmd == TIOCMSET) ? ~0 : val;
			if (cmd == TIOCMBIC)
				val = ~val;

			if (h->dispatch.set_dtr && (sigs & TIOCM_DTR))
				h->dispatch.set_dtr (h, (val & TIOCM_DTR) ? 1 : 0);
			if (h->dispatch.set_rts && (sigs & TIOCM_RTS))
				h->dispatch.set_rts (h, (val & TIOCM_RTS) ? 1 : 0);
			return 0;

		default:
			do {
				int result, error = -ENOSYS;

				if (h->type == CRONYX_ITEM_CHANNEL) {
					if (h->proto && h->proto->ioctl) {
						error = h->proto->ioctl (h, cmd, arg);
						if (error != -ENOSYS && error < 0)
							return error;
					}
				}
				if (h->dispatch.ioctl) {
					result = h->dispatch.ioctl (h, cmd, arg);
					if (result != -ENOSYS)
						error = result;
				}
				return error;
			} while (0);
	}
}

may_static int binder_item_ctl(unsigned cmd, unsigned long arg, struct cronyx_ctl_t *ctl)
{
	cronyx_binder_item_t *h;
	cronyx_proto_t *p;
	int error, bytes_in, bytes_out;
	binder_node_t *node;

	bytes_in = sizeof(int) * 2;
	if (IOC_IN & cmd) {
		bytes_in = _IOC_SIZE (cmd);
		if (bytes_in < sizeof(int) * 2)
			return -ENOSYS;
	}

	bytes_out = 0;
	if (IOC_OUT & cmd)
		bytes_out = _IOC_SIZE (cmd);

	if (!access_ok (VERIFY_READ, (void *) arg, bytes_in))
		return -EFAULT;
	if (bytes_out && !access_ok (VERIFY_WRITE, (void *) arg, bytes_out))
		return -EFAULT;

	ctl->target_id = 0;
	ctl->param_id = 0;
	if (copy_from_user (ctl, (void *) arg, bytes_in))
		return -EFAULT;

	node = binder_lookup_id (&binder_root, ctl->target_id);
	if (node == 0 || node->item == 0)
		return -ENODEV;

	error = 0;
	h = node->item;

	if (ctl->param_id == cronyx_proto) {
		if (h->type != CRONYX_ITEM_CHANNEL)
			return -EINVAL;
		if (cmd == CRONYX_GET)
			memcpy (ctl->u.proto.proname, h->proto ? h->proto->name : "\0\0\0\0\0\0\0", 8);
		else {
			if (ctl->u.proto.proname[0] == 0)
				p = NULL;
			else {
				for (p = proto_list; p; p = p->next)
					if (strncmp (p->name, ctl->u.proto.proname, 8) == 0)
						break;
				if (!p)
					return -ENOENT;
			}
			if (h->proto != p) {
				cronyx_proto_t *proto = node->item->proto;

				if (proto != NULL) {
					node->item->proto = NULL;
					if (node->item->dispatch.notify_proto_changed)
						node->item->dispatch.notify_proto_changed (node->item);
					binder_unlock();
					schedule();
					binder_deffered_flush_and_lock();
					error = proto->detach (h);
					if (error < 0) {
						node->item->proto = proto;
						if (node->item->dispatch.notify_proto_changed)
							node->item->dispatch.notify_proto_changed (node->item);
						return error;
					}
					if (p == NULL)
						binder_provider_put(h);
				} else if (p != NULL)
					binder_provider_get(h);
				if (p != NULL) {
					binder_unlock();
					error = p->attach ? p->attach (h) : -ENOENT;
					if (error >= 0) {
						h->proto = p;
						if (node->item->dispatch.notify_proto_changed)
							node->item->dispatch.notify_proto_changed (node->item);
					} else
						binder_provider_put(h);
					binder_deffered_flush_and_lock();
				}
			}
		}
	} else if (ctl->param_id == cronyx_modem_status) {
		if (h->type != CRONYX_ITEM_INTERFACE)
			return -EINVAL;
		if (cmd == CRONYX_GET) {
			ctl->u.param.value = cronyx_get_modem_status (h);
			ctl->u.param.extra = 0;
		} else
			return -ENOSYS;
	} else if (ctl->param_id == cronyx_debug) {
		if (cmd == CRONYX_GET)
			ctl->u.param.value = h->debug;
		else if (ctl->u.param.value < 0 || ctl->u.param.value > 9)
			return -EINVAL;
		else
			h->debug = ctl->u.param.value;
	} else {
		int channel_error;

		error = -ENOSYS;
		if (h->type == CRONYX_ITEM_CHANNEL) {
			if (h->proto) {
				if (cmd == CRONYX_GET && h->proto->ctl_get)
					error = h->proto->ctl_get(h, ctl);
				if (cmd == CRONYX_SET && h->proto->ctl_set)
					error = h->proto->ctl_set(h, ctl);
				if (error != -ENOSYS && error < 0)
					return error;
			}
		}
		channel_error = -ENOSYS;
		if (cmd == CRONYX_GET && h->dispatch.ctl_get)
			channel_error = h->dispatch.ctl_get(h, ctl);
		if (cmd == CRONYX_SET && h->dispatch.ctl_set)
			channel_error = h->dispatch.ctl_set(h, ctl);
		if (channel_error != -ENOSYS)
			error = channel_error;
	}

	if (error >= 0 && bytes_out && copy_to_user ((void *) arg, ctl, bytes_out))
		error = -EFAULT;
	return error;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 9)
typedef int ioctl_return;
#else
typedef long ioctl_return;
#endif

may_static ioctl_return binder_ioctl(
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 9)
		struct inode *inode,
#endif
		struct file *filp, unsigned cmd, unsigned long arg)
{
	cronyx_binder_item_t *h;
	struct cronyx_ctl_t *ctl;
	struct inode *inode = filp->f_dentry->d_inode;
	binder_node_t *node;
	int error;

	down (&binder_topology);
	binder_deffered_flush_and_lock();

	error = 0;
	switch (cmd) {
		case CRONYX_BUNDLE_VER:
			if (!access_ok (VERIFY_WRITE, (void *) arg, 256))
				error = -EFAULT;
			else if (copy_to_user ((void *) arg, CRONYX_VERSION_INFO,
					       (256 < sizeof(CRONYX_VERSION_INFO) ? 256 : sizeof(CRONYX_VERSION_INFO))))
				error = -EFAULT;
			break;

		case CRONYX_PUSH_EVO:
#if LINUX_VERSION_CODE < 0x020600
			if (!suser ())
#else
			if (!capable(CAP_SYS_ADMIN))
#endif
				error = -EPERM;
			else if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof(int)))
				error = -EFAULT;
			else if (copy_from_user (&binder_push_evolution, (void *) arg, sizeof(int)))
				error = -EFAULT;
			break;

		case CRONYX_ITEM_ENUM:
			if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof(struct cronyx_binder_enum_t)))
				error = -EFAULT;
			else {
				struct cronyx_binder_enum_t *user_enum =
					kzalloc (sizeof(struct cronyx_binder_enum_t), GFP_KERNEL);
				if (!user_enum)
					error = -ENOMEM;
				else if (copy_from_user (&user_enum->from, (void *) arg, sizeof(int)))
					error = -EFAULT;
				else if (user_enum->from > binder_total_items)
					error = -EINVAL;
				else {
					binder_enum (user_enum->from, user_enum->ids,
						     sizeof(user_enum->ids) / sizeof(user_enum->ids[0]));
					user_enum->total = binder_total_items;
					user_enum->evolution = binder_evolution;
					user_enum->push = binder_push_evolution;
					if (copy_to_user
					    ((void *) arg, user_enum, sizeof(struct cronyx_binder_enum_t)))
						error = -EFAULT;
					kfree(user_enum);
				}
			}
			break;

		case CRONYX_ITEM_INFO:
			if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof(struct cronyx_item_info_t)))
				error = -EFAULT;
			else {
				struct cronyx_item_info_t *item_info =
					kzalloc (sizeof(struct cronyx_item_info_t), GFP_KERNEL);

				if (!item_info)
					error = -ENOMEM;
				else {
					if (copy_from_user
					    (item_info, (void *) arg, sizeof(struct cronyx_item_info_t)))
						error = -EFAULT;
					else {
						if (item_info->id)
							node = binder_lookup_id (&binder_root, item_info->id);
						else {
							node = binder_lookup_name(&binder_root, item_info->name);
							if (node == NULL)
								node = binder_lookup_partial (item_info->name, &error);
						}
						if (node == NULL || node->item == NULL)
							error = -ENOENT;
						if (error >= 0) {
							h = node->item;
							strncpy (item_info->name, h->name, sizeof(item_info->name));
							strncpy (item_info->alias, h->alias,
								 sizeof(item_info->alias));
							item_info->id = h->id;
							item_info->parent =
								node->parent->item ? node->parent->item->id : 0;
							item_info->minor = h->minor;
							item_info->svc = h->svc;
							item_info->type = h->type;
							item_info->order = h->order;
							if (copy_to_user
							    ((void *) arg, item_info,
							     sizeof(struct cronyx_item_info_t)))
								error = -EFAULT;
						}
					}
					kfree(item_info);
				}
			}
			break;

		case CRONYX_SELF_INFO:
			node = inode2node(inode);
			if (node == NULL || node->item == NULL)
				error = -ENODEV;
			else if (!access_ok (VERIFY_WRITE, (void *) arg, sizeof(struct cronyx_item_info_t)))
				error = -EFAULT;
			else {
				struct cronyx_item_info_t *item_info =
					kzalloc (sizeof(struct cronyx_item_info_t), GFP_KERNEL);

				if (!item_info)
					error = -ENOMEM;
				else {
					h = node->item;
					strncpy (item_info->name, h->name, sizeof(item_info->name));
					strncpy (item_info->alias, h->alias, sizeof(item_info->alias));
					item_info->id = h->id;
					item_info->parent = node->parent->item ? node->parent->item->id : 0;
					item_info->minor = h->minor;
					item_info->svc = h->svc;
					item_info->type = h->type;
					item_info->order = h->order;
					if (copy_to_user ((void *) arg, item_info, sizeof(struct cronyx_item_info_t)))
						error = -EFAULT;
					kfree(item_info);
				}
			}
			break;

		case CRONYX_SET:
#if LINUX_VERSION_CODE < 0x020600
			if (!suser ()) {
#else
			if (!capable(CAP_SYS_ADMIN)) {
#endif
				error = -EPERM;
				break;
			}
		case CRONYX_GET:
			ctl = kzalloc (sizeof(struct cronyx_ctl_t), GFP_KERNEL);
			if (!ctl)
				error = -ENOMEM;
			else {
				error = binder_item_ctl(cmd, arg, ctl);
				kfree(ctl);
			}
			break;

		default:
			node = inode2node(inode);
			if (node == NULL || node->item == NULL)
				error = -ENODEV;
			else
				error = binder_channel_ioctl (node->item, cmd, arg);
	}

	binder_unlock();
	up (&binder_topology);
	return error;
}

may_static int binder_open (struct inode *inode, struct file *file)
{
	binder_node_t *node;

	binder_deffered_flush_and_lock();
	node = inode2node(inode);
	if (node == 0) {
		binder_unlock();
		return -ENODEV;
	}
	if (node->item && node->item->type == CRONYX_ITEM_CHANNEL) {
		if (!node->item->proto) {
			binder_unlock();
			return -EINVAL;
		}
		if (node->item->proto->open) {
			int error = node->item->proto->open (node->item);

			if (error < 0) {
				binder_unlock();
				return error;
			}
		}
	}
	binder_get();
	binder_unlock();
	return 0;
}

may_static int binder_release(struct inode *inode, struct file *file)
{
	binder_node_t *node;

	binder_deffered_flush_and_lock();
	node = inode2node(inode);
	if (node == 0) {
		binder_unlock();
		return -ENODEV;
	}
	if (node->item && node->item->type == CRONYX_ITEM_CHANNEL && node->item->proto) {
		fasync_helper (-1, file, 0, &node->item->proto->fasync);
		if (node->item->proto->release)
			node->item->proto->release(node->item);
	}
	binder_unlock();
	binder_put();
	return 0;
}

may_static ssize_t binder_read (struct file *filp, char *buf, size_t len, loff_t *offset)
{
	struct inode *inode = filp->f_dentry->d_inode;
	cronyx_binder_item_t *h = inode2chan (inode);

	if (!h) {
		printk (KERN_EMERG "%s, %s, %u: bug\n", __FILE__, __FUNCTION__, __LINE__);
		return -EINVAL;
	}
	if (!h->proto) {
		printk (KERN_EMERG "%s, %s, %u: bug\n", __FILE__, __FUNCTION__, __LINE__);
		return -EINVAL;
	}
	if (!h->proto->read) {
		printk (KERN_EMERG "%s, %s, %u: bug\n", __FILE__, __FUNCTION__, __LINE__);
		return -EINVAL;
	}

	if (!h || !h->proto || !h->proto->read)
		return -EINVAL;

	return h->proto->read (h, filp->f_flags, buf, len);
}

may_static ssize_t binder_write(struct file *filp, const char *buf, size_t len, loff_t *offset)
{
	struct inode *inode = filp->f_dentry->d_inode;
	cronyx_binder_item_t *h = inode2chan (inode);

	if (!h || !h->proto || !h->proto->write)
		return -EINVAL;

	return h->proto->write(h, filp->f_flags, buf, len);
}

may_static unsigned binder_poll (struct file *filp, struct poll_table_struct *st)
{
	struct inode *inode = filp->f_dentry->d_inode;
	cronyx_binder_item_t *h = inode2chan (inode);

	if (!h || !h->proto || !h->proto->select)
		return 0;
	return h->proto->select(h, st, filp);
}

may_static int binder_fasync (int fd, struct file *filp, int on)
{
	struct inode *inode = filp->f_dentry->d_inode;
	cronyx_binder_item_t *h = inode2chan (inode);
	int error;

	if (!h || !h->proto)
		return -EINVAL;
	error = fasync_helper (fd, filp, on, &h->proto->fasync);
	if (error < 0)
		return error;
	return 0;
}

may_static struct file_operations binder_fops = {
	.owner = THIS_MODULE,
	.read = binder_read,
	.write = binder_write,
	.poll = binder_poll,
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 9)
	.ioctl = binder_ioctl,
#else
	.compat_ioctl = binder_ioctl,
	.unlocked_ioctl = binder_ioctl,
#endif
	.open = binder_open,
	.release = binder_release,
	.fasync = binder_fasync
};

//-----------------------------------------------------------------------------

char *cronyx_format_irq (int raw_irq, int apic_irq)
{
	static char buffer[32];

	if (raw_irq <= 0)
		snprintf (buffer, sizeof(buffer) - 1, (apic_irq > 0) ? "%d" : "<none>", apic_irq);
	else if (raw_irq == apic_irq)
		snprintf (buffer, sizeof(buffer) - 1, "%d", apic_irq);
	else
		snprintf (buffer, sizeof(buffer) - 1, (apic_irq > 0) ? "%d/apic-%d" : "%d/apic-<none>", raw_irq,
			  apic_irq);
	return buffer;
}

EXPORT_SYMBOL(cronyx_format_irq);

int cronyx_binder_minor_get(cronyx_binder_item_t *h, int svc)
{
	h->svc = svc;
	if (h->minor < CRONYX_CONTROL_DEVICE) {
		int i;
		binder_node_t *node = binder_lookup_id (&binder_root, h->id);

		if (node == NULL || node->item != h)
			return -ENOENT;
		for (i = CRONYX_CONTROL_DEVICE + 1; i < CRONYX_MINOR_MAX; i++) {
			if (minor2item[i] == NULL) {
				minor2item[i] = node;
				h->minor = i;
				return i;
			}
		}
		if (binder_last_nominor == 0 || binder_last_nominor - jiffies > HZ * 3) {
			binder_last_nominor = jiffies | 1;
			printk (KERN_ERR "cbinder: no minor available for '%s/%s' proto '%s'\n",
				h->name, h->alias, h->proto->name);
		}
		return -EMLINK;
	}
	return 0;
}

EXPORT_SYMBOL(cronyx_binder_minor_get);

void cronyx_binder_minor_put(cronyx_binder_item_t *h)
{
	if (h->minor > CRONYX_CONTROL_DEVICE) {
		if (minor2item[h->minor]->item == h)
			minor2item[h->minor] = NULL;
		h->minor = -1;
		h->svc = CRONYX_SVC_NONE;
		binder_last_nominor = 0;
	}
}

EXPORT_SYMBOL(cronyx_binder_minor_put);

int cronyx_param_get(cronyx_binder_item_t *h, int id, long *value)
{
	int error;
	struct cronyx_ctl_t *ctl = kzalloc (sizeof(struct cronyx_ctl_t), GFP_KERNEL);

	if (ctl == NULL)
		return -ENOMEM;
	ctl->param_id = id;
	error = cronyx_ctl_get(h, ctl);
	if (error >= 0)
		*value = ctl->u.param.value;
	kfree(ctl);
	return error;
}

EXPORT_SYMBOL(cronyx_param_get);

int cronyx_param_set(cronyx_binder_item_t *h, int id, long value)
{
	int error;
	struct cronyx_ctl_t *ctl = kzalloc (sizeof(struct cronyx_ctl_t), GFP_KERNEL);

	if (ctl == NULL)
		return -ENOMEM;
	ctl->param_id = id;
	ctl->u.param.value = value;
	error = cronyx_ctl_set(h, ctl);
	kfree(ctl);
	return error;
}

EXPORT_SYMBOL(cronyx_param_set);

int cronyx_ctl_get(cronyx_binder_item_t *h, struct cronyx_ctl_t *ctl)
{
	int error;

	binder_deffered_flush_and_lock();
	error = -ENOSYS;
	if (h->dispatch.ctl_get)
		error = h->dispatch.ctl_get(h, ctl);
	binder_unlock();
	return error;
}

EXPORT_SYMBOL(cronyx_ctl_get);

int cronyx_ctl_set(cronyx_binder_item_t *h, struct cronyx_ctl_t *ctl)
{
	int error;

	binder_deffered_flush_and_lock();
	error = -ENOSYS;
	if (h->dispatch.ctl_set)
		error = h->dispatch.ctl_set(h, ctl);

	binder_unlock();
	binder_deffered_flush ();
	return error;
}

EXPORT_SYMBOL(cronyx_ctl_set);

int binder_deffered_queue1 (cronyx_binder_item_t *h, char *reason, void (*call) (cronyx_binder_item_t *))
{
	struct binder_deffered_t *d = binder_alloc_deffered(reason);

	if (!d)
		return -ENOMEM;

	d->param_1 = h;
	d->param_2 = d;
	d->call._2 = (void (*)(void *, void *)) call;
	binder_deffered_queue(d);
	return 0;
}

EXPORT_SYMBOL(binder_deffered_queue1);

int binder_deffered_queue2 (cronyx_binder_item_t *h, char *reason, void (*call) (cronyx_binder_item_t *, void *), void *param)
{
	struct binder_deffered_t *d = binder_alloc_deffered(reason);

	if (!d)
		return -ENOMEM;

	d->param_1 = h;
	d->param_2 = param;
	d->call._2 = (void (*)(void *, void *)) call;
	binder_deffered_queue(d);
	return 0;
}

EXPORT_SYMBOL(binder_deffered_queue2);

void cronyx_receive(cronyx_binder_item_t *h, struct sk_buff *skb)
{
	cronyx_proto_t *proto = h->proto;

	if (proto && proto->notify_receive) {
		if (proto->dispatch_priority)
			proto->notify_receive(h, skb);
		else {
			struct binder_deffered_t *d = binder_alloc_deffered("receive");

			if (unlikely (d == NULL))
				dev_kfree_skb_any (skb);
			else {
				d->param_1 = h;
				d->param_2 = skb;
				d->call._2 = (void (*)(void *, void *)) proto->notify_receive;
				binder_deffered_queue(d);
			}
		}
	} else
		dev_kfree_skb_any (skb);
}

EXPORT_SYMBOL(cronyx_receive);

void cronyx_receive_error (cronyx_binder_item_t *h, int errcode)
{
	cronyx_proto_t *proto = h->proto;

	if (proto && proto->notify_receive_error) {
		if (proto->dispatch_priority)
			proto->notify_receive_error (h, errcode);
		else {
			struct binder_deffered_t *d = binder_alloc_deffered("receive_error");

			if (likely (d != NULL)) {
				d->param_1 = h;
				d->param_2 = (void *) (unsigned long) errcode;
				d->call._2 = (void (*)(void *, void *)) proto->notify_receive_error;
				binder_deffered_queue(d);
			}
		}
	}
}

EXPORT_SYMBOL(cronyx_receive_error);

void cronyx_transmit_done(cronyx_binder_item_t *h)
{
	cronyx_proto_t *proto = h->proto;

	if (proto && proto->notify_transmit_done) {
		if (proto->dispatch_priority)
			proto->notify_transmit_done(h);
		else {
			struct binder_deffered_t *d = binder_alloc_deffered("transmit");

			if (likely (d != NULL)) {
				d->param_2 = d;
				d->param_1 = h;
				d->call._1 = (void (*)(void *)) proto->notify_transmit_done;
				binder_deffered_queue(d);
			}
		}
	}
}

EXPORT_SYMBOL(cronyx_transmit_done);

void cronyx_modem_event(cronyx_binder_item_t *h)
{
	cronyx_proto_t *proto = h->proto;

	if (proto && proto->notify_modem_event) {
		if (proto->dispatch_priority)
			proto->notify_modem_event(h);
		else {
			struct binder_deffered_t *d = binder_alloc_deffered("modem_event");

			if (likely (d != NULL)) {
				d->param_2 = d;
				d->param_1 = h;
				d->call._1 = (void (*)(void *)) proto->notify_modem_event;
				binder_deffered_queue(d);
			}
		}
	}
}

EXPORT_SYMBOL(cronyx_modem_event);

void cronyx_transmit_error (cronyx_binder_item_t *h, int errcode)
{
	cronyx_proto_t *proto = h->proto;

	if (proto && proto->notify_transmit_error) {
		if (proto->dispatch_priority)
			proto->notify_transmit_error (h, errcode);
		else {
			struct binder_deffered_t *d = binder_alloc_deffered("transmit_error");

			if (likely (d != NULL)) {
				d->param_1 = h;
				d->param_2 = (void *) (unsigned long) errcode;
				d->call._2 = (void (*)(void *, void *)) proto->notify_transmit_error;
				binder_deffered_queue(d);
			}
		}
	}
}

EXPORT_SYMBOL(cronyx_transmit_error);

#if LINUX_VERSION_CODE < 0x02060E
void *cronyx_kzalloc (size_t size, unsigned flags)
{
	void *p = kmalloc (size, flags);

	if (likely (p != 0))
		memset(p, 0, size);
	return p;
}

EXPORT_SYMBOL(cronyx_kzalloc);
#endif

#if LINUX_VERSION_CODE >= 0x020613
may_static irqreturn_t dummy_irq_handler (int irq, void *lock)
#elif LINUX_VERSION_CODE >= 0x020600
may_static irqreturn_t dummy_irq_handler (int irq, void *lock, struct pt_regs *regs)
#else
may_static void dummy_irq_handler (int irq, void *lock, struct pt_regs *regs)
#endif
{
	spin_lock((spinlock_t *) lock);
	spin_unlock((spinlock_t *) lock);
#if LINUX_VERSION_CODE >= 0x020600
	return IRQ_HANDLED;
#endif
}

int cronyx_probe_irq(int irq, void *ptr, void (*irq_updown) (void *, int))
{
	int count, probe;
	unsigned long mask, flags;
	spinlock_t lock;

	spin_lock_init(&lock);
	for (count = 0; count < 5; count++) {
		spin_lock_irqsave(&lock, flags);
		irq_updown (ptr, -1);
		spin_unlock_irqrestore(&lock, flags);
		udelay (100);
		mask = probe_irq_on ();
		probe = probe_irq_off (mask);
		if (probe == 0 && (mask & (1ul << irq)) != 0) {
			if (request_irq (irq, dummy_irq_handler, IRQF_DISABLED, "Probe IRQ (Cronyx)", &lock) != 0)
				return 0;
			free_irq (irq, &lock);
			for (count = 0; count < 5; count++) {
				mask = probe_irq_on ();
				spin_lock_irqsave(&lock, flags);
				irq_updown (ptr, irq);
				spin_unlock_irqrestore(&lock, flags);
				udelay (100);
				probe = probe_irq_off (mask);
				spin_lock_irqsave(&lock, flags);
				irq_updown (ptr, -1);
				spin_unlock_irqrestore(&lock, flags);
				udelay (100);
				if (probe > 0 && probe == irq)
					return 1;
			}
			break;
		}
	}
	return 0;
}

EXPORT_SYMBOL(cronyx_probe_irq);

#if (LINUX_VERSION_CODE >= 0x020600) && (LINUX_VERSION_CODE < 0x020611)

void *cronyx_mempool_kmalloc (int gfp_mask, void *pool_data)
{
	size_t size = (size_t) (long) pool_data;

	return kmalloc (size, gfp_mask);
}

void cronyx_mempool_kfree(void *element, void *pool_data)
{
	kfree(element);
}

EXPORT_SYMBOL(cronyx_mempool_kmalloc);
EXPORT_SYMBOL(cronyx_mempool_kfree);

#endif

//-----------------------------------------------------------------------------

int init_module(void)
{
	if (register_chrdev (CRONYX_MJR_BINDER, "cronyx/binder", &binder_fops)) {
		printk (KERN_ERR "cbinder: can not registered with major %d.\n", CRONYX_MJR_BINDER);
		return -EIO;
	}
	init_timer (&second_timer);
	init_timer (&led_timer);
#if LINUX_VERSION_CODE < 0x020600
	INIT_LIST_HEAD (&binder_work.list);
	binder_work.sync = 0;
	binder_work.routine = binder_deffered;
#else /* LINUX_VERSION_CODE < 0x020600 */
	pool_deffered_item = mempool_create_kmalloc_pool (64,
		sizeof(struct binder_deffered_t));
	if (!pool_deffered_item)
		return -ENOMEM;
#	if defined (CONFIG_SMP)
	binder_queue = create_singlethread_workqueue("cronyx/wq");
	if (!binder_queue) {
		mempool_destroy (pool_deffered_item);
		printk (KERN_ERR "cbinder: unable create workqueue.\n");
		return -ENOMEM;
	}
#	endif
	/* defined (CONFIG_SMP) */
	CRONYX_INIT_WORK (&binder_work, binder_deffered, NULL);
#endif /* LINUX_VERSION_CODE < 0x020600 */

	binder_kick_timers ();
	printk (KERN_DEBUG "cbinder: module loaded, Debug level = %d\n",Debug);
	return 0;
}

void cleanup_module(void)
{
	printk (KERN_DEBUG "cbinder: cleanup\n");
	unregister_chrdev (CRONYX_MJR_BINDER, "cronyx/binder");
	BUG_ON (!list_empty (&leds_list));
	BUG_ON (binder_root.child);

	binder_deffered_flush_and_lock();
	del_timer_sync (&second_timer);
	del_timer_sync (&led_timer);
	BUG_ON (!list_empty (&deffered_list));

#if LINUX_VERSION_CODE >= 0x020600
#ifdef CONFIG_SMP
	destroy_workqueue(binder_queue);
#endif
	mempool_destroy (pool_deffered_item);
#endif /* LINUX_VERSION_CODE >= 0x020600 && defined (CONFIG_SMP) */

	printk (KERN_DEBUG "cbinder: module unloaded\n");
	binder_unlock();
}

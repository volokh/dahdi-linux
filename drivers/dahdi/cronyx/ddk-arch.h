#ifndef __DDK_ARCH_H
#define __DDK_ARCH_H

#ifndef __cplusplus
#	ifndef false
#		define false 0
#	endif
#	ifndef true
#		define true 1
#	endif
#endif

#ifndef ddk_noop
#	if defined (_MSC_VER)
#		define ddk_noop __noop
#	else
#		define ddk_noop do{}while(0)
#	endif
#endif

#ifndef DDK_DEBUG
#	define DDK_DEBUG 0
#endif

#ifdef _WINDOWS_
	typedef __int8 s8;
	typedef unsigned __int8 u8;
	typedef __int16 s16;
	typedef unsigned __int16 u16;
	typedef __int32 s32;
	typedef unsigned __int32 u32;
	typedef __int64 s64;
	typedef unsigned __int64 u64;
#endif /* _WINDOWS_ */

#if defined (DDK_USERMODE_ONLY)
#	ifndef _WINDOWS_
#		include <sys/types.h>
		typedef __int8_t s8;
		typedef __uint8_t u8;
		typedef __int16_t s16;
		typedef __uint16_t u16;
		typedef __int32_t s32;
		typedef __uint32_t u32;
		typedef __int64_t s64;
		typedef __uint64_t u64;
#	endif /* ! _WINDOWS_ */
#else

#if defined (__KERNEL__)
#	define ddk_atomic_t atomic_t
#	define ddk_bitops_t unsigned long
#	define ddk_dma_addr_t dma_addr_t
#	define ddk_dma_addr2phys(x) (x)
#	define ddk_interrupt_sync(irq_obj) ddk_noop
#	define ddk_irql_t unsigned long
#	define ddk_cli_avail() (1)
#	define ddk_cli(context) local_irq_save(context)
#	define ddk_sti(context) local_irq_restore(context)
#	define ddk_memory_barrier() barrier()

#	ifndef BUILD_BUG_ON
#		define BUILD_BUG_ON(x) ((void)sizeof(char[1 - 2*!!(x)]))
#	endif

#	if (LINUX_VERSION_CODE < 0x020600)
#		ifndef in_atomic
#			define in_atomic() 0
#		endif
#		ifndef irqs_disabled
#			define irqs_disabled() in_interrupt()
#		endif
#		define ddk_ffs(value) ({					\
			BUG_ON (! (value));					\
			ffs(value);						\
		})
#		define ddk_bit_clear(target, bit) do {				\
			BUILD_BUG_ON (sizeof(ddk_bitops_t) != sizeof (target));	\
			(target) &= ~(1u << (bit));				\
		} while (0)
#	else
#		define ddk_ffs(value) ({					\
			BUG_ON (! (value));					\
			__ffs(value);						\
		})
#		define ddk_bit_clear(target, bit) do {				\
			BUILD_BUG_ON (sizeof(ddk_bitops_t) != sizeof (target));	\
			__clear_bit(bit, &(target));				\
		} while (0)
#	endif

#	define ddk_dbg_print(msg, ...)					\
		printk (KERN_DEBUG msg, ## __VA_ARGS__)

#	define DDK_DBG_TRACE						\
		ddk_dbg_print ("DBG-TRACE: %s, %u in %s\n",		\
			__FUNCTION__, __LINE__, __FILE__)

#	define ddk_flush_cpu_writecache() do {				\
		ddk_memory_barrier ();					\
		flush_write_buffers();					\
	} while (0)

#	define ddk_queue_head_t struct list_head
#	define ddk_queue_entry_t struct list_head

#	define ddk_queue_entry(entry, type, member)			\
		list_entry(entry, type, member)

#	define ddk_queue_put(queue, member)				\
		list_add_tail (member, queue)

#	define ddk_queue_get(queue, type, member) ({			\
		BUG_ON (list_empty(queue));				\
		ddk_queue_entry((queue)->next, type, member);		\
	})

#	define ddk_queue_foreach(pos, queue, type, member)		\
		list_for_each_entry(pos, queue, member)

#	define ddk_queue_init(queue)					\
		INIT_LIST_HEAD(queue);

#	define ddk_queue_isempty(queue)					\
		list_empty(queue)

	static __inline void* ddk_queue_iscontain (ddk_queue_head_t* queue,
					ddk_queue_entry_t* item) {
		ddk_queue_entry_t* scan;
		list_for_each (scan, queue) {
			if (scan == item)
				return item;
		}
		return 0;
	}

#	define ddk_queue_entry_detach(entry)				\
		list_del(entry)

#	define ddk_queue_entry_init(entry)				\
		INIT_LIST_HEAD(entry);

#	define ddk_yield_dmabus() do {					\
		cpu_relax(); cpu_relax(); cpu_relax();			\
	} while (0)

#	define ddk_yield_cpu() do {					\
		if (in_atomic () || irqs_disabled ())			\
			ddk_yield_dmabus ();				\
		else							\
			schedule ();					\
	} while (0)


#	define ddk_atomic_read(target) ({				\
		BUILD_BUG_ON (sizeof(atomic_t) != sizeof (target));	\
		atomic_read ((atomic_t*) &(target));			\
	})

#	define ddk_atomic_set(target, value) ({				\
		BUILD_BUG_ON (sizeof(atomic_t) != sizeof (target));	\
		atomic_set ((atomic_t*) &(target), value);		\
	})

#	define ddk_atomic_inc(target) do {				\
		BUILD_BUG_ON (sizeof(atomic_t) != sizeof (target));	\
		atomic_inc ((atomic_t*) &(target));			\
	} while (0)

#	define ddk_atomic_dec(target) do {				\
		BUILD_BUG_ON (sizeof(atomic_t) != sizeof (target));	\
		atomic_dec ((atomic_t*) &(target));			\
	} while (0)

#	define ddk_atomic_dec_and_test(target) ({			\
		BUILD_BUG_ON (sizeof(atomic_t) != sizeof (target));	\
		atomic_dec_and_test ((atomic_t*) &(target));		\
	})

#	define ddk_bit_set(target, bit) do {				\
		BUILD_BUG_ON (sizeof(ddk_bitops_t) != sizeof (target));	\
		__set_bit(bit, (ddk_bitops_t*) &(target));		\
	} while (0)

#	define ddk_bit_test(target, bit) ({				\
		BUILD_BUG_ON (sizeof(ddk_bitops_t) != sizeof (target));	\
		test_bit(bit, &(target));				\
	})

#	if defined (CONFIG_DEBUG_KERNEL) && CONFIG_DEBUG_KERNEL
#		define ddk_assert_check 1
#		define may_static
#	else
#		define ddk_assert_check 0
#	endif

#	if defined (__BIG_ENDIAN)
#		define DDK_BIG_ENDIAN 1
#	else
#		define DDK_BIG_ENDIAN 0
#	endif

#elif defined (_NTDDK_) || defined (_WDMDDK_)
#	pragma check_stack (off)
#	pragma runtime_checks ("", off)
#	if !DDK_DEBUG
#		pragma auto_inline (on)
#		pragma inline_recursion (on)
#		pragma inline_depth (64)
#		pragma optimize ("agyst", on)
#		pragma optimize ("w", off)
#	endif
	typedef struct _ddk_atomic_t {
		volatile LONG value;
	} ddk_atomic_t;
#	define ddk_packed
#	define ddk_bitops_t unsigned
#	define ddk_irql_t KIRQL
#	define ddk_irqobj_t PKINTERRUPT
#	define ddk_sti(context) KeLowerIrql (context)
#	define ddk_cli_avail() (1)
#	define ddk_cli(context) KeRaiseIrql (HIGH_LEVEL, &(context))
#	define ddk_emulu(a, b) UInt32x32To64(a, b)
#	define ddk_dma_addr_t PHYSICAL_ADDRESS
#	define ddk_dma_addr2phys(x) ((x).QuadPart)
#	define ddk_assert ASSERT

#	define ddk_atomic_dec(x) InterlockedDecrement(&(x).value)
#	define ddk_atomic_dec_and_test(x) (InterlockedDecrement(&(x).value) == 0)
#	define ddk_atomic_inc(x) InterlockedDecrement(&(x).value)
#	define ddk_atomic_read(x) ((x).value)
#	define ddk_atomic_set(x,s) do (x).value = (s); while (0)

#ifdef __cplusplus
extern "C"
{
#endif
	void _ReadWriteBarrier (void);
	unsigned char __fastcall _BitScanForward (unsigned *, unsigned);
#ifdef __cplusplus
}
#endif
#	pragma intrinsic(_ReadWriteBarrier, _BitScanForward)

#	define ddk_memory_barrier() _ReadWriteBarrier()
#	define ddk_dbg_print DbgPrint

#	define ddk_flush_cpu_writecache() do {				\
		ddk_memory_barrier ();					\
		__asm {lock add [esp], eax}				\
	} while (0)

#	define ddk_queue_head_t LIST_ENTRY
#	define ddk_queue_entry_t LIST_ENTRY

#	define ddk_queue_entry(entry, type, member)			\
		CONTAINING_RECORD(entry, type, member)

#	define ddk_queue_put(queue, member)				\
		InsertTailList(queue, member)

#	define ddk_queue_get(queue, type, member) (			\
		ddk_queue_entry((queue)->Flink, type, member)		\
	)

#	define ddk_queue_foreach(pos, queue, type, member)		\
		for (pos = ddk_queue_entry((queue)->Flink,		\
			type, member); 	&pos->member != (queue);	\
			pos = ddk_queue_entry(pos->member.Flink, type, member))

#	define ddk_queue_init(queue)					\
		InitializeListHead(queue);

#	define ddk_queue_isempty(queue)					\
		IsListEmpty(queue)

	static __forceinline void* ddk_queue_iscontain (ddk_queue_head_t* queue,
					ddk_queue_entry_t* item) {
		ddk_queue_entry_t* scan;
		for (scan = queue->Flink; scan != queue; scan = scan->Flink) {
			if (scan == item)
				return item;
		}
		return 0;
	}

#	define ddk_queue_entry_detach(entry)				\
		RemoveEntryList(entry)

#	define ddk_queue_entry_init(entry)				\
		InitializeListHead(entry);

#	define ddk_yield_dmabus() do {					\
		__asm {pause} __asm {pause} __asm {pause}		\
	} while (0)

#	define ddk_yield_cpu() do {					\
		__asm {pause}						\
	} while (0)

	static __forceinline unsigned ddk_ffs(unsigned value) {		\
		unsigned result;					\
		ddk_assert(value != 0);					\
		_BitScanForward (&result, value);			\
		return result;						\
	}

#	define ddk_bit_set(target, bit) (target) |= 1u << (bit)
#	define ddk_bit_clear(target, bit) (target) &= ~(1u << (bit))
#	define ddk_bit_test(target, bit) (((target) & (1u << (bit))) != 0)

	static BOOLEAN __stdcall ddk_esr (PVOID unused) {
		return false;
	}

#	define ddk_interrupt_sync(irq_obj) do {				\
		ddk_assert (KeGetCurrentIrql () < DISPATCH_LEVEL);	\
		if (irq_obj)						\
			KeSynchronizeExecution (irq_obj, ddk_esr, 0);	\
	} while (0)

#	pragma comment (linker, "/merge:.edata=.text")
#	pragma comment (linker, "/merge:.rdata=.text")
#	pragma comment (linker, "/merge:_TEXT=.text")
#	pragma comment (linker, "/merge:NOPAGED=.text")
#	pragma comment (linker, "/merge:NOPAGE=.text")
#	pragma comment (linker, "/merge:_PAGE=PAGE")
#	pragma comment (linker, "/merge:PAGECONS=PAGE")
#	pragma comment (linker, "/merge:PAGED=PAGE")
#	pragma comment (linker, "/base:0x10000")
#	pragma comment (linker, "/align:0x1000")
#	pragma comment (linker, "/release")
#	pragma comment (linker, "/driver")
#	if _WIN32_WINNT < 0x0501
#		pragma comment (linker, "/subsystem:native,5.00")
#	elif _WIN32_WINNT < 0x0502
#		pragma comment (linker, "/subsystem:native,5.01")
#	else
#		pragma comment (linker, "/subsystem:native,5.02")
#	endif
#elif defined (__GNUC__) /* ! _NTDDK_ */
	typedef signed char s8;
	typedef unsigned char u8;
	typedef signed short s16;
	typedef unsigned short u16;
	typedef signed int s32;
	typedef unsigned u32;
	typedef signed long long s64;
	typedef unsigned long long u64;

#	define ddk_atomic_t unsigned
#	define ddk_bitops_t unsigned
#	define ddk_irql_t unsigned

#	define ddk_memory_barrier()			\
		__asm __volatile (""::)

#	define ddk_atomic_inc(target) do {		\
		__asm __volatile (			\
			"lock\n\t"			\
			"incl %0"			\
				: "+m" (target)		\
				: : "cc");		\
	} while (0)

#	define ddk_atomic_dec_and_test(target) ({	\
		char __c;				\
		__asm __volatile (			\
			"lock\n\t"			\
			"decl %0\n\t"			\
			"sete %1"			\
				: "+m" (target), "=q" (__c)\
				: : "cc");		\
	})

#	define ddk_flush_cpu_writecache()		\
		__asm __volatile (			\
			"lock\n\t"			\
			"addl $0,(%%esp)" : : : "cc")

#	define ddk_ffs(value) ({			\
		unsigned __result;			\
		__asm ("bsfl %1,%0"			\
			: "=r" (__result)		\
			: "rm" (value)			\
			: "cc");			\
		__result;				\
	})

#	define ddk_bit_set(target, bit)			\
		__asm ("btsl %1,%0"			\
			: "+rm" (target)		\
			: "Ir" ((unsigned) (bit))	\
			: "cc")

#	define ddk_bit_clear(target, bit)		\
		__asm ("btrl %1,%0"			\
			: "+rm" (target)		\
			: "Ir" ((unsigned) (bit))	\
			: "cc")

#	define ddk_yield_dmabus()			\
		__asm __volatile (			\
			"pause\n\t"			\
			"pause\n\t"			\
			"pause")

#endif

#if defined (MSDOS) || defined (__MSDOS__)
#	include <dos.h>
#	include <string.h>
#	define ddk_inb(port)		inportb (port)
#	define ddk_outb(port, byte)	outportb (port, byte)
#	define ddk_inw(port)		inportw (port)
#	define ddk_outw(port, word)	outportw (port ,word)
#elif defined(NDIS_MINIPORT_DRIVER)
#	include <string.h>
#	define ddk_inb(port)		READ_PORT_UCHAR (port)
#	define ddk_outb(port, byte)	WRITE_PORT_UCHAR (port, byte)
#	define ddk_inw(port)		READ_PORT_USHORT (port)
#	define ddk_outw(port, word)	WRITE_PORT_USHORT (port, word)
#	pragma warning (disable: 4761)
#	pragma warning (disable: 4242)
#	pragma warning (disable: 4244)
#elif defined(__linux__)
#	undef REALLY_SLOW_IO
#	include <linux/ioport.h>
#	include <asm/io.h>
#	include <linux/string.h>
#	define ddk_inb(port)		inb (port)
#	define ddk_outb(port, byte)	outb (byte, port)
#	define ddk_inw(port)		inw (port)
#	define ddk_outw(port, word)	outw (word, port)
#elif defined(__FreeBSD__)
#	include <sys/param.h>
#	include <machine/cpufunc.h>
#	include <sys/libkern.h>
#	include <sys/systm.h>
#	define memset(a, b, c)		bzero (a,c)
#	define ddk_inb(port)		inb (port)
#	define ddk_outb(port, byte)	outb (port, byte)
#	define ddk_inw(port)		inw (port)
#	define ddk_outw(port, word)	outw (port, word)
#endif

//-----------------------------------------------------------------------------

#ifdef __GNUC__
#	ifndef __forceinline
#		if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ > 0)
#			define __forceinline __inline__ __attribute__ ((always_inline))
#		else
#			define __forceinline __inline__
#		endif
#	endif
#	ifndef __noinline
#		define __noinline __attribute__ ((noinline))
#	endif
#	if !defined (unlikely) || !defined (likely)
#		undef unlikely
#		undef likely
#		if __GNUC__ == 2 && __GNUC_MINOR__ < 96
#			define  __builtin_expect(x, expected_value) (x)
#		endif
#		define likely(x) __builtin_expect ((x),1)
#		define unlikely(x) __builtin_expect ((x),0)
#	endif
#	if __GNUC__ >= 4
#		define ddk_offsetof(TYPE,MEMBER) __builtin_offsetof(TYPE,MEMBER)
#	endif
#else /* __GNUC__ */
#	ifndef __noinline
#		define __noinline __declspec(noinline)
#	endif
#	if !defined (unlikely) || !defined (likely)
#		undef unlikely
#		undef likely
#		define likely(x) x
#		define unlikely(x) x
#	endif
#endif /* __GNUC__ */

#ifndef ddk_offsetof
#	define ddk_offsetof(TYPE,MEMBER) ((unsigned) &((TYPE *)0)->MEMBER)
#endif /* ddk_offsetof */

#ifndef ddk_packed
#	ifdef __GNUC__
#		define ddk_packed __attribute__((packed, aligned(1)))
#	else
#		define ddk_packed __declspec(align(1))
#	endif
#endif

#ifndef ddk_dbg_print
	static __inline void __ddk_dbg_print(const char* dummy, ...) {
	}
#	define ddk_dbg_print __ddk_dbg_print
#endif

#ifndef ddk_kd_print
#	if DDK_DEBUG || defined (_DEBUG) || (defined (DBG) && DBG)
#		define ddk_kd_print(x) ddk_dbg_print x
#	else
#		define ddk_kd_print(x) ddk_noop
#	endif
#endif

#ifndef ddk_trap
#	if defined (LINUX) || defined (__KERNEL__) || defined (__LINUX__)
#		define ddk_trap() BUG()
#	elif defined (__GNUC__)
#		define ddk_trap() __asm __volatile ("int $3")
#	else
#		define ddk_trap() __debugbreak()
#	endif
#endif

#ifndef ddk_assert
#	if defined (LINUX) || defined (__KERNEL__) || defined (__LINUX__)
#		define ddk_assert(x) BUG_ON(!(x))
#	else
#		define ddk_assert(x) ddk_noop
#	endif
#endif

#ifndef ddk_assume
#	if defined (_MSC_VER)
#		define ddk_assume(x) __assume(x)
#	else
#		define ddk_assume(x) ddk_noop
#	endif
#endif

#ifndef ddk_emulu
	static __inline u64 ddk_emulu (unsigned a, unsigned b) {
#ifdef __i386__
		u64 r;
		__asm ("mull %2" : "=A" (r)
			: "%a" (a), "rm" (b)
			: "cc");
		return r;
#else
		if (sizeof (unsigned) == 8)
			return a * b;
		else if (sizeof (unsigned long) == 8)
			return ((unsigned long) a) * b;
		else
			return ((u64) a) * b;
#endif
	}
#endif /* ddk_emulu */

#ifndef ddk_edivu
	static __inline u32 ddk_edivu (u64 divident, unsigned divisor) {
#ifdef __i386__
		u32 result, reminder;
		__asm ("divl %3"
			: "=a" (result), "=d" (reminder)
			: "A" (divident), "rm" (divisor)
			:  "cc");
		return result;
#else
		return divident / divisor;
#endif
	}
#endif /* ddk_edivu */

#ifndef ddk_memcpy
#	define ddk_memcpy(dest, src, len) memcpy (dest, src, len)
#endif /* ddk_memcpy */

#ifndef ddk_memset
#	define ddk_memset(dest, value, len) memset (dest, value, len)
#endif /* ddk_memset */

#endif /* DDK_USERMODE_ONLY */

#ifndef DDK_BIG_ENDIAN
#	define DDK_BIG_ENDIAN 0
#endif

#ifndef may_static
#	define may_static static
#endif

#endif /* __DDK_ARCH_H */

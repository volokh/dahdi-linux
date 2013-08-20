#ifndef __DDK_HOST_H
#define __DDK_HOST_H 1

#ifdef DDK_USERMODE_ONLY
#else
#	include <linux/version.h>
#	if defined (RHEL_MAJOR) || defined (RHEL_MINOR) \
		|| defined (RHEL_RELEASE_CODE) || defined (RHEL_RELEASE_VERSION)
#		/* warning "Cronyx don't fully support for RHEL, currently only 5.3 release was tested." */
#		define RHEL_VERSION_HELL		1
#	else
#		define RHEL_RELEASE_VERSION(a,b)	0
#		define RHEL_RELEASE_CODE		0
#	endif /* RHEL */
#	if (LINUX_VERSION_CODE < 0x020613) \
		&& (RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION (5,0))
#	       include <linux/config.h>
#	endif
#	include <linux/kernel.h>
#	include <linux/stddef.h>
#	include <linux/list.h>
#	include <asm/bitops.h>
#	include <asm/io.h>
#	include <asm/dma.h>
#	include <asm/div64.h>
#	if LINUX_VERSION_CODE < 0x020600
#		include <asm/system.h>
#		include <linux/sched.h>
#	endif
#	include <linux/interrupt.h>
#	include <linux/ioport.h>
#	include <linux/types.h>
#endif

#endif /* __DDK_HOST_H */

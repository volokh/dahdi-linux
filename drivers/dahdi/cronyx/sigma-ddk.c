/*
 * Firmware for Cronyx Sigma adapter.
 *
 * Copyright (C) 1999-2003 Cronyx Engineering.
 * Author: Serge Vakulenko <vak@cronyx.ru>
 *
 * Copyright (C) 2000-2004 Cronyx Engineering.
 * Author: Roman Kurakin <rik@cronyx.ru>
 *
 * Copyright (C) 2006-2008 Cronyx Engineering.
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 */

#include "ddk-host.h"
#include "ddk-arch.h"

#include "sigma-ddk.h"

#if !defined (ddk_assert_check) || !defined (ddk_assert)
#	define ddk_assert_check 0
#endif

#if !ddk_assert_check
#	if defined (ddk_assert)
#		undef ddk_assert
#	endif
#	define ddk_assert(x) ddk_noop
#endif

#ifndef ddk_dbg_print
	static __inline void __ddk_dbg_print(const char* dummy, ...) {
	}
#	define ddk_dbg_print __ddk_dbg_print
#endif

#ifndef ddk_kd_print
#	define ddk_kd_print(x) ddk_noop
#endif

#ifndef ddk_trap
#	define ddk_trap(x) ddk_noop
#endif

/*-----------------------------------------------------------------------------*/

/* Signal encoding */
#define CD2400_ENCOD_NRZ        0	/* NRZ mode */
#define CD2400_ENCOD_NRZI       1	/* NRZI mode */
#define CD2400_ENCOD_MANCHESTER 2	/* Manchester mode */

/* Clock source */
#define CD2400_CLK_0           0	/* clock 0 */
#define CD2400_CLK_1           1	/* clock 1 */
#define CD2400_CLK_2           2	/* clock 2 */
#define CD2400_CLK_3           3	/* clock 3 */
#define CD2400_CLK_4           4	/* clock 4 */
#define CD2400_CLK_EXT         6	/* external clock */
#define CD2400_CLK_RCV         7	/* receive clock */

/* Parity */
#define	PAR_EVEN	0	/* even parity */
#define	PAR_ODD		1	/* odd parity */

/* Parity mode */
#define	PAR_NOPAR	0	/* no parity */
#define	PAR_FORCE	1	/* force parity (odd = force 1, even = 0) */
#define	PAR_NORMAL	2	/* normal parity */

/* Flow control transparency mode */
#define	FLOWCC_PASS	0	/* pass flow ctl chars as exceptions */
#define FLOWCC_NOTPASS  1	/* don't pass flow ctl chars to the host */

/* Stop bit length */
#define	STOPB_1		2	/* 1 stop bit */
#define	STOPB_15	3	/* 1.5 stop bits */
#define	STOPB_2		4	/* 2 stop bits */

/* Action on break condition */
#define	BRK_INTR	0	/* generate an exception interrupt */
#define	BRK_NULL	1	/* translate to a NULL character */
#define	BRK_RESERVED	2	/* reserved */
#define	BRK_DISCARD	3	/* discard character */

/* Parity/framing error actions */
#define	ERR_INTR	0	/* generate an exception interrupt */
#define	ERR_NULL	1	/* translate to a NULL character */
#define	ERR_IGNORE	2	/* ignore error; char passed as good data */
#define	ERR_DISCARD	3	/* discard error character */
#define	ERR_FFNULL	5	/* translate to FF NULL char */

struct _sigma_dat_tst_t {
	u32 start;		/* verify start */
	u32 end;		/* verify end */
};

#include "sigma/cxreg.h"
#include "sigma/csigmafw.h"
#include "sigma/csigma.c"
#include "sigma/cxddk.c"

/*
 * Low-level subroutines for Cronyx Tau-ISA adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Copyright (C) 1999-2013 Cronyx Telecom (www.cronyx.ru, info@cronyx.ru).
 * Author: Serge Vakulenko <vak@cronyx.ru>
 * Author: Roman Kurakin <rik@cronyx.ru>
 * Author: Leo Yuriev <ly@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations a permission to use,
 * modify and redistribute this software in source and binary forms,
 * as long as this message is kept with the software, all derivative
 * works or modified versions.
 *
 */

#include "ddk-host.h"
#include "ddk-arch.h"
#include "tauisa-ddk.h"

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

#include "tauisa/ctddk.h"
#include "tauisa/ctaureg.h"
#include "tauisa/hdc64570.h"
#include "tauisa/ds2153.h"
#include "tauisa/am8530.h"
#include "tauisa/lxt318.h"

struct _tauisa_dat_tst_t {
	long start;		/* verify start */
	long end;		/* verify end */
};

static unsigned char irqmask[] = {
	BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_3,
	BCR0_IRQ_DIS, BCR0_IRQ_5, BCR0_IRQ_DIS, BCR0_IRQ_7,
	BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_10, BCR0_IRQ_11,
	BCR0_IRQ_12, BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_15,
};

static unsigned char dmamask[] = {
	BCR0_DMA_DIS, BCR0_DMA_DIS, BCR0_DMA_DIS, BCR0_DMA_DIS,
	BCR0_DMA_DIS, BCR0_DMA_5, BCR0_DMA_6, BCR0_DMA_7,
};

short tauisa_porttab[] = {	/* standard base port set */
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
};

short tauisa_irqtab[] = { 3, 5, 7, 10, 11, 12, 15, 0 };
short tauisa_dmatab[] = { 5, 6, 7, 0 };

long tauisa_baud = 256000;	/* default baud rate */
unsigned char tauisa_chan_mode = TAUISA_MODE_HDLC;	/* default mode */

#include "tauisa/ctaufw.h"
#include "tauisa/ctau2fw.h"
#include "tauisa/ctaug7fw.h"
#include "tauisa/ctaue1fw.h"

static int tauisa_download (unsigned short port, const unsigned char *firmware, long bits, const tauisa_dat_tst_t * tst);
static int tauisa_download2 (unsigned short port, const unsigned char *fwaddr);
static void tauisa_update_chan (tauisa_chan_t * c);
static void tauisa_init_chan (tauisa_board_t * b, int num);
static void tauisa_enable_loop (tauisa_chan_t * c);
static void tauisa_disable_loop (tauisa_chan_t * c);

#include "tauisa/ctddk.c"
#include "tauisa/ctau.c"

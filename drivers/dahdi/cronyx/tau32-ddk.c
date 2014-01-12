/*
 * DDK (Driver Development Kit) for Cronyx Tau-PCI/32 adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Copyright (C) 2003-2009 Cronyx Telecom, http://www.cronyx.ru, info@cronyx.ru
 * Author: Leo Yuriev <ly@cronyx.ru>, http://leo.yuriev.ru
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

#include "tau32-ddk.h"

#if !defined (ddk_assert_check) || !defined (ddk_assert)
#	define ddk_assert_check 0
#endif
#if !ddk_assert_check
#	if defined (ddk_assert)
#		undef ddk_assert
#	endif
#	define ddk_assert(x) do{}while(0)
#endif

#ifndef ddk_dbg_print
	static __inline void __ddk_dbg_print(const char* dummy, ...) {
	}
#	define ddk_dbg_print __ddk_dbg_print
#endif

#ifndef ddk_kd_print
#	define ddk_kd_print(x) do{}while(0)
#endif

#ifndef ddk_trap
#	define ddk_trap(x) do{}while(0)
#endif

#define ENABLE_E1_TES 0		/* do not use transmit elastic store */
#define ENABLE_E1_RES 1		/* use receive elastic store */
#include "tau32/ds21554.h"
#include "tau32/ct32reg.h"
#include "tau32/tau32.h"
#include "tau32/munich32x.h"

static __forceinline void TAU32_DecalogueAndMask (TAU32_Controller * pTau32)
{
	u32 disable_mask = pTau32->RegistersBar1_Hardware->IMASK_rw_0C;

	if (disable_mask != (u32) ~0ul) {
		disable_mask &= ~(M32X_STAT_PRI | M32X_STAT_LBII);
		/* LY: disable RX and E1 interrupt in TMA (aka phony) mode. */
		if (M32X_IsRxOnlyTMA (pTau32))
			disable_mask |= M32X_STAT_PRI | M32X_STAT_LBII;
		pTau32->RegistersBar1_Hardware->IMASK_rw_0C = disable_mask;
	}

	TAU32_decalogue (pTau32, TAU32_E1_ALL);
}

#include "tau32/munich32x.c"
#include "tau32/tau32.c"
#define __LY_BuildVersionInfo_INSTALL
#define __LY_BuildVersionInfo __LY_BuildVersionInfo_TAU32_DDK
#include "tau32/BuildSerial.inc"
unsigned const TAU32_ControllerObjectSize = sizeof (TAU32_Controller);
char TAU32_Infineon20321_BugWorkaround = 0;

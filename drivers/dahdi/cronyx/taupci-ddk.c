/*
 * Low-level subroutines for Cronyx Tau-PCI adapter.
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
 * $Id: $
 */

#include "ddk-host.h"
#include "ddk-arch.h"
#include "taupci-ddk.h"

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

/*-----------------------------------------------------------------------------*/

#include "taupci/ctaupci.h"
#include "taupci/peb20534.h"
#include "taupci/ds2153.h"
#include "taupci/lxt318.h"
#include "taupci/cpddk.c"
#include "taupci/BuildSerial.inc"

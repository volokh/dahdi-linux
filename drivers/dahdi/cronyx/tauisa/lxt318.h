/*
 * Level One LXT318 E1 transceiver registers.
 * Crystal CS61318 E1 Line Interface Unit registers.
 * Crystal CS61581 T1/E1 Line Interface Unit registers.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * Copyright (C) 1996-2013 Cronyx Telecom (www.cronyx.ru, info@cronyx.ru).
 * Author: Serge Vakulenko, <vak@cronyx.ru>
 * Author: Roman Kurakin, <rik@cronyx.ru>
 *
 * This software is distributed with NO WARRANTIES, not even the implied
 * warranties for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Authors grant any other persons or organisations permission to use
 * or modify this software as long as this message is kept with the software,
 * all derivative works or modified versions.
 *
 * $Id: lxt318.h,v 1.2 2006-05-31 15:19:21 ly Exp $
 */

#define LX_WRITE	0x00u
#define LX_READ		0x01u

#define LX_CCR1		0x10u
#define LX_CCR2		0x11u	/* CS61318 */
#define LX_EQGAIN	0x12u	/* CS61318 */
#define LX_RAM		0x13u	/* CS61318 */
#define LX_CCR3		0x14u	/* CS61581 */
#define LX_DPEC		0x15u	/* CS61581 */

#define LX_LOS		0x01u	/* loss of signal condition */
#define LX_HDB3		0x04u	/* HDB3 encoding enable */
#define LX_RLOOP	0x20u	/* remote loopback */
#define LX_LLOOP	0x40u	/* local loopback */
#define LX_TAOS		0x80u	/* transmit all ones */

#define LX_RESET	(LX_RLOOP | LX_LLOOP)	/* reset the chip */

#define LX_CCR2_LH	0x00u	/* Long Haul mode */
#define LX_CCR2_SH	0x01u	/* Long Haul mode */

#define LX_CCR3_E1_LH	0x60u	/* Long Haul mode */

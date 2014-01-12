/*
 * Level One LXT318 E1 transceiver registers.
 * Crystal CS61318 E1 Line Interface Unit registers.
 * Crystal CS61581 T1/E1 Line Interface Unit registers.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
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
 * $Id: lxt318.h,v 1.4 2007-01-18 22:40:06 ly Exp $
 */

#define LX_WRITE    0x00
#define LX_READ     0x01

#define LX_CCR1     0x10
#define LX_CCR2     0x11    /* CS61318 */
#define LX_EQGAIN   0x12    /* CS61318 */
#define LX_RAM      0x13    /* CS61318 */
#define LX_CCR3     0x14    /* CS61581 */
#define LX_DPEC     0x15    /* CS61581 */

#define LX_LOS      0x01    /* loss of signal condition */
#define LX_CLR_LOS  0x01
#define LX_CLR_LNP  0x02
#define LX_HDB3     0x04    /* HDB3 encoding enable */
#define LX_RLOOP    0x20    /* remote loopback */
#define LX_LLOOP    0x40    /* local loopback */
#define LX_TAOS     0x80    /* transmit all ones */

#define LX_RESET    (LX_RLOOP | LX_LLOOP)   /* reset the chip */

#define LX_CCR2_LH  0x00    /* Long Haul mode */
#define LX_CCR2_SH  0x01    /* Long Haul mode */

#define LX_CCR3_E1_LH   0x60    /* Long Haul mode */

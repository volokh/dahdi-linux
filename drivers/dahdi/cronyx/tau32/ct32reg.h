/*
 * Cronyx Tau-32 registers definitions
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (C) 2003-2013 Cronyx Telecom, info@cronyx.ru, http://www.cronyx.ru
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
 * $Id: ct32reg.h,v 1.15 2008-02-15 04:10:33 ly Exp $
 */

#define TAU32_OSC_HZ		32768000ul
#define TAU32_GEN_MAGIC_048	0x0C49BA5Eul	/* (256.0/125.0 - 2.0) * 2^32 */
#define TAU32_GEN_MAGIC_488	0x7D000000ul	/* 125.0/256.0 * 2^32 */

#define T32_CR		0	/* adapter control, write only */
#define T32_SR		0	/* adapter status, read only */
#define T32_CS0		1	/* chip select 0, write only */
#define T32_CS1		2	/* chip select 1, write only */
#define T32_CS2		3	/* chip select 2, write only */
#define T32_CS01	4	/* chip select 0 & 1, write only */
#define T32_DAT		5	/* selected chip read/write */
#define T32_CM		6	/* connection memory */
#define T32_CMAR	7	/* connection memory address */
#define T32_SACR	8	/* Sa-bits cross connector 0 (PEB) */
#define T32_UMR0	9
#define T32_UMR1	10
#define T32_TLOAD	11
#define T32_GLOAD0	12
#define T32_GLOAD1	13
#define T32_GLOAD2	14
#define T32_GLOAD3	15

#define UMR_2048	0x11
#define UMR_1024	0x12
#define UMR_512		0x13
#define UMR_256		0x14
#define UMR_128		0x15
#define UMR_64		0x16
#define UMR_ENABLE	0x10
#define UMR_SCR		0x08
#define UMR_FIFORST	0x20
#define UMR0_PEB_CHECK	0x80

/*
 * Configuration register bits.
 */
#define CR_CLK_MASK	0x23	/* transmit clock mask */
#define CR_CLK_STOP	0x00	/* transmit clock - internal, sync disabled */
#define CR_CLK_INT	0x01	/* transmit clock - internal */
#define CR_CLK_RCV0	0x02	/* transmit clock - RCLK0 */
#define CR_CLK_RCV1	0x03	/* transmit clock - RCLK1 */
#define CR_CLK_LYGEN	0x20	/* TDM CLK Generator */

#define CR_CMSWAP	0x04	/* DXC swap kick */
#define CR_CASEN	0x08	/* CAS framer enable */
#define CR_LED		0x10	/* LED control */
#define CR_GENRES	0x40	/* reset */
#define CR_CLKBLOCK	0x80	/* */

/*
 * Status register bits.
 */
#define SR_UMFERR0	0x01
#define SR_UMFERR1	0x02
#define SR_BLKERR	0x04
#define SR_RESERV	0x08
#define SR_TP0		0x10	/* twisted pair on channel 0 */
#define SR_TP1		0x20	/* twisted pair on channel 1 */
#define SR_LOF		0x40	/* LOF from CAS-framer */
#define SR_CMDONE	0x80	/* DXC connection memory swap done */

#define SF_TAU32_B	7
#define SF_TAU32_LITE	6
#define SF_TAU32_ADPCM	5
#define SF_TAU32_A	3
#define SF_SHIFT	5

#define SA_SRC_PEB	0x00
#define SA_SRC_0	0x01
#define SA_SRC_1	0x02
#define SA_SRC_ZERO	0x03
#define SA_SHIFT_0	0
#define SA_SHIFT_1	2
#define SA_EN_0		0x10	/* enable sa-cross for E1/0 */
#define SA_EN_1		0x20	/* enable sa-cross for E1/1 */
#define SA_ENPEB	0x40	/* enable sa-cross for peb */
#define SA_RESERVED	0x80	/* reserved */

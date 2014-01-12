/*
 * Low-level subroutines for Cronyx Tau-PCI adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 *
 * $Id: cpddk.c,v 1.98 2009-10-29 13:44:03 ly Exp $
 */

//#define HOLDBIT_MODE	// LY: broken
//#define USE_ISR_ALLS	// LY: double tx-irq rate

//#undef CCR2_XBRK
//#define CCR2_XBRK 0

static void taupci_init_chan (taupci_chan_t* c);
static u32 taupci_compute_clock (u32 hz, u32 baud, int *m, int *n);
static void taupci_setup_g703 (taupci_chan_t* c);
static void taupci_setup_e1 (taupci_chan_t* c);
static void taupci_setup_e3 (taupci_chan_t* c);
static void taupci_set_e1eps (taupci_chan_t* c);
static void taupci_init_dxc (taupci_board_t* b);
static void taupci_check_phony_mru (taupci_chan_t* c);
static unsigned taupci_ts_count (taupci_chan_t* c);
static void taupci_phony_stub_tx (taupci_chan_t* c);
static void taucpi_update_rdesc (taupci_chan_t* c);
static void taupci_wait_ready_for_action (taupci_board_t* b);
static void taupci_submit_action (taupci_board_t* b);
static void taupci_update_ccr1_ccr2 (taupci_chan_t* c);
static void taupci_wait_tsmem_ready (taupci_board_t* b);

static const u8 taupci_firmware_2e1a[] = {
#	include "fw-2e1-reva.h"
};

static const u8 taupci_firmware_4e1a[] = {
#	include "fw-4e1-reva.h"
};

static const u8 taupci_firmware_2e1b[] = {
#	include "fw-2e1-revb.h"
};

static const u8 taupci_firmware_4e1b[] = {
#	include "fw-4e1-revb.h"
};

static const u8 taupci_firmware_e3b[] = {
#	include "fw-e3-revb.h"
};

#define GPP_INIT(b) GPP = ((b)->base + PEB_GPDATA/4)
#define GPP_DATA(GPP)((GPP)[0])
#define GPP_DIR(GPP)((GPP)[-1])
#define GPP_DATA_BYTE(GPP)(((volatile u8 *)(GPP))[1])
#define GPP_1KDATA  0x01ul
#define GPP_DCLK    0x02ul
#define GPP_NCONFIG 0x04ul
#define GPP_NSTATUS 0x08ul
#define GPP_CONFDONE    0x10ul
#define GPP_1KLOAD  0x20ul
#define GPP_IOW  0x40ul
#define GPP_IOR  0x80ul

#define UNPACK_INIT(init_ptr) \
	const u8 *__unpack_ptr = (init_ptr) + 2; \
	int __unpack_count = 0; \
	unsigned __unpack_byte = 0; \
	unsigned __unpack_len = (init_ptr)[0] + ((init_ptr)[1] << 8)

#define UNPACK_BYTE(result) \
	do { \
		if (__unpack_count > 0) \
			--__unpack_count; \
		else { \
			__unpack_byte = *__unpack_ptr++; \
			if (__unpack_byte == 0) \
			__unpack_count = *__unpack_ptr++; \
		} \
		result = __unpack_byte; \
	} while (0)

static void taupci_gpp_setsafe (volatile u32 * GPP)
{
	GPP_DATA (GPP) = 0xFFFFFFFFul;
	GPP_DIR (GPP) = 0xFF;
	GPP_DATA (GPP) = 0xFFFFFFFFul;
}

static int taupci_detect_altera (volatile u32 * GPP)
{
	int i;
	// 1) reset ALTERA
	do {
		// LY: before here all pins are input, latch = 0xFFFFFFFFul

		// set direction: output - 0,1,2,5, all other - input/Z-state
		GPP_DIR (GPP) = GPP_NCONFIG | GPP_1KLOAD | GPP_IOW | GPP_IOR | GPP_DCLK | GPP_1KDATA;

		// set /1KLOAD = 0, nCONFIG = 1, pause 100 ns
		GPP_DATA (GPP) = GPP_IOW | GPP_IOR | GPP_NCONFIG;
		GPP_DATA (GPP) = GPP_IOW | GPP_IOR | GPP_NCONFIG;

		// set /1KLOAD = 0, nCONFIG = 0, pause 100 ns
		GPP_DATA (GPP) = GPP_IOW | GPP_IOR;
		GPP_DATA (GPP) = GPP_IOW | GPP_IOR;

	} while (0);

	// 2) wait for RESPONSE-1
	do {
		// wait for: nSTATUS == 0, CONF_DONE == 0
		i = 0;
		while (GPP_DATA (GPP) & (GPP_NSTATUS | GPP_CONFDONE)) {
			// each pci-read take at least 100ns
			// no more than 1.5 us (ALTERA promist no more than 1.0 us)
			if (++i > 15) {
				ddk_kd_print (("TAUPCI_Controller: bad status(1), seems firmware not needed\n"));
failed:
				taupci_gpp_setsafe (GPP);
				return 0;
			}
		}

		// nCONFIG low pulse-width must be not less than 2.0 us
		do
			// each pci-write take at least 100ns
			GPP_DATA (GPP) = 0;
		while (++i < 25);
	} while (0);

	// 3) set /1KLOAD = 0, nCONFIG = 1, wait for RESPONSE-2
	do {
		// delay up to 2.5 us (ALTERA need at least 2.0)
		i = 0;
		do
			// each pci-write take at least 100ns
			GPP_DATA (GPP) = GPP_NCONFIG;
		while (++i < 25);

		// wait for: nSTATUS == 1, CONF_DONE == 0
		while ((GPP_DATA (GPP) & (GPP_NSTATUS | GPP_CONFDONE)) != GPP_NSTATUS) {
			// each pci-read take at least 100ns
			if (++i > 1000 + 25) {
				// no more than 100 us (ALTERA promist 2.5 < x < ~50)
				ddk_kd_print (("TAUPCI_Controller: bad status(1), seems firmware not needed\n"));
				goto failed;
			}
		}

		// delay up to 7.5 us (ALTERA need at least 5.0)
		do
			// each pci-write take at least 100ns
			GPP_DATA (GPP) = GPP_NCONFIG;
		while (i++ <= 25 + 75);
	} while (0);
	return 1;
}

static unsigned taupci_download_firmware (volatile u32 * GPP,
					  const u8 * firmware_data, unsigned firmware_size)
{
	unsigned i, byte;
	char is_packed = 1;

	UNPACK_INIT (firmware_data);
	if (__unpack_len >= 0xFFFF) {
		__unpack_len = firmware_size;
		is_packed = false;
	} else if (__unpack_len < 16 || __unpack_len >= 0x10000ul) {
		ddk_kd_print (("TAUPCI_Controller: invalid firmware\n"));
		taupci_gpp_setsafe (GPP);
		return TAUPCI_DEAD_FW;
	}
	// LY: before here, IOW, IOR, CLK, DATA, NCONFIG are output
	do {
		// LY: check for: nSTATUS == 1
		if ((GPP_DATA (GPP) & GPP_NSTATUS) == 0) {
			ddk_kd_print (("TAUPCI_Controller: bad nstatus(3).\n"));
			goto failed;
		}
		// LY: check for CONF_DONE
		if (GPP_DATA (GPP) & GPP_CONFDONE) {
			// Ten extra clocks.
			i = 10;
			do {
				// LY: set DCLK = 0, DATA = 0
				GPP_DATA (GPP) = GPP_NCONFIG | GPP_IOW | GPP_IOR;

				// LY: DCLK = 1, DATA = 0
				GPP_DATA (GPP) = GPP_NCONFIG | GPP_DCLK | GPP_IOW | GPP_IOR;
			} while (--i);

			// LY: set DCLK = 0, DATA = 0
			GPP_DATA (GPP) = GPP_NCONFIG | GPP_IOW | GPP_IOR;

			// LY: check for: nSTATUS == 1, CONF_DONE == 1
			if ((GPP_DATA (GPP) & (GPP_NSTATUS | GPP_CONFDONE)) != (GPP_NSTATUS | GPP_CONFDONE)) {
				ddk_kd_print (("TAUPCI_Controller: bad nstatus(5).\n"));
				goto failed;
			}
			// LY: Succeeded.
			ddk_kd_print (("TAUPCI_Controller: firmware download succeeded, %d bytes remaining.\n", __unpack_len));

			// LY: set /1KLOAD = 1
			taupci_gpp_setsafe (GPP);
			return 0;
		}
		if (is_packed)
			UNPACK_BYTE (byte);
		else
			byte = *firmware_data++;
		byte += 1u << 8;

		do {
			// LY: set DCLK = 0, DATA = bit
			unsigned value = (byte & 1) | GPP_NCONFIG;

			GPP_DATA (GPP) = value;

			// LY: DCLK = 1, DATA = bit
			GPP_DATA (GPP) = value | GPP_DCLK;

			// LY: go to the next bit
			byte >>= 1;
		} while (byte != 1);
	} while (--__unpack_len);
	ddk_kd_print (("TAUPCI_Controller: bad confdone.\n"));

failed:
	ddk_kd_print (("TAUPCI_Controller: firmware downloading aborted, %d bytes remaining.\n", __unpack_len));
	taupci_gpp_setsafe (GPP);
	return TAUPCI_DEAD_FW;
}

/*
* Access to internal adapter registers.
* Leave local data bus in output mode.
LY: taupci_inb () takes at least 700 ns.
*/
static unsigned taupci_inb (taupci_board_t* b, unsigned reg)
{
	unsigned addr, result;
	volatile u32 *GPP;

	GPP_INIT (b);
	ddk_assert (reg >= 0 && reg < b->gpp_idle);
	addr = reg | b->gpp_idle;

	// LY: set address, /OIW = 1, /IOR = 1, /1KLOAD = 1
	GPP_DATA (GPP) = addr;

	// LY: set bus mode (address, /IOW, /IOR, /1KLOAD) = out, data = in
	GPP_DIR (GPP) = 0xFF;

	// LY: Important - set address again, Infineon-20534 can lost changes while gpp.pin is input
	GPP_DATA (GPP) = addr;

	// set address, /OIW = 1, /IOR = 0, /1KLOAD = 1
	GPP_DATA (GPP) = addr & ~GPP_IOR;

	// LY: Important - this 100ns pause is required, GPP latency may > 100 ns
	GPP_DATA (GPP) = addr & ~GPP_IOR;

	// LY: load data
	result = GPP_DATA_BYTE (GPP);

	// LY: leave address, set /OIW = 1, /IOR = 1, /1KLOAD = 1
	GPP_DATA (GPP) = addr;

	// LY: set bus (/IOW, /IOR, /1KLOAD) = out, (address, data) = in
	GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;
	return result;
}

/*
* LY: taupci_outb () takes at least 700 ns.
*/
static void taupci_outb (taupci_board_t* b, unsigned reg, unsigned val)
{
	unsigned addr_data;
	volatile u32 *GPP;

	GPP_INIT (b);
	ddk_assert (reg >= 0 && reg < b->gpp_idle);
	ddk_assert (val >= 0 && val <= 0xFF);
	addr_data = reg | b->gpp_idle | (val << 8);

	// LY: set address & data, /OIW = 1, /IOR = 1, /1KLOAD = 1
	GPP_DATA (GPP) = addr_data;

	// LY: set bus mode (address, data, /IOW, /IOR, /1KLOAD) = out
	GPP_DIR (GPP) = 0xFFFFFFFFul;

	// LY: Important - set address again, Infineon-20534 can lost changes while gpp.pin is input
	GPP_DATA (GPP) = addr_data;

	// LY: leave address, set /OIW = 0, /IOR = 1, /1KLOAD = 1
	GPP_DATA (GPP) = addr_data & ~GPP_IOW;

	// LY: Important - this 100ns pause is required, GPP latency may > 100 ns
	GPP_DATA (GPP) = addr_data & ~GPP_IOW;

	// LY: leave address, set /OIW = 1, /IOR = 1, /1KLOAD = 1
	GPP_DATA (GPP) = addr_data;

	// LY: set bus (/IOW, /IOR, /1KLOAD) = out, (address, data) = in
	GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;
}

/*
* LY: send command to 20534 and waitfor a status-ask, return 0 on success.
*/
static int taupci_wait_ask (taupci_board_t* b, u32 cmdr)
{
	int i;
	u32 r;

	ddk_flush_cpu_writecache();
	GSTAR (b) = 0xFFFFFFFFul;
	GCMDR (b) = cmdr;

	i = 1000 * 10; do {
		ddk_yield_dmabus ();
		r = GSTAR (b) & (GSTAR_ARF | GSTAR_ARACK);
		GSTAR (b) = r;
		switch (r) {
			case 0:
				break;
			case GSTAR_ARACK:
				b->action_pending = 0;
				return 0;   /* LY: ok */
			case GSTAR_ARF:
				ddk_kd_print (("cp%d wait-ask(%d) failed\n", b->num, 1000 * 10 - i));
				break;   /* LY: failed */
			default:
				ddk_kd_print (("cp%d wait-ask(%d) mad\n", b->num, 1000 * 10 - i));
				break;  /* LY: chip is mad */
		}
	} while (--i);  /* LY: wait no more ~1 ms (each gstar-read take 100 ns) */
	if (!i)
		ddk_kd_print (("cp%d wait-ask, timeout\n", b->num));
	return -1;    /* LY: timeout */
}

static void taupci_wait_ready_for_action (taupci_board_t* b)
{
	if (!(b->dead & TAUPCI_DEAD_20534) && b->action_pending) {
		unsigned i = 10000; do {
			u32 gstar = GSTAR (b);
			if (gstar & GSTAR_ARF) {
				ddk_kd_print (("cp %d: wait-ready action-failed (%d)\n", b->num, i));
				b->dead |= TAUPCI_DEAD_20534;
				if (b->irq_enabled)
					taupci_enable_interrupt (b, 0);
				return;
			}
			if (gstar & GSTAR_ARACK) {
				GSTAR (b) = GSTAR_ARACK;
				b->action_pending = 0;
				ddk_kd_print (("cp %d: wait-ready action-done (%d)\n", b->num, i));
				return;
			}
			ddk_yield_dmabus ();
			if (!b->action_pending)
				return;
		} while (--i);  /* LY: wait no more ~1 ms (each gstar-read take 100 ns) */
		b->dead |= TAUPCI_DEAD_20534;
		if (b->irq_enabled)
			taupci_enable_interrupt (b, 0);
		ddk_kd_print (("cp %d: wait-ready action-timeout\n", b->num));
	}
}

static void taupci_submit_action (taupci_board_t* b)
{
	ddk_flush_cpu_writecache();

	if (GSTAR (b) & GSTAR_ARACK)
		GSTAR (b) = GSTAR_ARACK;

	b->action_pending = 1;
	GCMDR (b) = b->irq_enabled ? 0x0001 : 0x0201;
	ddk_yield_dmabus ();

	if (GSTAR (b) & GSTAR_ARACK) {
		GSTAR (b) = GSTAR_ARACK;
		b->action_pending = 0;
	}
}

static void taupci_kick_setup (taupci_chan_t* c)
{
	c->setup_cnt = 2;
	c->status = TAUPCI_ESTS_UNKNOWN;
}

static void taupci_update_ccr1_ccr2 (taupci_chan_t* c)
{
	c->ccr2 &= ~0x00500009ul;
	c->ccr1 &= ~0x83ul;
	if (c->phony) {
		/*
		* Receive crc bytes, no preamble, idle in flag, no one insertion.
		*/
		c->ccr2 |= 0x00500009ul;
	} else {
		if (c->hdlc_shareflags)
			c->ccr1 |= 0x80ul;
		if (! c->hdlc_crclen)
			c->ccr2 |= 0x00500001;
		else if (c->hdlc_crclen & 2)
			c->ccr1 |= 0x01ul;
	}
	CCR1 (c) = c->ccr1;
	if (!c->unframed && c->ts == 0 && (c->type == TAUPCI_E1 || c->type == TAUPCI_DATA))
		c->ccr2 = (c->ccr2 & ~CCR2_RENAB) | CCR2_XBRK;
	CCR2 (c) = c->ccr2;
}

/*
* Access to DS2153 chips on the Tau/E1 adapter.
LY: takes 1.4 us.
*/
static unsigned taupci_dallas_in (taupci_chan_t* c, unsigned reg)
{
	unsigned val;

	taupci_outb (c->board, c->E1CS, reg);
	val = taupci_inb (c->board, c->board->E1DATA);
	//ddk_kd_print (("taupci: ds%d[0x%02X] -> 0x%02X, cs = 0x%X, data = 0x%X\n", c->num, reg, val, c->E1CS, c->board->E1DATA));
	return val;
}

static void taupci_dallas_out (taupci_chan_t* c, unsigned reg, unsigned val)
{
	//ddk_kd_print (("taupci: 0x%02X -> ds%d[0x%02X], cs = 0x%X, data = 0x%X\n", val, c->num, reg, c->E1CS, c->board->E1DATA));
	taupci_outb (c->board, c->E1CS, reg);
	taupci_outb (c->board, c->board->E1DATA, val);
}

/*
* Get the DS2153 status register, using write-read-write scheme.
LY: takes 4.2 us.
*/
static unsigned taupci_dallas_status (taupci_chan_t* c, unsigned reg)
{
	unsigned val;

	taupci_outb (c->board, c->E1CS, reg);
	taupci_outb (c->board, c->board->E1DATA, 0xFF); /* lock bits */
	taupci_outb (c->board, c->E1CS, reg);
	val = taupci_inb (c->board, c->board->E1DATA);   /* get values */
	taupci_outb (c->board, c->E1CS, reg);
	taupci_outb (c->board, c->board->E1DATA, val);  /* unlock bits */
	return val;
}

/*
 * Access to LXT318 chip on the Tau/G.703 adapter.
 * LY: takes 11.2 us.
 */
static void taupci_lxt318_io (taupci_board_t* b, unsigned val, unsigned cs)
{
	ddk_assert (val <= 0xFF);

	val += 1u << 8;
	do {
		cs &= ~(GSIO_SDI | GSIO_SCLK);
		if (val & 1)
			cs |= GSIO_SDI;
		taupci_outb (b, GSIO, cs);
		taupci_outb (b, GSIO, cs | GSIO_SCLK);
		val >>= 1;
	} while (val != 1);
	taupci_outb (b, GSIO, cs);
}

/*
 * LY: takes 23.8 us.
 */
static void taupci_lxt318_out (taupci_chan_t* c, unsigned reg, unsigned val)
{
	unsigned cs = GSIO_CS0 << c->num;

	taupci_outb (c->board, GSIO, cs);
	taupci_lxt318_io (c->board, (reg << 1) | LX_WRITE, cs);
	taupci_lxt318_io (c->board, val, cs);
	taupci_outb (c->board, GSIO, 0);
}

/*
 * LY: takes 29.4 us.
 */
static unsigned taupci_lxt318_in (taupci_chan_t* c, unsigned reg)
{
	unsigned cs = GSIO_CS0 << c->num;
	unsigned val = 0;
	unsigned bit = 1;

	taupci_outb (c->board, GSIO, cs);
	taupci_lxt318_io (c->board, reg + reg + LX_READ, cs);
	do {
		taupci_outb (c->board, GSIO, cs | GSIO_SCLK);
		if (taupci_inb (c->board, c->GLS) & GLS_SDO)
			val += bit;
		taupci_outb (c->board, GSIO, cs);
		bit += bit;
	} while (bit <= 0x80);
	taupci_outb (c->board, GSIO, 0);
	return val;
}

u32 taupci_regio (taupci_chan_t* c, int op, int reg, u32 val)
{
	switch (op) {
		case TAUPCI_REGIO_INB:
			val = taupci_inb (c->board, reg);
			break;
		case TAUPCI_REGIO_IN:
			val = taupci_dallas_in (c, reg);
			break;
		case TAUPCI_REGIO_INS:
			val = taupci_dallas_status (c, reg);
			break;
		case TAUPCI_REGIO_INX:
			val = taupci_lxt318_in (c, LX_CCR1);
			break;
		case TAUPCI_REGIO_INB_OUTB:
			val = taupci_inb (c->board, reg);
			taupci_outb (c->board, reg, val);
			break;
		case TAUPCI_REGIO_OUTB_INB:
			taupci_outb (c->board, reg, val);
			val = taupci_inb (c->board, reg);
			break;
		case TAUPCI_REGIO_OUTB:
			taupci_outb (c->board, reg, val);
			break;
		case TAUPCI_REGIO_OUTX:
			taupci_lxt318_out (c, LX_CCR1, val);
			break;
		case TAUPCI_REGIO_R_W:
			c->scc_regs[reg/4] = val;
			val = c->scc_regs[reg/4];
			break;
		case TAUPCI_REGIO_OUT_IN:
			taupci_dallas_out (c, reg, val);
			val = taupci_dallas_in (c, reg);
			break;
	}
	return val;
}

static unsigned taupci_altera_bustest (volatile u32 * GPP)
{
	/*
	* internal bus test
	*/
	int i = 0xFF;

	GPP_DIR (GPP) = i;

	do {
		unsigned test, pattern;

		GPP_DATA (GPP) = i | GPP_1KLOAD;
		// LY: Important - this 100ns pause is required, GPP latency may > 100 ns
		GPP_DATA (GPP) = i | GPP_1KLOAD;
		test = GPP_DATA_BYTE (GPP);
		pattern = (i & ~0x20) | ((~i & 0x10) << 1);
		if (test != pattern) {
			ddk_kd_print (("TAU32_Controller: bus test failed 0x%02X/0x%02X\n", test, pattern));
			return TAUPCI_DEAD_ABUS | (i << 8) | (pattern << 16) | (test << 24);
		}
	} while (--i >= 0);
	GPP_DATA (GPP) = 0xFFFFFFFFul;
	GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;
	return 0;
}

static void taupci_stall_1ms (taupci_board_t* b)
{
	unsigned i = 10000;
	do {
		ddk_yield_cpu ();
		GPDATA (b) = 0xFFFFFFFFul;
	} while (--i);
}

static void taupci_clear_peb20534 (taupci_board_t* b)
{
	unsigned i;

	/*
	* Enable local bus. Little endian; peripheral block
	* * configuration=100 (gp15..0 lad15..0);
	* * DMAC controlled via HOLD bit.
	*/
	GCMDR (b) = 0x0200ul;
	GPIM (b) = 0xFFFFFFFFul;
	GMODE (b) = 0x00040000ul;

	/*
	* Keep/set all pins as input
	*/
	GPDIR (b) = 0xFF;
	GPDATA (b) = 0xFFFFFFFFul;

	/*
	* reset LBI
	*/
	LCONF (b) = 0;

	/*
	* switch SSC to non-operation/config mode
	*/
	b->base[PEB_SSCCON/4] = 0;
	b->base[PEB_SSCCSE/4] = 0;
	b->base[PEB_SSCIM/4] = 0;

	i = 0x100/4; do {
		b->base[PEB_IMR/4 + i] = 0xFFFFFFFFul;
		b->base[PEB_CMDR/4 + i] = CMDR_XRES | CMDR_RRES;
		b->base[PEB_CCR0/4 + i] = 0;
		b->base[PEB_CCR1/4 + i] = 0;
		b->base[PEB_CCR2/4 + i] = 0;
		b->base[PEB_CMDR/4 + i] = CMDR_XRES | CMDR_RRES;
		i += 0x80/4;
	} while (i < 0x300/4);

	/*
	* LY: clear all config & state
	*/
	i = 0x0C/4; do {
		b->base[i] = 0;
	} while (++i < 0x100/4);
}

void taupci_halt (taupci_board_t* b)
{
	unsigned i;

	/*
	* Enable local bus. Little endian; peripheral block
	* * configuration=100 (gp15..0 lad15..0);
	* * DMAC controlled via HOLD bit.
	*/

	ddk_flush_cpu_writecache();
	taupci_clear_peb20534 (b);

	/*
	* LY: reset rx/tx DMA and kill buggy PEB :)
	*/
	b->base[PEB_0CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDR | CFG_RDT;
	b->base[PEB_1CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDR | CFG_RDT;
	b->base[PEB_2CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDR | CFG_RDT;
	b->base[PEB_3CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDR | CFG_RDT;
	taupci_wait_ask (b, 0xff300201ul);

	if (b->gpp_idle) {
		taupci_chan_t *c;

		taupci_outb (b, BCR, 0);
		taupci_outb (b, BCR, BCR_UBRUN);

		c = b->chan; do {
			switch (c->type) {
			case TAUPCI_G703:
				i = 0x10; do {
					taupci_lxt318_out (c, i, 0);
				} while (++i < 0x20);
				taupci_lxt318_out (c, LX_CCR1, LX_TAOS);
				taupci_lxt318_out (c, LX_CCR1, LX_RESET);
				taupci_lxt318_out (c, LX_CCR1, LX_TAOS);
				taupci_lxt318_out (c, LX_CCR1, LX_TAOS | LX_HDB3 | LX_CLR_LOS | LX_CLR_LNP);
				taupci_lxt318_in (c, LX_CCR1);
				break;
			case TAUPCI_E1:
				i = 0x10; do {
					taupci_dallas_out (c, i & 255, 0);
				} while (++i < 0x200);
				taupci_dallas_out (c, DS_LICR, LICR_POWERDOWN);
				taupci_outb (b, c->E1CR, 0);
				break;
			}
		} while (++c < b->chan + TAUPCI_NCHAN);
		taupci_outb (b, BCR, 0);
	}

	/*
	* LY: kick status-ask
	*/
	GSTAR (b) = 0xFFFFFFFFul;
}

static unsigned taupci_bustest_pattern (taupci_chan_t* c, int i)
{
	unsigned p;

	switch (c->type) {
		case TAUPCI_G703:
			i += c->num * 23;
			p = i & (LX_HDB3 | LX_RLOOP | LX_LLOOP | LX_TAOS);
			if ((p & (LX_RLOOP | LX_LLOOP | LX_TAOS)) == LX_RESET)
				p |= LX_TAOS;
			break;

		case TAUPCI_E1:
			i += c->num * 11;
			p = ((1ul << (i & 7)) + i / 16);
			if (i & 8)
				p = ~p;
			break;

		default:
			p = (i + c->num * 37) & 255;
	}
	return p;
}

static void taupci_bustest_clean (taupci_board_t* b)
{
	unsigned i;
	taupci_chan_t *c;

	taupci_outb (b, BCR, 0);
	taupci_outb (b, BCR, BCR_UBRUN);

	c = b->chan; do {
		switch (c->type) {
		case TAUPCI_G703:
			i = 0x10; do {
				taupci_lxt318_out (c, i, 0);
			} while (++i < 0x20);
			taupci_lxt318_out (c, LX_CCR1, LX_TAOS);
			taupci_lxt318_out (c, LX_CCR1, LX_RESET);
			taupci_lxt318_out (c, LX_CCR1, LX_TAOS);
			taupci_lxt318_out (c, LX_CCR1, LX_TAOS | LX_HDB3 | LX_CLR_LOS | LX_CLR_LNP);
			taupci_lxt318_in (c, LX_CCR1);
			break;
		case TAUPCI_E1:
			i = 0x10; do {
				taupci_dallas_out (c, i & 255, 0);
			} while (++i < 0x200);
			//taupci_outb (b, c->E1CR, E1CR_CLK_INT | E1CR_GRUN);
			break;
		}
	} while (++c < b->chan + TAUPCI_NCHAN);
}

static u32 taupci_bustest_extra (taupci_board_t* b)
{
	int i;
	taupci_chan_t *c;

	static const u8 ds2153_regs[] = {
		DS_TS + 0x0, DS_TS + 0x1, DS_TS + 0x2, DS_TS + 0x3,
		DS_TS + 0x4, DS_TS + 0x5, DS_TS + 0x6, DS_TS + 0x7,
		DS_TS + 0x8, DS_TS + 0x9, DS_TS + 0xA, DS_TS + 0xB,
		DS_TS + 0xC, DS_TS + 0xD, DS_TS + 0xE, DS_TS + 0xF,
		DS_TIR + 0, DS_TIR + 1, DS_TIR + 2, DS_TIR + 3, DS_TIDR,
		DS_RCBR + 0, DS_RCBR + 1, DS_RCBR + 2, DS_RCBR + 3,
		DS_TCBR + 0, DS_TCBR + 1, DS_TCBR + 2, DS_TCBR + 3,
		DS_TAF, DS_TNAF
	};

	ddk_kd_print ((">> taupci_bustest_extra()\n"));
	taupci_bustest_clean (b);

	ddk_kd_print (("== taupci_bustest_extra().1\n"));
	/*
	* LY: complete cross-chip/cs bus test
	*/
	i = 0; do {
		c = b->chan; do {
			/*
			* LY: write some to chip-regs
			*/
			unsigned r, n, p = taupci_bustest_pattern (c, i);

			switch (c->type) {
			case TAUPCI_G703:
				taupci_lxt318_out (c, LX_CCR1, p | LX_CLR_LOS | LX_CLR_LNP);
				break;
			case TAUPCI_E1:
				r = p % sizeof (ds2153_regs); n = 0; do {
					u8 byte_out = (r == n) ? p : ~p;
					taupci_dallas_out (c, ds2153_regs[n], byte_out);
					p = (p >> 1) | (p << 31);
				} while (++n < sizeof (ds2153_regs));
				break;
			}
		} while (++c < b->chan + TAUPCI_NCHAN);

		c = b->chan; do {
			/*
			* LY: read back and check
			*/
			unsigned r, n, p = taupci_bustest_pattern (c, i);
			u8 byte_in;

			switch (c->type) {
			case TAUPCI_G703:
				byte_in = taupci_lxt318_in (c, LX_CCR1) & (LX_HDB3 | LX_RLOOP | LX_LLOOP | LX_TAOS);
				if (p != byte_in)
					return (TAUPCI_DEAD_CS0 << c->num)
					| (i << 8) | (p << 16)
					| (byte_in << 24);
				break;
			case TAUPCI_E1:
				r = p % sizeof (ds2153_regs);
				n = 0; do {
					u8 byte_out, byte_addr;

					byte_addr = ds2153_regs[n];
					byte_in = taupci_dallas_in (c, byte_addr);
					byte_out = (u8) ((r == n) ? p : ~p);
					if (byte_out != byte_in)
						return (TAUPCI_DEAD_CS0 << c->num)
						| (byte_addr << 8) | (byte_out << 16) | (byte_in << 24);
					p = (p >> 1) | (p << 31);
				} while (++n < sizeof (ds2153_regs));
				break;
			}
		} while (++c < b->chan + TAUPCI_NCHAN);
	} while (++i < 256);

	ddk_kd_print (("== taupci_bustest_extra().2\n"));

	taupci_bustest_clean (b);

	ddk_kd_print (("<< taupci_bustest_extra()\n"));
	return 0;
}

static const struct tag_cp_firmware_table {
	const u8 *data;
	unsigned size;
} taupci_firmware_table[8] = {
	{ 0, 0 },                                                   // 0
	{ taupci_firmware_e3b, sizeof (taupci_firmware_e3b) },      // 1 == TAUPCI_FW_E3_B
	{ taupci_firmware_2e1b, sizeof (taupci_firmware_2e1b) },    // 2 == TAUPCI_FW_2E1_B
	{ taupci_firmware_2e1a, sizeof (taupci_firmware_2e1a) },    // 3 == TAUPCI_FW_2E1_A
	{ 0, 0 },                                                   // 4
	{ 0, 0 },                                                   // 5
	{ taupci_firmware_4e1b, sizeof (taupci_firmware_4e1b) },    // 6 == TAUPCI_FW_4E1_B
	{ taupci_firmware_4e1a, sizeof (taupci_firmware_4e1a) }     // 7 == TAUPCI_FW_4E1_A
};

/*
* Initialize the adapter structure.
*/
u32 taupci_init (taupci_board_t* b, int num, volatile u32 *base, taupci_iq_t * iq, u32 iq_dma)
{
	u8 bsr;
	volatile u32 *GPP;
	taupci_chan_t *c;
	int i;

	ddk_memset (b, 0, sizeof (taupci_board_t));

	c = b->chan; do {
		c->board = b;
	} while (++c < b->chan + TAUPCI_NCHAN);

	b->base = base;
	b->num = (u8) num;
	//b->mux = 0;  /* independent E1 channels */
	//b->dxc_status = 0;
	b->bcr = BCR_UBRUN;
	//b->e1cfg = 0;
	b->E1DATA = E1DAT;
	b->name = "Tau-PCI/broken";
	//b->gpp_idle = 0;
	//b->irq_enabled = 0;
	//b->dead = 0;

	taupci_clear_peb20534 (b);
	GMODE (b) = 0x00040001ul;

	/*
	* LY: reset rx/tx DMA
	*/
	b->base[PEB_0CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDT;
	b->base[PEB_1CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDT;
	b->base[PEB_2CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDT;
	b->base[PEB_3CFG/4] = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR | CFG_RDT;

	if (taupci_wait_ask (b, 0x0201))
		b->dead |= TAUPCI_DEAD_20534;

	b->gpp_idle = GPP_IOW | GPP_IOR;
	ddk_memset (iq, 0, sizeof (taupci_iq_t));

	GPP_INIT (b);
	if (taupci_detect_altera (GPP)) {
		const u8 *firmware_data = 0;
		unsigned firmware_size = 0;
		unsigned model_id = GPP_DATA_BYTE (GPP);

		if (model_id != GPP_DATA_BYTE (GPP) || model_id != GPP_DATA_BYTE (GPP)
			|| model_id != GPP_DATA_BYTE (GPP)) {
failed:
				b->gpp_idle = 0xFF;
				taupci_gpp_setsafe (GPP);
				return b->dead |= TAUPCI_DEAD_HW;
			}
			model_id >>= 5;
			ddk_kd_print (("taupci: hardware model id %d\n", model_id));
			if (model_id < sizeof (taupci_firmware_table) / sizeof (taupci_firmware_table[0])) {
				firmware_data = taupci_firmware_table[model_id].data;
				firmware_size = taupci_firmware_table[model_id].size;
			}
#ifdef TAUPCI_CUSTOM_FIRMWARE
			if (model_id < TAUPCI_FW_MAX_ID) {
				if (b->custom_firmware_data[model_id])
					firmware_data = b->custom_firmware_data[model_id];
				if (b->custom_firmware_size[model_id])
					firmware_size = b->custom_firmware_size[model_id];
			}
#endif
			if (firmware_size == 0 || firmware_data == 0)
				goto failed;

			b->gpp_idle |= GPP_1KLOAD;
			b->E1DATA = E4DAT;

			b->dead |= taupci_download_firmware (GPP, firmware_data, firmware_size);
			if ((b->dead & (TAUPCI_DEAD_FW | TAUPCI_DEAD_HW)) == 0)
				b->dead |= taupci_altera_bustest (GPP);
			if (b->dead)
				return b->dead;
	} else {
		/*
		* We do not need firmware, check for blinking bit
		*/
		bsr = taupci_inb (b, BSR);
		if ((bsr & BSR_BT_MASK) != BSR_BT_E3 && (bsr & BSR_BID) == (taupci_inb (b, BSR) & BSR_BID))
			goto failed;
	}

	bsr = taupci_inb (b, BSR);
	ddk_kd_print (("taupci_init(), bsr = 0x%02X\n", bsr));
	switch (bsr & BSR_BT_MASK) {
		default:
			b->model = 0;
			b->name = "Tau-PCI/Unknown";
			break;

		case BSR_BT_SERIAL:
			if ((bsr & BSR_CH23) != (taupci_inb (b, BSR) & BSR_CH23)) {
				b->model = TAUPCI_MODEL_LITE;
				b->name = "Tau-PCI/Lite";
			} else {
				b->model = TAUPCI_MODEL_2RS;
				b->name = "Tau-PCI/2xS";
				if (bsr & BSR_CH23) {
					b->model = TAUPCI_MODEL_4RS;
					b->name = "Tau-PCI/2xS+Delta";
				}
			}
			break;
		case BSR_BT_2E1:
			b->model = TAUPCI_MODEL_OE1;
			b->name = "Tau-PCI/E1";
			if (bsr & BSR_CH23) {
				b->model = TAUPCI_MODEL_OE1_2RS;
				b->name = "Tau-PCI/E1+Delta";
			}
			break;
		case BSR_BT_2G703:
			b->model = TAUPCI_MODEL_2G703;
			b->name = "Tau-PCI/G703";
			if (bsr & BSR_CH23) {
				b->model = TAUPCI_MODEL_2G703_2RS;
				b->name = "Tau-PCI/G703+Delta";
			}
			break;
		case BSR_BT_2E1G:
			b->model = TAUPCI_MODEL_2E1_4;
			b->name = "Tau-PCI/2E1";
			if (bsr & BSR_CH23) {
				b->model = TAUPCI_MODEL_2E1_2RS;
				b->name = "Tau-PCI/2E1+Delta";
			}
			break;
		case BSR_BT_4E1G:
			b->model = TAUPCI_MODEL_4E1;
			b->name = "Tau-PCI/4E1";
			break;
		case BSR_BT_E3:
			b->model = TAUPCI_MODEL_1E3;
			b->name = "Tau-PCI/E3";
			break;
		case BSR_BT_HSSI:
			b->model = TAUPCI_MODEL_1HSSI;
			b->name = "Tau-PCI/HSSI";
			break;
	}
	if (b->model == TAUPCI_MODEL_1E3) {
		switch (bsr & BSR_JOSC_MASK) {
		default:
			break;
		case BSR_JOSC_E3:
			b->chan->type = TAUPCI_E3;
			b->osc = 34368000;
			break;
		case BSR_JOSC_T3:
			b->name = "Tau-PCI/T3";
			b->chan->type = TAUPCI_T3;
			b->osc = 44736000;
			break;
		case BSR_JOSC_STS1:
			b->chan->type = TAUPCI_STS1;
			b->osc = 51840000;
			b->name = "Tau-PCI/STS-1";
			break;
		}
	} else {
		switch (bsr & BSR_JOSC_MASK) {
			default:
				break;
			case BSR_JOSC_32:
				b->osc = 32768000;
				break;
			case BSR_JOSC_16:
				b->osc = 16384000;
				break;
		}
	}

	IQLENR0 (b) = 0
		| ((sizeof (iq->iq_rx[0]) / 128 - 1) << 28)
		| ((sizeof (iq->iq_rx[1]) / 128 - 1) << 24)
		| ((sizeof (iq->iq_rx[2]) / 128 - 1) << 20)
		| ((sizeof (iq->iq_rx[3]) / 128 - 1) << 16)
		| ((sizeof (iq->iq_tx[0]) / 128 - 1) << 12)
		| ((sizeof (iq->iq_tx[1]) / 128 - 1) << 8)
		| ((sizeof (iq->iq_tx[2]) / 128 - 1) << 4)
		| ((sizeof (iq->iq_tx[3]) / 128 - 1) << 0);

	IQLENR1 (b) = 0
		| ((sizeof (iq->iq_cfg) / 128 - 1) << 20)
		| ((sizeof (iq->iq_lbi) / 128 - 1) << 16);

	IQPBAR (b) = iq_dma + ((char *) iq->iq_lbi - (char *) iq);
	IQCFGBAR (b) = iq_dma + ((char *) iq->iq_cfg - (char *) iq);

	/*
	* Initialize channel structures.
	*/
	c = b->chan; do {
		taupci_init_chan (c);
		/*
		* Set buffer physical addresses.
		*/
		c->iq_rx = iq->iq_rx[c->num];
		RXBAR (c) = iq_dma + ((char *) c->iq_rx - (char *) iq);
		c->iq_tx = iq->iq_tx[c->num];
		TXBAR (c) = iq_dma + ((char *) c->iq_tx - (char *) iq);

		c->tdesc = iq->iq_td[c->num];
		c->rdesc = iq->iq_rd[c->num];
		i = 0; do {
			c->td_dma[i] = iq_dma + ((char *) (c->tdesc + i) - (char *) iq);
			c->rd_dma[i] = iq_dma + ((char *) (c->rdesc + i) - (char *) iq);
		} while (++i < TAUPCI_NBUF);
		i = 0; do {
			c->tdesc[i].next = c->td_dma[(i + 1) & (TAUPCI_NBUF - 1)];
			c->rdesc[i].next = c->rd_dma[(i + 1) & (TAUPCI_NBUF - 1)];
		} while (++i < TAUPCI_NBUF);

	} while (++c < b->chan + TAUPCI_NCHAN);

	if ((b->dead & (TAUPCI_DEAD_HW | TAUPCI_DEAD_FW | TAUPCI_DEAD_ABUS)) == 0)
		b->dead |= taupci_bustest_extra (b);
	if (b->dead)
		return b->dead;

	/*
	* Reset the adapter (clear UBRUN).
	*/
	taupci_outb (b, BCR, b->bcr & ~BCR_UBRUN);
	taupci_led (b, 0);
	taupci_stall_1ms (b);

	/*
	* Initialize all channels.
	*/
	c = b->chan; do {
		if (c->type == TAUPCI_NONE)
			continue;
		/*
		* Set up the G.703/E1 controllers.
		*/
		if (c->type == TAUPCI_G703)
			taupci_setup_g703 (c);
		else if (c->type == TAUPCI_E1)
			taupci_setup_e1 (c);
		else if (c->board->model == TAUPCI_MODEL_1E3)
			taupci_setup_e3 (c);

		/*
		* set correct dir state
		*/
		taupci_set_e1eps (c);

		c->ccr0 = CCR0_INTVIS;
		CCR0 (c) = c->ccr0;
		c->ccr2 = 0x00140000ul | CCR2_XBRK;
		CCR2 (c) = c->ccr2;
		SCC_IMR (c) = c->scc_imr = 0xFFFFFFFFul;

		/*
		* Timeslot assignment for phony mode.
		* * Standard timeslot configuration,
		* * TTSN/RTSN=31, TCS/RCS=7, TCC/RCC=255.
		*/
		TSAX (c) = 0x1f0700fful;
		TSAR (c) = 0x1f0700fful;

		c->irn = 0;
		c->itn = 0;
		c->dma_imr = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
		CFG (c) = c->dma_imr | CFG_RDT/* | CFG_RDR*/; // LY: CFG_RDR was kill the PEB20534, sure :(
		CMDR (c) = CMDR_XRES | CMDR_RRES;
	} while (++c < b->chan + TAUPCI_NCHAN);

	if (taupci_wait_ask (b, 0xff300201ul))
		return b->dead = TAUPCI_DEAD_20534;

	taupci_set_mux (b, 0);
	taupci_stall_1ms (b);

	if (b->model <= TAUPCI_MODEL_LITE) {
		// LY: only one channel
		// LY: tx-fifo size = 124/1/1/1 dwords.
		FIFOCR1 (b) = (31ul << 27) | (1ul << 22) | (1ul << 17) | (1ul << 11);
		// LY: tx-treshhold mem->dma = 60/4/4/4 dwords.
		FIFOCR2 (b) = (30ul << 27) | (4ul << 22) | (4ul << 17) | (4ul << 11) | (1ul << 6);
		// LY: tx-treshhold dma->scc = 56/2/2/2 dwords.
		FIFOCR4 (b) = (56ul << 24) | (2ul << 16) | (2ul << 8) | (2ul << 0);
	} else if (b->model <= TAUPCI_MODEL_OE1) {
		// LY: only two channel
		// LY: tx-fifo size = 60/60/4/4 dwords.
		FIFOCR1 (b) = (15ul << 27) | (15ul << 22) | (1ul << 17) | (1ul << 11);
		// LY: tx-treshhold mem->dma = 48/48/4/4 dwords.
		FIFOCR2 (b) = (24ul << 27) | (24ul << 22) | (4ul << 17) | (4ul << 11) | (1ul << 6) | (1ul << 4);
		// LY: tx-treshhold dma->scc = 46/46/2/2 dwords.
		FIFOCR4 (b) = (46ul << 24) | (46ul << 16) | (2ul << 8) | (2ul << 0);
	} else {
		// LY: four channels
		// LY: tx-fifo size = 8*4 = 32 dwords per channel.
		FIFOCR1 (b) = (8ul << 27) | (8ul << 22) | (8ul << 17) | (8ul << 11);
		// LY: tx-treshhold mem->dma = 19 dwords (for each channel).
		FIFOCR2 (b) = (19ul << 27) | (19ul << 22) | (19ul << 17) | (19ul << 11);
		// LY: tx-treshhold dma->scc = 17 dwords (for each channel).
		FIFOCR4 (b) = (17ul << 24) | (17ul << 16) | (17ul << 8) | (17ul << 0);
	}
	// LY: rx-fifo threshold (rx-fifo -> dma), may just one dword.
	FIFOCR3 (b) = 15;

	if (b->E1DATA == E4DAT && b->model >= TAUPCI_MODEL_2E1_4) {
		u8 byte = taupci_inb (b, E1SR);
		if (byte & SR_CMBUSY)
			return b->dead = TAUPCI_DEAD_DXC | (1 << 8);

		taupci_outb (b, E1CFG, b->e1cfg | CFG_CMSWAP);
		for (i = 0;; i++) {
			byte = taupci_inb (b, E1SR);
			if (!(byte & SR_CMBUSY))
				break;
			if (i > 180)
				return b->dead = TAUPCI_DEAD_DXC | (2 << 8);
		}

		taupci_init_dxc (b);
		byte = taupci_inb (b, E1SR);
		if (!(byte & SR_CMBUSY))
			return b->dead = TAUPCI_DEAD_DXC | (3 << 8) | (byte << 24);
		for (i = 0;; i++) {
			byte = taupci_inb (b, E1SR);
			if (!(byte & SR_CMBUSY))
				break;
			if (i > 180)
				return b->dead = TAUPCI_DEAD_DXC | (4 << 8);
		}

		taupci_init_dxc (b);
		byte = taupci_inb (b, E1SR);
		if (!(byte & SR_CMBUSY))
			return b->dead = TAUPCI_DEAD_DXC | (5 << 8);
		for (i = 0;; i++) {
			byte = taupci_inb (b, E1SR);
			if (!(byte & SR_CMBUSY))
				break;
			if (i > 180)
				return b->dead = TAUPCI_DEAD_DXC | (6 << 8);
		}

		taupci_outb (b, E4TLOSR, 0xFF);
		taupci_outb (b, E4CLOSR, 0xFF);
	}

	return b->dead = 0;
}

static void taupci_set_e3cr0 (taupci_chan_t* c)
{
	u8 e3cr0 = E3CR0_BRDEN;

	if (c->type != TAUPCI_E3 && c->type != TAUPCI_T3 && c->type != TAUPCI_STS1)
		return;

	if (c->gsyn)
		e3cr0 |= E3CR0_SYNC;
	if (c->t3_long)
		e3cr0 |= E3CR0_LBO;
	if (c->ber)
		e3cr0 |= E3CR0_BER;
	else if (c->ais) {
		switch (c->type) {
			case TAUPCI_E3:
				e3cr0 |= E3CR0_AISE3;
				break;
			case TAUPCI_T3:
				e3cr0 |= E3CR0_AIST3;
				break;
			case TAUPCI_STS1:
			default:
				break;
		}
	}
	if (c->rloop)
		e3cr0 |= E3CR0_RLOOP;
	else if (c->lloop)
		e3cr0 |= E3CR0_LLOOP;
	if (c->monitor)
		e3cr0 |= E3CR0_MONEN;
	taupci_outb (c->board, E3CR0, e3cr0);
}

/*
* Initialize the channel structure.
*/
static void taupci_init_chan (taupci_chan_t* c)
{
	static const char altera_regs[4][8] = {
		{GMD0, GLS0, E1CS0, E1CR0, E4MD0, E4EPS0, E4CS0},
		{GMD1, GLS1, E1CS1, E1CR1, E4MD1, E4EPS1, E4CS1},
		{GMD2, GLS2, E1CS2, E1CR2, E4MD2, E4EPS2, E4CS2},
		{GMD3, GLS3, E1CS3, E1CR3, E4MD3, E4EPS3, E4CS3}
	};

	//c->E1EPS = 0;
	c->num = c - c->board->chan;
	c->qlen = TAUPCI_NBUF;
	//c->baud = 0;        /* external clock */
	//c->dpll = 0;        /* no dpll */
	//c->nrzi = 0;        /* nrz */
	//c->invtxc = 0;    /* no tx clock invertion */
	//c->invrxc = 0;    /* no rx clock invertion */
	//c->lloop = 0;      /* no local loopback */
	//c->rloop = 0;      /* no remote loopback */
	//c->gsyn = TAUPCI_GSYN_INT;  /* internal G.703 clock */
	//c->scrambler = 0;   /* no scrambler */
	c->ts = 0xFFFFFFFEul;   /* all timeslots exclude 0 */
	//c->higain = 0;    /* low E1 sensitivity */
	//c->cas = TAUPCI_CAS_OFF;
	//c->crc4 = 0;        /* no CRC4 */
	c->dir = c->num;
	//c->ccr = 0;
	//c->gmd = GMD_2048;
	//c->e1cr = 0;
	//c->ds21x54 = 0;
	//c->ais = 0;  /* no AIS */
	//c->ber = 0;  /* no BER */
	//c->t3_long = 0;  /* short cable */
	c->losais = 1;    /* send LOS on AIS */
	c->raw_mios = c->mios = TAUPCI_MTU;
	c->hdlc_crclen = 1; /* 16 bit */

	c->scc_regs = c->board->base + c->num * 0x80/4 + 0x100/4;
	c->core_regs1 = c->board->base + c->num * 4/4;
	c->core_regs2 = c->board->base + c->num * 12/4;

	switch (c->board->model) {
		default:
			c->type = TAUPCI_NONE;
			return;
		case TAUPCI_MODEL_LITE:
			if (c->num)
				return;
		case TAUPCI_MODEL_2RS:
			if (c->num > 1)
				return;
		case TAUPCI_MODEL_4RS:
			c->type = TAUPCI_SERIAL;
			break;
		case TAUPCI_MODEL_OE1:
			if (c->num > 1)
				return;
		case TAUPCI_MODEL_4E1:
			c->type = TAUPCI_E1;
			break;
		case TAUPCI_MODEL_2G703:
			if (c->num > 1)
				return;
		case TAUPCI_MODEL_4G703:
			c->type = TAUPCI_G703;
			break;
		case TAUPCI_MODEL_2E1_2RS:
		case TAUPCI_MODEL_OE1_2RS:
			c->type = (c->num < 2) ? TAUPCI_E1 : TAUPCI_SERIAL;
			break;
		case TAUPCI_MODEL_2E1_4:
			c->type = (c->num < 2) ? TAUPCI_E1 : TAUPCI_DATA;
			break;
		case TAUPCI_MODEL_2G703_2RS:
			c->type = (c->num < 2) ? TAUPCI_G703 : TAUPCI_SERIAL;
			break;
		case TAUPCI_MODEL_1E3:
			if (c->num)
				return;
			break;
		case TAUPCI_MODEL_1HSSI:
			if (c->num)
				return;
			c->type = TAUPCI_HSSI;
			break;
	}

	if (c->type <= TAUPCI_DATA)
		c->ts = 0;

	c->GMD = altera_regs[c->num][0];
	c->GLS = altera_regs[c->num][1];
	c->E1CS = altera_regs[c->num][2];
	c->E1CR = altera_regs[c->num][3];
	if (c->board->E1DATA == E4DAT) {
		/*
		* For models with loadable firmware.
		*/
		c->ds21x54 = 1; /* new E1 transceiver */
		c->GMD = altera_regs[c->num][4];
		c->E1EPS = altera_regs[c->num][5];
		c->E1CS = altera_regs[c->num][6];
	}
}

static void taupci_set_e1eps (taupci_chan_t* c)
{
	u8 e1eps;

	if (!c->E1EPS)
		return;

	/*
	* Set dir
	*/
	e1eps = c->dir;
	if (c->board->chan[c->dir].unframed)
		e1eps = c->num;

	if (!c->unframed && !c->phony)
		e1eps |= E1EPS_PTSM;

	taupci_outb (c->board, c->E1EPS, e1eps);
}

/*
* Perform the hardware reset.
* PCI configuration will be lost.
*/
void taupci_hard_reset (taupci_board_t* b)
{
	/*
	* Altera would delay reset for 970ns.
	* * This time should be enough to complete taupci_outb.
	* * After caller of this function will get control back, it should
	* * wait the reset to be commited by altera and all will get
	* * to normal state. 2us should be enough.
	*/
	taupci_outb (b, BCR, BCR_RST);
}

/*
* Stop HDLC channel.
*/
void taupci_stop_chan (taupci_chan_t* c)
{
	u32 cmdr = 0;
	u32 cfg = CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;

	ddk_assert (c->type != TAUPCI_NONE);
	if (!c->type)
		return;

	if (c->ccr2 & CCR2_RENAB) {
		cfg |= CFG_RDR;
		cmdr |= CMDR_RRES;
	}

	if (BTDA (c) != 0) {
#ifdef HOLDBIT_MODE
		unsigned i = 0; do {
			c->tdesc[i].len = DESC_FE | DESC_HOLD;
		} while (++i < TAUPCI_NBUF);
		ddk_flush_cpu_writecache ();
#endif
		cfg |= CFG_RDT;
		cmdr |= CMDR_XRES;
	}
	c->ccr2 = (c->ccr2 & ~CCR2_RENAB) | CCR2_XBRK;
	CCR2 (c) = c->ccr2;
	c->dma_imr = cfg & (CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR);
	taupci_wait_ready_for_action (c->board);
	CMDR (c) = cmdr;
	CFG (c) = cfg;
	taupci_submit_action (c->board);
	ddk_yield_dmabus ();
	c->dacs_ts = 0;
	SCC_IMR (c) = c->scc_imr = 0xFFFFFFFFul;

	BTDA (c) = 0;
}

void taupci_poweroff_chan (taupci_chan_t* c)
{
	if (c->type)
		CCR0 (c) = (c->ccr0 &= ~0x80000000ul);
}

/*
* Start HDLC channel.
*/
void taupci_start_chan (taupci_chan_t* c, taupci_buf_t * tx, u32 tx_dma, taupci_buf_t * rx, u32 rx_dma)
{
	int i;
	u32 cmdr = 0;
	u32 cfg;

	ddk_assert (c->type != TAUPCI_NONE);
	if (!c->type)
		return;

	/*
	* Manual RTS control, TxD push-pull output, /CD inverted,
	* * ignore /CTS and /CD. Transparent mode 0, no PPP,
	* * no shared flags, CRC-CCITT.
	*/
	c->ccr1 = (c->ccr1 & 0x00100100ul) | 0x02448000ul;

	if (c->board->model == TAUPCI_MODEL_1E3) {
		/*
		* High speed mode, clock mode 4
		*/
		c->ccr0 |= 0x000C;
		/*
		* RTS == TXCOUT
		*/
		c->ccr1 |= 0x00200000ul;
	}

	/* LY: set_phony() will update CCR1 & CCR2 by calling update_ccr1_ccr2(). */
	taupci_set_phony (c, c->phony);

	/*
	* Set receive length limit.
	*/
	RLCR (c) = 0x8000ul | (TAUPCI_BUFSZ / 32 - 1);

	/*
	* Set up the G.703/E1 controllers.
	*/
	if (c->type == TAUPCI_G703)
		taupci_setup_g703 (c);

	taupci_set_nrzi (c, c->nrzi);
	taupci_set_dir (c, c->dir);
	taupci_set_baud (c, c->baud);
	cfg = c->dma_imr;

	/*
	* Enable transmitter.
	*/
	if (tx) {
		i = 0; do {
			c->tbuf[i] = tx->bufs[i];
			c->tx_dma[i] = tx_dma + ((char *) c->tbuf[i] - (char *) tx);
			c->tdesc[i].len = DESC_FE | DESC_HOLD;
			c->tdesc[i].data = c->tx_dma[i];
			c->tdesc[i].status = 0;
			c->tdesc[i].fe = 0;
			c->tag[i] = 0;
		} while (++i < TAUPCI_NBUF);
		ddk_flush_cpu_writecache ();
		c->tn = 0;
		c->te = c->tn;
		BTDA (c) = 0;
#ifndef HOLDBITM_MODE
		LTDA (c) = c->td_dma[0];
#endif

		/*
		* LY: Clear transmit-break on SCC.
		*/
		c->ccr2 &= ~CCR2_XBRK;

		/*
		* Enable tx-underrun and all-sent interrupts.
		*/
#ifdef USE_ISR_ALLS
		c->scc_imr &= ~(ISR_XDU | ISR_ALLS);
#else
		c->scc_imr &= ~ISR_XDU;
#endif /* USE_ISR_ALLS */

		/*
		* Reset transmitter.
		*/
		cmdr |= CMDR_XRES;
		cfg &= ~(CFG_TFI | CFG_TERR);
	}

	if (c->rdesc)
		taucpi_update_rdesc (c);
	/*
	* Enable receiver.
	*/
	if (rx) {
		i = 0; do {
			c->rbuf[i] = rx->bufs[i];
			c->rx_dma[i] = rx_dma + ((char *) c->rbuf[i] - (char *) rx);
			c->rdesc[i].data = c->rx_dma[i];
			c->rdesc[i].status = 0;
		} while (++i < TAUPCI_NBUF);
		ddk_flush_cpu_writecache ();
		c->rn = 0;

		/*
		* Enable SCC receiver.
		*/
		c->ccr2 |= CCR2_RENAB;

		/*
		* Enable overrun interrupts.
		*/
		c->scc_imr &= ~(ISR_RFO | ISR_RDO);

		/*
		* Reset receiver.
		*/
		cmdr |= CMDR_RRES;

		/*
		* Clear G.703 errors.
		*/
		if (c->type == TAUPCI_G703)
			taupci_outb (c->board, c->GLS, 0xff);

		/*
		* Initialize dma receiver.
		*/
		BRDA (c) = c->rd_dma[0];
#ifndef HOLDBIT_MODE
		LRDA (c) = c->rd_dma[TAUPCI_NBUF - 1];
#endif
		cfg = (cfg & ~(CFG_RFI | CFG_RERR)) | CFG_IDR;
	}

	if (c->phony && !(cfg & CFG_TFI))
		cfg |= CFG_RFI; /* LY: disable rx-interrupt in phony-mode if tx-enabled */

	c->dma_imr = cfg & (CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR);
	if (c->baud > 0 || c->ts > 1 || c->type == TAUPCI_SERIAL) {
		ddk_kd_print (("cp%d.%d: start dir=%d, baud=%ul, ts=0x%08lx, type=%d\n",
			c->board->num, c->num,
			c->dir, c->baud, c->ts, c->type));
		taupci_wait_ready_for_action (c->board);
		CMDR (c) = cmdr;
		//taupci_stall_1us (c->board);
		if (!c->board->irq_enabled)
			cfg |= CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
		CFG (c) = cfg;
		taupci_submit_action (c->board);
	} else
		ddk_kd_print (("cp%d.%d: skip start to avoid stall\n",
			c->board->num, c->num));
	taupci_update_ccr1_ccr2 (c);
	if (c->board->irq_enabled)
		SCC_IMR (c) = c->scc_imr;

	/*
	* Power up.
	*/
	c->ccr0 |= 0x80000000ul;
	CCR0 (c) = c->ccr0;

	taupci_phony_stub_tx (c);
}

/*
* Stop E1 transceiver.
*/
void taupci_stop_e1 (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->type != TAUPCI_E1 || c->ds_ien == 0)
		return;

	c->ds_ien = 0;
	/*
	* Disable E1 controller interrupts.
	*/
	taupci_dallas_out (c, DS_IMR2, 0);
}

/*
* Start E1 transceiver.
*/
void taupci_start_e1 (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->type != TAUPCI_E1 || c->ds_ien)
		return;

	c->ds_ien = 1;
	/*
	* Enable E1 controller interrupts.
	*/
	taupci_dallas_out (c, DS_IMR2, SR2_SEC);
}

/*
* Control DTR signal for the channel.
*/
void taupci_set_dtr (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);

	c->dtr = (on != 0);
	c->ccr &= ~BCCR_DTR;
	if (c->dtr)
		c->ccr |= BCCR_DTR;
	taupci_outb (c->board, BCCR0 + c->num, c->ccr);
}

/*
* Control RTS signal for the channel.
*/
void taupci_set_rts (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);

	c->rts = (on != 0);
	c->ccr1 &= ~0x00100000ul;
	if (c->rts)
		c->ccr1 |= 0x00100000ul;
	CCR1 (c) = c->ccr1;
}

/*
* Get the state of DSR signal of the channel.
*/
int taupci_get_dsr (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->type != TAUPCI_SERIAL)
		return 0;
	return (taupci_inb (c->board, BCCR0 + c->num) & BCSR_DSR) != 0;
}

/*
* Get the state of CARRIER signal of the channel.
*/
int taupci_get_cd (taupci_chan_t* c)
{
	u32 star;
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->board->model == TAUPCI_MODEL_1E3)
		return (taupci_inb (c->board, E3SR0) & E3SR0_LOS) == 0;
	if (c->type == TAUPCI_DATA)
		c = c->board->chan + c->dir;
	/*
	* We should read STAR twice.
	*/
	star = STAR (c);
	star = STAR (c);
	return (star & 0x00200000ul) == 0;
}

/*
* Get the state of CTS signal of the channel.
*/
int taupci_get_cts (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type != TAUPCI_SERIAL)
		return 0;
	return (taupci_inb (c->board, BCCR0 + c->num) & BCSR_CTS) != 0;
}

/*
* Check the transmit clock.
*/
int taupci_get_txcerr (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type != TAUPCI_SERIAL)
		return 0;
	return (taupci_inb (c->board, BCCR0 + c->num) & BCSR_TXCERR) != 0;
}

/*
* Check the receive clock.
*/
int taupci_get_rxcerr (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type != TAUPCI_SERIAL)
		return 0;
	return (taupci_inb (c->board, BCCR0 + c->num) & BCSR_RXCERR) != 0;
}

void taupci_set_baud (taupci_chan_t* c, int baud)
{
	int n, m, r;
	ddk_assert (c->type != TAUPCI_NONE);

	/*
	* Not valid for E1 channels.
	*/
	if (baud < 0)
		baud = 0;
	if (c->type == TAUPCI_G703 || (c->type == TAUPCI_E1 && c->unframed)) {
		int kbps = (baud + 500) / 1000;

		if (c->phony)
			kbps = 2048;
		c->gmd &= ~GMD_RATE_MASK;
		if (!kbps || kbps > 1024) {
			c->gmd |= GMD_2048;
			baud = 2048000;
		} else if (kbps > 512) {
			c->gmd |= GMD_1024;
			baud = 1024000;
		} else if (kbps > 256) {
			c->gmd |= GMD_512;
			baud = 512000;
		} else if (kbps > 128) {
			c->gmd |= GMD_256;
			baud = 256000;
		} else if (kbps > 64) {
			c->gmd |= GMD_128;
			baud = 128000;
		} else {
			c->gmd |= GMD_64;
			baud = 64000;
		}
		taupci_outb (c->board, c->GMD, c->gmd);
	} else if ((c->type == TAUPCI_E1 || c->type == TAUPCI_DATA) && !c->phony)
		baud = taupci_ts_count (c) * 64000;
	else if (c->type == TAUPCI_E1 && c->phony)
		baud = 2048000;

	c->ccr0 &= ~0x0037;
	if (c->type == TAUPCI_E3 || c->type == TAUPCI_T3 || c->type == TAUPCI_STS1) {
		baud = c->board->osc;
		c->ccr0 |= 0x000C;
		m = n = 0;
	} else {
		if ((unsigned) baud > c->board->osc)
			baud = c->board->osc;
		r = baud;
		if (! r || c->type != TAUPCI_SERIAL) {
			/*
			* Mode 0a: external tx/rx clock.
			*/
			c->dpll = 0;
			if (c->type == TAUPCI_SERIAL)
				c->lloop = 0;
			m = n = 0;
			r = 0;
		} else if (c->dpll) {
			/*
			* Mode 6b: txc - internal, rxc - DPLL .
			*/
			c->ccr0 |= 0x0036;
			baud = r = taupci_compute_clock (c->board->osc / 16, r, &m, &n);
		} else if (c->lloop) {
			/*
			* Mode 7b: internal tx/rx clock.
			*/
			c->ccr0 |= 0x0037;
			baud = r = taupci_compute_clock (c->board->osc, r, &m, &n);
		} else {
			/*
			* Mode 0b: txc - internal, rxc - external.
			*/
			c->ccr0 |= 0x0030;
			baud = r = taupci_compute_clock (c->board->osc, r, &m, &n);
		}
		c->ccr |= BCCR_TDIR;
		if (r > 0)
			c->ccr &= ~BCCR_TDIR;
	}
	c->baud = baud;
	/* Avoid conflict on the clock bus. */
	taupci_outb (c->board, BCCR0 + c->num, c->ccr & ~BCCR_TDIR);
	CCR0 (c) = c->ccr0;
	BRR (c) = (m << 8) | n;
	taupci_outb (c->board, BCCR0 + c->num, c->ccr);

	c->ccr1 &= ~0x0100ul;
	if (c->type == TAUPCI_SERIAL && c->lloop)
		c->ccr1 |= 0x0100ul;
	CCR1 (c) = c->ccr1;
}

void taupci_set_hdlcmode (taupci_chan_t* c, int bits, int share)
{
	ddk_assert (c->type != TAUPCI_NONE);
	switch (bits) {
		default:
			return;
		case 16:
		case 32:
		case 0:
			c->hdlc_crclen = bits >> 4;
			break;
	}

	c->hdlc_shareflags = 0;
	if (share)
		c->hdlc_shareflags = 1;

	taupci_update_ccr1_ccr2 (c);
}

void taupci_set_dpll (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Valid only for HDLC channels.
	*/
	if (c->type != TAUPCI_SERIAL)
		return;
	c->dpll = on && (c->baud != 0);
	taupci_set_baud (c, c->baud);
}

static void taupci_set_tcr1 (taupci_chan_t* c)
{
	u8 tcr1 = 0;

	if (c->unframed)
		tcr1 |= TCR1_TSO | TCR1_TFPT;
	else {
		if (c->board->mux && c->E1EPS)
			tcr1 |= TCR1_TSI;
		else
			tcr1 |= TCR1_TSO | TCR1_TSIS;
		if (c->cas == TAUPCI_CAS_SET)
			tcr1 |= TCR1_T16S;
	}
	taupci_dallas_out (c, DS_TCR1, tcr1);
}

static void taupci_set_tcr2 (taupci_chan_t* c)
{
	u8 tcr2 = 0;

	if (!c->unframed && c->crc4)
		tcr2 |= TCR2_AEBE;
	if (!c->ds21x54 && (c->phony || c->lloop))
		tcr2 |= TCR2_P16F; // LY: disable RLOS pin (swith it to indicate TCLK-loss).

	taupci_dallas_out (c, DS_TCR2, tcr2);
}

static void taupci_set_ccr1 (taupci_chan_t* c)
{
	u8 ccr1 = CCR1_THDB3 | CCR1_RHDB3;

	if (!c->unframed) {
		if (c->crc4)
			ccr1 |= CCR1_RCRC4 | CCR1_TCRC4;
		if (c->cas == TAUPCI_CAS_OFF)
			ccr1 |= CCR1_CCS;
	}
	taupci_dallas_out (c, DS_CCR1, ccr1);
}

static void taupci_set_ccr2 (taupci_chan_t* c)
{
	u8 ccr2 = CCR2_CNTCV;

	if (!c->unframed)
		ccr2 |= CCR2_LOFA1 | CCR2_AUTORA;

	if (c->lloop && !c->ds21x54)
		ccr2 |= CCR2_LLOOP;

	taupci_dallas_out (c, DS_CCR2, ccr2);
}

static void taupci_set_ccr3 (taupci_chan_t* c)
{
	u8 ccr3 = CCR3_TSCLKM;

	if (c->board->mux && c->cas == TAUPCI_CAS_CROSS)
		/*
		* Enable hardware insertion of transmit signaling.
		* * TCBRs define which signaling bits to be inserted.
		*/
		ccr3 |= CCR3_THSE | CCR3_TCBFS;
	taupci_dallas_out (c, DS_CCR3, ccr3);
}

static void taupci_set_rcr1 (taupci_chan_t* c)
{
	u8 rcr1 = 0; /* Dual mode - RSYNC is output */

	if (c->unframed)
		rcr1 = RCR1_SYNCD;  /* Unframed */
	else if (c->board->mux)
		rcr1 = RCR1_RSI;    /* Mux - RSYNC is input */

	taupci_dallas_out (c, DS_RCR1, rcr1);
}

static void taupci_set_rcr2 (taupci_chan_t* c)
{
	u8 rcr2 = RCR2_RSCLKM;
	/*
	* Always enable elastic store on new models.
	*/
	if (c->board->mux || c->ds21x54)
		rcr2 = RCR2_RSCLKM | RCR2_RESE;

	taupci_dallas_out (c, DS_RCR2, rcr2);
}

static void taupci_set_licr (taupci_chan_t* c)
{
	u8 licr = 0;

	if (c->higain)
		licr |= LICR_HIGAIN;

	if (c->lloop)
		/*
		* Enable jitter attenuator at the transmit side.
		*/
		licr |= LICR_JA_TX;

	// licr |= LICR_LB75P; // LY: LICR_LB75P == 0
	if ((taupci_inb (c->board, E1SR) >> c->num) & E1SR_TP0)
		licr |= LICR_LB120P;
	taupci_dallas_out (c, DS_LICR, licr);
}

static void taupci_set_tir (taupci_chan_t* c)
{
	taupci_board_t *b = c->board;
	taupci_chan_t *x;
	u32 tir;

	if (c->unframed)
		tir = 0;
	else if (c->E1EPS) {
		if (b->mux)
			tir = 0;
		else {
			tir = 0xFFFFFFFFul;

			/*
			* Open data timeslots.
			*/
			x = b->chan; do {
				if ((x->type == TAUPCI_E1 || x->type == TAUPCI_DATA) && x->dir == c->num)
					tir &= ~x->ts;
			} while (++x < b->chan + TAUPCI_NCHAN);
		}
	} else if (b->mux) {
		/*
		* Old Tau/E1 adapter in mux mode:
		* * both data channels are directed to link0,
		* * and the remaining timeslots are translated
		* * between link0 and link1.
		*/
		tir = c->num ? (b->chan[0].ts | b->chan[1].ts) : 0;
	} else
		tir = ~c->ts;

	taupci_dallas_out (c, DS_TIR, (u8) (tir & ~1u));
	taupci_dallas_out (c, DS_TIR + 1, (u8) (tir >> 8));
	taupci_dallas_out (c, DS_TIR + 2, (u8) (tir >> 16));
	taupci_dallas_out (c, DS_TIR + 3, (u8) (tir >> 24));
}

/*
* Set up monitoring mode.
* Use receiver gain 30 dB.
*/
void taupci_set_monitor (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if ((c->type != TAUPCI_E1 || !c->ds21x54)
		&& (c->type == TAUPCI_NONE || c->board->model != TAUPCI_MODEL_1E3))
		return;
	c->monitor = (on != 0);

	if (c->board->model == TAUPCI_MODEL_1E3)
		/*
		* 20 dB
		*/
		taupci_set_e3cr0 (c);
	else
		/*
		* 0x70 = 30 dB, 0x72 = 12 dB
		*/
		taupci_dallas_out (c, DS_TEST3, on ? 0x70 : 0);

	taupci_kick_setup (c);
}

/*
* Set up direction for E1 channels.
* New models 2E1 and 4E1 support flexible selection
* of E1 link for every data channel.
*/
void taupci_set_dir (taupci_chan_t* c, int dir)
{
	taupci_board_t *b = c->board;
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->type != TAUPCI_E1 && c->type != TAUPCI_DATA) {
		/*
		* Dirs are fixed for digital ports.
		* * (Delta on Tau-PCI-E1)
		*/
		c->dir = c->num;
		taupci_set_e1eps (c);
		return;
	}

	dir &= 3;
	if ((c->board->chan + 2)->type != TAUPCI_E1)
		dir &= 1;

	if (c->unframed)
		dir = c->num;

	if (!c->E1EPS)
		dir = (c->num == 1) ? (b->mux ? 0 : 1) : c->num;

	c->dir = dir;

	if (c->E1EPS)
		taupci_set_e1eps (c);

	taupci_set_ts (c, c->ts);
}

/*
* Turn G.704 framing on/off
*/
void taupci_set_unfram (taupci_chan_t* c, int on)
{
	taupci_chan_t *x;
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->type != TAUPCI_E1)
		return;
	if (!c->E1EPS)
		on = 0;
	c->unframed = on != 0;
	c->e1cr &= ~E1CR_GRAW;
	if (c->unframed)
		c->e1cr |= E1CR_GRAW;
	taupci_outb (c->board, c->E1CR, c->e1cr);

	if (c->unframed) {
		taupci_set_dir (c, c->num);
		x = c->board->chan; do {
			if ((x->type == TAUPCI_E1 || x->type == TAUPCI_DATA) && x->dir == c->num && x != c)
				taupci_set_dir (x, x->num);
		} while (++x < c->board->chan + TAUPCI_NCHAN);
	} else {
		taupci_set_dir (c, c->dir);
		x = c->board->chan; do {
			if (x != c && c->type != TAUPCI_NONE)
				taupci_set_e1eps (x);
		} while (++x < c->board->chan + TAUPCI_NCHAN);
	}

	taupci_set_tcr1 (c);
	taupci_set_tcr2 (c);
	taupci_set_ccr1 (c);
	taupci_set_ccr2 (c);
	taupci_set_ccr3 (c);
	taupci_set_rcr1 (c);

	if (c->unframed)
		taupci_set_gsyn (c, c->gsyn);
	taupci_kick_setup (c);
}

/*
* Turn local loopback on/off.
*/
void taupci_set_lloop (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Local loopback requires internal clock (not for E1/G703).
	*/
	if ((c->type == TAUPCI_SERIAL && c->baud == 0) || c->type == TAUPCI_DATA)
		return;
	c->lloop = (on != 0);
	if (c->type == TAUPCI_E1) {
		c->e1cr &= ~E1CR_LLOOP;
		if (c->lloop || c->phony)
			c->e1cr |= E1CR_LLOOP;
		taupci_outb (c->board, c->E1CR, c->e1cr);
		if (c->ds21x54)
			taupci_dallas_out (c, DS_CCR4, c->lloop ? CCR4_LLB : 0);
		taupci_set_ccr2 (c);
		taupci_set_licr (c);

	} else if (c->type == TAUPCI_G703) {
		/*
		* Hdb3 encoding, no interrupts.
		*/
		u8 lx = LX_LOS | LX_HDB3;

		if (c->lloop)
			/*
			* Error in CS61318: cannot use TAOS with LLOOP.
			*/
			lx |= LX_LLOOP;
		taupci_lxt318_out (c, LX_CCR1, lx);
	} else if (c->board->model == TAUPCI_MODEL_1E3) {
		taupci_set_e3cr0 (c);
	} else
		taupci_set_baud (c, c->baud);
	taupci_kick_setup (c);
}

void taupci_set_nrzi (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Valid only for HDLC channels.
	*/
	if (c->type != TAUPCI_SERIAL)
		return;
	c->nrzi = (on != 0);

	c->ccr0 &= ~0x00200000ul;
	if (c->nrzi)
		c->ccr0 |= 0x00200000ul;
	CCR0 (c) = c->ccr0;
}

void taupci_set_invtxc (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Valid only for HDLC channels.
	*/
	if (c->type != TAUPCI_SERIAL)
		return;
	c->invtxc = (on != 0);
	c->ccr &= ~BCCR_TINV;
	if (c->invtxc)
		c->ccr |= BCCR_TINV;
	taupci_outb (c->board, BCCR0 + c->num, c->ccr);
}

void taupci_set_invrxc (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Valid only for HDLC channels.
	*/
	if (c->type != TAUPCI_SERIAL)
		return;
	c->invrxc = (on != 0);
	c->ccr &= ~BCCR_RINV;
	if (c->invrxc)
		c->ccr |= BCCR_RINV;
	taupci_outb (c->board, BCCR0 + c->num, c->ccr);
}

/*
* Query the remote loopback state.
*/
int taupci_get_rloop (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Auto remote loopback mode is valid only for TauPCI/G703.
	*/
	if (c->type == TAUPCI_G703)
		return (taupci_inb (c->board, c->GLS) & GLS_LREQ) != 0;
	if (c->type != TAUPCI_NONE && c->board->model == TAUPCI_MODEL_1E3)
		return c->rloop;
	return 0;
}

/*
* Query the cable type.
*/
int taupci_get_cable (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_SERIAL)
		switch (taupci_inb (c->board, BCCR0 + c->num) & BCSR_IFT_MASK) {
			case BCSR_IFT_X21:
				return TAUPCI_CABLE_X21;
			case BCSR_IFT_RS530:
				return TAUPCI_CABLE_RS530;
			case BCSR_IFT_V35:
				return TAUPCI_CABLE_V35;
			case BCSR_IFT_RS232:
				return TAUPCI_CABLE_RS232;
			case BCSR_IFT_RS485:
				return TAUPCI_CABLE_RS485;
			default:
				return TAUPCI_CABLE_NOT_ATTACHED;
	} else if (c->type == TAUPCI_E1)
		return (taupci_inb (c->board, E1SR) >> c->num & E1SR_TP0) ? TAUPCI_CABLE_TP : TAUPCI_CABLE_COAX;
	else if (c->type == TAUPCI_E3)
		return TAUPCI_CABLE_COAX;
	else
		return 0;
}

void taupci_set_rloop (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_NONE || c->board->model != TAUPCI_MODEL_1E3)
		return;

	c->rloop = (on != 0);
	taupci_set_e3cr0 (c);
}

void taupci_set_ber (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_NONE || c->board->model != TAUPCI_MODEL_1E3)
		return;

	c->ber = (on != 0);
	taupci_set_e3cr0 (c);
}

void taupci_set_losais (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_NONE || c->board->model != TAUPCI_MODEL_1E3)
		return;

	c->losais = (on != 0);
	if (c->losais == 0 && c->ais)
		c->ais = 0;

	taupci_set_e3cr0 (c);
}

void taupci_set_t3_long (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_NONE || c->type == TAUPCI_E3 || c->board->model != TAUPCI_MODEL_1E3)
		return;

	c->t3_long = (on != 0);
	taupci_set_e3cr0 (c);
	taupci_kick_setup (c);
}

/*
* Turn LED on/off.
*/
void taupci_led (taupci_board_t* b, int on)
{
	b->bcr &= ~BCR_LED;
	if (on)
		b->bcr |= BCR_LED;
	taupci_outb (b, BCR, b->bcr);
}

/*
* Get the G.703 line signal level.
*/
int taupci_get_lq (taupci_chan_t* c)
{
	static const unsigned lq_to_santibells[] = {0, 95, 195, 285};
	u8 q1, q2, q3;
	int i;

	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type != TAUPCI_G703)
		return 0;
	q1 = taupci_inb (c->board, c->GLS) & GLS_DBMASK;

	/*
	* Repeat reading the register to produce a 10-usec delay.
	*/
	q2 = taupci_inb (c->board, c->GLS) & GLS_DBMASK;
	for (i = 1; i < 20; ++i)
		q2 = taupci_inb (c->board, c->GLS) & GLS_DBMASK;

	q3 = taupci_inb (c->board, c->GLS) & GLS_DBMASK;
	for (i = 1; i < 20; ++i)
		q3 = taupci_inb (c->board, c->GLS) & GLS_DBMASK;
	if (q1 <= q2 && q2 <= q3)
		return lq_to_santibells[q2 >> 2];
	if (q2 <= q3 && q3 <= q1)
		return lq_to_santibells[q3 >> 2];
	if (q3 <= q1 && q1 <= q2)
		return lq_to_santibells[q1 >> 2];
	if (q1 <= q3 && q3 <= q2)
		return lq_to_santibells[q3 >> 2];
	if (q3 <= q2 && q2 <= q1)
		return lq_to_santibells[q2 >> 2];
	/*
	* if (q2 <= q1 && q1 <= q3)
	*/ return lq_to_santibells[q1 >> 2];
}

static u32 taupci_compute_clock (u32 hz, u32 baud, int *m, int *n)
{
	for (*m = 0;; ++*m) {
		*n = (((2 * hz / baud) >> *m) + 1) / 2 - 1;
		if (*n < 0 || *m > 15) {
			*m = 0;
			*n = 0;
			return 0;
		}
		if (*n <= 63)
			return hz / ((*n + 1) << *m);
	}
}

/*
* Set up the E3 controller.
*/
static void taupci_setup_e3 (taupci_chan_t* c)
{
	taupci_set_e3cr0 (c);
	/*
	* Set error level for E3 like models
	* ~1-2 errors per packet 500 bytes length
	*/
	if (c->type == TAUPCI_E3 || c->type == TAUPCI_T3 || c->type == TAUPCI_STS1) {
		taupci_outb (c->board, E3ELR0, (2048 >> 8) & 0xff);
		taupci_outb (c->board, E3ELR1, (2048 >> 16) & 0xff);
	}
}

/*
* Set up the G.703 controller.
*/
static void taupci_setup_g703 (taupci_chan_t* c)
{
	/*
	* Set up the Tau/G.703 adapter.
	*/
	if (c->type != TAUPCI_G703)
		return;
	taupci_set_gsyn (c, c->gsyn);
	taupci_set_scrambler (c, c->scrambler);

	/*
	* Reset the G.703 transceiver.
	*/
	taupci_lxt318_out (c, LX_CCR1, LX_RESET);
	taupci_lxt318_out (c, LX_CCR1, LX_LOS | LX_HDB3);
	taupci_lxt318_out (c, LX_CCR2, LX_CCR2_LH); /* setup Long Haul mode */
	taupci_lxt318_out (c, LX_CCR3, LX_CCR3_E1_LH);  /* setup Long Haul mode */
	taupci_set_lloop (c, c->lloop); /* to set up LX */
}

/*
* Set up the E1 controller.
*/
static void taupci_setup_e1 (taupci_chan_t* c)
{
	int i;

	/*
	* Stop E1 clock.
	*/
	c->e1cr &= ~E1CR_GRUN;
	taupci_outb (c->board, c->E1CR, c->e1cr);

	i = 0; do {
		taupci_dallas_out (c, i & 255, 0);  /* LY: double clear all DS registers */
	} while (++i < 512);

	c->ds_ien = 0;    /* cause DS_IMR2 = 0 */

	taupci_set_gsyn (c, c->gsyn);
	taupci_set_unfram (c, c->unframed);
	taupci_set_phony (c, c->phony);
	taupci_set_lloop (c, c->lloop);
	taupci_set_monitor (c, c->monitor);

	taupci_dallas_out (c, DS_TAF, 0x9b);    /* transmit align frame */
	taupci_dallas_out (c, DS_TNAF, 0xdf);   /* transmit non-align frame */
	taupci_dallas_out (c, DS_TIDR, 0xff);   /* transmit idle definition */

	if (c->ds21x54) {
		/*
		* transmit idle definition
		*/
		taupci_outb (c->board, E4TLOSR, 0xff);
	}

	taupci_dallas_out (c, DS_TS, 0x0b); /* transmit signaling 0 */
	i = 1; do {
		/*
		* transmit signaling 1..15
		*/
		taupci_dallas_out (c, (u8) (DS_TS + i), 0xff);
	} while (++i < 16);

	/* LY: PLL reset, DS21554 errata. */
	if (c->ds21x54) {
		taupci_dallas_out (c, DS_TEST3, 1);
		taupci_stall_1ms (c->board);
		taupci_dallas_out (c, DS_TEST3, 0);
	}

	/*
	* Reset the line interface.
	*/
	if (c->ds21x54) {
		taupci_dallas_out (c, DS_CCR5, CCR5_LIRST);
		taupci_dallas_out (c, DS_CCR5, 0);
	} else {
		taupci_dallas_out (c, DS_CCR3, CCR3_TSCLKM | CCR3_LIRESET);
		taupci_dallas_out (c, DS_CCR3, CCR3_TSCLKM);
	}

	/*
	* Reset the elastic store.
	*/
	if (c->ds21x54) {
		taupci_dallas_out (c, DS_CCR6, CCR6_RESR | CCR6_TESR);
		taupci_dallas_out (c, DS_CCR6, 0);
	} else {
		taupci_dallas_out (c, DS_CCR3, CCR3_TSCLKM | CCR3_ESRESET);
		taupci_dallas_out (c, DS_CCR3, CCR3_TSCLKM);
	}

	/*
	* Clear status registers.
	*/
	taupci_dallas_status (c, DS_SR1);
	taupci_dallas_status (c, DS_SR2);
	taupci_dallas_status (c, DS_RIR);

	/*
	* Start E1 clock (GRUN).
	*/
	c->e1cr |= E1CR_GRUN;
	taupci_outb (c->board, c->E1CR, c->e1cr);
}

void taupci_set_mux (taupci_board_t* b, int on)
{
	taupci_chan_t *c;
	int turn_on;

	/*
	* Valid only for TauPCI-E1.
	*/
	if ((b->E1DATA != E4DAT || b->model < TAUPCI_MODEL_2E1_4) && b->model != TAUPCI_MODEL_OE1)
		return;

	turn_on = (on && !b->mux);
	b->mux = on != 0;

	b->e1cfg &= ~CFG_MUX;
	if (b->mux)
		b->e1cfg |= CFG_MUX;

	taupci_outb (b, E1CFG, b->e1cfg);

	/*
	* Change the direction of chan#1
	* * on old Tau-E1 models.
	*/
	if (!b->chan->E1EPS)
		taupci_set_dir (b->chan + 1, b->chan[1].dir);

	c = b->chan; do {
		if (c->type == TAUPCI_E1) {
			taupci_set_tcr1 (c);
			taupci_set_rcr1 (c);
			taupci_set_rcr2 (c);
			taupci_set_ccr3 (c);
			taupci_set_ts (c, c->ts);
			// taupci_set_tir (c); LY: it was be done by taupci_set_ts()
		} else if (c->type == TAUPCI_DATA)
			taupci_set_ts (c, c->ts);
	} while (++c < b->chan + TAUPCI_NCHAN);

	if (b->mux)
		taupci_set_gsyn (b->chan, b->chan->gsyn);
}

static void taupci_init_dxc (taupci_board_t* b)
{
	int i, width = (b->model == TAUPCI_MODEL_4E1) ? 128 : 64;

	taupci_outb (b, E4CMAR, 0);
	i = 0; do {
		b->dxc.flip[0][i] = i ^ 32;
		b->dxc.flip[1][i] = i ^ 32;
		taupci_outb (b, E4CMEM, i);
	} while (++i < width);

	taupci_outb (b, E4CMAR, 128);
	i = 0; do {
		b->dxc.flip[0][128 + i] = i ^ 32;
		b->dxc.flip[1][128 + i] = i ^ 32;
		taupci_outb (b, E4CMEM, i);
	} while (++i < width);

	b->dxc.active = b->dxc.flip[0];
	b->dxc.shadow = b->dxc.flip[1];
	taupci_outb (b, E1CFG, b->e1cfg | CFG_CMSWAP);
}

static int taupci_dxc_build_compare (int base, int width, u8 * user, u8 * active,
				     u8 * pending)
{
	int r = 0;
	int i = base;
	int end = base + width;

	do {
		u8 t = user[i];
		if (t >= width)
			t = 0xFF;
		if (r || t != active[i]) {
			pending[i] = t;
			if (unlikely (r == 0)) {
				int j;
				for (j = 0; j < i; j++)
					pending[j] = active[j];
				r = 1;
			}
		}
	} while (++i < end);
	return r;
}

static int taupci_dxc_compare (taupci_board_t* b)
{
	int i = 0;
	do {
		if (i == 64 && b->model != TAUPCI_MODEL_4E1)
			i = 128;
		if (((unsigned *) b->dxc.pending)[i] != ((unsigned *) b->dxc.active)[i])
			return true;
	} while (++i < 256 / sizeof (unsigned));
	return false;
}

static void taupci_dxc_update (taupci_board_t* b)
{
	u8 *swap;
	int last = -1, i = 0;

	do {
		if (i == 64 && b->model != TAUPCI_MODEL_4E1)
			i = 128;
		if (b->dxc.pending[i] != b->dxc.shadow[i]) {
			if (last != i)
				taupci_outb (b, E4CMAR, i);
			taupci_outb (b, E4CMEM, b->dxc.shadow[i] = b->dxc.pending[i]);
			last = i + 1;
		}
	} while (++i < 256);
	taupci_outb (b, E1CFG, b->e1cfg | CFG_CMSWAP);
	swap = b->dxc.shadow;
	b->dxc.shadow = b->dxc.active;
	b->dxc.active = swap;
	b->dxc_status = TAUPCI_CROSS_PENDING;
}

static void taupci_set_dxc (taupci_board_t* b, u8 * user, int offset)
{
	if (b->E1DATA != E4DAT || b->model < TAUPCI_MODEL_2E1_4)
		return;

	if (taupci_dxc_build_compare (offset, (b->model == TAUPCI_MODEL_4E1) ? 128 : 64, user, b->dxc.active, b->dxc.pending)) {
		if (taupci_inb (b, E1SR) & SR_CMBUSY) {
			/*
			* LY: some updates are loaded into and now pending on the chip,
			* we must wait for done-pending for safe update cross-matrix.
			*/
			b->dxc_status |= TAUPCI_CROSS_WAITING;
		} else {
			/*
			* LY: no any updates are pending on chip,
			* we can safe upload it now.
			*/
			taupci_dxc_update (b);
		}
	} else if ((b->dxc_status & TAUPCI_CROSS_WAITING) && !taupci_dxc_compare (b)) {
		/*
		* LY: no any differences from currently active cross-matrix,
		* we need cancel any waiting updates.
		*/
		b->dxc_status &= ~TAUPCI_CROSS_WAITING;
	}
}

void taupci_set_dxc_ts (taupci_board_t* b, u8 * user)
{
	taupci_set_dxc (b, user, 0);
}

void taupci_set_dxc_cas (taupci_board_t* b, u8 * user)
{
	taupci_set_dxc (b, user - 128, 128);
}

static void taupci_wait_tsmem_ready (taupci_board_t* b)
{
	while (taupci_inb (b, BSR) & BSR_MBUSY)
		ddk_yield_dmabus ();
}

static void taupci_tsmem_put_single (taupci_chan_t* c, int ts, int bit)
{
	int index = ((ts - 1) & 31) + (c->num << 6);

	taupci_wait_tsmem_ready (c->board);
	taupci_outb (c->board, E4TSAR, index);

	taupci_wait_tsmem_ready (c->board);
	taupci_outb (c->board, E4TSMEM, bit);

	taupci_wait_tsmem_ready (c->board);
	taupci_outb (c->board, E4TSAR, index + 32);

	taupci_wait_tsmem_ready (c->board);
	taupci_outb (c->board, E4TSMEM, bit);
}

/*
* Set up rx/tx timeslots.
*/
void taupci_set_ts (taupci_chan_t* c, u32 ts)
{
	taupci_board_t *b = c->board;
	taupci_chan_t *x;
	int i;
	ddk_assert (c->type != TAUPCI_NONE);

	/*
	* Valid only for TauPCI-E1.
	*/
	if (c->type != TAUPCI_E1 && c->type != TAUPCI_DATA)
		return;

	if (b->chan[c->dir].unframed || b->chan[c->dir].type != TAUPCI_E1)
		ts = 0;

	/*
	Skip timeslot 0.
	*/
	if (!c->unframed) {
		ts &= ~1ul;

		/*
		* Skip timeslot 16 in CAS mode.
		*/
		if (b->chan[c->dir].cas == TAUPCI_CAS_SET)
			ts &= ~(1ul << 16);
	}

	/*
	* Skip timeslots, used by other channels.
	*/
	x = b->chan; do {
		if ((x->type == TAUPCI_E1 || x->type == TAUPCI_DATA) && x != c && x->dir == c->dir)
			ts &= ~x->ts;
	} while (++x < b->chan + TAUPCI_NCHAN);

	if (c->phony && !c->ds21x54 && taupci_hamming (ts) == 1)
		ts = 0;
	if (c->ts != ts) {
		c->ts = ts;
		taupci_update_ccr1_ccr2 (c);
	}

	/*
	* Block clocks if we have zero timeslots
	*/
	c->ccr &= ~BCCR_ASYNC;
	if ((ts & 0xFFFE) == 0 && c->unframed)
		c->ccr |= BCCR_ASYNC;
	taupci_outb (c->board, BCCR0 + c->num, c->ccr);

	if (c->ds21x54) {
		if (c->unframed && c->dir == c->num)
			ts = 0xFFFFFFFFul;
		else if (b->mux)
			ts &= ~c->dacs_ts;

		taupci_wait_tsmem_ready (c->board);
		taupci_outb (c->board, E4TSAR, c->num << 6);
		/*
		* Write timeslot mask
		*/
		i = 64; do {
			/*
			* 32 x 2 - Transmit and Receive the timeslot's bit-flag.
			*/
			taupci_wait_tsmem_ready (c->board);
			ts = (ts >> 1) | (ts << 31);
			taupci_outb (b, E4TSMEM, ts & 1);
		} while (--i);
	} else if (c->type == TAUPCI_E1) {
		/*
		* old Tau-PCI/E1.
		* Each channel uses the same timeslots for receive and transmit,
		* i.e. RCBRi == TCBRi.
		*/
		taupci_dallas_out (c, DS_TCBR, (u8) ts);
		taupci_dallas_out (c, DS_RCBR, (u8) ts);
		ts >>= 8;
		taupci_dallas_out (c, DS_TCBR + 1, (u8) ts);
		taupci_dallas_out (c, DS_RCBR + 1, (u8) ts);
		ts >>= 8;
		taupci_dallas_out (c, DS_TCBR + 2, (u8) ts);
		taupci_dallas_out (c, DS_RCBR + 2, (u8) ts);
		ts >>= 8;
		taupci_dallas_out (c, DS_TCBR + 3, ts);
		taupci_dallas_out (c, DS_RCBR + 3, ts);
	}

	taupci_set_tir (b->chan);
	if (!c->E1EPS)
		taupci_set_tir (b->chan + 1);

	taupci_set_baud (c, c->baud);
	if (c->phony)
		taupci_check_phony_mru (c);
}

void taupci_set_higain (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Valid only for TauPCI-E1.
	*/
	if (c->type != TAUPCI_E1)
		return;
	c->higain = (on != 0);
	taupci_set_licr (c);
	taupci_kick_setup (c);
}

void taupci_set_cas (taupci_chan_t* c, int mode)
{
	taupci_chan_t *x;
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->type != TAUPCI_E1 || mode < TAUPCI_CAS_OFF || mode > TAUPCI_CAS_CROSS)
		return;

	c->cas = mode;
	taupci_set_ccr1 (c);
	taupci_set_tcr1 (c);
	taupci_set_ccr3 (c);
	x = c->board->chan; do {
		if (x->dir == c->num && c->type != TAUPCI_NONE)
			taupci_set_ts (x, x->ts);
	} while (++x < c->board->chan + TAUPCI_NCHAN);
	taupci_kick_setup (c);
}

unsigned taupci_hamming (u32 ts)
{
	ts = (ts & 0x55555555ul) + ((ts >> 1) & 0x55555555ul);
	ts = (ts & 0x33333333ul) + ((ts >> 2) & 0x33333333ul);
	ts = (ts & 0x07070707ul) + ((ts >> 4) & 0x07070707ul);
	ts = (ts & 0x000F000Ful) + ((ts >> 8) & 0x000F000Ful);
	ts += ts >> 16;
	return (u8) ts;
}

static unsigned taupci_ts_count (taupci_chan_t* c)
{
	if (c->unframed)
		return (c->dir == c->num) ? c->baud / 64000 : 0;
	return taupci_hamming (c->ts);
}

static void taupci_check_phony_mru (taupci_chan_t* c)
{
	unsigned chunk = taupci_ts_count (c);
	unsigned hdlc_chunk = c->ds21x54 ? 1024 : (chunk << 4);
	if (chunk == 0 || c->mios % chunk || (c->ds21x54 && c->mios / chunk * 32 != c->raw_mios))
		taupci_set_mru (c, 0);
	else if (c->mios > hdlc_chunk || (!c->ds21x54 && c->mios != hdlc_chunk))
		taupci_set_mru (c, hdlc_chunk);
}

void taupci_set_phony (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	/*
	* Valid only for TauPCI-E1.
	*/
	on = (on != 0);
	if (c->type != TAUPCI_E1)
		on = 0;

	if (on)
		c->dir = c->num;
	taupci_set_dir (c, c->dir);

	if (c->type == TAUPCI_E1) {
		if (c->phony != on) {
			u32 cfg = c->dma_imr & (CFG_TFI | CFG_RERR | CFG_TERR); /* LY: get cfg without CFG_RFI, e.g. enable rx-interrupt */
			if (c->scc_imr & ISR_RFO)
				cfg |= CFG_RFI; /* LY: disable rx-interrupt if rx not started */
			if ((c->phony = on) != 0) {
				if (!c->ds21x54 && taupci_hamming (c->ts) == 1)
					taupci_set_ts (c, 0);
				taupci_check_phony_mru (c);
				if (!(cfg & CFG_TFI))
					cfg |= CFG_RFI; /* LY: disable rx-interrupt in phony-mode if tx-enabled */
			} else {
				taupci_set_mru (c, 0);
			}
			taupci_wait_ready_for_action (c->board);
			c->dma_imr = cfg;
			if (!c->board->irq_enabled)
				cfg |= CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
			CFG (c) = cfg;
			taupci_submit_action (c->board);
		}
		taupci_set_tcr2 (c);
		c->e1cr &= ~(E1CR_PHONY | E1CR_LLOOP);
		if (c->phony)
			c->e1cr |= E1CR_PHONY | E1CR_LLOOP;
		if (c->lloop)
			c->e1cr |= E1CR_LLOOP;
		taupci_outb (c->board, c->E1CR, c->e1cr);
		/*
		* Correct dependences
		*/
		taupci_set_baud (c, c->baud);
		taupci_set_scrambler (c, c->scrambler);
	}

	taupci_update_ccr1_ccr2 (c);
}

int taupci_set_mru (taupci_chan_t* c, unsigned mios)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->phony) {
		unsigned chunk = taupci_ts_count (c);
		unsigned hdlc_chunk = c->ds21x54 ? 1024 : (chunk << 4);
		if (! chunk)
			return -1;
		if (mios == 0) {
			mios = hdlc_chunk;
			if (c->ds21x54) {
				mios = c->mios - c->mios % chunk;
				if (mios < chunk)
					mios = chunk;
				if (mios > (chunk << 5))
					mios = chunk << 5;
			}
		}
		if (mios % chunk)
			return -1;
		if (c->ds21x54) {
			if (mios > (chunk << 5))
				return -1;
		} else if (mios != hdlc_chunk)
			return -1;
		c->raw_mios = c->ds21x54 ? (mios << 5) / chunk : mios;
	} else {
		if (mios == 0)
			mios = TAUPCI_MTU;
		if (mios < 8 || mios > TAUPCI_MTU)
			return -1;
		c->raw_mios = mios;
	}
	c->mios = mios;

	if (c->rdesc)
		taucpi_update_rdesc (c);

	return mios;
}

static void taucpi_update_rdesc (taupci_chan_t* c)
{
	unsigned i;
	u32 len = (c->raw_mios + TAUPCI_BUFSZ - TAUPCI_MTU) << 16;
	if (c->phony) {
		len = c->raw_mios << 16;
		if ((c->scc_imr & ISR_XDU) && c->ds21x54)
			// LY: tx is disabled, so we need hi-bit.
			len += DESC_HI;
	}

	i = 0; do {
#ifdef HOLDBIT_MODE
		c->rdesc[i].len = (c->rdesc[i].len & ~(0x1FFF0000ul | DESC_HI)) + len;
#else
		c->rdesc[i].len = len | DESC_HOLD;
#endif
	} while (++i < TAUPCI_NBUF);
	ddk_flush_cpu_writecache ();
}

void taupci_set_crc4 (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_E1) {
		c->crc4 = (on != 0);
		taupci_set_ccr1 (c);
		taupci_set_tcr2 (c);
		taupci_kick_setup (c);
	}
}

void taupci_set_scrambler (taupci_chan_t* c, int on)
{
	ddk_assert (c->type != TAUPCI_NONE);
	if (c->type == TAUPCI_G703 || (c->type == TAUPCI_E1 && c->ds21x54)) {
		if (c->ds21x54 && (c->phony || !c->unframed))
			on = 0;
		c->scrambler = (on != 0);
		c->gmd &= ~GMD_SCR;
		if (c->scrambler)
			c->gmd |= GMD_SCR;
		taupci_outb (c->board, c->GMD, c->gmd);
	}
}

void taupci_set_gsyn (taupci_chan_t* c, int syn)
{
	u8 e1cr = 0;
	taupci_chan_t *x;
	taupci_board_t *b = c->board;
	ddk_assert (c->type != TAUPCI_NONE);

	switch (c->type) {
		case TAUPCI_E1:
			if (c->unframed && syn != TAUPCI_GSYN_INT)
				syn = TAUPCI_GSYN_RCV;
			switch (syn) {
				default:
					syn = TAUPCI_GSYN_INT;
					e1cr = E1CR_CLK_INT;
					break;
				case TAUPCI_GSYN_RCV:
					syn = TAUPCI_GSYN_RCV0 + c->num;
					e1cr = E1CR_CLK_RCV0 + c->num;
					break;
				case TAUPCI_GSYN_RCV0:
					e1cr = E1CR_CLK_RCV0;
					break;
				case TAUPCI_GSYN_RCV1:
					e1cr = E1CR_CLK_RCV1;
					break;
				case TAUPCI_GSYN_RCV2:
					e1cr = E1CR_CLK_RCV2;
					if (b->chan[2].type != TAUPCI_E1)
						return;
					break;
				case TAUPCI_GSYN_RCV3:
					e1cr = E1CR_CLK_RCV3;
					if (b->chan[3].type != TAUPCI_E1)
						return;
					break;
			}
			x = b->chan; do {
				if (x->type != TAUPCI_E1 || (!b->mux && x != c))
					continue;
				taupci_outb (b, x->E1CR, x->e1cr & ~E1CR_GRUN);
				x->e1cr = (x->e1cr & ~E1CR_CLK_MASK) | e1cr;
				x->gsyn = (u8) syn;
				taupci_outb (b, x->E1CR, x->e1cr);
			} while (++x < b->chan + TAUPCI_NCHAN);
			break;
		case TAUPCI_G703:
			c->gsyn = TAUPCI_GSYN_INT;
			c->gmd &= ~GMD_RSYNC;
			if (syn) {
				c->gsyn = TAUPCI_GSYN_RCV;
				c->gmd |= GMD_RSYNC;
			}
			taupci_outb (c->board, c->GMD, c->gmd);
			break;
		case TAUPCI_E3:
		case TAUPCI_T3:
		case TAUPCI_STS1:
			c->gsyn = TAUPCI_GSYN_INT;
			if (syn)
				c->gsyn = TAUPCI_GSYN_RCV;
			taupci_set_e3cr0 (c);
			break;
	}
}

/*
* Register the event processing functions.
*/
void taupci_register_transmit (taupci_chan_t* c, void (*func) (taupci_chan_t *, void *, int))
{
	ddk_assert (c->type != TAUPCI_NONE);
	c->transmit = func;
}

void taupci_register_receive (taupci_chan_t* c, void (*func) (taupci_chan_t *, u8 *, int))
{
	ddk_assert (c->type != TAUPCI_NONE);
	c->receive = func;
}

void taupci_register_error (taupci_chan_t* c, void (*func) (taupci_chan_t *, int))
{
	ddk_assert (c->type != TAUPCI_NONE);
	c->error = func;
}

static void taupci_stat_rotate (taupci_chan_t* c)
{
	/*
	* Degraded minutes -- having error rate more than 10e-6,
	* * not counting unavailable and severely errored seconds.
	*/
	if (c->cursec % 60 == 0) {
		/* if (c->degerr * 2 > c->degsec) */
		if (c->degerr > c->degsec * 2048 / 1000)
			++c->currnt.dm;
		c->degsec = 0;
		c->degerr = 0;
	}

	/*
	* Rotate statistics every 15 minutes.
	*/
	if (c->cursec == 15 * 60) {
		int i = 47; do {
			c->interval[i] = c->interval[i - 1];
		} while (--i);
		c->interval[0] = c->currnt;

		/*
		* Accumulate total statistics.
		*/
		c->total.bpv += c->currnt.bpv;
		c->total.fse += c->currnt.fse;
		c->total.crce += c->currnt.crce;
		c->total.rcrce += c->currnt.rcrce;
		c->total.uas += c->currnt.uas;
		c->total.les += c->currnt.les;
		c->total.es += c->currnt.es;
		c->total.bes += c->currnt.bes;
		c->total.ses += c->currnt.ses;
		c->total.oofs += c->currnt.oofs;
		c->total.css += c->currnt.css;
		c->total.dm += c->currnt.dm;
		c->currnt.bpv = 0;
		c->currnt.fse = 0;
		c->currnt.crce = 0;
		c->currnt.rcrce = 0;
		c->currnt.uas = 0;
		c->currnt.les = 0;
		c->currnt.es = 0;
		c->currnt.bes = 0;
		c->currnt.ses = 0;
		c->currnt.oofs = 0;
		c->currnt.css = 0;
		c->currnt.dm = 0;

		c->totsec += c->cursec;
		c->cursec = 0;
	}
}

/*
* Handle E1 channel once-per-second interrupt.
* Get the channel status and error counters.
* Compute the channel statistics, conforming to SNMP (RFC 1406).
*/
static void taupci_e1_interrupt (taupci_chan_t* c)
{
	unsigned bpv, fas, crc4, ebit, pcv, status;
	u8 sr1 = taupci_dallas_status (c, DS_SR1);

	c->e1_intr++;
	if (c->setup_cnt) {
		c->status = TAUPCI_ESTS_UNKNOWN;
		--c->setup_cnt;
		return;
	}

	if (! c->ds21x54)
		sr1 &= ~SR1_RSLIP;

	/*
	* Count seconds.
	* * During the first second after the channel startup
	* * the status registers are not stable yet,
	* * we will so skip the first second.
	*/
	++c->cursec;

	status = 0;
	fas = 0;
	crc4 = 0;
	ebit = 0;
	bpv = 0;

	if (sr1 & SR1_RCL)
		status = TAUPCI_ESTS_LOS;   /* loss of signal */
	else {
		bpv = VCR (taupci_dallas_in (c, DS_VCR1), taupci_dallas_in (c, DS_VCR2));
		if (sr1 & SR1_RUA1)
			status = TAUPCI_ESTS_AIS;   /* receiving all ones */
		else if (!c->unframed) {
			u8 ssr = taupci_dallas_in (c, DS_SSR);

			if (ssr & SSR_SYNC)
				status = TAUPCI_ESTS_LOF;   /* loss of framing */
			else {
				if (c->cas > TAUPCI_CAS_OFF) {
					if ((sr1 & (SR1_RSA1 | SR1_RSA0)) == SR1_RSA1)
						status = TAUPCI_ESTS_AIS16 | TAUPCI_ESTS_LOMF; /* signaling all ones */
					else if ((ssr & SSR_SYNC_CAS) || (sr1 & (SR1_RSA1 | SR1_RSA0)) == SR1_RSA0)
						status = TAUPCI_ESTS_LOMF;  /* loss of cas-multiframing */
					else if (sr1 & SR1_RDMA)
						status = TAUPCI_ESTS_RDMA;   /* alarm in timeslot 16 */
				}
				if (sr1 & SR1_RRA)
					status |= TAUPCI_ESTS_RA;    /* far loss of framing */
				if (c->crc4 && (ssr & SSR_SYNC_CRC4))
					status |= TAUPCI_ESTS_LOMF;  /* loss of crc4-multiframing */
			}
			fas = FASCR (taupci_dallas_in (c, DS_FASCR1), taupci_dallas_in (c, DS_FASCR2));
			crc4 = CRCCR (taupci_dallas_in (c, DS_CRCCR1), taupci_dallas_in (c, DS_CRCCR2));
			ebit = EBCR (taupci_dallas_in (c, DS_EBCR1), taupci_dallas_in (c, DS_EBCR2));
		}
	}

	/*
	* Controlled slip second -- any slip event.
	*/
	if (sr1 & SR1_RSLIP)
		++c->currnt.css;
	if (status & TAUPCI_ESTS_LOS)
		status = TAUPCI_ESTS_LOS;
	if (status & TAUPCI_ESTS_AIS)
		status = TAUPCI_ESTS_AIS;

	if (status == 0)
		status = TAUPCI_ESTS_NOALARM;
	c->status = status;

	/*
	* Compute the SNMP-compatible channel status.
	*/
	pcv = fas;
	c->currnt.bpv += bpv;
	c->currnt.fse += fas;

	/*
	* Unavaiable second -- receiving all ones, or
	* * loss of carrier, or loss of signal.
	*/
	if (sr1 & (SR1_RUA1 | SR1_RCL))
		/*
		* Unavailable second -- no other counters.
		*/
		++c->currnt.uas;
	else {
		/*
		* Path code violation is frame sync error if CRC4 disabled,
		* * or CRC error if CRC4 enabled.
		*/
		if (c->crc4) {
			c->currnt.crce += crc4;
			c->currnt.rcrce += ebit;
			pcv += crc4;
		}

		/*
		* Line errored second -- any BPV.
		*/
		if (bpv)
			++c->currnt.les;

		/*
		* Errored second -- any PCV, or out of frame sync,
		* * or any slip events.
		*/
		if (pcv || (status & (TAUPCI_ESTS_LOMF | TAUPCI_ESTS_LOF))
			|| ((c->board->mux || c->ds21x54) && (sr1 & SR1_RSLIP)))
			++c->currnt.es;

		/*
		* Severely errored framing second -- out of frame sync.
		*/
		if (status & (TAUPCI_ESTS_LOMF | TAUPCI_ESTS_LOF))
			++c->currnt.oofs;

		/*
		* Severely errored seconds --
		* * 832 or more PCVs, or 2048 or more BPVs.
		*/
		if (bpv >= 2048 || pcv >= 832)
			++c->currnt.ses;
		else {
			/*
			* Bursty errored seconds --
			* * no SES and more than 1 PCV.
			*/
			if (pcv > 1)
				++c->currnt.bes;

			/*
			* Collect data for computing
			* * degraded minutes.
			*/
			++c->degsec;
			c->degerr += bpv + pcv;
		}
	}

	taupci_stat_rotate (c);
}

/*
* G.703 mode channel: process 1-second timer interrupts.
* Read error and request registers, and fill the status field.
*/
void taupci_g703_timer (taupci_chan_t* c)
{
	u8 ls;
	int cd;
	ddk_assert (c->type != TAUPCI_NONE);

	/*
	* Count seconds.
	* * During the first second after the channel startup
	* * the status registers are not stable yet,
	* * we will so skip the first second.
	*/
	if (c->setup_cnt) {
		c->status = TAUPCI_ESTS_UNKNOWN;
		--c->setup_cnt;
		return;
	}

	++c->cursec;
	c->status = 0;

	/*
	* Compute the SNMP-compatible channel status.
	*/
	ls = taupci_inb (c->board, c->GLS);
	taupci_outb (c->board, c->GLS, ls);
	cd = taupci_get_cd (c);

	if (ls & GLS_BPV)
		++c->currnt.bpv;    /* bipolar violation */
	if (!cd)
		c->status |= TAUPCI_ESTS_LOS;   /* loss of signal */
	if (ls & GLS_ERR)
		c->status |= TAUPCI_ESTS_TSTERR;    /* test error */
	if (ls & GLS_LREQ)
		c->status |= TAUPCI_ESTS_TSTREQ;    /* test code detected */

	if (!c->status)
		c->status = TAUPCI_ESTS_NOALARM;

	/*
	* Unavaiable second -- loss of carrier, or receiving test code.
	*/
	if (!cd || (ls & GLS_LREQ))
		/*
		* Unavailable second -- no other counters.
		*/
		++c->currnt.uas;
	else {
		/*
		* Line errored second -- any BPV.
		*/
		if (ls & GLS_BPV)
			++c->currnt.les;

		/*
		* Collect data for computing
		* * degraded minutes.
		*/
		++c->degsec;
		if (cd && (ls & GLS_BPV))
			++c->degerr;
	}

	taupci_stat_rotate (c);
}

/*
* E3 channel: should be called four times or more per second.
*/
void taupci_e3_timer (taupci_chan_t* c)
{
	u8 sr;
	u32 er;
	ddk_assert (c->type != TAUPCI_NONE);

	if (c->board->model != TAUPCI_MODEL_1E3)
		return;

	c->status = TAUPCI_ESTS_UNKNOWN;
	if (c->setup_cnt) {
		--c->setup_cnt;
		return;
	}
	/*
	* Freeze counter
	*/
	taupci_outb (c->board, E3ER0, 0);
	er = taupci_inb (c->board, E3ER0);
	er += taupci_inb (c->board, E3ER1) << 8;
	er += taupci_inb (c->board, E3ER2) << 16;

	sr = taupci_inb (c->board, E3SR0);

	taupci_outb (c->board, E3CR1, c->e3cr1 | E3CR1_SRCLR);

	if (sr & E3SR0_LOS) {
		if (c->losais && (c->status & TAUPCI_ESTS_LOS) != 0 && !c->ais) {
			c->ais = 1;
			taupci_set_e3cr0 (c);
		}
		c->status = TAUPCI_ESTS_LOS;
	} else {
		if (c->losais && (c->status & TAUPCI_ESTS_LOS) == 0 && c->ais) {
			c->ais = 0;
			taupci_set_e3cr0 (c);
		}
	}

	if (sr & E3SR0_DM)
		c->status |= TAUPCI_ESTS_TXE;

	if (sr & E3SR0_AIS && !(sr & E3SR0_LOS))
		c->status |= TAUPCI_ESTS_AIS;

	if (!(c->status & TAUPCI_ESTS_LOS))
		c->e3ccv += er;

	if (++c->e3csec_5 == 5 * 60 * 15) {
		int i = 47; do {
			c->e3icv[i] = c->e3icv[i - 1];
		} while (--i);

		c->e3icv[0] = c->e3ccv;
		c->e3tcv += c->e3ccv;
		c->e3tsec += c->e3csec_5 / 5;
		c->e3ccv = 0;
		c->e3csec_5 = 0;
	}

	if (c->status == TAUPCI_ESTS_UNKNOWN)
		c->status = TAUPCI_ESTS_NOALARM;
}

static const u8 taupci_reverse[256] = {
	0, 128, 64, 192, 32, 160, 96, 224, 16, 144, 80, 208, 48, 176, 112,
	240, 8, 136, 72, 200, 40, 168, 104, 232, 24, 152, 88, 216, 56, 184,
	120, 248, 4, 132, 68, 196, 36, 164, 100, 228, 20, 148, 84, 212, 52,
	180, 116, 244, 12, 140, 76, 204, 44, 172, 108, 236, 28, 156, 92, 220,
	60, 188, 124, 252, 2, 130, 66, 194, 34, 162, 98, 226, 18, 146, 82,
	210, 50, 178, 114, 242, 10, 138, 74, 202, 42, 170, 106, 234, 26, 154,
	90, 218, 58, 186, 122, 250, 6, 134, 70, 198, 38, 166, 102, 230, 22,
	150, 86, 214, 54, 182, 118, 246, 14, 142, 78, 206, 46, 174, 110, 238,
	30, 158, 94, 222, 62, 190, 126, 254, 1, 129, 65, 193, 33, 161, 97,
	225, 17, 145, 81, 209, 49, 177, 113, 241, 9, 137, 73, 201, 41, 169,
	105, 233, 25, 153, 89, 217, 57, 185, 121, 249, 5, 133, 69, 197, 37,
	165, 101, 229, 21, 149, 85, 213, 53, 181, 117, 245, 13, 141, 77, 205,
	45, 173, 109, 237, 29, 157, 93, 221, 61, 189, 125, 253, 3, 131, 67,
	195, 35, 163, 99, 227, 19, 147, 83, 211, 51, 179, 115, 243, 11, 139,
	75, 203, 43, 171, 107, 235, 27, 155, 91, 219, 59, 187, 123, 251, 7,
	135, 71, 199, 39, 167, 103, 231, 23, 151, 87, 215, 55, 183, 119, 247,
	15, 143, 79, 207, 47, 175, 111, 239, 31, 159, 95, 223, 63, 191, 127,
	255
};

static void taupci_receive_done (taupci_chan_t* c, u32 status)
{
	u32 len;
	unsigned rsta;
#ifdef HOLDBIT_MODE
	unsigned rh, cn;
#endif

	len = DESC_LEN (status);
	rsta = RSTA_VFR;

	if (likely (status & DESC_FE)) {
		rsta = 0;
		if (len > 0)
			rsta = c->rbuf[c->rn][--len];
	} else if (!c->phony)
		goto frame;

	if (unlikely (rsta & RSTA_RDO)) {
		/*
		 * Receive overrun error
		 */
		++c->overrun;
		if (c->error)
			c->error (c, TAUPCI_OVERRUN);
	} else if (unlikely (!(rsta & RSTA_VFR))) {
		/*
		 * Receive frame error
		 */
frame:
		++c->frame;
		if (c->error)
			c->error (c, TAUPCI_FRAME);
	} else if (unlikely (!c->phony && !(rsta & RSTA_CRC))) {
		/*
		 * Receive CRC error
		 */
		++c->crc;
		if (c->error)
			c->error (c, TAUPCI_CRC);
	} else if (likely (len)) {
		/*
		 * Valid packet
		 */
		if (c->phony) {
			u8 *bufptr = c->rbuf[c->rn];
			unsigned i = 0;
			if (len != c->raw_mios)
				goto frame;
			if (c->ds21x54) {
				if (c->ts != 0xFFFFFFFFul && c->ts) {
					unsigned j = 0;
					u32 ts = c->ts;
					do {
						if (ts & 1)
							bufptr[j++] = bufptr[i];
						ts = (ts >> 1) | (ts << 31);
					} while (++i < len);
					len = j;
				}
			} else {
				do {
					bufptr[i] = taupci_reverse[bufptr[i]];
				} while (++i < len);
			}
		}
		if (c->receive)
			c->receive (c, c->rbuf[c->rn], len);
		c->ibytes += len;
		++c->ipkts;
	}
#ifdef HOLDBIT_MODE
	c->rdesc[c->rn].len |= DESC_HOLD;
	ddk_flush_cpu_writecache ();
	rh = (c->rn - 1) & (TAUPCI_NBUF - 1);
	c->rdesc[rh].len &= ~DESC_HOLD;
	ddk_flush_cpu_writecache ();
	cn = (FRDA (c) - c->rd_dma[0]) / sizeof (taupci_desc_t);
	if (cn == rh) {
		u32 cfg;
		taupci_wait_ready_for_action (b);
		BRDA (c) = c->rd_dma[rh];

		cfg = c->dma_imr | CFG_IDR;
		if (!c->board->irq_enabled)
			cfg |= CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
		CFG (c) = cfg;

		taupci_submit_action (b);
	}
#else
	LRDA (c) = c->rd_dma[(c->rn - 1) & (TAUPCI_NBUF - 1)];
#endif
	c->rn = (c->rn + 1) & (TAUPCI_NBUF - 1);
}

/*
* Receive interrupt handler.
*/
static void taupci_receive_interrupt (taupci_chan_t* c)
{
	u32 iq_overflow, rx_overrun;
	u32 vector, status;

	rx_overrun = 0;
again:
	iq_overflow = 0;
	vector = c->iq_rx[c->irn];
	while (vector) {
		c->iq_rx[c->irn] = 0;
		iq_overflow |= c->iq_rx[(c->irn - 1) & (TAUPCI_QSZ - 1)];
		if (unlikely (vector & ISR_TX)) {
			/*
			 * Ignore transmit interrupts.
			 */
		} else {
			++c->rintr;
			if (unlikely (vector & ISR_SCC)) {
				/*
				 * SCC interrupt.
				 */
				if (vector & (ISR_RDO | ISR_RFO)) {
					rx_overrun |= vector & (ISR_RDO | ISR_RFO);
					CMDR (c) = CMDR_RRES;
					ddk_kd_print (("cp%d.%d: vector-overrun 0x%08lX\n", c->board->num, c->num, vector));
					//CMDR (c) = CMDR_RFRD;
				}
			} else {
				/*
				* DMA interrupt.
				*/
				ddk_yield_dmabus ();
				status = c->rdesc[c->rn].status;
				if (likely (status)) {
					c->rdesc[c->rn].status = 0;
					taupci_receive_done (c, status);
				}
			}
		}
		c->irn = (c->irn + 1) & (TAUPCI_QSZ - 1);
		ddk_yield_dmabus ();
		vector = c->iq_rx[c->irn];
	}

	if (unlikely (iq_overflow)) {
		unsigned irn = c->irn; do {
			irn = (irn + 1) & (TAUPCI_QSZ - 1);
			if (c->iq_rx[irn]) {
				c->irn = irn;
				goto again;
			}
		} while (irn != c->irn);
	}

	ddk_yield_dmabus ();
	status = c->rdesc[c->rn].status;
	if (unlikely (status)) {
		c->rdesc[c->rn].status = 0;
		taupci_receive_done (c, status);
		goto again;
	}

	if (rx_overrun) {
		++c->overrun;
		if (c->error)
			c->error (c, TAUPCI_OVERRUN);
#ifdef HOLDBIT_MODE
		if (rx_overrun & ISR_RFO) {
			u32 cfg;
			taupci_wait_ready_for_action (c->board);
			BRDA (c) = c->rd_dma[c->rn];

			cfg =  c->dma_imr | CFG_IDR;
			if (!c->board->irq_enabled)
				cfg |= CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
			CFG (c) = cfg;

			taupci_submit_action (c->board);
		}
#endif
	}
}

static int taupci_transmit_done (taupci_chan_t* c)
{
	int result;
	unsigned len = c->tdesc[c->tn].fe;
	void *tag = c->tag[c->tn];
	c->tdesc[c->tn].fe = 0;

	c->tn = (c->tn + 1) & (TAUPCI_NBUF - 1);
	result = c->te - c->tn;

	if (likely (!(c->scc_imr & ISR_XDU))) {
#ifdef HOLDBIT_MODE
		if (result) {
			unsigned cn = (FTDA (c) - c->td_dma[0]) / sizeof (taupci_desc_t);

			if (cn == ((c->tn - 1) & (TAUPCI_NBUF - 1))) {
				/*
				* kick transmitter from HOLD.
				*/
				GCMDR (c->board) = 0x0400ul << c->num;
			}
		}
#endif
		if (len > 0 ) {
			c->obytes += len;
			++c->opkts;
			if (c->transmit)
				c->transmit (c, tag, len);
		}
	}
	taupci_phony_stub_tx (c);
	return result;
}

/*
* LY: insert stub, this is may be required for rx-processing.
*/
static void taupci_phony_stub_tx (taupci_chan_t* c)
{
	if (c->phony /* LY: only in phony mode */
		&& !(c->scc_imr & (ISR_RFO | ISR_XDU)) /* LY: only if rx and tx both are started */
		&& c->te == c->tn /* LY: only if tx-queue is empty */) {
			ddk_memset (c->tbuf[c->te], (!c->unframed && c->voice) ? 0xD5 : 0xFF, c->raw_mios);
			c->tag[c->te] = 0;
			c->tdesc[c->te].fe = 0;
			c->tdesc[c->te].status = 0;
			c->tdesc[c->te].len = (c->raw_mios << 16) + DESC_FE + DESC_HOLD;
			ddk_flush_cpu_writecache ();
			c->te = (c->te + 1) & (TAUPCI_NBUF - 1);

			if (unlikely (BTDA (c) == 0)) {
				u32 cfg;
				taupci_wait_ready_for_action (c->board);
				BTDA (c) = c->td_dma[c->tn];

				cfg = c->dma_imr | CFG_IDT;
				if (!c->board->irq_enabled)
					cfg |= CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
				CFG (c) = cfg;

				taupci_submit_action (c->board);
			}
#ifdef HOLDBIT_MODE
			else {
				// taupci_wait_ready_for_action (c->board);
				GCMDR (c->board) = 0x0400ul << c->num;
			}
#else
			LTDA (c) = c->td_dma[c->te];
#endif
		}
}

/*
* Transmit interrupt handler.
*/
static int taupci_transmit_interrupt (taupci_chan_t* c)
{
	int result, underrun_flag;
	u32 vector, iq_overflow;
	result = underrun_flag = 0;

again:
	iq_overflow = 0;
	vector = c->iq_tx[c->itn];
	while (vector) {
		c->iq_tx[c->itn] = 0;
		iq_overflow |= c->iq_tx[(c->itn - 1) & (TAUPCI_QSZ - 1)];
		if (unlikely (!(vector & ISR_TX))) {
			/*
			* Ignore receive interrupts.
			*/
		} else {
			++c->tintr;
			if (vector & ISR_SCC) {
				/*
				* SCC interrupt.
				*/
				if (vector & ISR_XDU)
					underrun_flag |= 1;
#ifdef USE_ISR_ALLS
				if (vector & ISR_ALLS) {
					goto maydone;
				}
#endif
			} else {
				/*
				* DMA interrupt.
				*/
#ifdef USE_ISR_ALLS
maydone:
#endif
				ddk_yield_dmabus ();
				if (c->tn != c->te && c->tdesc[c->tn].status) {
					if (unlikely (!taupci_transmit_done (c)) && c->phony)
						underrun_flag |= 2;
					result = 1;
				}
			}
		}
		c->itn = (c->itn + 1) & (TAUPCI_QSZ - 1);
		ddk_yield_dmabus ();
		vector = c->iq_tx[c->itn];
	}

	if (unlikely (iq_overflow)) {
		unsigned itn = c->itn; do {
			itn = (itn + 1) & (TAUPCI_QSZ - 1);
			if (c->iq_tx[itn]) {
				c->itn = itn;
				goto again;
			}
		} while (itn != c->itn);
	}

	if (likely (c->tn != c->te)) {
		ddk_yield_dmabus ();
		if (unlikely (c->tdesc[c->tn].status)) {
			if (unlikely (!taupci_transmit_done (c)) && c->phony)
				underrun_flag |= 2;
			result = 1;
			goto again;
		}
	}

	if (unlikely (underrun_flag)) {
		/*
		* Underrun - reset transmitter and dma.
		*/
		if (underrun_flag & 1)
			CMDR (c) = CMDR_XRES;
		taupci_phony_stub_tx (c);
		++c->underrun;
		if (c->error)
			c->error (c, TAUPCI_UNDERRUN);
	}

	return result;
}

void taupci_e1_timer (taupci_chan_t *c)
{
	if (c->type == TAUPCI_E1 && (taupci_dallas_status (c, DS_SR2) & SR2_SEC))
		taupci_e1_interrupt (c);
}

/*
* Peripheral interrupt handler.
*/
static void taupci_peripheral_interrupt (taupci_board_t* b)
{
	taupci_chan_t *c;

	c = b->chan; do
		taupci_e1_timer (c);
	while (++c < b->chan + TAUPCI_NCHAN);
}

int taupci_is_interrupt_pending (taupci_board_t* b)
{
	return GSTAR (b) != 0;
}

static void taupci_chan_wd (taupci_chan_t* c)
{
	if (likely (c->rdesc != 0))
		taupci_receive_interrupt (c);
	if (likely (c->tdesc != 0))
		taupci_transmit_interrupt (c);
}

int taupci_handle_interrupt (taupci_board_t* b)
{
	int loopcount = 0;
	u32 gstar;

	goto loop_enter; do {
		GSTAR (b) = gstar;

		if (unlikely (gstar & (1ul << 1))) {
			b->dead |= TAUPCI_DEAD_20534;
			if (b->irq_enabled)
				taupci_enable_interrupt (b, 0);
			ddk_kd_print (("cp %d: TAUPCI_IRQ_FAILED\n", b->num));
			return TAUPCI_IRQ_FAILED;
		}
		if (gstar & 1) {
			b->action_pending = 0;
			//ddk_dbg_print ("cp %d: irq action-done\n", b->num);
		}
		if (gstar & ((1ul << 19) | (1ul << 18) | (1ul << 16))) {
			LCONF (b) = 0;
			taupci_peripheral_interrupt (b);
			if (b->irq_enabled)
				LCONF (b) = 0x00400000ul;
		}
		if (gstar & (0x01ul << 24))
			taupci_transmit_interrupt (b->chan + 0);
		if (gstar & (0x10ul << 24))
			taupci_receive_interrupt (b->chan + 0);
		if (likely (b->model > TAUPCI_MODEL_LITE)) {
			if (gstar & (0x01ul << 25))
				taupci_transmit_interrupt (b->chan + 1);
			if (gstar & (0x10ul << 25))
				taupci_receive_interrupt (b->chan + 1);
			if (b->model > TAUPCI_MODEL_OE1) {
				if (gstar & (0x01ul << 26))
					taupci_transmit_interrupt (b->chan + 2);
				if (gstar & (0x10ul << 26))
					taupci_receive_interrupt (b->chan + 2);
				if (gstar & (0x01ul << 27))
					taupci_transmit_interrupt (b->chan + 3);
				if (gstar & (0x10ul << 27))
					taupci_receive_interrupt (b->chan + 3);
			}
		}
		if (++loopcount > 1000) {
			if (b->irq_enabled)
				taupci_enable_interrupt (b, 0);
			ddk_kd_print (("cp %d: TAUPCI_IRQ_STORM\n", b->num));
			return TAUPCI_IRQ_STORM;
		}
loop_enter:
		ddk_yield_dmabus ();
		gstar = GSTAR (b);
	} while (gstar);

	if (unlikely (b->dxc_status) && !(taupci_inb (b, E1SR) & SR_CMBUSY)) {
		b->dxc_status &= ~TAUPCI_CROSS_PENDING;
		if (b->dxc_status & TAUPCI_CROSS_WAITING)
			taupci_dxc_update (b);
	}

	++b->intr;
	ddk_yield_dmabus ();
	taupci_chan_wd (b->chan + 0);
	if (b->model > TAUPCI_MODEL_LITE) {
		taupci_chan_wd (b->chan + 1);
		if (b->model > TAUPCI_MODEL_OE1) {
			taupci_chan_wd (b->chan + 2);
			taupci_chan_wd (b->chan + 3);
		}
	}

	return loopcount;
}

/*
* Enable/disable adapter interrupts.
*/
void taupci_enable_interrupt (taupci_board_t* b, int on)
{
	taupci_chan_t *c;

	b->irq_enabled = (on != 0);
	LCONF (b) = b->irq_enabled ? 0x00400000ul : 0;
	taupci_wait_ready_for_action (b);
	c = b->chan; do {
		if (c->type != TAUPCI_NONE) {
			SCC_IMR (c) = b->irq_enabled ? c->scc_imr : 0xFFFFFFFFul;
			CFG (c) = b->irq_enabled ? c->dma_imr : (CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR);
		}
	} while (++c < b->chan + TAUPCI_NCHAN);
	taupci_submit_action (b);
}

int taupci_transmit_space (taupci_chan_t* c)
{
	ddk_assert (c->type != TAUPCI_NONE);
	return (TAUPCI_NBUF + c->tn - c->te - 1) & (TAUPCI_NBUF - 1);
}

int taupci_send_packet (taupci_chan_t* c, u8 *data, unsigned len, void *tag)
{
	u8 *bufptr;
	unsigned i = 0;
	ddk_assert (c->type != TAUPCI_NONE);

	if (unlikely (!taupci_transmit_space (c) || c->tdesc == 0))
		return TAUPCI_SEND_NOSPACE;

	if (unlikely (len <= 0 || len > c->mios))
		return TAUPCI_SEND_INVALID_LEN;

	c->tag[c->te] = tag;
	c->tdesc[c->te].fe = len;
	c->tdesc[c->te].status = 0;
	bufptr = c->tbuf[c->te];
	if (c->phony) {
		if (c->ds21x54) {
			u32 ts = c->ts;
			if (unlikely (!ts || ts == 0xFFFFFFFFul))
				goto flat_copy;
			do {
				*bufptr++ = (ts & 1) ? data[i++] : -1;
				ts = (ts >> 1) | (ts << 31);
			} while (i < len);
			len = (bufptr - c->tbuf[c->te] + 31u) & ~31u;
		} else {
			do {
				bufptr[i] = taupci_reverse[data[i]];
			} while (++i < len);
		}
	} else if (likely (data != bufptr)) {
flat_copy:
		ddk_memcpy (bufptr, data, len);
	}

	/*
	* Set up the tx descriptor.
	*/
	c->tdesc[c->te].len = (len << 16) + DESC_FE + DESC_HOLD;
#ifdef HOLDBIT_MODE
	ddk_flush_cpu_writecache ();
	c->tdesc[(c->te - 1) & (TAUPCI_NBUF - 1)].len &= ~DESC_HOLD;
#endif
	ddk_flush_cpu_writecache ();
	c->te = (c->te + 1) & (TAUPCI_NBUF - 1);

	if (unlikely (BTDA (c) == 0)) {
		/*
		* Start the transmitter.
		*/
		u32 cfg;
		taupci_wait_ready_for_action (c->board);
		BTDA (c) = c->td_dma[c->tn];

		cfg = c->dma_imr | CFG_IDT;
		if (!c->board->irq_enabled)
			cfg |= CFG_RFI | CFG_TFI | CFG_RERR | CFG_TERR;
		CFG (c) = cfg;

		taupci_submit_action (c->board);
	}
#ifdef HOLDBIT_MODE
	else if (c->tn == c->te) {
		//taupci_wait_ready_for_action (c->board);
		GCMDR (c->board) = 0x0400ul << c->num;
	}
#else
	LTDA (c) = c->td_dma[c->te];
#endif
	return 0;
}

int taupci_dacs (taupci_board_t* b, int ch_a, int ts_a, int ch_b, int ts_b, int on, int include_cas)
{
	taupci_chan_t *ca, *cb;
	int la, lb;

	if (b->E1DATA != E4DAT || !b->mux)
		return -1;

	if (ch_a < 0 || ch_a >= TAUPCI_NCHAN || ch_b < 0 || ch_b >= TAUPCI_NCHAN)
		return -1;

	if (ts_a < 0 || ts_a >= 32 || ts_b < 0 || ts_b >= 32)
		return -1;

	ca = b->chan + ch_a;
	cb = b->chan + ch_b;
	if (ca->type != TAUPCI_E1 || cb->type != TAUPCI_E1 || ca->unframed || cb->unframed)
		return -1;

	if (!(ca->ts & (1ul << ts_a)) || !(cb->ts & (1ul << ts_b)))
		return -1;

	la = (ca->dir << 5) + ts_a;
	lb = (cb->dir << 5) + ts_b;
	if (on) {
		ca->dacs_ts |= 1ul << ts_a;
		cb->dacs_ts |= 1ul << ts_b;
		b->dxc.pending[la] = lb;
		b->dxc.pending[lb] = la;
		if (include_cas) {
			b->dxc.pending[128 + la] = lb;
			b->dxc.pending[128 + lb] = la;
		}
	} else {
		ca->dacs_ts &= ~(1ul << ts_a);
		cb->dacs_ts &= ~(1ul << ts_b);
		b->dxc.pending[la] = 0xFF;
		b->dxc.pending[lb] = 0xFF;
		if (include_cas) {
			b->dxc.pending[128 + la] = 0xFF;
			b->dxc.pending[128 + lb] = 0xFF;
		}
	}

	if (taupci_dxc_compare (b)) {
		if (taupci_inb (b, E1SR) & SR_CMBUSY)
			b->dxc_status |= TAUPCI_CROSS_WAITING;
		else
			taupci_dxc_update (b);
	} else if (b->dxc_status & TAUPCI_CROSS_WAITING)
		b->dxc_status &= ~TAUPCI_CROSS_WAITING;

	taupci_tsmem_put_single (ca, ts_a, on ? 0 : 1);
	taupci_tsmem_put_single (cb, ts_b, on ? 0 : 1);

	return 0;
}

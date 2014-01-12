/*
 * DDK for Cronyx Sigma adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 */

//#define DMA_MASK	0xd4	/* DMA mask register */
//#define DMA_MASK_CLEAR	0x04	/* DMA clear mask */
//#define DMA_MODE	0xd6	/* DMA mode register */
//#define DMA_MODE_MASTER	0xc0	/* DMA master mode */

#define BYTE *(u8*)&

static u8 irqmask[] = {
	BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_3,
	BCR0_IRQ_DIS, BCR0_IRQ_5, BCR0_IRQ_DIS, BCR0_IRQ_7,
	BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_10, BCR0_IRQ_11,
	BCR0_IRQ_12, BCR0_IRQ_DIS, BCR0_IRQ_DIS, BCR0_IRQ_15,
};

static u8 dmamask[] = {
	BCR0_DMA_DIS, BCR0_DMA_DIS, BCR0_DMA_DIS, BCR0_DMA_DIS,
	BCR0_DMA_DIS, BCR0_DMA_5, BCR0_DMA_6, BCR0_DMA_7,
};

/* standard base port set */
unsigned sigma_porttab[] = {
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2a0, 0x2c0, 0x2e0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0x3c0, 0x3e0, 0
};

/* valid IRQs and DRQs */
unsigned sigma_irqtab[] = { 3, 5, 7, 10, 11, 12, 15, 0 };
unsigned sigma_dmatab[] = { 5, 6, 7, 0 };

static int valid (unsigned value, unsigned *list)
{
	while (*list)
		if (value == *list++)
			return 1;
	return 0;
}

u32 sigma_rxbaud = 9600;	/* receiver baud rate */
u32 sigma_txbaud = 9600;	/* transmitter baud rate */

int sigma_univ_mode = SIGMA_MODE_HDLC;	/* univ. chan. mode: async or sync */
int sigma_sync_mode = SIGMA_MODE_HDLC;	/* sync. chan. mode: HDLC, Bisync or X.21 */
int sigma_iftype = 0;		/* univ. chan. interface: upper/lower */

static int sigma_probe_chip (unsigned base);
static void sigma_setup_chip (sigma_chan_t * c);

/*
 * Wait for CCR to clear.
 */
void sigma_cmd (unsigned base, int cmd)
{
	unsigned port = CCR (base);
	int count;

	/*
	 * Wait 10 msec for the previous command to complete.
	 */
	for (count = 0; ddk_inb (port) && count < 20000; ++count)
		continue;

	/*
	 * Issue the command.
	 */
	ddk_outb (port, cmd);

	/*
	 * Wait 10 msec for the command to complete.
	 */
	for (count = 0; ddk_inb (port) && count < 20000; ++count)
		continue;
}

/*
 * Reset the chip.
 */
static int sigma_reset (unsigned port)
{
	int count;

	/*
	 * Wait up to 10 msec for revision code to appear after reset.
	 */
	for (count = 0; count < 20000; ++count)
		if (ddk_inb (GFRCR (port)) != 0)
			break;

	sigma_cmd (port, CCR_RSTALL);

	/*
	 * Firmware revision code should clear imediately.
	 */
	/*
	 * Wait up to 10 msec for revision code to appear again.
	 */
	for (count = 0; count < 20000; ++count)
		if (ddk_inb (GFRCR (port)) != 0)
			return (1);

	/*
	 * Reset failed.
	 */
	return (0);
}

int sigma_download (unsigned port, const u8 *firmware, u32 bits, const sigma_dat_tst_t * tst)
{
	u8 cr2, sr;
	u32 i, n, maxn = (bits + 7) / 8;
	int v, b;

	ddk_inb (BDET (port));
	for (i = n = 0; n < maxn; ++n) {
		v = ((firmware[n] ^ ' ') << 1) | (firmware[n] >> 7 & 1);
		for (b = 0; b < 7; b += 2, i += 2) {
			if (i >= bits)
				break;
			cr2 = 0;
			if (v >> b & 1)
				cr2 |= BCR2_TMS;
			if (v >> b & 2)
				cr2 |= BCR2_TDI;
			ddk_outb (BCR2 (port), cr2);
			sr = ddk_inb (BSR (port));
			ddk_outb (BCR0 (port), BCR0800_TCK);
			ddk_outb (BCR0 (port), 0);
			if (i >= tst->end)
				++tst;
			if (i >= tst->start && (sr & BSR800_LERR))
				return (0);
		}
	}
	return (1);
}

/*
 * Check if the Sigma-XXX board is present at the given base port.
 */
static int sigma_probe_chained_board (unsigned port, int *c0, int *c1)
{
	int rev, i;

	/*
	 * Read and check the board revision code.
	 */
	rev = ddk_inb (BSR (port));
	*c0 = *c1 = 0;
	switch (rev & BSR_VAR_MASK) {
		case CRONYX_100:
			*c0 = 1;
			break;
		case CRONYX_400:
			*c1 = 1;
			break;
		case CRONYX_500:
			*c0 = *c1 = 1;
			break;
		case CRONYX_410:
			*c0 = 1;
			break;
		case CRONYX_810:
			*c0 = *c1 = 1;
			break;
		case CRONYX_410s:
			*c0 = 1;
			break;
		case CRONYX_810s:
			*c0 = *c1 = 1;
			break;
		case CRONYX_440:
			*c0 = 1;
			break;
		case CRONYX_840:
			*c0 = *c1 = 1;
			break;
		case CRONYX_401:
			*c0 = 1;
			break;
		case CRONYX_801:
			*c0 = *c1 = 1;
			break;
		case CRONYX_401s:
			*c0 = 1;
			break;
		case CRONYX_801s:
			*c0 = *c1 = 1;
			break;
		case CRONYX_404:
			*c0 = 1;
			break;
		case CRONYX_703:
			*c0 = *c1 = 1;
			break;
		default:
			return (0);	/* invalid variant code */
	}

	switch (rev & BSR_OSC_MASK) {
		case BSR_OSC_20:	/* 20 MHz */
		case BSR_OSC_18432:	/* 18.432 MHz */
			break;
		default:
			return (0);	/* oscillator frequency does not match */
	}

	for (i = 2; i < 0x10; i += 2)
		if ((ddk_inb (BSR (port) + i) & BSR_REV_MASK) != (rev & BSR_REV_MASK))
			return (0);	/* status changed? */
	return (1);
}

/*
 * Check if the Sigma-800 board is present at the given base port.
 * Read board status register 1 and check identification bits
 * which should invert every next read.
 */
static int sigma_probe_800_chained_board (unsigned port)
{
	u8 det, odet;
	int i;

	odet = ddk_inb (BDET (port));
	if ((odet & (BDET_IB | BDET_IB_NEG)) != BDET_IB && (odet & (BDET_IB | BDET_IB_NEG)) != BDET_IB_NEG)
		return (0);
	for (i = 0; i < 100; ++i) {
		det = ddk_inb (BDET (port));
		if (((det ^ odet) & (BDET_IB | BDET_IB_NEG)) != (BDET_IB | BDET_IB_NEG))
			return (0);
		odet = det;
	}
	/*
	 * Reset the controller.
	 */
	ddk_outb (BCR0 (port), 0);
	ddk_outb (BCR1 (port), 0);
	ddk_outb (BCR2 (port), 0);
	return (1);
}

/*
 * Check if the Sigma-2x board is present at the given base port.
 */
static int sigma_probe_2x_board (unsigned port)
{
	int rev, i;

	/*
	 * Read and check the board revision code.
	 */
	rev = ddk_inb (BSR (port));
	if ((rev & BSR2X_VAR_MASK) != CRONYX_22 && (rev & BSR2X_VAR_MASK) != CRONYX_24)
		return (0);	/* invalid variant code */

	for (i = 2; i < 0x10; i += 2)
		if ((ddk_inb (BSR (port) + i) & BSR2X_REV_MASK) != (rev & BSR2X_REV_MASK))
			return (0);	/* status changed? */
	return (1);
}

/*
 * Check if the Cronyx-Sigma board is present at the given base port.
 */
int sigma_probe_board (unsigned port, int irq, int dma)
{
	int c0, c1, c2 = 0, c3 = 0, result;

	if (!valid (port, sigma_porttab))
		return 0;

	if (irq > 0 && !valid (irq, sigma_irqtab))
		return 0;

	if (dma > 0 && !valid (dma, sigma_dmatab))
		return 0;

	if (sigma_probe_800_chained_board (port)) {
		/*
		 * Sigma-800 detected.
		 */
		if (!(ddk_inb (BSR (port)) & BSR_NOCHAIN)) {
			/*
			 * chained board attached
			 */
			if (!sigma_probe_800_chained_board (port + 0x10))
				/*
				 * invalid chained board?
				 */
				return (0);
			if (!(ddk_inb (BSR (port + 0x10)) & BSR_NOCHAIN))
				/*
				 * invalid chained board flag?
				 */
				return (0);
		}
		return 1;
	}
	if (sigma_probe_chained_board (port, &c0, &c1)) {
		/*
		 * Sigma-XXX detected.
		 */
		if (!(ddk_inb (BSR (port)) & BSR_NOCHAIN)) {
			/*
			 * chained board attached
			 */
			if (!sigma_probe_chained_board (port + 0x10, &c2, &c3))
				/*
				 * invalid chained board?
				 */
				return (0);
			if (!(ddk_inb (BSR (port + 0x10)) & BSR_NOCHAIN))
				/*
				 * invalid chained board flag?
				 */
				return (0);
		}
	} else if (sigma_probe_2x_board (port)) {
		c0 = 1;		/* Sigma-2x detected. */
		c1 = 0;
	} else
		return (0);	/* no board detected */

	/*
	 * Turn off the reset bit.
	 */
	ddk_outb (BCR0 (port), BCR0_NORESET);
	if (c2 || c3)
		ddk_outb (BCR0 (port + 0x10), BCR0_NORESET);

	result = 1;
	if (c0 && !sigma_probe_chip (CS0 (port)))
		result = 0;	/* no CD2400 chip here */
	else if (c1 && !sigma_probe_chip (CS1A (port))
		 && !sigma_probe_chip (CS1 (port)))
		result = 0;	/* no second CD2400 chip */
	else if (c2 && !sigma_probe_chip (CS0 (port + 0x10)))
		result = 0;	/* no CD2400 chip on the slave board */
	else if (c3 && !sigma_probe_chip (CS1 (port + 0x10)))
		result = 0;	/* no second CD2400 chip on the slave board */

	/*
	 * Reset the controller.
	 */
	ddk_outb (BCR0 (port), 0);
	if (c2 || c3)
		ddk_outb (BCR0 (port + 0x10), 0);

	/*
	 * Yes, we really have valid Sigma board.
	 */
	return (result);
}

/*
 * Check if the CD2400 chip is present at the given base port.
 */
static int sigma_probe_chip (unsigned base)
{
	int rev, newrev, count;

	/*
	 * Wait up to 10 msec for revision code to appear after reset.
	 */
	rev = 0;
	for (count = 0; rev == 0; ++count) {
		if (count >= 20000)
			return (0);	/* reset failed */
		rev = ddk_inb (GFRCR (base));
	}

	/*
	 * Read and check the global firmware revision code.
	 */
	if (!(rev >= REVCL_MIN && rev <= REVCL_MAX) && !(rev >= REVCL31_MIN && rev <= REVCL31_MAX))
		return (0);	/* CD2400/2431 revision does not match */

	/*
	 * Reset the chip.
	 */
	if (!sigma_reset (base))
		return (0);

	/*
	 * Read and check the new global firmware revision code.
	 */
	newrev = ddk_inb (GFRCR (base));
	if (newrev != rev)
		return (0);	/* revision changed */

	/*
	 * Yes, we really have CD2400/2431 chip here.
	 */
	return (1);
}

/*
 * Check that the irq is functional.
 * irq>0  - activate the interrupt from the adapter (irq=on)
 * irq<0  - deactivate the interrupt (irq=off)
 * irq==0 - free the interrupt line (irq=tri-state)
 */
void sigma_probe_irq (sigma_board_t * b, int irq)
{
	int rev;
	unsigned port;

	rev = ddk_inb (BSR (b->port));
	port = ((rev & BSR_VAR_MASK) != CRONYX_400) ? CS0 (b->port) : CS1 (b->port);

	if (irq > 0) {
		ddk_outb (BCR0 (b->port), BCR0_NORESET | irqmask[irq]);
		ddk_outb (CAR (port), 0);
		sigma_cmd (port, CCR_CLRCH);
		ddk_outb (CMR (port), CMR_HDLC);
		ddk_outb (TCOR (port), 0);
		ddk_outb (TBPR (port), 1);
		sigma_cmd (port, CCR_INITCH | CCR_ENTX);
		ddk_outb (IER (port), IER_TXMPTY);
	} else {
		ddk_outb (BCR0 (b->port), 0);
		sigma_reset (port);
	}
}

static int sigma_chip_revision (unsigned port, int rev)
{
	int count;

	/*
	 * Model 400 has no first chip.
	 */
	port = ((rev & BSR_VAR_MASK) != CRONYX_400) ? CS0 (port) : CS1 (port);

	/*
	 * Wait up to 10 msec for revision code to appear after reset.
	 */
	for (count = 0; ddk_inb (GFRCR (port)) == 0; ++count)
		if (count >= 20000)
			return (0);	/* reset failed */

	return ddk_inb (GFRCR (port));
}

/*
 * Probe and initialize the board structure.
 */
void sigma_init (sigma_board_t * b, int num, unsigned port, int irq, int dma)
{
	int gfrcr, rev, chain, mod = 0, rev2 = 0, mod2 = 0;

	rev = ddk_inb (BSR (port));
	chain = !(rev & BSR_NOCHAIN);
	if (sigma_probe_800_chained_board (port)) {
		sigma_init_800 (b, num, port, irq, dma, chain);
		return;
	}
	if ((rev & BSR2X_VAR_MASK) == CRONYX_22 || (rev & BSR2X_VAR_MASK) == CRONYX_24) {
		sigma_init_22 (b, num, port, irq, dma, (rev & BSR2X_VAR_MASK), (rev & BSR2X_OSC_33));
		return;
	}

	ddk_outb (BCR0 (port), BCR0_NORESET);
	if (chain)
		ddk_outb (BCR0 (port + 0x10), BCR0_NORESET);
	gfrcr = sigma_chip_revision (port, rev);
	if (gfrcr >= REVCL31_MIN && gfrcr <= REVCL31_MAX)
		mod = 1;
	if (chain) {
		rev2 = ddk_inb (BSR (port + 0x10));
		gfrcr = sigma_chip_revision (port + 0x10, rev2);
		if (gfrcr >= REVCL31_MIN && gfrcr <= REVCL31_MAX)
			mod2 = 1;
		ddk_outb (BCR0 (port + 0x10), 0);
	}
	ddk_outb (BCR0 (port), 0);

	sigma_init_board (b, num, port, irq, dma, chain,
			  (rev & BSR_VAR_MASK), (rev & BSR_OSC_MASK), mod, (rev2 & BSR_VAR_MASK), (rev2 & BSR_OSC_MASK), mod2);
}

/*
 * Initialize the board structure, given the type of the board.
 */
void
sigma_init_board (sigma_board_t * b, int num, unsigned port, int irq, int dma,
		  int chain, int rev, int osc, int mod, int rev2, int osc2, int mod2)
{
	sigma_chan_t *c;
	char *type;
	int i;

	/*
	 * Initialize board structure.
	 */
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->opt = board_opt_dflt;

	b->type = SIGMA_MODEL_OLD;
	b->if0type = b->if8type = sigma_iftype;

	/*
	 * Set channels 0 and 8 mode, set DMA and IRQ.
	 */
	b->bcr0 = b->bcr0b = BCR0_NORESET | dmamask[b->dma] | irqmask[b->irq];

	/*
	 * Clear DTR[0..3] and DTR[8..12].
	 */
	b->bcr1 = b->bcr1b = 0;

	/*------------------ Master board -------------------*/

	/*
	 * Read and check the board revision code.
	 */
	strcpy (b->name, mod ? "m" : "");
	switch (rev) {
		default:
			type = "?";
			break;
		case CRONYX_100:
			type = "100";
			break;
		case CRONYX_400:
			type = "400";
			break;
		case CRONYX_500:
			type = "500";
			break;
		case CRONYX_410:
			type = "410";
			break;
		case CRONYX_810:
			type = "810";
			break;
		case CRONYX_410s:
			type = "410s";
			break;
		case CRONYX_810s:
			type = "810s";
			break;
		case CRONYX_440:
			type = "440";
			break;
		case CRONYX_840:
			type = "840";
			break;
		case CRONYX_401:
			type = "401";
			break;
		case CRONYX_801:
			type = "801";
			break;
		case CRONYX_401s:
			type = "401s";
			break;
		case CRONYX_801s:
			type = "801s";
			break;
		case CRONYX_404:
			type = "404";
			break;
		case CRONYX_703:
			type = "703";
			break;
	}
	strcat (b->name, type);

	switch (osc) {
		default:
		case BSR_OSC_20:	/* 20 MHz */
			b->chan[0].oscfreq = b->chan[1].oscfreq =
				b->chan[2].oscfreq = b->chan[3].oscfreq =
				b->chan[4].oscfreq = b->chan[5].oscfreq =
				b->chan[6].oscfreq = b->chan[7].oscfreq = mod ? 33000000L : 20000000L;
			strcat (b->name, "a");
			break;
		case BSR_OSC_18432:	/* 18.432 MHz */
			b->chan[0].oscfreq = b->chan[1].oscfreq =
				b->chan[2].oscfreq = b->chan[3].oscfreq =
				b->chan[4].oscfreq = b->chan[5].oscfreq =
				b->chan[6].oscfreq = b->chan[7].oscfreq = mod ? 20000000L : 18432000L;
			strcat (b->name, "b");
			break;
	}

	/*------------------ Slave board -------------------*/

	if (chain) {
		/*
		 * Read and check the board revision code.
		 */
		strcat (b->name, mod2 ? "/m" : "/");
		switch (rev2) {
			default:
				type = "?";
				break;
			case CRONYX_100:
				type = "100";
				break;
			case CRONYX_400:
				type = "400";
				break;
			case CRONYX_500:
				type = "500";
				break;
			case CRONYX_410:
				type = "410";
				break;
			case CRONYX_810:
				type = "810";
				break;
			case CRONYX_410s:
				type = "410s";
				break;
			case CRONYX_810s:
				type = "810s";
				break;
			case CRONYX_440:
				type = "440";
				break;
			case CRONYX_840:
				type = "840";
				break;
			case CRONYX_401:
				type = "401";
				break;
			case CRONYX_801:
				type = "801";
				break;
			case CRONYX_401s:
				type = "401s";
				break;
			case CRONYX_801s:
				type = "801s";
				break;
			case CRONYX_404:
				type = "404";
				break;
			case CRONYX_703:
				type = "703";
				break;
		}
		strcat (b->name, type);

		switch (osc2) {
			default:
			case BSR_OSC_20:	/* 20 MHz */
				b->chan[8].oscfreq = b->chan[9].oscfreq =
					b->chan[10].oscfreq =
					b->chan[11].oscfreq =
					b->chan[12].oscfreq =
					b->chan[13].oscfreq =
					b->chan[14].oscfreq = b->chan[15].oscfreq = mod2 ? 33000000L : 20000000L;
				strcat (b->name, "a");
				break;
			case BSR_OSC_18432:	/* 18.432 MHz */
				b->chan[8].oscfreq = b->chan[9].oscfreq =
					b->chan[10].oscfreq =
					b->chan[11].oscfreq =
					b->chan[12].oscfreq =
					b->chan[13].oscfreq =
					b->chan[14].oscfreq = b->chan[15].oscfreq = mod2 ? 20000000L : 18432000L;
				strcat (b->name, "b");
				break;
		}
	}

	/*
	 * Initialize channel structures.
	 */
	for (i = 0; i < 4; ++i) {
		b->chan[i + 0].port = CS0 (port);
		b->chan[i + 4].port = sigma_probe_chip (CS1A (port)) ? CS1A (port) : CS1 (port);
		b->chan[i + 8].port = CS0 (port + 0x10);
		b->chan[i + 12].port = CS1 (port + 0x10);
	}
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c) {
		c->board = b;
		c->num = c - b->chan;
		c->type = SIGMA_TYPE_NONE;
	}

	/*------------------ Master board -------------------*/

	switch (rev) {
		case CRONYX_400:
			for (i = 4; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_100:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_500:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS232;
			for (i = 4; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_410:
			b->chan[0].type = SIGMA_TYPE_UNIV_V35;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_810:
			b->chan[0].type = SIGMA_TYPE_UNIV_V35;
			for (i = 1; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_410s:
			b->chan[0].type = SIGMA_TYPE_UNIV_V35;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
			break;
		case CRONYX_810s:
			b->chan[0].type = SIGMA_TYPE_UNIV_V35;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
			for (i = 4; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_440:
			b->chan[0].type = SIGMA_TYPE_UNIV_V35;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_V35;
			break;
		case CRONYX_840:
			b->chan[0].type = SIGMA_TYPE_UNIV_V35;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_V35;
			for (i = 4; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_401:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS449;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_801:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS449;
			for (i = 1; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_401s:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS449;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
			break;
		case CRONYX_801s:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS449;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
			for (i = 4; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
		case CRONYX_404:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS449;
			for (i = 1; i < 4; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_RS449;
			break;
		case CRONYX_703:
			b->chan[0].type = SIGMA_TYPE_UNIV_RS449;
			for (i = 1; i < 3; ++i)
				b->chan[i].type = SIGMA_TYPE_SYNC_RS449;
			for (i = 4; i < 8; ++i)
				b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
			break;
	}

	/*------------------ Slave board -------------------*/

	if (chain) {
		switch (rev2) {
			case CRONYX_400:
				break;
			case CRONYX_100:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_500:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS232;
				for (i = 12; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_410:
				b->chan[8].type = SIGMA_TYPE_UNIV_V35;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_810:
				b->chan[8].type = SIGMA_TYPE_UNIV_V35;
				for (i = 9; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_410s:
				b->chan[8].type = SIGMA_TYPE_UNIV_V35;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
				break;
			case CRONYX_810s:
				b->chan[8].type = SIGMA_TYPE_UNIV_V35;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
				for (i = 12; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_440:
				b->chan[8].type = SIGMA_TYPE_UNIV_V35;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_V35;
				break;
			case CRONYX_840:
				b->chan[8].type = SIGMA_TYPE_UNIV_V35;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_V35;
				for (i = 12; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_401:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS449;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_801:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS449;
				for (i = 9; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_401s:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS449;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_801s:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS449;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_RS232;
				for (i = 12; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
			case CRONYX_404:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS449;
				for (i = 9; i < 12; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_RS449;
				break;
			case CRONYX_703:
				b->chan[8].type = SIGMA_TYPE_UNIV_RS449;
				for (i = 9; i < 11; ++i)
					b->chan[i].type = SIGMA_TYPE_SYNC_RS449;
				for (i = 12; i < 16; ++i)
					b->chan[i].type = SIGMA_TYPE_UNIV_RS232;
				break;
		}
	}

	b->nuniv = b->nsync = b->nasync = 0;
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c)
		switch (c->type) {
			case SIGMA_TYPE_ASYNC:
				++b->nasync;
				break;
			case SIGMA_TYPE_UNIV_UNKNOWN:
			case SIGMA_TYPE_UNIV_RS232:
			case SIGMA_TYPE_UNIV_RS449:
			case SIGMA_TYPE_UNIV_V35:
				++b->nuniv;
				break;
			case SIGMA_TYPE_SYNC_RS232:
			case SIGMA_TYPE_SYNC_V35:
			case SIGMA_TYPE_SYNC_RS449:
				++b->nsync;
				break;
		}

	sigma_reinit_board (b);
}

/*
 * Initialize the Sigma-800 board structure.
 */
void sigma_init_800 (sigma_board_t * b, int num, unsigned port, int irq, int dma, int chain)
{
	sigma_chan_t *c;
	int i;

	/*
	 * Initialize board structure.
	 */
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->opt = board_opt_dflt;
	b->type = SIGMA_MODEL_800;

	/*
	 * Set channels 0 and 8 mode, set DMA and IRQ.
	 */
	b->bcr0 = b->bcr0b = dmamask[b->dma] | irqmask[b->irq];

	/*
	 * Clear DTR[0..7] and DTR[8..15].
	 */
	b->bcr1 = b->bcr1b = 0;

	strcpy (b->name, "800");
	if (chain)
		strcat (b->name, "/800");

	/*
	 * Initialize channel structures.
	 */
	for (i = 0; i < 4; ++i) {
		b->chan[i + 0].port = CS0 (port);
		b->chan[i + 4].port = sigma_probe_chip (CS1A (port)) ? CS1A (port) : CS1 (port);
		b->chan[i + 8].port = CS0 (port + 0x10);
		b->chan[i + 12].port = CS1 (port + 0x10);
	}
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c) {
		c->board = b;
		c->num = c - b->chan;
		c->oscfreq = 33000000L;
		c->type = (c->num < 8 || chain) ? SIGMA_TYPE_UNIV_RS232 : SIGMA_TYPE_NONE;
	}

	b->nuniv = b->nsync = b->nasync = 0;
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c)
		switch (c->type) {
			case SIGMA_TYPE_ASYNC:
				++b->nasync;
				break;
			case SIGMA_TYPE_UNIV_UNKNOWN:
			case SIGMA_TYPE_UNIV_RS232:
			case SIGMA_TYPE_UNIV_RS449:
			case SIGMA_TYPE_UNIV_V35:
				++b->nuniv;
				break;
			case SIGMA_TYPE_SYNC_RS232:
			case SIGMA_TYPE_SYNC_V35:
			case SIGMA_TYPE_SYNC_RS449:
				++b->nsync;
				break;
		}

	sigma_reinit_board (b);
}

/*
 * Initialize the Sigma-2x board structure.
 */
void sigma_init_22 (sigma_board_t * b, int num, unsigned port, int irq, int dma, int rev, int osc)
{
	sigma_chan_t *c;
	int i;

	/*
	 * Initialize board structure.
	 */
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->opt = board_opt_dflt;

	b->type = SIGMA_MODEL_22;

	/*
	 * Set channels 0 and 8 mode, set DMA and IRQ.
	 */
	b->bcr0 = BCR0_NORESET | dmamask[b->dma] | irqmask[b->irq];
	if (b->type == SIGMA_MODEL_22 && b->opt.fast)
		b->bcr0 |= BCR02X_FAST;

	/*
	 * Clear DTR[0..3] and DTR[8..12].
	 */
	b->bcr1 = 0;

	/*
	 * Initialize channel structures.
	 */
	for (i = 0; i < 4; ++i) {
		b->chan[i + 0].port = CS0 (port);
		b->chan[i + 4].port = CS1 (port);
		b->chan[i + 8].port = CS0 (port + 0x10);
		b->chan[i + 12].port = CS1 (port + 0x10);
	}
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c) {
		c->board = b;
		c->num = c - b->chan;
		c->type = SIGMA_TYPE_NONE;
		c->oscfreq = (osc & BSR2X_OSC_33) ? 33000000L : 20000000L;
	}

	/*
	 * Check the board revision code.
	 */
	strcpy (b->name, "22");
	b->chan[0].type = SIGMA_TYPE_UNIV_UNKNOWN;
	b->chan[1].type = SIGMA_TYPE_UNIV_UNKNOWN;
	b->nsync = b->nasync = 0;
	b->nuniv = 2;
	if (rev == CRONYX_24) {
		strcpy (b->name, "24");
		b->chan[2].type = SIGMA_TYPE_UNIV_UNKNOWN;
		b->chan[3].type = SIGMA_TYPE_UNIV_UNKNOWN;
		b->nuniv += 2;
	}
	strcat (b->name, (osc & BSR2X_OSC_33) ? "c" : "a");
	sigma_reinit_board (b);
}

/*
 * Reinitialize all channels, using new options and baud rate.
 */
void sigma_reinit_board (sigma_board_t * b)
{
	sigma_chan_t *c;

	b->opt = board_opt_dflt;
	if (b->type == SIGMA_MODEL_22) {
		b->bcr0 &= ~BCR02X_FAST;
		if (b->opt.fast)
			b->bcr0 |= BCR02X_FAST;
	} else
		b->if0type = b->if8type = sigma_iftype;
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c) {
		switch (c->type) {
			default:
			case SIGMA_TYPE_NONE:
				continue;
			case SIGMA_TYPE_UNIV_UNKNOWN:
			case SIGMA_TYPE_UNIV_RS232:
			case SIGMA_TYPE_UNIV_RS449:
			case SIGMA_TYPE_UNIV_V35:
				c->mode = (sigma_univ_mode == SIGMA_MODE_ASYNC) ? SIGMA_MODE_ASYNC : sigma_sync_mode;
				break;
			case SIGMA_TYPE_SYNC_RS232:
			case SIGMA_TYPE_SYNC_V35:
			case SIGMA_TYPE_SYNC_RS449:
				c->mode = sigma_sync_mode;
				break;
			case SIGMA_TYPE_ASYNC:
				c->mode = SIGMA_MODE_ASYNC;
				break;
		}
		c->rxbaud = sigma_rxbaud;
		c->txbaud = sigma_txbaud;
		c->opt = chan_opt_dflt;
		c->aopt = opt_async_dflt;
		c->hopt = opt_hdlc_dflt;
	}
}

/*
 * Set up the board.
 */
int sigma_setup_board (sigma_board_t * b, const u8 *firmware, u32 bits, const sigma_dat_tst_t * tst)
{
	int i;

#if 0 && ! defined (NDIS_MINIPORT_DRIVER)
	/*
	 * Disable DMA channel.
	 */
	ddk_outb (DMA_MASK, (b->dma & 3) | DMA_MASK_CLEAR);
#endif
	/*
	 * Reset the controller.
	 */
	ddk_outb (BCR0 (b->port), 0);
	if (b->chan[8].type || b->chan[12].type)
		ddk_outb (BCR0 (b->port + 0x10), 0);

	/*
	 * Load the firmware.
	 */
	if (b->type == SIGMA_MODEL_800) {
		/*
		 * Reset the controllers.
		 */
		ddk_outb (BCR2 (b->port), BCR2_TMS);
		if (b->chan[8].type || b->chan[12].type)
			ddk_outb (BCR2 (b->port + 0x10), BCR2_TMS);
		ddk_outb (BCR2 (b->port), 0);
		if (b->chan[8].type || b->chan[12].type)
			ddk_outb (BCR2 (b->port + 0x10), 0);

		if (firmware &&
		    (!sigma_download (b->port, firmware, bits, tst) ||
		     ((b->chan[8].type || b->chan[12].type) && !sigma_download (b->port + 0x10, firmware, bits, tst))))
			return (0);
	}

	/*
	 * Set channels 0 and 8 to RS232 async. mode.
	 * Enable DMA and IRQ.
	 */
	ddk_outb (BCR0 (b->port), b->bcr0);
	if (b->chan[8].type || b->chan[12].type)
		ddk_outb (BCR0 (b->port + 0x10), b->bcr0b);

	/*
	 * Clear DTR[0..3] and DTR[8..12].
	 */
	ddk_outw (BCR1 (b->port), b->bcr1);
	if (b->chan[8].type || b->chan[12].type)
		ddk_outw (BCR1 (b->port + 0x10), b->bcr1b);

	if (b->type == SIGMA_MODEL_800)
		ddk_outb (BCR2 (b->port), b->opt.fast & (BCR2_BUS0 | BCR2_BUS1));

	/*
	 * Initialize all controllers.
	 */
	for (i = 0; i < SIGMA_NCHAN; i += 4)
		if (b->chan[i].type != SIGMA_TYPE_NONE)
			sigma_setup_chip (b->chan + i);
#if 0 && ! defined (NDIS_MINIPORT_DRIVER)
	/*
	 * Set up DMA channel to master mode.
	 */
	ddk_outb (DMA_MODE, (b->dma & 3) | DMA_MODE_MASTER);

	/*
	 * Enable DMA channel.
	 */
	ddk_outb (DMA_MASK, b->dma & 3);
#endif
	/*
	 * Initialize all channels.
	 */
	for (i = 0; i < SIGMA_NCHAN; ++i)
		if (b->chan[i].type != SIGMA_TYPE_NONE)
			sigma_setup_chan (b->chan + i);
	return (1);
}

/*
 * Initialize the board.
 */
static void sigma_setup_chip (sigma_chan_t * c)
{
	/*
	 * Reset the chip.
	 */
	sigma_reset (c->port);

	/*
	 * Set all interrupt level registers to the same value.
	 * This enables the internal CD2400 priority scheme.
	 */
	ddk_outb (RPILR (c->port), BRD_INTR_LEVEL);
	ddk_outb (TPILR (c->port), BRD_INTR_LEVEL);
	ddk_outb (MPILR (c->port), BRD_INTR_LEVEL);

	/*
	 * Set bus error count to zero.
	 */
	ddk_outb (BERCNT (c->port), 0);

	/*
	 * Set 16-bit DMA mode.
	 */
	ddk_outb (DMR (c->port), 0);

	/*
	 * Set timer period register to 1 msec (approximately).
	 */
	ddk_outb (TPR (c->port), 10);
}

/*
 * Initialize the CD2400 channel.
 */
void sigma_update_chan (sigma_chan_t * c)
{
	int clock, period;

	if (c->board->type == SIGMA_MODEL_OLD)
		switch (c->num) {
			case 0:
				c->board->bcr0 &= ~BCR0_UMASK;
				if (c->mode != SIGMA_MODE_ASYNC)
					c->board->bcr0 |= BCR0_UM_SYNC;
				if (c->board->if0type && (c->type == SIGMA_TYPE_UNIV_RS449 || c->type == SIGMA_TYPE_UNIV_V35))
					c->board->bcr0 |= BCR0_UI_RS449;
				ddk_outb (BCR0 (c->board->port), c->board->bcr0);
				break;
			case 8:
				c->board->bcr0b &= ~BCR0_UMASK;
				if (c->mode != SIGMA_MODE_ASYNC)
					c->board->bcr0b |= BCR0_UM_SYNC;
				if (c->board->if8type && (c->type == SIGMA_TYPE_UNIV_RS449 || c->type == SIGMA_TYPE_UNIV_V35))
					c->board->bcr0b |= BCR0_UI_RS449;
				ddk_outb (BCR0 (c->board->port + 0x10), c->board->bcr0b);
				break;
		}

	/*
	 * set current channel number
	 */
	ddk_outb (CAR (c->port), c->num & 3);

	switch (c->mode) {	/* initialize the channel mode */
		case SIGMA_MODE_ASYNC:
			/*
			 * set receiver timeout register
			 */
			ddk_outw (RTPR (c->port), 10);	/* 10 msec, see TPR */
			c->opt.rcor.encod = CD2400_ENCOD_NRZ;

			ddk_outb (CMR (c->port), CMR_RXDMA | CMR_TXDMA | CMR_ASYNC);
			ddk_outb (COR1 (c->port), BYTE c->aopt.cor1);
			ddk_outb (COR2 (c->port), BYTE c->aopt.cor2);
			ddk_outb (COR3 (c->port), BYTE c->aopt.cor3);
			ddk_outb (COR6 (c->port), BYTE c->aopt.cor6);
			ddk_outb (COR7 (c->port), BYTE c->aopt.cor7);
			ddk_outb (SCHR1 (c->port), c->aopt.schr1);
			ddk_outb (SCHR2 (c->port), c->aopt.schr2);
			ddk_outb (SCHR3 (c->port), c->aopt.schr3);
			ddk_outb (SCHR4 (c->port), c->aopt.schr4);
			ddk_outb (SCRL (c->port), c->aopt.scrl);
			ddk_outb (SCRH (c->port), c->aopt.scrh);
			ddk_outb (LNXT (c->port), c->aopt.lnxt);
			break;
		case SIGMA_MODE_HDLC:
			ddk_outb (CMR (c->port), CMR_RXDMA | CMR_TXDMA | CMR_HDLC);
			ddk_outb (COR1 (c->port), BYTE c->hopt.cor1);
			ddk_outb (COR2 (c->port), BYTE c->hopt.cor2);
			ddk_outb (COR3 (c->port), BYTE c->hopt.cor3);
			ddk_outb (RFAR1 (c->port), c->hopt.rfar1);
			ddk_outb (RFAR2 (c->port), c->hopt.rfar2);
			ddk_outb (RFAR3 (c->port), c->hopt.rfar3);
			ddk_outb (RFAR4 (c->port), c->hopt.rfar4);
			ddk_outb (CPSR (c->port), c->hopt.cpsr);
			break;
	}

	/*
	 * set mode-independent options
	 */
	ddk_outb (COR4 (c->port), BYTE c->opt.cor4);
	ddk_outb (COR5 (c->port), BYTE c->opt.cor5);

	/*
	 * set up receiver clock values
	 */
	if (c->mode == SIGMA_MODE_ASYNC || c->opt.rcor.dpll || c->opt.tcor.llm) {
		sigma_clock (c->oscfreq, c->rxbaud, &clock, &period);
		c->opt.rcor.clk = clock;
	} else {
		c->opt.rcor.clk = CD2400_CLK_EXT;
		period = 1;
	}
	ddk_outb (RCOR (c->port), BYTE c->opt.rcor);
	ddk_outb (RBPR (c->port), period);

	/*
	 * set up transmitter clock values
	 */
	if (c->mode == SIGMA_MODE_ASYNC || !c->opt.tcor.ext1x) {
		unsigned ext1x = c->opt.tcor.ext1x;

		c->opt.tcor.ext1x = 0;
		sigma_clock (c->oscfreq, c->txbaud, &clock, &period);
		c->opt.tcor.clk = clock;
		c->opt.tcor.ext1x = ext1x;
	} else {
		c->opt.tcor.clk = CD2400_CLK_EXT;
		period = 1;
	}
	ddk_outb (TCOR (c->port), BYTE c->opt.tcor);
	ddk_outb (TBPR (c->port), period);
}

/*
 * Initialize the CD2400 channel.
 */
void sigma_setup_chan (sigma_chan_t * c)
{
	/*
	 * set current channel number
	 */
	ddk_outb (CAR (c->port), c->num & 3);

	/*
	 * reset the channel
	 */
	sigma_cmd (c->port, CCR_CLRCH);

	/*
	 * set LIVR to contain the board and channel numbers
	 */
	ddk_outb (LIVR (c->port), c->board->num << 6 | c->num << 2);

	/*
	 * clear DTR, RTS, set TXCout/DTR pin
	 */
	ddk_outb (MSVR_RTS (c->port), 0);
	ddk_outb (MSVR_DTR (c->port), c->mode == SIGMA_MODE_ASYNC ? 0 : MSV_TXCOUT);

	/*
	 * set receiver A buffer physical address
	 */
	ddk_outw (ARBADRU (c->port), (u16) (c->arphys >> 16));
	ddk_outw (ARBADRL (c->port), (u16) c->arphys);

	/*
	 * set receiver B buffer physical address
	 */
	ddk_outw (BRBADRU (c->port), (u16) (c->brphys >> 16));
	ddk_outw (BRBADRL (c->port), (u16) c->brphys);

	/*
	 * set transmitter A buffer physical address
	 */
	ddk_outw (ATBADRU (c->port), (u16) (c->atphys >> 16));
	ddk_outw (ATBADRL (c->port), (u16) c->atphys);

	/*
	 * set transmitter B buffer physical address
	 */
	ddk_outw (BTBADRU (c->port), (u16) (c->btphys >> 16));
	ddk_outw (BTBADRL (c->port), (u16) c->btphys);

	c->dtr = 0;
	c->rts = 0;

	sigma_update_chan (c);
}

/*
 * Control DTR signal for the channel.
 * Turn it on/off.
 */
void sigma_set_dtr (sigma_chan_t * c, int on)
{
	sigma_board_t *b = c->board;

	c->dtr = on ? 1 : 0;

	if (b->type == SIGMA_MODEL_22) {
		if (on)
			b->bcr1 |= BCR1_DTR (c->num);
		else
			b->bcr1 &= ~BCR1_DTR (c->num);
		ddk_outw (BCR1 (b->port), b->bcr1);
		return;
	}
	if (b->type == SIGMA_MODEL_800) {
		if (c->num >= 8) {
			if (on)
				b->bcr1b |= BCR1800_DTR (c->num);
			else
				b->bcr1b &= ~BCR1800_DTR (c->num);
			ddk_outb (BCR1 (b->port + 0x10), b->bcr1b);
		} else {
			if (on)
				b->bcr1 |= BCR1800_DTR (c->num);
			else
				b->bcr1 &= ~BCR1800_DTR (c->num);
			ddk_outb (BCR1 (b->port), b->bcr1);
		}
		return;
	}
	if (c->mode == SIGMA_MODE_ASYNC) {
		ddk_outb (CAR (c->port), c->num & 3);
		ddk_outb (MSVR_DTR (c->port), on ? MSV_DTR : 0);
		return;
	}

	switch (c->num) {
		default:
			/*
			 * Channels 4..7 and 12..15 in syncronous mode
			 * * have no DTR signal.
			 */
			break;

		case 1:
		case 2:
		case 3:
			if (c->type == SIGMA_TYPE_UNIV_RS232)
				break;
		case 0:
			if (on)
				b->bcr1 |= BCR1_DTR (c->num);
			else
				b->bcr1 &= ~BCR1_DTR (c->num);
			ddk_outw (BCR1 (b->port), b->bcr1);
			break;

		case 9:
		case 10:
		case 11:
			if (c->type == SIGMA_TYPE_UNIV_RS232)
				break;
		case 8:
			if (on)
				b->bcr1b |= BCR1_DTR (c->num & 3);
			else
				b->bcr1b &= ~BCR1_DTR (c->num & 3);
			ddk_outw (BCR1 (b->port + 0x10), b->bcr1b);
			break;
	}
}

/*
 * Control RTS signal for the channel.
 * Turn it on/off.
 */
void sigma_set_rts (sigma_chan_t * c, int on)
{
	c->rts = on ? 1 : 0;
	ddk_outb (CAR (c->port), c->num & 3);
	ddk_outb (MSVR_RTS (c->port), on ? MSV_RTS : 0);
}

/*
 * Get the state of DSR signal of the channel.
 */
int sigma_get_dsr (sigma_chan_t * c)
{
	u8 sigval;

	if (c->board->type == SIGMA_MODEL_22 || c->board->type == SIGMA_MODEL_800 || c->mode == SIGMA_MODE_ASYNC) {
		ddk_outb (CAR (c->port), c->num & 3);
		return (ddk_inb (MSVR (c->port)) & MSV_DSR ? 1 : 0);
	}

	/*
	 * Channels 4..7 and 12..15 don't have DSR signal available.
	 */
	switch (c->num) {
		default:
			return (1);

		case 1:
		case 2:
		case 3:
			if (c->type == SIGMA_TYPE_UNIV_RS232)
				return (1);
		case 0:
			sigval = ddk_inw (BSR (c->board->port)) >> 8;
			break;

		case 9:
		case 10:
		case 11:
			if (c->type == SIGMA_TYPE_UNIV_RS232)
				return (1);
		case 8:
			sigval = ddk_inw (BSR (c->board->port + 0x10)) >> 8;
			break;
	}
	return (~sigval >> (c->num & 3) & 1);
}

/*
 * Get the state of CARRIER signal of the channel.
 */
int sigma_get_cd (sigma_chan_t * c)
{
	u8 sigval;

	if (c->board->type == SIGMA_MODEL_22 || c->board->type == SIGMA_MODEL_800 || c->mode == SIGMA_MODE_ASYNC) {
		ddk_outb (CAR (c->port), c->num & 3);
		return (ddk_inb (MSVR (c->port)) & MSV_CD ? 1 : 0);
	}

	/*
	 * Channels 4..7 and 12..15 don't have CD signal available.
	 */
	switch (c->num) {
		default:
			return (1);

		case 1:
		case 2:
		case 3:
			if (c->type == SIGMA_TYPE_UNIV_RS232)
				return (1);
		case 0:
			sigval = ddk_inw (BSR (c->board->port)) >> 8;
			break;

		case 9:
		case 10:
		case 11:
			if (c->type == SIGMA_TYPE_UNIV_RS232)
				return (1);
		case 8:
			sigval = ddk_inw (BSR (c->board->port + 0x10)) >> 8;
			break;
	}
	return (~sigval >> 4 >> (c->num & 3) & 1);
}

/*
 * Get the state of CTS signal of the channel.
 */
int sigma_get_cts (sigma_chan_t * c)
{
	ddk_outb (CAR (c->port), c->num & 3);
	return (ddk_inb (MSVR (c->port)) & MSV_CTS ? 1 : 0);
}

/*
 * Compute CD2400 clock values.
 */
void sigma_clock (u32 hz, u32 ba, int *clk, int *div)
{
	static unsigned clocktab[] = { 8, 32, 128, 512, 2048, 0 };

	for (*clk = 0; clocktab[*clk]; ++*clk) {
		u32 c = ba * clocktab[*clk];

		if (hz <= c * 256) {
			*div = (2 * hz + c) / (2 * c) - 1;
			return;
		}
	}
	/*
	 * Incorrect baud rate.  Return some meaningful values.
	 */
	*clk = 0;
	*div = 255;
}

/*
 * Turn LED on/off.
 */
void sigma_led (sigma_board_t * b, int on)
{
	switch (b->type) {
		case SIGMA_MODEL_22:
			if (on)
				b->bcr0 |= BCR02X_LED;
			else
				b->bcr0 &= ~BCR02X_LED;
			ddk_outb (BCR0 (b->port), b->bcr0);
			break;
	}
}

#if 0 && ! defined (NDIS_MINIPORT_DRIVER)
void sigma_disable_dma (sigma_board_t * b)
{
	/*
	 * Disable DMA channel.
	 */
	ddk_outb (DMA_MASK, (b->dma & 3) | DMA_MASK_CLEAR);
}
#endif

sigma_board_opt_t board_opt_dflt = {	/* board options */
	SIGMA_BUS_NORMAL,	/* normal bus master timing */
};

sigma_chan_opt_t chan_opt_dflt = {	/* mode-independent options */
	{			/* cor4 */
	 7,			/* FIFO threshold, odd is better */
	 0,
	 0,			/* don't detect 1 to 0 on CTS */
	 1,			/* detect 1 to 0 on CD */
	 0,			/* detect 1 to 0 on DSR */
	 },
	{			/* cor5 */
	 0,			/* receive flow control FIFO threshold */
	 0,
	 0,			/* don't detect 0 to 1 on CTS */
	 1,			/* detect 0 to 1 on CD */
	 0,			/* detect 0 to 1 on DSR */
	 },
	{			/* rcor */
	 0,			/* dummy clock source */
	 CD2400_ENCOD_NRZ,	/* NRZ mode */
	 0,			/* disable DPLL */
	 0,
	 0,			/* transmit line value */
	 },
	{			/* tcor */
	 0,
	 0,			/* local loopback mode */
	 0,
	 1,			/* external 1x clock mode */
	 0,
	 0,			/* dummy transmit clock source */
	 },
};

sigma_opt_async_t opt_async_dflt = {	/* default async options */
	{			/* cor1 */
	 8 - 1,			/* 8-bit char length */
	 0,			/* don't ignore parity */
	 SIGMA_PAR_NONE,	/* no parity */
	 SIGMA_PAR_EVEN,	/* even parity */
	 },
	{			/* cor2 */
	 0,			/* disable automatic DSR */
	 1,			/* enable automatic CTS */
	 0,			/* disable automatic RTS */
	 0,			/* no remote loopback */
	 0,
	 0,			/* disable embedded cmds */
	 0,			/* disable XON/XOFF */
	 0,			/* disable XANY */
	 },
	{			/* cor3 */
	 STOPB_1,		/* 1 stop bit */
	 0,
	 0,			/* disable special char detection */
	 FLOWCC_PASS,		/* pass flow ctl chars to the host */
	 0,			/* range detect disable */
	 0,			/* disable extended spec. char detect */
	 },
	{			/* cor6 */
	 ERR_INTR,		/* generate exception on parity errors */
	 BRK_INTR,		/* generate exception on break condition */
	 0,			/* don't translate NL to CR on input */
	 0,			/* don't translate CR to NL on input */
	 0,			/* don't discard CR on input */
	 },
	{			/* cor7 */
	 0,			/* don't translate CR to NL on output */
	 0,			/* don't translate NL to CR on output */
	 0,
	 0,			/* don't process flow ctl err chars */
	 0,			/* disable LNext option */
	 0,			/* don't strip 8 bit on input */
	 },
	0, 0, 0, 0, 0, 0, 0,	/* clear schr1-4, scrl, scrh, lnxt */
};

sigma_opt_hdlc_t opt_hdlc_dflt = {	/* default hdlc options */
	{			/* cor1 */
	 2,			/* 2 inter-frame flags */
	 0,			/* no-address mode */
	 SIGMA_CLRDET_DISABLE,	/* disable clear detect */
	 SIGMA_AFLO_1OCT,	/* 1-byte address field length */
	 },
	{			/* cor2 */
	 0,			/* disable automatic DSR */
	 0,			/* disable automatic CTS */
	 0,			/* disable automatic RTS */
	 0,
	 SIGMA_CRC_INVERT,	/* use CRC V.41 */
	 0,
	 SIGMA_FCS_NOTPASS,	/* don't pass received CRC to the host */
	 0,
	 },
	{			/* cor3 */
	 0,			/* 0 pad characters sent */
	 SIGMA_IDLE_FLAG,	/* idle in flag */
	 0,			/* enable FCS */
	 SIGMA_FCSP_ONES,	/* FCS preset to all ones (V.41) */
	 SIGMA_SYNC_AA,		/* use AAh as sync char */
	 0,			/* disable pad characters */
	 },
	0, 0, 0, 0,		/* clear rfar1-4 */
	SIGMA_POLY_V41,		/* use V.41 CRC polynomial */
};

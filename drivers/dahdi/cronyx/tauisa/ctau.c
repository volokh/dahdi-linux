/*
 * DDK for Cronyx Tau-ISA adapter.
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
//#define DMA_MODE_MASTER 0xc0	/* DMA master mode */

#define BYTE *(unsigned char*)&

static int valid (short value, short *list)
{
	while (*list)
		if (value == *list++)
			return 1;
	return 0;
}

static int tauisa_download (unsigned short port, const unsigned char *firmware, long bits, const tauisa_dat_tst_t * tst)
{
	unsigned char cr1, sr2;
	long i, n, maxn = (bits + 7) >> 3;
	int v, b;

	ddk_inb (BSR3 (port));
	for (i = n = 0; n < maxn; ++n) {
		v = ((firmware[n] ^ ' ') << 1) | ((firmware[n] >> 7) & 1);
		for (b = 0; b < 7; b += 2, i += 2) {
			if (i >= bits)
				break;
			cr1 = 0;
			if (v >> b & 1)
				cr1 |= BCR1_TMS;
			if (v >> b & 2)
				cr1 |= BCR1_TDI;
			ddk_outb (BCR1 (port), cr1);
			sr2 = ddk_inb (BSR2 (port));
			ddk_outb (BCR0 (port), BCR0_TCK);
			ddk_outb (BCR0 (port), 0);
			if (i >= tst->end)
				++tst;
			if (i >= tst->start && (sr2 & BSR2_LERR))
				return (0);
		}
	}
	return (1);
}

/*
 * Firmware unpack algorithm.
 */
typedef struct {
	const unsigned char *ptr;
	unsigned char byte;
	unsigned char count;
} unpack_t;

static unsigned short unpack_init (unpack_t * t, const unsigned char *ptr)
{
	unsigned short len;

	len = *ptr++;
	len += *ptr++ << 8;
	t->ptr = ptr;
	t->byte = 0;
	t->count = 0;
	return len;
}

static unsigned char unpack_getchar (unpack_t * t)
{
	if (t->count > 0) {
		--t->count;
		return t->byte;
	}
	t->byte = *t->ptr++;
	if (t->byte == 0)
		t->count = *t->ptr++;
	return t->byte;
}

/*
 * Firmware download signals.
 */
#define nstatus(b)	(ddk_inb(BSR3(b)) & BSR3_NSTATUS)

#define confdone(b)	(ddk_inb(BSR3(b)) & BSR3_CONF_DN)

#define nconfig_set(b)	ddk_outb (bcr1_port, (bcr1 &= ~BCR1_NCONFIGI))
#define nconfig_clr(b)	ddk_outb (bcr1_port, (bcr1 |= BCR1_NCONFIGI))

#define dclk_tick(b) 	ddk_outb (BCR3(b), 0)

#define putbit(b,bit) { if (bit) bcr1 |= BCR1_1KDAT; \
			else bcr1 &= ~BCR1_1KDAT; \
			ddk_outb (bcr1_port, bcr1); \
			dclk_tick (b); }

#define TAUISA_DEBUG(x)		/*trace_str x */

static int tauisa_download2 (unsigned short port, const unsigned char *fwaddr)
{
	unsigned short bytes;
	unsigned char bcr1, val;
	unsigned short bcr1_port;
	unpack_t t;

	/*
	 * Set NCONFIG and wait until NSTATUS is set.
	 */
	bcr1_port = BCR1 (port);
	bcr1 = 0;
	nconfig_set (port);
	for (val = 0; val < 255; ++val)
		if (nstatus (port))
			break;

	/*
	 * Clear NCONFIG, wait 2 usec and check that NSTATUS is cleared.
	 */
	nconfig_clr (port);
	for (val = 0; val < 2 * 3; ++val)
		nconfig_clr (port);
	if (nstatus (port)) {
		TAUISA_DEBUG (("Bad nstatus, downloading aborted (bsr3=0x%x).\n", ddk_inb (BSR3 (port))));
		nconfig_set (port);
		return 0;
	}

	/*
	 * Set NCONFIG and wait 5 usec.
	 */
	nconfig_set (port);
	for (val = 0; val < 5 * 3; ++val)	/* Delay 5 msec. */
		nconfig_set (port);

	/*
	 * С адреса `fwaddr' в памяти должны лежать упакованные данные
	 * для загрузки firmware. Значение должно быть согласовано с параметром
	 * вызова утилиты `megaprog' в скрипте загрузки (и Makefile).
	 */
	bytes = unpack_init (&t, fwaddr);
	for (; bytes > 0; --bytes) {
		val = unpack_getchar (&t);

		if (nstatus (port) == 0) {
			TAUISA_DEBUG (("Bad nstatus, %d bytes remaining.\n", bytes));
			goto failed;
		}

		if (confdone (port)) {
			/*
			 * Ten extra clocks. Hope 50 is enough.
			 */
			for (val = 0; val < 50; ++val)
				dclk_tick (port);

			if (nstatus (port) == 0) {
				TAUISA_DEBUG (("Bad nstatus after confdone, %d bytes remaining (%d).\n", bytes,
					       t.ptr - fwaddr));
				goto failed;
			}

			/*
			 * Succeeded.
			 */
			/*
			 * TAUISA_DEBUG (("Download succeeded.\n"));
			 */
			return 1;
		}

		putbit (port, val & 0x01);
		putbit (port, val & 0x02);
		putbit (port, val & 0x04);
		putbit (port, val & 0x08);
		putbit (port, val & 0x10);
		putbit (port, val & 0x20);
		putbit (port, val & 0x40);
		putbit (port, val & 0x80);

		/*
		 * if ((bytes & 1023) == 0) putch ('.');
		 */
	}

	TAUISA_DEBUG (("Bad confdone.\n"));
failed:
	TAUISA_DEBUG (("Downloading aborted.\n"));
	return 0;
}

/*
 * Detect Tau2 adapter.
 */
static int tauisa_probe2_board (unsigned short port)
{
	unsigned char sr3, osr3;
	int i;

	if (!valid (port, tauisa_porttab))
		return 0;

	osr3 = ddk_inb (BSR3 (port));
	if ((osr3 & (BSR3_IB | BSR3_IB_NEG)) != BSR3_IB && (osr3 & (BSR3_IB | BSR3_IB_NEG)) != BSR3_IB_NEG)
		return (0);
	for (i = 0; i < 100; ++i) {
		/*
		 * Do it twice
		 */
		sr3 = ddk_inb (BSR3 (port));
		sr3 = ddk_inb (BSR3 (port));
		if (((sr3 ^ osr3) & (BSR3_IB | BSR3_IB_NEG | BSR3_ZERO)) != (BSR3_IB | BSR3_IB_NEG))
			return (0);
		osr3 = sr3;
	}
	/*
	 * Reset the controller.
	 */
	ddk_outb (BCR0 (port), 0);
	return 1;
}

/*
 * Check if the Tau board is present at the given base port.
 * Read board status register 1 and check identification bits
 * which should invert every next read.
 * The "zero" bit should remain stable.
 */
int tauisa_probe_board (unsigned short port, int irq, int dma)
{
	unsigned char sr3, osr3;
	int i;

	if (!valid (port, tauisa_porttab))
		return 0;

	if ((irq > 0) && (!valid (irq, tauisa_irqtab)))
		return 0;

	if ((dma > 0) && (!valid (dma, tauisa_dmatab)))
		return 0;

	osr3 = ddk_inb (BSR3 (port));
	if ((osr3 & (BSR3_IB | BSR3_IB_NEG)) != BSR3_IB && (osr3 & (BSR3_IB | BSR3_IB_NEG)) != BSR3_IB_NEG)
		return (0);
	for (i = 0; i < 100; ++i) {
		sr3 = ddk_inb (BSR3 (port));
		if (((sr3 ^ osr3) & (BSR3_IB | BSR3_IB_NEG | BSR3_ZERO)) != (BSR3_IB | BSR3_IB_NEG))
			return tauisa_probe2_board (port);
		osr3 = sr3;
	}
	/*
	 * Reset the controller.
	 */
	ddk_outb (BCR0 (port), 0);
	return (1);
}

/*
 * Check that the irq is functional.
 * irq>0  - activate the interrupt from the adapter (irq=on)
 * irq<=0  - deactivate the interrupt (irq=off) and free the interrupt line (irq=tri-state)
 */
void tauisa_probe_irq (tauisa_board_t * b, int irq)
{

	if (irq > 0) {
		ddk_outb (BCR0 (b->port), BCR0_HDRUN | irqmask[irq]);
		ddk_outb (R (b->port, HD_TEPR_0R), 0);
		ddk_outw (R (b->port, HD_TCONR_0R), 1);
		ddk_outw (R (b->port, HD_TCNT_0R), 0);
		ddk_outb (R (b->port, HD_TCSR_0R), TCSR_ENABLE | TCSR_INTR);
		ddk_outb (IER2 (b->port), IER2_RX_TME_0);
	} else {
		ddk_outb (BCR0 (b->port), b->bcr0);
		ddk_outb (IER0 (b->port), 0);
		ddk_outb (IER1 (b->port), 0);
		ddk_outb (IER2 (b->port), 0);
		ddk_outb (R (b->port, HD_TCSR_0R), 0);
		cte_out (E1CS0 (b->port), DS_IMR2, 0);
		cte_out (E1CS1 (b->port), DS_IMR2, 0);
	}
}

void tauisa_init_board (tauisa_board_t * b, int num, unsigned short port, int irq, int dma, int type, long osc)
{
	int i;

	/*
	 * Initialize board structure.
	 */
	b->type = type;
	b->port = port;
	b->num = num;
	b->irq = irq;
	b->dma = dma;
	b->osc = osc;

	/*
	 * Get the board type.
	 */
	switch (b->type) {
		default:
			strcpy (b->name, "Tau-ISA/?");
			break;
		case TAUISA_MODEL_BASIC:
			strcpy (b->name, "Tau-ISA");
			break;
		case TAUISA_MODEL_E1:
			strcpy (b->name, "Tau-ISA/E1");
			break;
		case TAUISA_MODEL_E1_c:
			strcpy (b->name, "Tau-ISA/E1+");
			break;
		case TAUISA_MODEL_E1_d:
			strcpy (b->name, "Tau-ISA/E1+phony");
			break;
		case TAUISA_MODEL_G703:
			strcpy (b->name, "Tau-ISA/G703");
			break;
		case TAUISA_MODEL_G703_d:
			strcpy (b->name, "Tau-ISA/G703+");
			break;
		case TAUISA_MODEL_2BASIC:
			strcpy (b->name, "Tau2-ISA");
			break;
		case TAUISA_MODEL_2E1:
			strcpy (b->name, "Tau2-ISA/E1");
			break;
		case TAUISA_MODEL_2E1_d:
			strcpy (b->name, "Tau2-ISA/E1+phony");
			break;
		case TAUISA_MODEL_2G703:
			strcpy (b->name, "Tau2-ISA/G703");
			break;
	}

	/*
	 * Set DMA and IRQ.
	 */
	b->bcr0 = BCR0_HDRUN | dmamask[b->dma] | irqmask[b->irq];

	/*
	 * Clear DTR[0..1].
	 */
	b->bcr1 = 0;
	b->e1cfg = 0;

	/*
	 * Initialize channel structures.
	 */
	for (i = 0; i < TAUISA_NCHAN; ++i)
		tauisa_init_chan (b, i);
	tauisa_reinit_board (b);
}

/*
 * Initialize the board structure.
 */
void
tauisa_init (tauisa_board_t * b, int num, unsigned short port, int irq, int dma,
	     const unsigned char *firmware, long bits, const tauisa_dat_tst_t * tst, const unsigned char *firmware2)
{
	static long tlen = 182;
	static tauisa_dat_tst_t tvec[] = { {114, 178}, {182, 182} };
	static tauisa_dat_tst_t tvec2[] = { {50, 178}, {182, 182} };
	static unsigned char tau[] = { 155, 153, 113, 48, 64, 236,
		48, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49, 49,
		49, 183,
	};
	static unsigned char e1[] = { 155, 153, 113, 48, 64, 236,
		112, 37, 49, 37, 33, 116, 101, 100, 112, 37, 49, 37, 33, 116,
		101, 100,
		230,
	};
	static unsigned char e1_2[] = { 155, 153, 113, 48, 64, 236,
		112, 37, 49, 37, 33, 116, 101, 100, 96, 97, 53, 49, 49, 96,
		97, 100, 230,
	};
	static unsigned char e1_3[] = { 155, 153, 113, 48, 64, 236,
		96, 97, 53, 49, 49, 96, 97, 100, 96, 97, 53, 49, 49, 96, 97,
		100, 230,
	};
	static unsigned char e1_4[] = { 155, 153, 113, 48, 64, 236,
		96, 97, 53, 49, 49, 96, 97, 100, 112, 37, 49, 37, 33, 116,
		101, 100, 230,
	};
	static unsigned char g703[] = { 155, 153, 113, 48, 64, 236,
		112, 37, 49, 37, 33, 116, 101, 32, 117, 37, 49, 37, 33, 116,
		101, 100,
		230,
	};
	static unsigned char g703_2[] = { 155, 153, 113, 48, 64, 236,
		112, 37, 49, 37, 33, 116, 101, 32, 101, 97, 53, 49, 49, 96,
		97, 100, 230,
	};
	static unsigned char g703_3[] = { 155, 153, 113, 48, 64, 236,
		96, 97, 53, 49, 49, 96, 97, 32, 101, 97, 53, 49, 49, 96, 97,
		100, 230,
	};
	static unsigned char g703_4[] = { 155, 153, 113, 48, 64, 236,
		96, 97, 53, 49, 49, 96, 97, 32, 117, 37, 49, 37, 33, 116, 101,
		100, 230,
	};

	int type = TAUISA_MODEL_BASIC;
	long osc = (ddk_inb (BSR3 (port)) & BSR3_ZERO) ? 8192000 : 10000000;

	/*
	 * Get the board type.
	 */
	if (tauisa_probe2_board (port) && tauisa_download2 (port, firmware2)) {
		/*
		 * Tau2, 1k30-based model
		 */
		unsigned char sr0 = ddk_inb (BSR0 (port));

		if (!(sr0 & BSR0_T703))
			type = TAUISA_MODEL_2G703;
		else if (sr0 & BSR0_TE1)
			type = TAUISA_MODEL_2BASIC;
		else if (ddk_inb (E1SR (port)) & E1SR_REV)
			type = TAUISA_MODEL_2E1_d;
		else
			type = TAUISA_MODEL_2E1;
	} else if (tauisa_download (port, tau, tlen, tvec)) {
		if (!tauisa_download (port, firmware, bits, tst))
			type = TAUISA_MODEL_BASIC;
		else {
			unsigned char sr0 = ddk_inb (BSR0 (port));

			if (!(sr0 & BSR0_T703))
				type = TAUISA_MODEL_G703_d;
			else if (sr0 & BSR0_TE1)
				type = TAUISA_MODEL_BASIC;
			else if (ddk_inb (E1SR (port)) & E1SR_REV)
				type = TAUISA_MODEL_E1_d;
			else
				type = TAUISA_MODEL_E1_c;
		}
	} else if (tauisa_download (port, e1, tlen, tvec2) ||
		   tauisa_download (port, e1_2, tlen, tvec2) ||
		   tauisa_download (port, e1_3, tlen, tvec2) || tauisa_download (port, e1_4, tlen, tvec2))
		type = TAUISA_MODEL_E1;
	else if (tauisa_download (port, g703, tlen, tvec2) ||
		 tauisa_download (port, g703_2, tlen, tvec2) ||
		 tauisa_download (port, g703_3, tlen, tvec2) || tauisa_download (port, g703_4, tlen, tvec2))
		type = TAUISA_MODEL_G703;
	tauisa_init_board (b, num, port, irq, dma, type, osc);
}

/*
 * Initialize the channel structure.
 */
static void tauisa_init_chan (tauisa_board_t * b, int i)
{
	tauisa_chan_t *c = b->chan + i;
	unsigned short port = b->port;

	c->num = i;
	c->board = b;
	switch (b->type) {
		case TAUISA_MODEL_BASIC:
		case TAUISA_MODEL_2BASIC:
			c->type = TAUISA_SERIAL;
			break;
		case TAUISA_MODEL_E1:
		case TAUISA_MODEL_E1_c:
		case TAUISA_MODEL_E1_d:
		case TAUISA_MODEL_2E1:
		case TAUISA_MODEL_2E1_d:
			c->type = TAUISA_E1;
			break;
		case TAUISA_MODEL_G703:
		case TAUISA_MODEL_G703_d:
		case TAUISA_MODEL_2G703:
			c->type = TAUISA_G703;
			break;
	}
	if (c->num)
		c->type |= TAUISA_SERIAL;

#define reg(X,N) HD_##X##_##N
#define set(X,N) c->X = R(port,reg(X,N))
#define srx(X,N) c->RX.X = R(port,reg(X,N##R))
#define stx(X,N) c->TX.X = R(port,reg(X,N##T))
	if (i == 0) {
		set (MD0, 0);
		set (MD1, 0);
		set (MD2, 0);
		set (CTL, 0);
		set (RXS, 0);
		set (TXS, 0);
		set (TMC, 0);
		set (CMD, 0);
		set (ST0, 0);
		set (ST1, 0);
		set (ST2, 0);
		set (ST3, 0);
		set (FST, 0);
		set (IE0, 0);
		set (IE1, 0);
		set (IE2, 0);
		set (FST, 0);
		set (IE0, 0);
		set (IE1, 0);
		set (IE2, 0);
		set (FIE, 0);
		set (SA0, 0);
		set (SA1, 0);
		set (IDL, 0);
		set (TRB, 0);
		set (RRC, 0);
		set (TRC0, 0);
		set (TRC1, 0);
		set (CST, 0);
		srx (DAR, 0);
		srx (DARB, 0);
		srx (SAR, 0);
		srx (SARB, 0);
		srx (CDA, 0);
		srx (EDA, 0);
		srx (BFL, 0);
		srx (BCR, 0);
		srx (DSR, 0);
		srx (DMR, 0);
		srx (FCT, 0);
		srx (DIR, 0);
		srx (DCR, 0);
		srx (TCNT, 0);
		srx (TCONR, 0);
		srx (TCSR, 0);
		srx (TEPR, 0);
		stx (DAR, 0);
		stx (DARB, 0);
		stx (SAR, 0);
		stx (SARB, 0);
		stx (CDA, 0);
		stx (EDA, 0);
		stx (BCR, 0);
		stx (DSR, 0);
		stx (DMR, 0);
		stx (FCT, 0);
		stx (DIR, 0);
		stx (DCR, 0);
		stx (TCNT, 0);
		stx (TCONR, 0);
		stx (TCSR, 0);
		stx (TEPR, 0);
	} else {
		set (MD0, 1);
		set (MD1, 1);
		set (MD2, 1);
		set (CTL, 1);
		set (RXS, 1);
		set (TXS, 1);
		set (TMC, 1);
		set (CMD, 1);
		set (ST0, 1);
		set (ST1, 1);
		set (ST2, 1);
		set (ST3, 1);
		set (FST, 1);
		set (IE0, 1);
		set (IE1, 1);
		set (IE2, 1);
		set (FST, 1);
		set (IE0, 1);
		set (IE1, 1);
		set (IE2, 1);
		set (FIE, 1);
		set (SA0, 1);
		set (SA1, 1);
		set (IDL, 1);
		set (TRB, 1);
		set (RRC, 1);
		set (TRC0, 1);
		set (TRC1, 1);
		set (CST, 1);
		srx (DAR, 1);
		srx (DARB, 1);
		srx (SAR, 1);
		srx (SARB, 1);
		srx (CDA, 1);
		srx (EDA, 1);
		srx (BFL, 1);
		srx (BCR, 1);
		srx (DSR, 1);
		srx (DMR, 1);
		srx (FCT, 1);
		srx (DIR, 1);
		srx (DCR, 1);
		srx (TCNT, 1);
		srx (TCONR, 1);
		srx (TCSR, 1);
		srx (TEPR, 1);
		stx (DAR, 1);
		stx (DARB, 1);
		stx (SAR, 1);
		stx (SARB, 1);
		stx (CDA, 1);
		stx (EDA, 1);
		stx (BCR, 1);
		stx (DSR, 1);
		stx (DMR, 1);
		stx (FCT, 1);
		stx (DIR, 1);
		stx (DCR, 1);
		stx (TCNT, 1);
		stx (TCONR, 1);
		stx (TCSR, 1);
		stx (TEPR, 1);
	}
#undef set
#undef srx
#undef stx
#undef reg
}

/*
 * Reinitialize the channels, using new options.
 */
void tauisa_reinit_chan (tauisa_chan_t * c)
{
	tauisa_board_t *b = c->board;
	long s;
	int i;

	if (c->hopt.txs == CLK_LINE) {
		/*
		 * External clock mode -- set zero baud rate.
		 */
		if (c->mode != TAUISA_MODE_ASYNC)
			c->baud = 0;
	} else if (c->baud == 0) {
		/*
		 * No baud rate in internal clock mode -- set default values.
		 */
		if (c->mode == TAUISA_MODE_ASYNC)
			c->baud = 9600;
		else if (c->mode == TAUISA_MODE_HDLC)
			c->baud = 64000;
	}
	switch (c->type) {
		case TAUISA_E1_SERIAL:
			if (b->opt.cfg == TAUISA_CFG_B)
				break;
			/*
			 * Fall through...
			 */
		case TAUISA_E1:
			c->mode = TAUISA_MODE_E1;
			c->hopt.txs = CLK_LINE;

			/*
			 * Compute the baud value.
			 */
			if (c->num) {
				s = b->opt.s1;
				if (b->opt.cfg == TAUISA_CFG_C)
					s &= ~b->opt.s0;
			} else
				s = b->opt.s0;
			/*
			 * Skip timeslot 16 in CAS mode.
			 */
			if (c->gopt.cas)
				s &= ~(1L << 16);

			c->baud = 0;
			for (i = 0; i < 32; ++i)
				if ((s >> i) & 1)
					c->baud += 64000;
			c->gopt.rate = c->baud / 1000;

			/*
			 * Set NRZ and clear INVCLK.
			 */
			c->opt.md2.encod = MD2_ENCOD_NRZ;
			c->board->opt.bcr2 &= c->num ? ~(BCR2_INVTXC1 | BCR2_INVRXC1) : ~(BCR2_INVTXC0 | BCR2_INVRXC0);
			break;

		case TAUISA_G703_SERIAL:
			if (b->opt.cfg == TAUISA_CFG_B)
				break;
			/*
			 * Fall through...
			 */
		case TAUISA_G703:
			c->mode = TAUISA_MODE_G703;
			c->hopt.txs = CLK_LINE;
			c->baud = c->gopt.rate * 1000L;

			/*
			 * Set NRZ/NRZI and clear INVCLK.
			 */
			if (c->opt.md2.encod != MD2_ENCOD_NRZ && c->opt.md2.encod != MD2_ENCOD_NRZI)
				c->opt.md2.encod = MD2_ENCOD_NRZ;
			c->board->opt.bcr2 &= c->num ? ~(BCR2_INVTXC1 | BCR2_INVRXC1) : ~(BCR2_INVTXC0 | BCR2_INVRXC0);
			break;
	}
}

/*
 * Reinitialize all channels, using new options and baud rate.
 */
void tauisa_reinit_board (tauisa_board_t * b)
{
	tauisa_chan_t *c;

	b->opt = tauisa_board_opt_dflt;
	for (c = b->chan; c < b->chan + TAUISA_NCHAN; ++c) {
		c->opt = tauisa_chan_opt_dflt;
		c->hopt = tauisa_opt_hdlc_dflt;
		c->gopt = tauisa_opt_g703_dflt;
		c->mode = tauisa_chan_mode;
		c->baud = tauisa_baud;

		tauisa_reinit_chan (c);
	}
}

/*
 * Set up the E1 controller of the Tau/E1 board.
 * Frame sync signals:
 * Configuration	Rsync0	Tsync0	Rsync1	Tsync1
 * ---------------------------------------------------
 * A (II)		out	out	out	out
 * B,C,D (HI,K,D)	in	out	in	out
 * ---------------------------------------------------
 * BI			out	out	in	in	-- not implemented
 * old B,C,D (HI,K,D)	out	in	out	in	-- old
 */
static void tauisa_setup_ctlr (tauisa_chan_t * c)
{
	tauisa_board_t *b = c->board;
	unsigned short p = c->num ? E1CS1 (b->port) : E1CS0 (b->port);
	unsigned char rcr1, rcr2, tcr1, tcr2, ccr1, licr;
	u32 xcbr, tir;
	int i;

	rcr2 = RCR2_RSCLKM;
	tcr1 = TCR1_TSIS | TCR1_TSO;
	tcr2 = 0;
	ccr1 = 0;
	licr = 0;

	if (b->opt.cfg != TAUISA_CFG_D) {
		/*
		 * Enable monitoring channel, when not in telephony mode.
		 */
		rcr2 |= RCR2_SA_4;
		tcr2 |= TCR2_SA_4;
	}
	if (b->opt.cfg == TAUISA_CFG_A) {
		rcr1 = RCR1_RSO;
	} else {
		rcr1 = RCR1_RSI;
		rcr2 |= RCR2_RESE;
	}

	if (c->gopt.cas)
		tcr1 |= TCR1_T16S;
	else
		ccr1 |= CCR1_CCS;

	if (c->gopt.hdb3)
		ccr1 |= CCR1_THDB3 | CCR1_RHDB3;

	if (c->gopt.crc4) {
		ccr1 |= CCR1_TCRC4 | CCR1_RCRC4;
		tcr2 |= TCR2_AEBE;
	}

	if (c->gopt.higain)
		licr |= LICR_HIGAIN;
	if (ddk_inb (E1SR (b->port)) & (c->num ? E1SR_TP1 : E1SR_TP0))
		licr |= LICR_LB120P;
	else
		licr |= LICR_LB75P;

	cte_out (p, DS_RCR1, rcr1);	/* receive control 1 */
	cte_out (p, DS_RCR2, rcr2);	/* receive control 2 */
	cte_out (p, DS_TCR1, tcr1);	/* transmit control 1 */
	cte_out (p, DS_TCR2, tcr2);	/* transmit control 2 */
	cte_out (p, DS_CCR1, ccr1);	/* common control 1 */
	cte_out (p, DS_CCR2, CCR2_CNTCV | CCR2_AUTORA | CCR2_LOFA1);	/* common control 2 */
	cte_out (p, DS_CCR3, CCR3_TSCLKM);	/* common control 3 */
	cte_out (p, DS_LICR, licr);	/* line interface control */
	cte_out (p, DS_IMR1, 0);	/* interrupt mask 1 */
	cte_out (p, DS_IMR2, SR2_SEC);	/* interrupt mask 2 */
	cte_out (p, DS_TEST1, 0);
	cte_out (p, DS_TEST2, 0);
	cte_out (p, DS_TAF, 0x9b);	/* transmit align frame */
	cte_out (p, DS_TNAF, 0xdf);	/* transmit non-align frame */
	cte_out (p, DS_TIDR, 0xff);	/* transmit idle definition */

	cte_out (p, DS_TS, 0x0b);	/* transmit signaling 1 */
	for (i = 1; i < 16; ++i)
		/*
		 * transmit signaling 2..16
		 */
		cte_out (p, (unsigned char) (DS_TS + i), 0xff);

	/*
	 * S0 == list of timeslots for channel 0
	 * S1 == list of timeslots for channel 1
	 * S2 == list of timeslots for E1 subchannel (pass through)
	 *
	 * Each channel uses the same timeslots for receive and transmit,
	 * i.e. RCBRi == TCBRi.
	 */
	if (b->opt.cfg == TAUISA_CFG_B)
		b->opt.s1 = 0;
	else if (b->opt.cfg == TAUISA_CFG_C)
		b->opt.s1 &= ~b->opt.s0;
	if (c->gopt.cas) {
		/*
		 * Skip timeslot 16 in CAS mode.
		 */
		b->opt.s0 &= ~(1L << 16);
		b->opt.s1 &= ~(1L << 16);
	}
	b->opt.s2 &= ~b->opt.s0;
	b->opt.s2 &= ~b->opt.s1;

	/*
	 * Configuration A:
	 *      xCBRi := Si
	 *      TIRi  := ~Si
	 *
	 * Configuration B:
	 *      xCBRi := Si
	 *      TIRi  := 0
	 *
	 * Configuration C:             (S0 & S2 == 0)
	 *      xCBR0 := S0
	 *      xCBR1 := 0
	 *      TIR0  := ~S0 & ~S2
	 *      TIR1  := ~S2
	 *
	 * Configuration D:             (Si & Sj == 0)
	 *      xCBR0 := S0
	 *      xCBR1 := S1
	 *      TIR0  := ~S0 & ~S1 & ~S2
	 *      TIR1  := ~S2
	 */
	xcbr = c->num ? b->opt.s1 : b->opt.s0;
	if (b->opt.cfg == TAUISA_CFG_A)
		tir = ~xcbr;
	else if (b->opt.cfg == TAUISA_CFG_D)
		tir = 0;
	else if (c->num == 0)
		tir = ~(b->opt.s0 | b->opt.s1 | b->opt.s2);
	else
		tir = ~b->opt.s2;

	/*
	 * Mark idle channels.
	 */
	cte_out (p, DS_TIR, (unsigned char) (tir & 0xfe));
	cte_out (p, DS_TIR + 1, (unsigned char) (tir >> 8));
	cte_out (p, DS_TIR + 2, (unsigned char) (tir >> 16));
	cte_out (p, DS_TIR + 3, (unsigned char) (tir >> 24));

	/*
	 * Set up rx/tx timeslots.
	 */
	cte_out (p, DS_RCBR, (unsigned char) (xcbr & 0xfe));
	cte_out (p, DS_RCBR + 1, (unsigned char) (xcbr >> 8));
	cte_out (p, DS_RCBR + 2, (unsigned char) (xcbr >> 16));
	cte_out (p, DS_RCBR + 3, (unsigned char) (xcbr >> 24));
	cte_out (p, DS_TCBR, (unsigned char) (xcbr & 0xfe));
	cte_out (p, DS_TCBR + 1, (unsigned char) (xcbr >> 8));
	cte_out (p, DS_TCBR + 2, (unsigned char) (xcbr >> 16));
	cte_out (p, DS_TCBR + 3, (unsigned char) (xcbr >> 24));

	/*
	 * Reset the line interface.
	 */
	cte_out (p, DS_CCR3, CCR3_TSCLKM | CCR3_LIRESET);
	cte_out (p, DS_CCR3, CCR3_TSCLKM);

	/*
	 * Reset the elastic store.
	 */
	cte_out (p, DS_CCR3, CCR3_TSCLKM | CCR3_ESRESET);
	cte_out (p, DS_CCR3, CCR3_TSCLKM);

	/*
	 * Clear status registers.
	 */
	cte_ins (p, DS_SR1, 0xff);
	cte_ins (p, DS_SR2, 0xff);
	cte_ins (p, DS_RIR, 0xff);
}

/*
 * Set up the serial controller of the Tau/E1 board.
 */
static void tauisa_setup_scc (unsigned short port)
{
#define SET(r,v) { cte_out2 (port, r, v); cte_out2 (port, AM_A | r, v); }

	/*
	 * hardware reset
	 */
	cte_out2 (port, AM_MICR, MICR_RESET_HW);

	SET (AM_PMR, 0x0c);	/* 2 stop bits */
	SET (AM_IMR, 0);	/* no W/REQ signal */
	cte_out2 (port, AM_IVR, 0);	/* interrupt vector */
	SET (AM_RCR, 0xc0);	/* rx 8 bits/char */
	SET (AM_TCR, 0x60);	/* tx 8 bits/char */
	SET (AM_SAF, 0);	/* sync address field */
	SET (AM_SFR, 0x7e);	/* sync flag register */
	cte_out2 (port, AM_MICR, 0);	/* master interrupt control */
	SET (AM_MCR, 0);	/* NRZ mode */
	SET (AM_CMR, 0x08);	/* rxclk=RTxC, txclk=TRxC */
	SET (AM_TCL, 0);	/* time constant low */
	SET (AM_TCH, 0);	/* time constant high */
	SET (AM_BCR, 0);	/* disable baud rate generator */

	SET (AM_RCR, 0xc1);	/* enable rx */
	SET (AM_TCR, 0x68);	/* enable tx */

	SET (AM_SICR, 0);	/* no status interrupt */
	SET (AM_CR, CR_RST_EXTINT);	/* reset external status */
	SET (AM_CR, CR_RST_EXTINT);	/* reset ext/status twice */
#undef SET
}

/*
 * Set up the Tau/E1 board.
 */
void tauisa_setup_e1 (tauisa_board_t * b)
{
	/*
	 * Control register 0:
	 * 1) board configuration
	 * 2) clock modes
	 */
	b->e1cfg &= E1CFG_LED;
	switch (b->opt.cfg) {
		case TAUISA_CFG_C:
			b->e1cfg |= E1CFG_K;
			break;
		case TAUISA_CFG_B:
			b->e1cfg |= E1CFG_HI;
			break;
		case TAUISA_CFG_D:
			b->e1cfg |= E1CFG_D;
			break;
		default:
			b->e1cfg |= E1CFG_II;
			break;
	}

	if (b->opt.clk0 == TAUISA_CLK_RCV)
		b->e1cfg |= E1CFG_CLK0_RCV;
	if (b->opt.clk0 == TAUISA_CLK_XFER)
		b->e1cfg |= E1CFG_CLK0_RCLK1;
	else
		b->e1cfg |= E1CFG_CLK0_INT;
	if (b->opt.clk1 == TAUISA_CLK_RCV)
		b->e1cfg |= E1CFG_CLK1_RCV;
	if (b->opt.clk1 == TAUISA_CLK_XFER)
		b->e1cfg |= E1CFG_CLK1_RCLK0;
	else
		b->e1cfg |= E1CFG_CLK1_INT;

	ddk_outb (E1CFG (b->port), b->e1cfg);

	/*
	 * Set up serial controller.
	 */
	tauisa_setup_scc (b->port);

	/*
	 * Set up E1 controllers.
	 */
	tauisa_setup_ctlr (b->chan + 0);	/* channel 0 */
	tauisa_setup_ctlr (b->chan + 1);	/* channel 1 */

	/*
	 * Start the board (GRUN).
	 */
	b->e1cfg |= E1CFG_GRUN;
	ddk_outb (E1CFG (b->port), b->e1cfg);
}

/*
 * Set up the G.703 controller of the Tau/G.703 board.
 */
static void tauisa_setup_lxt (tauisa_chan_t * c)
{
	ctg_outx (c, LX_CCR1, LX_RESET);	/* reset the chip */
	/*
	 * Delay
	 */
	ctg_inx (c, LX_CCR1);
	c->lx = LX_LOS;		/* disable loss of sync interrupt */
	if (c->num && c->board->opt.cfg == TAUISA_CFG_B)
		c->lx |= LX_TAOS;	/* idle channel--transmit all ones */
	if (c->gopt.hdb3)
		c->lx |= LX_HDB3;	/* enable HDB3 encoding */
	ctg_outx (c, LX_CCR1, c->lx);	/* setup the new mode */
	ctg_outx (c, LX_CCR2, LX_CCR2_LH);	/* setup Long Haul mode */
	ctg_outx (c, LX_CCR3, LX_CCR3_E1_LH);	/* setup Long Haul mode */
}

/*
 * Set up the Tau/G.703 board.
 */
void tauisa_setup_g703 (tauisa_board_t * b)
{
	b->gmd0 = GMD_2048;
	if (b->chan[0].gopt.pce) {
		if (b->chan[0].gopt.pce2)
			b->gmd0 |= GMD_PCE_PCM2;
		else
			b->gmd0 |= GMD_PCE_PCM2D;
	}
	if (b->opt.clk0)
		b->gmd0 |= GMD_RSYNC;

	b->gmd1 = 0;
	if (b->chan[1].gopt.pce) {
		if (b->chan[1].gopt.pce2)
			b->gmd1 |= GMD_PCE_PCM2;
		else
			b->gmd1 |= GMD_PCE_PCM2D;
	}
	if (b->opt.clk1)
		b->gmd1 |= GMD_RSYNC;

	switch (b->chan[0].gopt.rate) {
		case 2048:
			b->gmd0 |= GMD_2048;
			break;
		case 1024:
			b->gmd0 |= GMD_1024;
			break;
		case 512:
			b->gmd0 |= GMD_512;
			break;
		case 256:
			b->gmd0 |= GMD_256;
			break;
		case 128:
			b->gmd0 |= GMD_128;
			break;
		case 64:
			b->gmd0 |= GMD_64;
			break;
	}
	switch (b->chan[1].gopt.rate) {
		case 2048:
			b->gmd1 |= GMD_2048;
			break;
		case 1024:
			b->gmd1 |= GMD_1024;
			break;
		case 512:
			b->gmd1 |= GMD_512;
			break;
		case 256:
			b->gmd1 |= GMD_256;
			break;

		case 128:
			b->gmd1 |= GMD_128;
			break;
		case 64:
			b->gmd1 |= GMD_64;
			break;
	}

	ddk_outb (GMD0 (b->port), b->gmd0);
	ddk_outb (GMD1 (b->port), b->gmd1 | GMD1_NCS0 | GMD1_NCS1);

	b->gmd2 &= GMD2_LED;
	if (b->opt.cfg == TAUISA_CFG_B)
		b->gmd2 |= GMD2_SERIAL;
	ddk_outb (GMD2 (b->port), b->gmd2);

	/*
	 * Set up G.703 controllers.
	 */
	if ((b->chan + 0)->lx & LX_LLOOP) {
		tauisa_setup_lxt (b->chan + 0);	/* channel 0 */
		tauisa_enable_loop (b->chan + 0);
	} else {
		tauisa_setup_lxt (b->chan + 0);	/* channel 0 */
	}
	if ((b->chan + 1)->lx & LX_LLOOP) {
		tauisa_setup_lxt (b->chan + 1);	/* channel 1 */
		tauisa_enable_loop (b->chan + 1);
	} else {
		tauisa_setup_lxt (b->chan + 1);	/* channel 1 */
	}

	/*
	 * Clear errors.
	 */
	ddk_outb (GERR (b->port), 0xff);
	ddk_outb (GLDR (b->port), 0xff);
}

/*
 * Set up the board.
 */
int tauisa_setup_board (tauisa_board_t * b, const unsigned char *firmware, long bits, const tauisa_dat_tst_t * tst)
{
	tauisa_chan_t *c;

#if 0
	/*
	 * Disable DMA channel.
	 */
	ddk_outb (DMA_MASK, (b->dma & 3) | DMA_MASK_CLEAR);
#endif

	/*
	 * Reset the controller.
	 */
	ddk_outb (BCR0 (b->port), 0);

	/*
	 * Load the firmware.
	 */
	if (firmware && (b->type == TAUISA_MODEL_BASIC || b->type == TAUISA_MODEL_E1 ||
			 b->type == TAUISA_MODEL_G703) && !tauisa_download (b->port, firmware, bits, tst))
		return (0);
	if (firmware && (b->type == TAUISA_MODEL_2BASIC || b->type == TAUISA_MODEL_2E1 ||
			 b->type == TAUISA_MODEL_2E1_d || b->type == TAUISA_MODEL_2G703) &&
	    !tauisa_download2 (b->port, firmware))
		return (0);

	/*
	 * Enable DMA and IRQ.
	 */
	ddk_outb (BCR0 (b->port), BCR0_HDRUN);
	ddk_outb (BCR0 (b->port), b->bcr0);

	/*
	 * Clear DTR[0..1].
	 */
	ddk_outb (BCR1 (b->port), b->bcr1);

	/*
	 * Set bus timing.
	 */
	b->bcr2 = b->opt.bcr2;
	ddk_outb (BCR2 (b->port), b->bcr2);

	/*
	 * Initialize the controller.
	 */
	/*
	 * Zero wait state mode.
	 */
	ddk_outb (WCRL (b->port), 0);
	ddk_outb (WCRM (b->port), 0);
	ddk_outb (WCRH (b->port), 0);

	/*
	 * Clear interrupt modified vector register.
	 */
	ddk_outb (IMVR (b->port), 0);
	ddk_outb (ITCR (b->port), ITCR_CYCLE_SINGLE | ITCR_VECT_MOD);

	/*
	 * Disable all interrupts.
	 */
	ddk_outb (IER0 (b->port), 0);
	ddk_outb (IER1 (b->port), 0);
	ddk_outb (IER2 (b->port), 0);

	/*
	 * Set DMA parameters, enable master DMA mode.
	 */
	ddk_outb (PCR (b->port), BYTE b->opt.pcr);
	ddk_outb (DMER (b->port), DME_ENABLE);

#if 0
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
	 * Disable byte-sync mode for Tau/G.703.
	 */
	if (b->type == TAUISA_MODEL_G703)
		ddk_outb (GMD2 (b->port), 0);

	/*
	 * Initialize all channels.
	 */
	for (c = b->chan; c < b->chan + TAUISA_NCHAN; ++c)
		tauisa_setup_chan (c);

	switch (b->type) {
		case TAUISA_MODEL_G703:
		case TAUISA_MODEL_G703_d:
		case TAUISA_MODEL_2G703:
			tauisa_setup_g703 (b);
			break;
		case TAUISA_MODEL_E1:
		case TAUISA_MODEL_E1_c:
		case TAUISA_MODEL_E1_d:
		case TAUISA_MODEL_2E1:
		case TAUISA_MODEL_2E1_d:
			tauisa_setup_e1 (b);
			break;
	}
	return (1);
}

/*
 * Update the channel mode options.
 */
static void tauisa_update_chan (tauisa_chan_t * c)
{
	int txbr, rxbr, tmc, txcout;
	unsigned char rxs, txs, dmr = 0;
	tauisa_md0_async_t amd0;
	tauisa_md0_hdlc_t hmd0;
	tauisa_md1_async_t amd1;

	switch (c->mode) {	/* initialize the channel mode */
		case TAUISA_MODE_ASYNC:
		default:
			rxs = CLK_INT;
			txs = CLK_INT;

			amd0.mode = MD0_MODE_ASYNC;
			amd0.stopb = MD0_STOPB_1;
			amd0.cts_rts_dcd = 0;

			amd1.clk = MD1_CLK_16;
			amd1.txclen = amd1.rxclen = MD1_CLEN_8;
			amd1.parmode = MD1_PAR_NO;

			ddk_outb (c->MD0, BYTE amd0);
			ddk_outb (c->MD1, BYTE amd1);
			ddk_outb (c->CTL, c->rts ? 0 : CTL_RTS_INV);
			break;

		case TAUISA_MODE_E1:
		case TAUISA_MODE_G703:
		case TAUISA_MODE_HDLC:
			rxs = c->hopt.rxs;
			txs = c->hopt.txs;

			if (c->mode == TAUISA_MODE_E1 && c->board->opt.cfg == TAUISA_CFG_D) {
				hmd0 = c->hopt.md0;
				hmd0.crc = 0;

				ddk_outb (c->MD0, BYTE hmd0);
				ddk_outb (c->MD1, BYTE c->hopt.md1);
				ddk_outb (c->CTL, c->hopt.ctl & ~CTL_IDLE_PTRN);
				ddk_outb (c->SA0, c->hopt.sa0);
				ddk_outb (c->SA1, c->hopt.sa1);
				ddk_outb (c->IDL, 0x7e);	/* HDLC flag 01111110 */
			} else {
				ddk_outb (c->MD0, BYTE c->hopt.md0);
				ddk_outb (c->MD1, BYTE c->hopt.md1);
				ddk_outb (c->SA0, c->hopt.sa0);
				ddk_outb (c->SA1, c->hopt.sa1);
				ddk_outb (c->IDL, 0x7e);	/* HDLC flag 01111110 */

				if (c->rts)
					ddk_outb (c->CTL, c->hopt.ctl & ~CTL_RTS_INV);
				else
					ddk_outb (c->CTL, c->hopt.ctl | CTL_RTS_INV);
			}

			/*
			 * Chained-block DMA mode with frame counter.
			 */
			dmr |= DMR_CHAIN_CNTE | DMR_CHAIN_NF | DMR_TMOD;
			break;

	}
	ddk_outb (c->RX.DMR, dmr);
	ddk_outb (c->TX.DMR, dmr);

	/*
	 * set mode-independent options
	 */
	c->opt.md2.dpll_clk = MD2_DPLL_CLK_8;
	ddk_outb (c->MD2, BYTE c->opt.md2);

	/*
	 * set up receiver and transmitter clocks
	 */
	if (c->baud > 1024000) {
		/*
		 * turn off DPLL if the baud rate is too high
		 */
		if (rxs == CLK_RXS_LINE_NS)
			rxs = CLK_LINE;
		else if (rxs == CLK_RXS_DPLL_INT)
			rxs = CLK_INT;
	}
	if (rxs == CLK_RXS_LINE_NS || rxs == CLK_RXS_DPLL_INT) {
		/*
		 * Using 1:8 sampling rate.
		 */
		tauisa_compute_clock (c->board->osc, c->baud * 8, &rxbr, &tmc);
		txbr = rxbr + 3;
	} else if (c->mode == TAUISA_MODE_ASYNC) {
		/*
		 * Using 1:16 sampling rate.
		 */
		tauisa_compute_clock (c->board->osc, c->baud * 8, &rxbr, &tmc);
		--rxbr;
		txbr = rxbr;
	} else {
		tauisa_compute_clock (c->board->osc, c->baud, &rxbr, &tmc);
		txbr = rxbr;
	}
	txs |= txbr;
	rxs |= rxbr;
	ddk_outb (c->TMC, tmc);
	ddk_outb (c->RXS, rxs);

	/*
	 * Disable TXCOUT before changing TXS
	 * * to avoid two transmitters on the same line.
	 * * Enable it after TXS is set, if needed.
	 */
	txcout = c->num ? BCR1_TXCOUT1 : BCR1_TXCOUT0;
	c->board->bcr1 &= ~txcout;
	ddk_outb (BCR1 (c->board->port), c->board->bcr1);
	ddk_outb (c->TXS, txs);
	if ((txs & CLK_MASK) != CLK_LINE) {
		c->board->bcr1 |= txcout;
		ddk_outb (BCR1 (c->board->port), c->board->bcr1);
	}
	if (c->board->type == TAUISA_MODEL_E1_d || c->board->type == TAUISA_MODEL_2E1_d)
		tauisa_set_phony (c, c->gopt.phony);
}

/*
 * Initialize the channel.
 */
void tauisa_setup_chan (tauisa_chan_t * c)
{
	/*
	 * reset the channel
	 */
	ddk_outb (c->RX.DSR, DSR_DMA_DISABLE);
	ddk_outb (c->TX.DSR, DSR_DMA_DISABLE);
	ddk_outb (c->CMD, CMD_TX_RESET);
	ddk_outb (c->CMD, CMD_TX_ABORT);
	ddk_outb (c->CMD, CMD_CHAN_RESET);

	/*
	 * disable interrupts
	 */
	ddk_outb (c->IE0, 0);
	ddk_outb (c->IE1, 0);
	ddk_outb (c->IE2, 0);
	ddk_outb (c->FIE, 0);

	/*
	 * clear DTR, RTS
	 */
	tauisa_set_dtr (c, 0);
	tauisa_set_rts (c, 0);

	c->lx = LX_LOS;
	tauisa_update_chan (c);
}

u32 tauisa_get_ts (tauisa_chan_t * c)
{
	return c->num ? c->board->opt.s1 : c->board->opt.s0;
}

/*
 * Data transfer speed
 */
u32 tauisa_get_baud (tauisa_chan_t * c)
{
	u32 speed;
	u32 ts;

	if (c->mode == TAUISA_MODE_G703) {
		speed = 1000 * c->gopt.rate;
	} else if (c->mode == TAUISA_MODE_E1) {
		ts = tauisa_get_ts (c);
		for (speed = 0; ts; ts >>= 1)	/* Each timeslot is 64 Kbps */
			if (ts & 1)
				speed += 64000;
	} else
		speed = (c->hopt.txs == CLK_INT) ? c->baud : 0;
	return speed;
}

/*
 * Turn local loopback on
 */
static void tauisa_enable_loop (tauisa_chan_t * c)
{
	if (c->mode == TAUISA_MODE_E1) {
		unsigned short p = c->num ? E1CS1 (c->board->port) : E1CS0 (c->board->port);

		/*
		 * Local loopback.
		 */
		cte_out (p, DS_CCR2, cte_in (p, DS_CCR2) | CCR2_LLOOP);

		/*
		 * Enable jitter attenuator at the transmit side.
		 */
		cte_out (p, DS_LICR, cte_in (p, DS_LICR) | LICR_JA_TX);
		return;
	} else if (c->mode == TAUISA_MODE_G703) {
		c->lx = LX_LOS | LX_HDB3;
		ctg_outx (c, LX_CCR1, c->lx |= LX_LLOOP);
		return;
	} else if (c->mode == TAUISA_MODE_HDLC && tauisa_get_baud (c)) {
		unsigned char rxs = ddk_inb (c->RXS) & ~CLK_MASK;
		unsigned char txs = ddk_inb (c->TXS) & ~CLK_MASK;
		int txcout = c->num ? BCR1_TXCOUT1 : BCR1_TXCOUT0;

		c->opt.md2.loop = MD2_LLOOP;

		/*
		 * Disable TXCOUT before changing TXS
		 */
		/*
		 * to avoid two transmitters on the same line.
		 */
		/*
		 * Enable it after TXS is set.
		 */
		ddk_outb (BCR1 (c->board->port), c->board->bcr1 & ~txcout);

		ddk_outb (c->RXS, rxs | CLK_INT);
		ddk_outb (c->TXS, txs | CLK_INT);

		c->board->bcr1 |= txcout;
		ddk_outb (BCR1 (c->board->port), c->board->bcr1);

		ddk_outb (c->MD2, *(unsigned char *) &c->opt.md2);
		return;
	}
}

/*
 * Turn local loopback off
 */
static void tauisa_disable_loop (tauisa_chan_t * c)
{
	if (c->mode == TAUISA_MODE_E1) {
		unsigned short p = c->num ? E1CS1 (c->board->port) : E1CS0 (c->board->port);

		/*
		 * Local loopback.
		 */
		cte_out (p, DS_CCR2, cte_in (p, DS_CCR2) & ~CCR2_LLOOP);

		/*
		 * Disable jitter attenuator at the transmit side.
		 */
		cte_out (p, DS_LICR, cte_in (p, DS_LICR) & ~LICR_JA_TX);
		return;
	} else if (c->mode == TAUISA_MODE_G703) {
		c->lx = LX_LOS | LX_HDB3;
		ctg_outx (c, LX_CCR1, c->lx);
		return;
	} else if (c->mode == TAUISA_MODE_HDLC && tauisa_get_baud (c)) {
		unsigned char rxs = ddk_inb (c->RXS) & ~CLK_MASK;
		unsigned char txs = ddk_inb (c->TXS) & ~CLK_MASK;
		int txcout = c->num ? BCR1_TXCOUT1 : BCR1_TXCOUT0;

		c->opt.md2.loop = MD2_FDX;

		ddk_outb (BCR1 (c->board->port), c->board->bcr1 & ~txcout);

		ddk_outb (c->RXS, rxs | CLK_LINE);
		if (tauisa_get_baud (c))
			ddk_outb (c->TXS, txs | CLK_INT);
		else
			ddk_outb (c->TXS, txs | CLK_LINE);

		c->board->bcr1 |= txcout;
		ddk_outb (BCR1 (c->board->port), c->board->bcr1);

		ddk_outb (c->MD2, *(unsigned char *) &c->opt.md2);
		return;
	}
}

/*
 * Turn local loopback on/off
 */
void tauisa_set_loop (tauisa_chan_t * c, int on)
{
	if (on)
		tauisa_enable_loop (c);
	else
		tauisa_disable_loop (c);
}

int tauisa_get_loop (tauisa_chan_t * c)
{
	if (c->mode == TAUISA_MODE_E1) {
		unsigned short p = c->num ? E1CS1 (c->board->port) : E1CS0 (c->board->port);

		return cte_in (p, DS_CCR2) & CCR2_LLOOP;
	}
	if (c->mode == TAUISA_MODE_G703)
		return c->lx & LX_LLOOP;

	/*
	 * TAUISA_MODE_HDLC
	 */
	return (c->opt.md2.loop & MD2_LLOOP) != 0;
}

int tauisa_set_phony (tauisa_chan_t * c, int on)
{
	/*
	 * Valid only for TauPCI-E1.
	 */
	if (c->board->type != TAUISA_MODEL_E1_d && c->board->type != TAUISA_MODEL_2E1_d)
		return -1;
	c->gopt.phony = (on != 0);
	if (c->gopt.phony) {
		c->board->e1syn |= c->num ? E1SYN_ENS1 : E1SYN_ENS0;
		/*
		 * No receive/transmit crc.
		 */
		c->hopt.md0.crc = 0;
	} else {
		c->board->e1syn &= ~(c->num ? E1SYN_ENS1 : E1SYN_ENS0);
		/*
		 * Enable crc.
		 */
		c->hopt.md0.crc = 1;
	}
	ddk_outb (c->MD0, BYTE c->hopt.md0);
	ddk_outb (E1SYN (c->board->port), c->board->e1syn);
	return 0;
}

void tauisa_start_receiver (tauisa_chan_t * c, int dma, u32 buf, unsigned len, u32 desc, u32 lim)
{
	int ier0 = ddk_inb (IER0 (c->board->port));
	int ier1 = ddk_inb (IER1 (c->board->port));
	int ier2 = ddk_inb (IER2 (c->board->port));
	int ie0 = ddk_inb (c->IE0);
	int ie2 = ddk_inb (c->IE2);

	if (dma) {
		ier1 |= c->num ? (IER1_RX_DMERE_1 | IER1_RX_DME_1) : (IER1_RX_DMERE_0 | IER1_RX_DME_0);
		if (c->mode == TAUISA_MODE_ASYNC) {
			ier0 |= c->num ? IER0_RX_INTE_1 : IER0_RX_INTE_0;
			ie0 |= IE0_RX_INTE;
			ie2 |= IE2_OVRNE | IE2_ASYNC_FRMEE | IE2_ASYNC_PEE;
		}
	} else {
		ier0 |= c->num ? (IER0_RX_INTE_1 | IER0_RX_RDYE_1) : (IER0_RX_INTE_0 | IER0_RX_RDYE_0);
		ie0 |= IE0_RX_INTE | IE0_RX_RDYE;
	}

	/*
	 * Start timer.
	 */
	if (!dma) {
		ddk_outb (c->RX.TEPR, TEPR_64);	/* prescale to 16 kHz */
		ddk_outw (c->RX.TCONR, 160);	/* period is 10 msec */
		ddk_outw (c->RX.TCNT, 0);
		ddk_outb (c->RX.TCSR, TCSR_ENABLE | TCSR_INTR);
		ier2 |= c->num ? IER2_RX_TME_1 : IER2_RX_TME_0;
	}

	/*
	 * Enable interrupts.
	 */
	ddk_outb (IER0 (c->board->port), ier0);
	ddk_outb (IER1 (c->board->port), ier1);
	ddk_outb (IER2 (c->board->port), ier2);
	ddk_outb (c->IE0, ie0);
	ddk_outb (c->IE2, ie2);

	/*
	 * RXRDY:=1 when the receive buffer has more than RRC chars
	 */
	ddk_outb (c->RRC, dma ? c->opt.dma_rrc : c->opt.pio_rrc);

	/*
	 * Start receiver.
	 */
	if (dma) {
		ddk_outb (c->RX.DCR, DCR_ABORT);
		if (c->mode == TAUISA_MODE_ASYNC) {
			/*
			 * Single-buffer DMA mode.
			 */
			ddk_outb (c->RX.DARB, (unsigned char) (buf >> 16));
			ddk_outw (c->RX.DAR, (unsigned short) buf);
			ddk_outw (c->RX.BCR, len);
			ddk_outb (c->RX.DIR, DIR_EOTE);
		} else {
			/*
			 * Chained-buffer DMA mode.
			 */
			ddk_outb (c->RX.SARB, (unsigned char) (desc >> 16));
			ddk_outw (c->RX.EDA, (unsigned short) lim);
			ddk_outw (c->RX.CDA, (unsigned short) desc);
			ddk_outw (c->RX.BFL, len);
			ddk_outb (c->RX.DIR, DIR_CHAIN_EOME | DIR_CHAIN_BOFE | DIR_CHAIN_COFE);
		}
		ddk_outb (c->RX.DSR, DSR_DMA_ENABLE);
	}
	ddk_outb (c->CMD, CMD_RX_ENABLE);
}

void
tauisa_start_transmitter (tauisa_chan_t * c, int dma, u32 buf, unsigned len, u32 desc, u32 lim)
{
	int ier0 = ddk_inb (IER0 (c->board->port));
	int ier1 = ddk_inb (IER1 (c->board->port));
	int ie0 = ddk_inb (c->IE0);
	int ie1 = ddk_inb (c->IE1);

	/*
	 * Enable underrun interrupt in HDLC and raw modes.
	 */
	if (c->mode != TAUISA_MODE_ASYNC) {
		ier0 |= c->num ? IER0_TX_INTE_1 : IER0_TX_INTE_0;
		ie0 |= IE0_TX_INTE;
		ie1 |= IE1_HDLC_UDRNE;
	}
	if (dma)
		ier1 |= c->num ? (IER1_TX_DMERE_1 | IER1_TX_DME_1) : (IER1_TX_DMERE_0 | IER1_TX_DME_0);
	else {
		ier0 |= c->num ? IER0_TX_RDYE_1 : IER0_TX_RDYE_0;
		ie0 |= IE0_TX_RDYE;
	}

	/*
	 * Enable interrupts.
	 */
	ddk_outb (IER0 (c->board->port), ier0);
	ddk_outb (IER1 (c->board->port), ier1);
	ddk_outb (c->IE0, ie0);
	ddk_outb (c->IE1, ie1);

	/*
	 * TXRDY:=1 when the transmit buffer has TRC0 or less chars,
	 * * TXRDY:=0 when the transmit buffer has more than TRC1 chars
	 */
	ddk_outb (c->TRC0, dma ? c->opt.dma_trc0 : c->opt.pio_trc0);
	ddk_outb (c->TRC1, dma ? c->opt.dma_trc1 : c->opt.pio_trc1);

	/*
	 * Start transmitter.
	 */
	if (dma) {
		ddk_outb (c->TX.DCR, DCR_ABORT);
		if (c->mode == TAUISA_MODE_ASYNC) {
			/*
			 * Single-buffer DMA mode.
			 */
			ddk_outb (c->TX.SARB, (unsigned char) (buf >> 16));
			ddk_outw (c->TX.SAR, (unsigned short) buf);
			ddk_outw (c->TX.BCR, len);
			ddk_outb (c->TX.DIR, DIR_EOTE);
		} else {
			/*
			 * Chained-buffer DMA mode.
			 */
			ddk_outb (c->TX.SARB, (unsigned char) (desc >> 16));
			ddk_outw (c->TX.EDA, (unsigned short) lim);
			ddk_outw (c->TX.CDA, (unsigned short) desc);
			ddk_outb (c->TX.DIR,	/* DIR_CHAIN_EOME | */
				    DIR_CHAIN_BOFE | DIR_CHAIN_COFE);
		}
		/*
		 * Set DSR_DMA_ENABLE to begin!
		 */
	}
	ddk_outb (c->CMD, CMD_TX_ENABLE);

	/*
	 * Clear errors.
	 */
	if (c->board->type == TAUISA_MODEL_G703) {
		ddk_outb (GERR (c->board->port), 0xff);
		ddk_outb (GLDR (c->board->port), 0xff);
	}
}

/*
 * Control DTR signal for the channel.
 * Turn it on/off.
 */
void tauisa_set_dtr (tauisa_chan_t * c, int on)
{
	if (on) {
		c->dtr = 1;
		c->board->bcr1 |= c->num ? BCR1_DTR1 : BCR1_DTR0;
	} else {
		c->dtr = 0;
		c->board->bcr1 &= ~(c->num ? BCR1_DTR1 : BCR1_DTR0);
	}
	ddk_outb (BCR1 (c->board->port), c->board->bcr1);
}

/*
 * Control RTS signal for the channel.
 * Turn it on/off.
 */
void tauisa_set_rts (tauisa_chan_t * c, int on)
{
	c->rts = (on != 0);
	if (c->rts)
		ddk_outb (c->CTL, ddk_inb (c->CTL) & ~CTL_RTS_INV);
	else
		ddk_outb (c->CTL, ddk_inb (c->CTL) | CTL_RTS_INV);
}

/*
 * Control BREAK state in asynchronous mode.
 * Turn it on/off.
 */
void tauisa_set_brk (tauisa_chan_t * c, int on)
{
	if (c->mode != TAUISA_MODE_ASYNC)
		return;
	if (on)
		ddk_outb (c->CTL, ddk_inb (c->CTL) | CTL_BRK);
	else
		ddk_outb (c->CTL, ddk_inb (c->CTL) & ~CTL_BRK);
}

/*
 * Get the state of DSR signal of the channel.
 */
int tauisa_get_dsr (tauisa_chan_t * c)
{
	return (ddk_inb (BSR1 (c->board->port)) & (c->num ? BSR1_DSR1 : BSR1_DSR0)) != 0;
}

/*
 * Get the G.703 line signal level.
 */
int tauisa_get_lq (tauisa_chan_t * c)
{
	unsigned char q1, q2, q3;
	static int lq_to_santibells[] = { 0, 95, 195, 285 };
	int i;

	if (!(c->type & TAUISA_G703))
		return 0;
	q1 = ddk_inb (GLQ (c->board->port));
	/*
	 * Repeat reading the register to produce a 10-usec delay.
	 */
	i = 20;
	do
		q2 = ddk_inb (GLQ (c->board->port));
	while (--i);

	i = 20;
	do
		q3 = ddk_inb (GLQ (c->board->port));
	while (--i);

	if (c->num) {
		q1 >>= GLQ_SHIFT;
		q2 >>= GLQ_SHIFT;
		q3 >>= GLQ_SHIFT;
	}
	q1 &= GLQ_MASK;
	q2 &= GLQ_MASK;
	q3 &= GLQ_MASK;
	if (q1 <= q2 && q2 <= q3)
		return lq_to_santibells[q2];
	if (q2 <= q3 && q3 <= q1)
		return lq_to_santibells[q3];
	if (q3 <= q1 && q1 <= q2)
		return lq_to_santibells[q1];
	if (q1 <= q3 && q3 <= q2)
		return lq_to_santibells[q3];
	if (q3 <= q2 && q2 <= q1)
		return lq_to_santibells[q2];
	/*
	 * if (q2 <= q1 && q1 <= q3)
	 */ return lq_to_santibells[q1];
}

/*
 * Get the state of CARRIER signal of the channel.
 */
int tauisa_get_cd (tauisa_chan_t * c)
{
	return (ddk_inb (c->ST3) & ST3_DCD_INV) == 0;
}

/*
 * Get the state of CTS signal of the channel.
 */
int tauisa_get_cts (tauisa_chan_t * c)
{
	return (ddk_inb (c->ST3) & ST3_CTS_INV) == 0;
}

/*
 * Turn LED on/off.
 */
void tauisa_led (tauisa_board_t * b, int on)
{
	switch (b->type) {
		case TAUISA_MODEL_G703:
		case TAUISA_MODEL_G703_d:
		case TAUISA_MODEL_2G703:
			if (on)
				b->gmd2 |= GMD2_LED;
			else
				b->gmd2 &= ~GMD2_LED;
			ddk_outb (GMD2 (b->port), b->gmd2);
			break;
		default:
			if (on)
				b->e1cfg |= E1CFG_LED;
			else
				b->e1cfg &= ~E1CFG_LED;
			ddk_outb (E1CFG (b->port), b->e1cfg);
			break;
	}
}

void tauisa_compute_clock (long hz, long baud, int *txbr, int *tmc)
{
	if (baud < 100)
		baud = 100;
	*txbr = 0;
	if (4 * baud > 3 * hz)
		*tmc = 1;
	else {
		while (((hz / baud) >> ++*txbr) > 256)
			continue;
		*tmc = (((2 * hz / baud) >> *txbr) + 1) / 2;
	}
}

/*
 * Access to DS2153 chips on the Tau/E1 board.
 * Examples:
 *	val = cte_in (E1CSi (base), DS_RCR1)
 *	cte_out (E1CSi (base), DS_CCR1, val)
 *	val = cte_ins (E1CSi (base), DS_SSR)
 */
unsigned char cte_in (unsigned short base, unsigned char reg)
{
	ddk_outb (base, reg);
	return ddk_inb (E1DAT (base & 0x3e0));
}

void cte_out (unsigned short base, unsigned char reg, unsigned char val)
{
	ddk_outb (base, reg);
	ddk_outb (E1DAT (base & 0x3e0), val);
}

/*
 * Get the DS2153 status register, using write-read-write scheme.
 */
unsigned char cte_ins (unsigned short base, unsigned char reg, unsigned char mask)
{
	unsigned char val;
	unsigned short rw = E1DAT (base & 0x3e0);

	ddk_outb (base, reg);
	ddk_outb (rw, mask);	/* lock bits */
	ddk_outb (base, reg);
	val = ddk_inb (rw) & mask;	/* get values */
	ddk_outb (base, reg);
	ddk_outb (rw, val);	/* unlock bits */
	return val;
}

/*
 * Access to 8530 chip on the Tau/E1 board.
 * Examples:
 *	val = cte_in2 (base, AM_RSR | AM_A)
 *	cte_out2 (base, AM_IMR, val)
 */
unsigned char cte_in2 (unsigned short base, unsigned char reg)
{
	ddk_outb (E1CS2 (base), E1CS2_SCC | reg >> 4);
	ddk_outb (E1DAT (base), reg & 15);
	return ddk_inb (E1DAT (base));
}

void cte_out2 (unsigned short base, unsigned char reg, unsigned char val)
{
	ddk_outb (E1CS2 (base), E1CS2_SCC | reg >> 4);
	ddk_outb (E1DAT (base), reg & 15);
	ddk_outb (E1DAT (base), val);
}

/*
 * Read the data from the 8530 receive fifo.
 */
unsigned char cte_in2d (tauisa_chan_t * c)
{
	ddk_outb (E1CS2 (c->board->port), E1CS2_SCC | E1CS2_DC | (c->num ? 0 : E1CS2_AB));
	return ddk_inb (E1DAT (c->board->port));
}

/*
 * Send the 8530 command.
 */
void cte_out2c (tauisa_chan_t * c, unsigned char val)
{
	ddk_outb (E1CS2 (c->board->port), E1CS2_SCC | (c->num ? 0 : E1CS2_AB));
	ddk_outb (E1DAT (c->board->port), val);
}

/*
 * Write the data to the 8530 transmit fifo.
 */
void cte_out2d (tauisa_chan_t * c, unsigned char val)
{
	ddk_outb (E1CS2 (c->board->port), E1CS2_SCC | E1CS2_DC | (c->num ? 0 : E1CS2_AB));
	ddk_outb (E1DAT (c->board->port), val);
}

/*
 * Access to LXT318 chip on the Tau/G.703 board.
 * Examples:
 *	val = ctg_inx (c)
 *	ctg_outx (c, val)
 */
static void ctg_output (unsigned short port, unsigned char val, unsigned char v0)
{
	int i;

	for (i = 0; i < 8; ++i) {
		unsigned char v = v0;

		if ((val >> i) & 1)
			v |= GMD0_SDI;
		ddk_outb (port, v);
		ddk_outb (port, v);
		ddk_outb (port, v);
		ddk_outb (port, v);
		ddk_outb (port, v | GMD0_SCLK);
		ddk_outb (port, v | GMD0_SCLK);
		ddk_outb (port, v | GMD0_SCLK);
		ddk_outb (port, v | GMD0_SCLK);
	}
	ddk_outb (port, v0);
}

void ctg_outx (tauisa_chan_t * c, unsigned char reg, unsigned char val)
{
	unsigned short port = GMD0 (c->board->port);

	ddk_outb (GMD1 (c->board->port), c->board->gmd1 | GMD1_NCS0 | GMD1_NCS1);
	ddk_outb (GMD1 (c->board->port), c->board->gmd1 | (c->num ? GMD1_NCS0 : GMD1_NCS1));
	ctg_output (port, (reg << 1) | LX_WRITE, c->board->gmd0);
	ctg_output (port, val, c->board->gmd0);
	ddk_outb (GMD1 (c->board->port), c->board->gmd1 | GMD1_NCS0 | GMD1_NCS1);
}

unsigned char ctg_inx (tauisa_chan_t * c, unsigned char reg)
{
	unsigned short port = GMD0 (c->board->port);
	unsigned short data = GLDR (c->board->port);
	unsigned char val = 0, mask = c->num ? GLDR_C1 : GLDR_C0;
	int i;

	ddk_outb (GMD1 (c->board->port), c->board->gmd1 | GMD1_NCS0 | GMD1_NCS1);
	ddk_outb (GMD1 (c->board->port), c->board->gmd1 | (c->num ? GMD1_NCS0 : GMD1_NCS1));
	ctg_output (port, (reg << 1) | LX_READ, c->board->gmd0);
	for (i = 0; i < 8; ++i) {
		ddk_outb (port, c->board->gmd0 | GMD0_SCLK);
		if (ddk_inb (data) & mask)
			val |= 1 << i;
		ddk_outb (port, c->board->gmd0);
	}
	ddk_outb (GMD1 (c->board->port), c->board->gmd1 | GMD1_NCS0 | GMD1_NCS1);
	return val;
}

/*
 * Adapter options
 */
tauisa_board_opt_t tauisa_board_opt_dflt = {
	0,			/* board control register 2 */
	{			/* DMA priority control register */
	 PCR_PRIO_ROTATE,
	 0,			/* all channels share the the bus hold */
	 0,			/* hold the bus until all transfers done */
	 }
	,
	TAUISA_CFG_A,		/* E1/G.703 config: two independent channels */
	TAUISA_CLK_INT,		/* E1/G.703 chan 0 internal tx clock source */
	TAUISA_CLK_INT,		/* E1/G.703 chan 1 internal tx clock source */
	(u32) ~0UL << 1,		/* E1 channel 0 timeslots 1..31 */
	(u32) ~0UL << 1,		/* E1 channel 1 timeslots 1..31 */
	0,			/* no E1 subchannel timeslots */
};

/*
 * Mode-independent channel options
 */
tauisa_chan_opt_t tauisa_chan_opt_dflt = {
	{			/* mode register 2 */
	 MD2_FDX,		/* full duplex communication */
	 0,			/* empty ADPLL clock rate */
	 MD2_ENCOD_NRZ,		/* NRZ encoding */
	 }
	,
	/*
	 * DMA mode FIFO marks
	 */
	15, 24, 30,		/* rx ready, tx empty, tx full */
	/*
	 * port i/o mode FIFO marks
	 */
	15, 16, 30,		/* rx ready, tx empty, tx full */
};

/*
 * Default HDLC options
 */
tauisa_opt_hdlc_t tauisa_opt_hdlc_dflt = {
	{			/* mode register 0 */
	 1,			/* CRC preset to all ones (V.41) */
	 1,			/* CRC-CCITT */
	 1,			/* enable CRC */
	 0,			/* disable automatic CTS/DCD */
	 MD0_MODE_HDLC,		/* HDLC mode */
	 }
	,
	{			/* mode register 1 */
	 MD1_ADDR_NOCHK,	/* do not check address field */
	 }
	,
	(unsigned char) (CTL_IDLE_PTRN | CTL_UDRN_ABORT | CTL_RTS_INV),	/* control register */
	0, 0,			/* empty sync/address registers 0,1 */
	CLK_LINE,		/* receive clock source: RXC line input */
	CLK_LINE,		/* transmit clock source: TXC line input */
};

/*
 * Default E1/G.703 options
 */
tauisa_opt_g703_t tauisa_opt_g703_dflt = {
	1,			/* HDB3 enable */
	0,			/* precoder disable */
	TAUISA_TEST_DISABLED,	/* test disabled, normal operation */
	0,			/* CRC4 disable */
	0,			/* CCS signaling */
	0,			/* low gain */
	0,			/* no raw mode */
	0,			/* no PCM2 precoder compatibility */
	2048,			/* data rate 2048 kbit/sec */
};

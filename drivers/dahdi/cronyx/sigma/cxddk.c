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

#define BYTE *(u8*)&

/*
 * Compute the optimal size of the receive buffer.
 */
static int sigma_compute_buf_len (sigma_chan_t * c)
{
	int rbsz;

	if (c->mode == SIGMA_MODE_ASYNC) {
		rbsz = (c->rxbaud + 800 - 1) / 800 * 2;
		if (rbsz < 4)
			rbsz = 4;
		else if (rbsz > SIGMA_DMABUFSZ)
			rbsz = SIGMA_DMABUFSZ;
	} else
		rbsz = SIGMA_DMABUFSZ;

	return rbsz;
}

/*
 * Auto-detect the installed adapters.
 */
int sigma_find (unsigned *board_ports)
{
	int i, n;

	for (i = 0, n = 0; sigma_porttab[i] && n < SIGMA_NBRD; i++)
		if (sigma_probe_board (sigma_porttab[i], -1, -1))
			board_ports[n++] = sigma_porttab[i];
	return n;
}

/*
 * Initialize the adapter.
 */
int sigma_open_board (sigma_board_t * b, int num, unsigned port, int irq, int dma)
{
	sigma_chan_t *c;

	if (num >= SIGMA_NBRD || !sigma_probe_board (port, irq, dma))
		return 0;

	/*
	 * init callback pointers 
	 */
	for (c = b->chan; c < b->chan + SIGMA_NCHAN; ++c) {
		c->call_on_tx = 0;
		c->call_on_rx = 0;
		c->call_on_err = 0;
	}

	sigma_init (b, num, port, irq, dma);

	/*
	 * Loading firmware 
	 */
	if (!sigma_setup_board (b, csigma_fw_data, csigma_fw_len, csigma_fw_tvec))
		return 0;
	return 1;
}

/*
 * Shutdown the adapter.
 */
void sigma_close_board (sigma_board_t * b)
{
	sigma_setup_board (b, 0, 0, 0);

	/*
	 * Reset the controller. 
	 */
	ddk_outb (BCR0 (b->port), 0);
	if (b->chan[8].type || b->chan[12].type)
		ddk_outb (BCR0 (b->port + 0x10), 0);
}

/*
 * Start the channel.
 */
void sigma_start_chan (sigma_chan_t * c, sigma_buf_t * cb, u32 phys)
{
	int rbsz, mode;
	

	c->overflow = 0;

	/*
	 * Setting up buffers 
	 */
	if (cb) {
		c->arbuf = cb->rbuffer[0];
		c->brbuf = cb->rbuffer[1];
		c->atbuf = cb->tbuffer[0];
		c->btbuf = cb->tbuffer[1];
		c->arphys = phys + ((char *) c->arbuf - (char *) cb);
		c->brphys = phys + ((char *) c->brbuf - (char *) cb);
		c->atphys = phys + ((char *) c->atbuf - (char *) cb);
		c->btphys = phys + ((char *) c->btbuf - (char *) cb);
	}

	/*
	 * Set current channel number 
	 */
	ddk_outb (CAR (c->port), c->num & 3);

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

	/*
	 * Set mode 
	 */
	mode = (c->mode == SIGMA_MODE_ASYNC) ? CMR_ASYNC : CMR_HDLC;
	if (c->board->dma)
		mode |= CMR_RXDMA | CMR_TXDMA;
	ddk_outb (CMR (c->port), mode);

	/*
	 * Clear and initialize channel 
	 */
	sigma_cmd (c->port, CCR_CLRCH);
	sigma_cmd (c->port, CCR_INITCH | CCR_DISRX | CCR_DISTX);
	if (c->mode != SIGMA_MODE_ASYNC)
		ddk_outb (STCR (c->port), STC_SNDSPC);

	/*
	 * Setup receiver 
	 */
	rbsz = sigma_compute_buf_len (c);
	ddk_outw (ARBCNT (c->port), rbsz);
	ddk_outw (BRBCNT (c->port), rbsz);
	ddk_outw (ARBSTS (c->port), BSTS_OWN24);
	ddk_outw (BRBSTS (c->port), BSTS_OWN24);

	/*
	 * Enable interrupts 
	 */
	ddk_outb (IER (c->port), (c->mode == SIGMA_MODE_ASYNC) ? IER_MDM : 0);

	/*
	 * Clear DTR and RTS 
	 */
	sigma_set_dtr (c, 0);
	sigma_set_rts (c, 0);
}

/*
 * Turn the receiver on/off.
 */
void sigma_enable_receive (sigma_chan_t * c, int on)
{
	u8 bits;

	bits = IER_RXD;
	if (c->mode == SIGMA_MODE_ASYNC)
		bits |= IER_RET;
		
	if (sigma_receive_enabled (c) && !on) {
		ddk_outb (CAR (c->port), c->num & 3);
		ddk_outb (IER (c->port), ddk_inb (IER (c->port)) & ~bits);
		sigma_cmd (c->port, CCR_DISRX);
	} else if (!sigma_receive_enabled (c) && on) {
		ddk_outb (CAR (c->port), c->num & 3);
		ddk_outb (IER (c->port), ddk_inb (IER (c->port)) | bits);
		sigma_cmd (c->port, CCR_ENRX);
	}
}

/*
 * Turn the transmiter on/off.
 */
void sigma_enable_transmit (sigma_chan_t * c, int on)
{
	u8 bits;

	bits = IER_TXD;
	if (c->mode != SIGMA_MODE_ASYNC)
		bits |= IER_TXMPTY;

	if (sigma_transmit_enabled (c) && !on) {
		ddk_outb (CAR (c->port), c->num & 3);
		ddk_outb (IER (c->port), ddk_inb (IER (c->port)) & ~bits);
		if (c->mode != SIGMA_MODE_ASYNC)
			ddk_outb (STCR (c->port), STC_ABORTTX | STC_SNDSPC);
		sigma_cmd (c->port, CCR_DISTX);
	} else if (!sigma_transmit_enabled (c) && on) {
		ddk_outb (CAR (c->port), c->num & 3);
		ddk_outb (IER (c->port), ddk_inb (IER (c->port)) | bits);
		sigma_cmd (c->port, CCR_ENTX);
	}
}

/*
 * Get channel status.
 */
int sigma_receive_enabled (sigma_chan_t * c)
{
	ddk_outb (CAR (c->port), c->num & 3);
	return (ddk_inb (CSR (c->port)) & CSRA_RXEN) != 0;
}

int sigma_transmit_enabled (sigma_chan_t * c)
{
	ddk_outb (CAR (c->port), c->num & 3);
	return (ddk_inb (CSR (c->port)) & CSRA_TXEN) != 0;
}

u32 sigma_get_baud (sigma_chan_t * c)
{
	return (c->opt.tcor.clk == CD2400_CLK_EXT) ? 0 : c->txbaud;
}

int sigma_get_loop (sigma_chan_t * c)
{
	return c->opt.tcor.llm ? 1 : 0;
}

int sigma_get_nrzi (sigma_chan_t * c)
{
	return c->opt.rcor.encod == CD2400_ENCOD_NRZI;
}

int sigma_get_dpll (sigma_chan_t * c)
{
	return c->opt.rcor.dpll ? 1 : 0;
}

void sigma_set_baud (sigma_chan_t * c, u32 bps)
{
	int clock, period;

	c->txbaud = c->rxbaud = bps;

	/*
	 * Set current channel number 
	 */
	ddk_outb (CAR (c->port), c->num & 3);
	if (bps) {
		if (c->mode == SIGMA_MODE_ASYNC || c->opt.rcor.dpll || c->opt.tcor.llm) {
			/*
			 * Receive baud - internal 
			 */
			sigma_clock (c->oscfreq, c->rxbaud, &clock, &period);
			c->opt.rcor.clk = clock;
			ddk_outb (RCOR (c->port), BYTE c->opt.rcor);
			ddk_outb (RBPR (c->port), period);
		} else {
			/*
			 * Receive baud - external 
			 */
			c->opt.rcor.clk = CD2400_CLK_EXT;
			ddk_outb (RCOR (c->port), BYTE c->opt.rcor);
			ddk_outb (RBPR (c->port), 1);
		}

		/*
		 * Transmit baud - internal 
		 */
		sigma_clock (c->oscfreq, c->txbaud, &clock, &period);
		c->opt.tcor.clk = clock;
		c->opt.tcor.ext1x = 0;
		ddk_outb (TBPR (c->port), period);
	} else if (c->mode != SIGMA_MODE_ASYNC) {
		/*
		 * External clock - disable local loopback and DPLL 
		 */
		c->opt.tcor.llm = 0;
		c->opt.rcor.dpll = 0;

		/*
		 * Transmit baud - external 
		 */
		c->opt.tcor.ext1x = 1;
		c->opt.tcor.clk = CD2400_CLK_EXT;
		ddk_outb (TBPR (c->port), 1);

		/*
		 * Receive baud - external 
		 */
		c->opt.rcor.clk = CD2400_CLK_EXT;
		ddk_outb (RCOR (c->port), BYTE c->opt.rcor);
		ddk_outb (RBPR (c->port), 1);
	}
	if (c->opt.tcor.llm)
		ddk_outb (COR2 (c->port), (BYTE c->hopt.cor2) & ~3);
	else
		ddk_outb (COR2 (c->port), BYTE c->hopt.cor2);
	ddk_outb (TCOR (c->port), BYTE c->opt.tcor);
}

void sigma_set_loop (sigma_chan_t * c, int on)
{
	if (!c->txbaud)
		return;

	c->opt.tcor.llm = on ? 1 : 0;
	sigma_set_baud (c, c->txbaud);
}

void sigma_set_dpll (sigma_chan_t * c, int on)
{
	if (!c->txbaud)
		return;

	c->opt.rcor.dpll = on ? 1 : 0;
	sigma_set_baud (c, c->txbaud);
}

void sigma_set_nrzi (sigma_chan_t * c, int nrzi)
{
	c->opt.rcor.encod = (nrzi ? CD2400_ENCOD_NRZI : CD2400_ENCOD_NRZ);
	ddk_outb (CAR (c->port), c->num & 3);
	ddk_outb (RCOR (c->port), BYTE c->opt.rcor);
}

static int sigma_send (sigma_chan_t * c, char *data, int len, void *attachment)
{
	u8 *buf;
	unsigned cnt_port, sts_port;
	void **attp;

	/*
	 * Set the current channel number. 
	 */
	ddk_outb (CAR (c->port), c->num & 3);

	/*
	 * Determine the buffer order. 
	 */
	if (ddk_inb (DMABSTS (c->port)) & DMABSTS_NTBUF) {
		if (ddk_inb (BTBSTS (c->port)) & BSTS_OWN24) {
			buf = c->atbuf;
			cnt_port = ATBCNT (c->port);
			sts_port = ATBSTS (c->port);
			attp = &c->attach[0];
		} else {
			buf = c->btbuf;
			cnt_port = BTBCNT (c->port);
			sts_port = BTBSTS (c->port);
			attp = &c->attach[1];
		}
	} else {
		if (ddk_inb (ATBSTS (c->port)) & BSTS_OWN24) {
			buf = c->btbuf;
			cnt_port = BTBCNT (c->port);
			sts_port = BTBSTS (c->port);
			attp = &c->attach[1];
		} else {
			buf = c->atbuf;
			cnt_port = ATBCNT (c->port);
			sts_port = ATBSTS (c->port);
			attp = &c->attach[0];
		}
	}
	/*
	 * Is it busy? 
	 */
	if (ddk_inb (sts_port) & BSTS_OWN24)
		return -1;

	memcpy (buf, data, len);
	*attp = attachment;

	/*
	 * Start transmitter. 
	 */
	ddk_outw (cnt_port, len);
	ddk_outb (sts_port, BSTS_EOFR | BSTS_INTR | BSTS_OWN24);

	/*
	 * Enable TXMPTY interrupt,
	 * * to catch the case when the second buffer is empty. 
	 */
	if (c->mode != SIGMA_MODE_ASYNC) {
		if ((ddk_inb (ATBSTS (c->port)) & BSTS_OWN24) && (ddk_inb (BTBSTS (c->port)) & BSTS_OWN24)) {
			ddk_outb (IER (c->port), IER_RXD | IER_TXD | IER_TXMPTY);
		} else
			ddk_outb (IER (c->port), IER_RXD | IER_TXD);
	}
	return 0;
}

/*
 * Number of free buffs
 */
int sigma_buf_free (sigma_chan_t * c)
{
	return !(ddk_inb (ATBSTS (c->port)) & BSTS_OWN24) + !(ddk_inb (BTBSTS (c->port)) & BSTS_OWN24);
}

/*
 * Send the data packet.
 */
int sigma_send_packet (sigma_chan_t * c, char *data, int len, void *attachment)
{
	if (len > SIGMA_DMABUFSZ)
		return -2;
	if (c->mode == SIGMA_MODE_ASYNC) {
		static char buf[SIGMA_DMABUFSZ * 2];
		char *p, *t = buf;

		/*
		 * Async -- double all nulls. 
		 */
		for (p = data; p < data + len; ++p)
			if ((*t++ = *p) == 0)
				*t++ = 0;
		if (t > buf + SIGMA_DMABUFSZ) {
			int err = sigma_send (c, buf, SIGMA_DMABUFSZ, attachment);
			if (err)
				return err;
			return sigma_send (c, buf + SIGMA_DMABUFSZ, t - buf - SIGMA_DMABUFSZ, attachment);
		}
		return sigma_send (c, buf, t - buf, attachment);
	}
	return sigma_send (c, data, len, attachment);
}

static int sigma_receive_interrupt (sigma_chan_t * c)
{
	unsigned risr;
	int len = 0, rbsz;

	++c->rintr;
	risr = ddk_inw (RISR (c->port));

	/*
	 * Compute optimal receiver buffer length 
	 */
	rbsz = sigma_compute_buf_len (c);
	if (c->mode == SIGMA_MODE_ASYNC && (risr & RISA_TIMEOUT)) {
		u32 rcbadr = (u16) ddk_inw (RCBADRL (c->port))
			| (u32) ddk_inw (RCBADRU (c->port)) << 16;
		u8 *buf = 0;
		unsigned cnt_port = 0, sts_port = 0;

		if (rcbadr >= c->brphys && rcbadr < c->brphys + SIGMA_DMABUFSZ) {
			buf = c->brbuf;
			len = rcbadr - c->brphys;
			cnt_port = BRBCNT (c->port);
			sts_port = BRBSTS (c->port);
		} else if (rcbadr >= c->arphys && rcbadr < c->arphys + SIGMA_DMABUFSZ) {
			buf = c->arbuf;
			len = rcbadr - c->arphys;
			cnt_port = ARBCNT (c->port);
			sts_port = ARBSTS (c->port);
		}

		if (len) {
			c->ibytes += len;
			c->received_data = buf;
			c->received_len = len;

			/*
			 * Restart receiver. 
			 */
			ddk_outw (cnt_port, rbsz);
			ddk_outb (sts_port, BSTS_OWN24);
		}
		return (REOI_TERMBUFF);
	}

	/*
	 * Receive errors. 
	 */
	if (risr & RIS_OVERRUN) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_OVERRUN);
	} else if (c->mode != SIGMA_MODE_ASYNC && (risr & RISH_CRCERR)) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_CRC);
	} else if (c->mode != SIGMA_MODE_ASYNC && (risr & (RISH_RXABORT | RISH_RESIND))) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_FRAME);
	} else if (c->mode == SIGMA_MODE_ASYNC && (risr & RISA_PARERR)) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_CRC);
	} else if (c->mode == SIGMA_MODE_ASYNC && (risr & RISA_FRERR)) {
		++c->ierrs;
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_FRAME);
	} else if (c->mode == SIGMA_MODE_ASYNC && (risr & RISA_BREAK)) {
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_BREAK);
	} else if (!(risr & RIS_EOBUF)) {
		++c->ierrs;
	} else {
		/*
		 * Handle received data. 
		 */
		len = (risr & RIS_BB) ? ddk_inw (BRBCNT (c->port)) : ddk_inw (ARBCNT (c->port));

		if (len > SIGMA_DMABUFSZ) {
			/*
			 * Fatal error: actual DMA transfer size
			 * * exceeds our buffer size.  It could be caused
			 * * by incorrectly programmed DMA register or
			 * * hardware fault.  Possibly, should panic here. 
			 */
			len = SIGMA_DMABUFSZ;
		} else if (c->mode != SIGMA_MODE_ASYNC && !(risr & RIS_EOFR)) {
			/*
			 * The received frame does not fit in the DMA buffer.
			 * * It could be caused by serial lie noise,
			 * * or if the peer has too big MTU. 
			 */
			if (!c->overflow) {
				if (c->call_on_err)
					c->call_on_err (c, SIGMA_ERR_OVERFLOW);
				c->overflow = 1;
				++c->ierrs;
			}
		} else if (!c->overflow) {
			if (risr & RIS_BB) {
				c->received_data = c->brbuf;
				c->received_len = len;
			} else {
				c->received_data = c->arbuf;
				c->received_len = len;
			}
			if (c->mode != SIGMA_MODE_ASYNC)
				++c->ipkts;
			c->ibytes += len;
		} else
			c->overflow = 0;
	}

	/*
	 * Restart receiver. 
	 */
	if (!(ddk_inb (ARBSTS (c->port)) & BSTS_OWN24)) {
		ddk_outw (ARBCNT (c->port), rbsz);
		ddk_outb (ARBSTS (c->port), BSTS_OWN24);
	}
	if (!(ddk_inb (BRBSTS (c->port)) & BSTS_OWN24)) {
		ddk_outw (BRBCNT (c->port), rbsz);
		ddk_outb (BRBSTS (c->port), BSTS_OWN24);
	}

	/*
	 * Discard exception characters. 
	 */
	if ((risr & RISA_SCMASK) && c->aopt.cor2.ixon)
		return REOI_DISCEXC;
	return 0;
}

static void sigma_transmit_interrupt (sigma_chan_t * c)
{
	u8 tisr;
	int len = 0;

	++c->tintr;
	tisr = ddk_inb (TISR (c->port));
	if (tisr & TIS_UNDERRUN) {	/* Transmit underrun error */
		if (c->call_on_err)
			c->call_on_err (c, SIGMA_ERR_UNDERRUN);
		++c->oerrs;
	} else if (tisr & (TIS_EOBUF | TIS_TXEMPTY | TIS_TXDATA)) {
		/*
		 * Call processing function 
		 */
		if (tisr & TIS_BB) {
			len = ddk_inw (BTBCNT (c->port));
			if (c->call_on_tx)
				c->call_on_tx (c, c->attach[1], len);
		} else {
			len = ddk_inw (ATBCNT (c->port));
			if (c->call_on_tx)
				c->call_on_tx (c, c->attach[0], len);
		}
		if (c->mode != SIGMA_MODE_ASYNC && len != 0)
			++c->opkts;
		c->obytes += len;
	}

	/*
	 * Enable TXMPTY interrupt,
	 * * to catch the case when the second buffer is empty. 
	 */
	if (c->mode != SIGMA_MODE_ASYNC) {
		if ((ddk_inb (ATBSTS (c->port)) & BSTS_OWN24) && (ddk_inb (BTBSTS (c->port)) & BSTS_OWN24)) {
			ddk_outb (IER (c->port), IER_RXD | IER_TXD | IER_TXMPTY);
		} else
			ddk_outb (IER (c->port), IER_RXD | IER_TXD);
	}
}

void sigma_int_handler (sigma_board_t * b)
{
	u8 livr;
	sigma_chan_t *c;

	while (!(ddk_inw (BSR (b->port)) & BSR_NOINTR)) {
		/*
		 * Enter the interrupt context, using IACK bus cycle.
		 * Read the local interrupt vector register. 
		 */
		livr = ddk_inb (IACK (b->port, BRD_INTR_LEVEL));
		c = b->chan + (livr >> 2 & 0xf);
		if (c->type == SIGMA_TYPE_NONE)
			continue;
		switch (livr & 3) {
			case LIV_MODEM:	/* modem interrupt */
				ddk_outb (MEOIR (c->port), 0);
				break;
			case LIV_EXCEP:	/* receive exception */
			case LIV_RXDATA:	/* receive interrupt */
				ddk_outb (REOIR (c->port), sigma_receive_interrupt (c));
				if (c->call_on_rx && c->received_data) {
					c->call_on_rx (c, c->received_data, c->received_len);
					c->received_data = 0;
				}
				break;
			case LIV_TXDATA:	/* transmit interrupt */
				sigma_transmit_interrupt (c);
				ddk_outb (TEOIR (c->port), 0);
				break;
		}
	}
}

/*
 * Register event processing functions
 */
void sigma_register_transmit (sigma_chan_t * c, void (*func) (sigma_chan_t * c, void *attachment, int len))
{
	c->call_on_tx = func;
}

void sigma_register_receive (sigma_chan_t * c, void (*func) (sigma_chan_t * c, char *data, int len))
{
	c->call_on_rx = func;
}

void sigma_register_error (sigma_chan_t * c, void (*func) (sigma_chan_t * c, int data))
{
	c->call_on_err = func;
}

/*
 * Async protocol functions.
 */

/*
 * Enable/disable transmitter.
 */
void sigma_transmitter_ctl (sigma_chan_t * c, int start)
{
	ddk_outb (CAR (c->port), c->num & 3);
	sigma_cmd (c->port, start ? CCR_ENTX : CCR_DISTX);
}

/*
 * Discard all data queued in transmitter.
 */
void sigma_purge_transmit (sigma_chan_t * c)
{
	ddk_outb (CAR (c->port), c->num & 3);
	sigma_cmd (c->port, CCR_CLRTX);
}

int sigma_is_transmit (sigma_chan_t * c)
{
	return ((ddk_inb (ATBSTS (c->port)) | ddk_inb (BTBSTS (c->port)))
		& BSTS_OWN24) != 0;
}

/*
 * Send the XON/XOFF flow control symbol.
 */
void sigma_xflow_ctl (sigma_chan_t * c, int on)
{
	ddk_outb (CAR (c->port), c->num & 3);
	ddk_outb (STCR (c->port), STC_SNDSPC | (on ? STC_SSPC_1 : STC_SSPC_2));
}

/*
 * Send the break signal for a given number of milliseconds.
 */
void sigma_send_break (sigma_chan_t * c, int msec)
{
	static u8 buf[128];
	u8 *p;

	p = buf;
	*p++ = 0;		/* extended transmit command */
	*p++ = 0x81;		/* send break */

	if (msec > 10000)	/* max 10 seconds */
		msec = 10000;
	if (msec < 10)		/* min 10 msec */
		msec = 10;
	while (msec > 0) {
		int ms = 250;	/* 250 msec */

		if (ms > msec)
			ms = msec;
		msec -= ms;
		*p++ = 0;	/* extended transmit command */
		*p++ = 0x82;	/* insert delay */
		*p++ = ms;
	}
	*p++ = 0;		/* extended transmit command */
	*p++ = 0x83;		/* stop break */

	sigma_send (c, buf, p - buf, 0);
}

/*
 * Set async parameters.
 */
void
sigma_set_async_param (sigma_chan_t * c, int baud, int bits, int parity,
		       int stop2, int ignpar, int rtscts, int ixon, int ixany, int symstart, int symstop)
{
	int clock, period;
	sigma_cor1_async_t cor1;

	/*
	 * Set character length and parity mode. 
	 */
	BYTE cor1 = 0;
	cor1.charlen |= bits - 1;
	if (ignpar)
		cor1.ignpar |= 1;
	switch (parity) {
		default:
		case SIGMA_PAR_NONE:
			break;
		case SIGMA_PAR_EVEN:
			cor1.parmode |= PAR_NORMAL;
			cor1.parity |= PAR_EVEN;
			break;
		case SIGMA_PAR_ODD:
			cor1.parmode |= PAR_NORMAL;
			cor1.parity |= PAR_ODD;
			break;
		case SIGMA_PAR_0:
			cor1.parmode |= PAR_FORCE;
			cor1.parity |= PAR_EVEN;
			break;
		case SIGMA_PAR_1:
			cor1.parmode |= PAR_FORCE;
			cor1.parity |= PAR_ODD;
			break;
	}

	/*
	 * Enable/disable hardware CTS. 
	 */
	c->aopt.cor2.ctsae = rtscts ? 1 : 0;

	/*
	 * Enable extended transmit command mode.
	 * * Unfortunately, there is no other method for sending break. 
	 */
	c->aopt.cor2.etc = 1;

	/*
	 * Enable/disable hardware XON/XOFF. 
	 */
	c->aopt.cor2.ixon = ixon ? 1 : 0;
	c->aopt.cor2.ixany = ixany ? 1 : 0;

	/*
	 * Set the number of stop bits. 
	 */
	if (stop2)
		c->aopt.cor3.stopb = STOPB_2;
	else
		c->aopt.cor3.stopb = STOPB_1;

	/*
	 * Disable/enable passing XON/XOFF chars to the host. 
	 */
	c->aopt.cor3.scde = ixon ? 1 : 0;
	c->aopt.cor3.flowct = ixon ? FLOWCC_NOTPASS : FLOWCC_PASS;

	c->aopt.schr1 = symstart;	/* XON */
	c->aopt.schr2 = symstop;	/* XOFF */

	/*
	 * Set current channel number. 
	 */
	ddk_outb (CAR (c->port), c->num & 3);

	/*
	 * Set up clock values. 
	 */
	if (baud) {
		c->rxbaud = c->txbaud = baud;

		/*
		 * Receiver. 
		 */
		sigma_clock (c->oscfreq, c->rxbaud, &clock, &period);
		c->opt.rcor.clk = clock;
		ddk_outb (RCOR (c->port), BYTE c->opt.rcor);
		ddk_outb (RBPR (c->port), period);

		/*
		 * Transmitter. 
		 */
		sigma_clock (c->oscfreq, c->txbaud, &clock, &period);
		c->opt.tcor.clk = clock;
		c->opt.tcor.ext1x = 0;
		ddk_outb (TCOR (c->port), BYTE c->opt.tcor);
		ddk_outb (TBPR (c->port), period);
	}
	ddk_outb (COR2 (c->port), BYTE c->aopt.cor2);
	ddk_outb (COR3 (c->port), BYTE c->aopt.cor3);
	ddk_outb (SCHR1 (c->port), c->aopt.schr1);
	ddk_outb (SCHR2 (c->port), c->aopt.schr2);

	if (BYTE c->aopt.cor1 != BYTE cor1) {
		BYTE c->aopt.cor1 = BYTE cor1;

		ddk_outb (COR1 (c->port), BYTE c->aopt.cor1);
		/*
		 * Any change to COR1 require reinitialization. 
		 */
		/*
		 * Unfortunately, it may cause transmitter glitches... 
		 */
		sigma_cmd (c->port, CCR_INITCH);
	}
}

/*
 * Set mode: SIGMA_MODE_ASYNC or SIGMA_MODE_HDLC.
 * Both receiver and transmitter are disabled.
 */
int sigma_set_mode (sigma_chan_t * c, int mode)
{
	if (mode == SIGMA_MODE_HDLC) {
		if (c->type == SIGMA_TYPE_ASYNC)
			return -1;

		if (c->mode == SIGMA_MODE_HDLC)
			return 0;

		c->mode = SIGMA_MODE_HDLC;
	} else if (mode == SIGMA_MODE_ASYNC) {
		if (c->type == SIGMA_TYPE_SYNC_RS232 || c->type == SIGMA_TYPE_SYNC_V35 || c->type == SIGMA_TYPE_SYNC_RS449)
			return -1;

		if (c->mode == SIGMA_MODE_ASYNC)
			return 0;

		c->mode = SIGMA_MODE_ASYNC;
		c->opt.tcor.ext1x = 0;
		c->opt.tcor.llm = 0;
		c->opt.rcor.dpll = 0;
		c->opt.rcor.encod = CD2400_ENCOD_NRZ;
		if (!c->txbaud || !c->rxbaud)
			c->txbaud = c->rxbaud = 9600;
	} else
		return -1;

	sigma_setup_chan (c);
	sigma_start_chan (c, 0, 0);
	sigma_enable_receive (c, 0);
	sigma_enable_transmit (c, 0);
	return 0;
}

/*
 * Set port type for old models of Sigma
 */
void sigma_set_port (sigma_chan_t * c, int iftype)
{
	if (c->board->type == SIGMA_MODEL_OLD) {
		switch (c->num) {
			case 0:
				if ((c->board->if0type != 0) == (iftype != 0))
					return;
				c->board->if0type = iftype;
				c->board->bcr0 &= ~BCR0_UMASK;
				if (c->board->if0type && (c->type == SIGMA_TYPE_UNIV_RS449 || c->type == SIGMA_TYPE_UNIV_V35))
					c->board->bcr0 |= BCR0_UI_RS449;
				ddk_outb (BCR0 (c->board->port), c->board->bcr0);
				break;
			case 8:
				if ((c->board->if8type != 0) == (iftype != 0))
					return;
				c->board->if8type = iftype;
				c->board->bcr0b &= ~BCR0_UMASK;
				if (c->board->if8type && (c->type == SIGMA_TYPE_UNIV_RS449 || c->type == SIGMA_TYPE_UNIV_V35))
					c->board->bcr0b |= BCR0_UI_RS449;
				ddk_outb (BCR0 (c->board->port + 0x10), c->board->bcr0b);
				break;
		}
	}
}

/*
 * Get port type for old models of Sigma
 * -1 Fixed port type or auto detect
 *  0 RS232
 *  1 V35
 *  2 RS449
 */
int sigma_get_port (sigma_chan_t * c)
{
	int iftype;

	if (c->board->type == SIGMA_MODEL_OLD) {
		switch (c->num) {
			case 0:
				iftype = c->board->if0type;
				break;
			case 8:
				iftype = c->board->if8type;
				break;
			default:
				return -1;
		}

		if (iftype)
			switch (c->type) {
				case SIGMA_TYPE_UNIV_V35:
					return 1;
					break;
				case SIGMA_TYPE_UNIV_RS449:
					return 2;
					break;
				default:
					return -1;
					break;
		} else
			return 0;
	} else
		return -1;
}

void sigma_intr_off (sigma_board_t * b)
{
	ddk_outb (BCR0 (b->port), b->bcr0 & ~BCR0_IRQ_MASK);
	if (b->chan[8].port || b->chan[12].port)
		ddk_outb (BCR0 (b->port + 0x10), b->bcr0b & ~BCR0_IRQ_MASK);
}

void sigma_intr_on (sigma_board_t * b)
{
	ddk_outb (BCR0 (b->port), b->bcr0);
	if (b->chan[8].port || b->chan[12].port)
		ddk_outb (BCR0 (b->port + 0x10), b->bcr0b);
}

int sigma_checkintr (sigma_board_t * b)
{
	return (!(ddk_inw (BSR (b->port)) & BSR_NOINTR));
}

/*
 * Debug code for Cronyx Tau-32
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
 * $Id: $
 */

static void M32X_Bar1_Dump (M32X_RegistersBar1 * RegistersBar1)
{
	ddk_assert (sizeof (M32X_RegistersBar1) == M32X_RegistersBar1Size);
	ddk_kd_print (("M32X::Bar1_Dump ()\n"));

#define DUMP_BAR1_REG(item) \
        ddk_dbg_print ("%-16s /0x%02X = 0x%08X\n", #item, (unsigned) &(((M32X_RegistersBar1*)0)->item), (unsigned) RegistersBar1->item)

	DUMP_BAR1_REG (CONF_rw_00.all);
	DUMP_BAR1_REG (CMD_w_04);	/*dedicated, writeonly */
	DUMP_BAR1_REG (STAT_rw_08);
	DUMP_BAR1_REG (IMASK_rw_0C);
	DUMP_BAR1_REG (reserved_10);
	DUMP_BAR1_REG (PIQBA_rw_14);
	DUMP_BAR1_REG (PIQL_rw_18);
	DUMP_BAR1_REG (reserved_1C);

	DUMP_BAR1_REG (MODE1_rw_20.all);
	DUMP_BAR1_REG (MODE2_rw_24.all);
	DUMP_BAR1_REG (CCBA_rw_28);
	DUMP_BAR1_REG (TXPOLL_rw_2C);
	DUMP_BAR1_REG (TIQBA_rw_30);
	DUMP_BAR1_REG (TIQL_rw_34);
	DUMP_BAR1_REG (RIQBA_rw_38);
	DUMP_BAR1_REG (RIQL_rw_3C);

	DUMP_BAR1_REG (LCONF_rw_40);
	DUMP_BAR1_REG (LCCBA_rw_44);
	DUMP_BAR1_REG (reserved_48);
	DUMP_BAR1_REG (LTRAN_w_4C);	/*dedicated, writeonly */
	DUMP_BAR1_REG (LTIQBA_rw_50);
	DUMP_BAR1_REG (LTIQL_rw_54);
	DUMP_BAR1_REG (LRIQBA_rw_58);
	DUMP_BAR1_REG (LRIQL_rw_5C);
	DUMP_BAR1_REG (LREG0_rw_60);
	DUMP_BAR1_REG (LREG1_rw_64);
	DUMP_BAR1_REG (LREG2_rw_68);
	DUMP_BAR1_REG (LREG3_rw_6C);
	DUMP_BAR1_REG (LREG4_rw_70);
	DUMP_BAR1_REG (LREG5_rw_74);
	DUMP_BAR1_REG (LREG6_rw_78);
	DUMP_BAR1_REG (LSTAT_r_7C);	/*readonly */

	DUMP_BAR1_REG (GPDIR_rw_80);
	DUMP_BAR1_REG (GPDATA_rw_84);
	DUMP_BAR1_REG (GPOD_rw_88);
	DUMP_BAR1_REG (reserved_8C);

	DUMP_BAR1_REG (SSCCON_rw_90);
	DUMP_BAR1_REG (SSCBR_rw_94);
	DUMP_BAR1_REG (SSCTB_rw_98);
	DUMP_BAR1_REG (SSCRB_r_9C);	/*readonly */
	DUMP_BAR1_REG (SSCCSE_rw_A0);
	DUMP_BAR1_REG (SSCIM_rw_A4);
	DUMP_BAR1_REG (reserved_A8);
	DUMP_BAR1_REG (reserved_AC);

	DUMP_BAR1_REG (IOMCON1_rw_B0);
	DUMP_BAR1_REG (IOMCON2_rw_B4);
	DUMP_BAR1_REG (IOMSTAT_r_B8);	/*readonly */
	DUMP_BAR1_REG (reserved_BC);
	DUMP_BAR1_REG (IOMCIT0_rw_C0);
	DUMP_BAR1_REG (IOMCIT1_rw_C4);
	DUMP_BAR1_REG (IOMCIR0_r_C8);	/*readonly */
	DUMP_BAR1_REG (IOMCIR1_r_CC);	/*readonly */
	DUMP_BAR1_REG (IOMTMO_rw_D0);
	DUMP_BAR1_REG (IOMRMO_r_D4);	/*readonly */
	DUMP_BAR1_REG (reserved_D8);
	DUMP_BAR1_REG (reserved_DC);

	DUMP_BAR1_REG (MBCMD_rw_E0);
	DUMP_BAR1_REG (MBDATA1_rw_E4);
	DUMP_BAR1_REG (MBDATA2_rw_E8);
	DUMP_BAR1_REG (MBDATA3_rw_EC);
	DUMP_BAR1_REG (MBDATA4_rw_F0);
	DUMP_BAR1_REG (MBDATA5_rw_F4);
	DUMP_BAR1_REG (MBDATA6_rw_F8);
	DUMP_BAR1_REG (MBDATA7_rw_FC);

#undef DUMP_BAR1_REG
	ddk_kd_print (("\n"));
}

static void M32X_Bar1_DumpNonZeroWriteables (M32X_RegistersBar1 * RegistersBar1)
{
	ddk_assert (sizeof (M32X_RegistersBar1) == M32X_RegistersBar1Size);
	ddk_dbg_print ("=== M32X_Bar1_DumpNonZeroWriteables:\n");

#define DUMP_BAR1_REG(item) \
        if (RegistersBar1->item != 0) \
            ddk_dbg_print ("   %-12s /0x%02X = 0x%08X\n", #item, (unsigned) &(((M32X_RegistersBar1*)0)->item), (unsigned) RegistersBar1->item)

	DUMP_BAR1_REG (CONF_rw_00.all);
	/*
	 * DUMP_BAR1_REG (CMD_w_04); dedicated
	 */
	/*
	 * DUMP_BAR1_REG (STAT_rw_08); special
	 */
	DUMP_BAR1_REG (IMASK_rw_0C);
	/*
	 * DUMP_BAR1_REG (reserved_10);
	 */
	DUMP_BAR1_REG (PIQBA_rw_14);
	DUMP_BAR1_REG (PIQL_rw_18);
	/*
	 * DUMP_BAR1_REG (reserved_1C);
	 */

	DUMP_BAR1_REG (MODE1_rw_20.all);
	DUMP_BAR1_REG (MODE2_rw_24.all);
	DUMP_BAR1_REG (CCBA_rw_28);
	DUMP_BAR1_REG (TXPOLL_rw_2C);
	DUMP_BAR1_REG (TIQBA_rw_30);
	DUMP_BAR1_REG (TIQL_rw_34);
	DUMP_BAR1_REG (RIQBA_rw_38);
	DUMP_BAR1_REG (RIQL_rw_3C);

	DUMP_BAR1_REG (LCONF_rw_40);
	DUMP_BAR1_REG (LCCBA_rw_44);
	/*
	 * DUMP_BAR1_REG (reserved_48);
	 */
	/*
	 * DUMP_BAR1_REG (LTRAN_w_4C); dedicated
	 */
	DUMP_BAR1_REG (LTIQBA_rw_50);
	DUMP_BAR1_REG (LTIQL_rw_54);
	DUMP_BAR1_REG (LRIQBA_rw_58);
	DUMP_BAR1_REG (LRIQL_rw_5C);
	DUMP_BAR1_REG (LREG0_rw_60);
	DUMP_BAR1_REG (LREG1_rw_64);
	DUMP_BAR1_REG (LREG2_rw_68);
	DUMP_BAR1_REG (LREG3_rw_6C);
	DUMP_BAR1_REG (LREG4_rw_70);
	DUMP_BAR1_REG (LREG5_rw_74);
	DUMP_BAR1_REG (LREG6_rw_78);
	/*
	 * DUMP_BAR1_REG (LSTAT_r_7C); readonly
	 */

	DUMP_BAR1_REG (GPDIR_rw_80);
	DUMP_BAR1_REG (GPDATA_rw_84);
	DUMP_BAR1_REG (GPOD_rw_88);
	/*
	 * DUMP_BAR1_REG (reserved_8C);
	 */

	DUMP_BAR1_REG (SSCCON_rw_90);
	DUMP_BAR1_REG (SSCBR_rw_94);
	DUMP_BAR1_REG (SSCTB_rw_98);
	/*
	 * DUMP_BAR1_REG (SSCRB_r_9C); readonly
	 */
	DUMP_BAR1_REG (SSCCSE_rw_A0);
	DUMP_BAR1_REG (SSCIM_rw_A4);
	/*
	 * DUMP_BAR1_REG (reserved_A8);
	 */
	/*
	 * DUMP_BAR1_REG (reserved_AC);
	 */

	DUMP_BAR1_REG (IOMCON1_rw_B0);
	DUMP_BAR1_REG (IOMCON2_rw_B4);
	/*
	 * DUMP_BAR1_REG (IOMSTAT_r_B8); readonly
	 */
	/*
	 * DUMP_BAR1_REG (reserved_BC);
	 */
	DUMP_BAR1_REG (IOMCIT0_rw_C0);
	DUMP_BAR1_REG (IOMCIT1_rw_C4);
	/*
	 * DUMP_BAR1_REG (IOMCIR0_r_C8); readonly
	 */
	/*
	 * DUMP_BAR1_REG (IOMCIR1_r_CC); readonly
	 */
	/*
	 * DUMP_BAR1_REG (IOMTMO_rw_D0); too hot...
	 */
	/*
	 * DUMP_BAR1_REG (IOMRMO_r_D4); readonly
	 */
	/*
	 * DUMP_BAR1_REG (reserved_D8);
	 */
	/*
	 * DUMP_BAR1_REG (reserved_DC);
	 */

	DUMP_BAR1_REG (MBCMD_rw_E0);
	DUMP_BAR1_REG (MBDATA1_rw_E4);
	DUMP_BAR1_REG (MBDATA2_rw_E8);
	DUMP_BAR1_REG (MBDATA3_rw_EC);
	DUMP_BAR1_REG (MBDATA4_rw_F0);
	DUMP_BAR1_REG (MBDATA5_rw_F4);
	DUMP_BAR1_REG (MBDATA6_rw_F8);
	DUMP_BAR1_REG (MBDATA7_rw_FC);

#undef DUMP_BAR1_REG
}

static void M32X_DumpChannelSpecification (M32X* b, unsigned number)
{
	u32 flags, mask, command, mode;

	flags = b->PCM_ccb.Channels[number].flags_a.all;
	ddk_dbg_print ("\n\t%02d - 0x%08X | FRDA = 0x%08X | FTDA = 0x%08X | 0x%08X",
		  number, flags, b->PCM_ccb.Channels[number].frda,
		  b->PCM_ccb.Channels[number].ftda, b->PCM_ccb.Channels[number].flags_b.all);

	mask = (flags >> 24) & 0xFF;
	ddk_kd_print (("\n\t   MSK = 0x%02X | ", mask));
	if (mask & 0x01)
		ddk_kd_print (("FIT "));
	if (mask & 0x02)
		ddk_kd_print (("FIR "));
	if (mask & 0x04)
		ddk_kd_print (("RE "));
	if (mask & 0x08)
		ddk_kd_print (("TE "));
	if (mask & 0x10)
		ddk_kd_print (("CH "));
	if (mask & 0x20)
		ddk_kd_print (("IFC "));
	if (mask & 0x40)
		ddk_kd_print (("SFE "));
	if (mask & 0x80)
		ddk_kd_print (("FE2 "));
	if (mask == 0)
		ddk_kd_print (("all enabled"));

	command = (flags >> 16) & 0x7F;
	ddk_kd_print (("\n\t   CMD = 0x%02X | ", command));
	/*
	 * 0 RI TI TO | TA TH RO RA
	 */
	switch (command & 0x43) {
		case 0x00:
			ddk_kd_print (("RX-CLEAR"));
			break;

		case 0x01:
			ddk_kd_print (("RX-FAST-ABORT"));
			break;

		case 0x02:
			ddk_kd_print (("RX-OFF"));
			break;

		case 0x03:
			ddk_kd_print (("RX-ABORT"));
			break;

		case 0x40:
			ddk_kd_print (("RX-JUMP"));
			break;

		case 0x41:
			ddk_kd_print (("RX-INIT"));
			break;

		default:
			ddk_kd_print (("illegal %d", command & 0x43));
	}
	ddk_kd_print ((" | "));

	/*
	 * 0 RI TI TO | TA TH RO RA
	 */
	switch (command & 0x38) {
		case 0x00:
			ddk_kd_print (("TX-CLEAR"));
			break;

		case 0x08:
			ddk_kd_print (("TX-FAST-ABORT"));
			break;

		case 0x10:
			ddk_kd_print (("TX-OFF"));
			break;

		case 0x18:
			ddk_kd_print (("TX-ABORT"));
			break;

		case 0x20:
			ddk_kd_print (("TX-JUMP"));
			break;

		case 0x28:
			ddk_kd_print (("TX-INIT"));
			break;

		default:
			ddk_kd_print (("illegal %d", command & 0x38));
	}
	if (command & 0x04)
		ddk_kd_print ((" TX-HOLD"));
	if (flags & 0x00800000u)
		ddk_kd_print ((" | new ITBS"));

	ddk_kd_print (("\n\t   CFG = %04X | ", flags & 0xFFFF));
	mode = (flags >> 1) & 0x03;
	switch (mode) {
		case 0:
			ddk_kd_print (("TMA | "));
			if (flags & 0x08)
				ddk_kd_print (("FILL/FILTER = 0x%02X", (flags >> 8) & 0xFF));
			else {
				ddk_kd_print (("FILL/FILTER-OFF"));
				if ((flags >> 8) & 0xFF)
					ddk_kd_print ((" illegal = 0x%02X", (flags >> 8) & 0xFF));
			}
			if (flags & 0x40)
				ddk_kd_print (("| CRC = 1"));
			if (flags & 0x30)
				ddk_kd_print (("| illegal TRV = %d", (flags >> 4) & 3));
			break;

		case 1:
			if (flags & 0x40)
				ddk_kd_print (("TMR"));
			else
				ddk_kd_print (("TMB"));
			if (flags & 0x30)
				ddk_kd_print (("| illegal TRV = %d", (flags >> 4) & 3));
			break;

		case 2:
			ddk_kd_print (("V.110/X30 | TRV = %d", (flags >> 4) & 3));
			if (flags & 0x40)
				ddk_kd_print (("| illegal CRC = 1"));
			break;

		case 3:
			ddk_dbg_print ("HDLC | CS = %d | IFTF = 0x%02X | CRC%02d",
				  (flags >> 8) & 1, (flags & 1) ? 0xFF : 0x7E, (flags & 0x40) ? 32 : 16);
			break;
	}

	ddk_kd_print ((" | ITBS = %d", b->PCM_ccb.Channels[number].flags_b.all & 0x3F));
	if (b->PCM_ccb.Channels[number].flags_b.all & ~0x3Fu)
		ddk_kd_print ((" | B/illegal 0x%08X", b->PCM_ccb.Channels[number].flags_b.all & ~0x3Fu));

	if (flags & 0x80)
		ddk_kd_print ((" | INVERSION"));
}

static void M32X_DumpAction (M32X* b, u32 action)
{
	int count = 0;
	int channel = -1;
	unsigned i;

	ddk_kd_print (("M32X-Action-Dump: 0x%04X | ", action));
	if (action & 0x0004) {
		ddk_kd_print (("IA"));
		count++;
	}
	if (action & 0x0040) {
		ddk_kd_print (("RES"));
		count++;
	}
	if (action & 0x8000) {
		ddk_kd_print (("IN"));
		count++;
	}
	if (action & 0x4000) {
		ddk_kd_print (("ICO"));
		count++;
	}
	if (action & 0x0038) {
		ddk_kd_print ((" | LOOP "));
		switch ((action & 0x0038) >> 3) {
			case 1:
				ddk_kd_print (("INT"));
				break;

			case 2:
				ddk_kd_print (("EXT"));
				break;

			case 3:
				ddk_kd_print (("OFF"));
				break;

			case 5:
				ddk_kd_print (("ch-INT"));
				break;

			case 6:
				ddk_kd_print (("ch-EXT"));
				break;

			default:
				ddk_kd_print (("illegal %d", (action & 0x0038) >> 3));
		}
		count++;
	}
	if (action & 0xC020) {
		channel = (action >> 8) & 0x1F;
		ddk_kd_print ((" | channel %02d", channel));
	}
	if ((action & (0x0004 | 0x0040 | 0x8000 | 0x4000 | 0x0038)) == 0)
		ddk_kd_print (("NOOP"));
	if (action & ~0x0000DF7C)
		ddk_kd_print (("\n\t*illegal bits %0x08X*", action & ~0x0000DF7C));
	if (count > 1)
		ddk_kd_print (("\n\t*illegal commands combinations (%d)*", count));
	if (count == 1) {
		if (action & 0x8040) {
			ddk_kd_print (("\n    All Timeslots Assignment:"));
			for (i = 0; i < M32X_UseTimeslots; i++) {
				M32X_TimeslotAssignment TS;

				TS.raw = b->PCM_ccb.Timeslots[i].raw;
				ddk_dbg_print ("\n\t%02d - 0x%08X"
					  " | rti = %d, rx_ch = %02d, rx_mask = 0x%02X"
					  " | tti = %d, tx_ch = %02d, tx_mask = 0x%02X",
					  i, TS.raw, TS.fields.rti,
					  TS.fields.rx_chan,
					  TS.fields.rx_mask, TS.fields.tti, TS.fields.tx_chan, TS.fields.tx_mask);
				if (i > 8)
					break;
			}
		}
		if (action & 0x0040) {
			ddk_kd_print (("\n    All Channels Specification:"));
			for (i = 0; i < M32X_UseChannels; i++) {
				M32X_DumpChannelSpecification (b, i);
				break;
			}
		} else if (action & 0xC000) {
			ddk_kd_print (("\n    Selected c Specification:"));
			M32X_DumpChannelSpecification (b, channel);
		}
	}
	ddk_kd_print (("\n"));
}

#ifdef WIN32
#       ifdef __cplusplus
extern "C"
#       endif
int __cdecl sprintf (char *, const char *, ...);
#endif

static void M32X_DumpActionLite (M32X* b, u32 action)
{
	char buffer[1024];
	char *s = buffer;
	int count = 0;
	int channel = -1;
	unsigned i;

	if (action & 0x0004) {
		s += sprintf (s, "IA ");
		count++;
	}
	if (action & 0x0040) {
		s += sprintf (s, "RES ");
		count++;
	}
	if (action & 0x8000) {
		s += sprintf (s, "IN ");
		count++;
	}
	if (action & 0x4000) {
		s += sprintf (s, "ICO ");
		count++;
	}
	if (action & 0x0038) {
		s += sprintf (s, "| LOOP ");
		switch ((action & 0x0038) >> 3) {
			case 1:
				s += sprintf (s, "INT ");
				break;

			case 2:
				s += sprintf (s, "EXT ");
				break;

			case 3:
				s += sprintf (s, "OFF ");
				break;

			case 5:
				s += sprintf (s, "ch-INT ");
				break;

			case 6:
				s += sprintf (s, "ch-EXT ");
				break;

			default:
				s += sprintf (s, "illegal %d ", (action & 0x0038) >> 3);
		}
		count++;
	}
	if (action & 0xC020) {
		channel = (action >> 8) & 0x1F;
		s += sprintf (s, "| ch %02d ", channel);
	}
	if ((action & (0x0004 | 0x0040 | 0x8000 | 0x4000 | 0x0038)) == 0)
		s += sprintf (s, "NOOP ");
	if (action & ~0x0000DF7C)
		s += sprintf (s, "\n\t*illegal bits %0x08X*", action & ~0x0000DF7C);
	if (count > 1)
		s += sprintf (s, "\n\t*illegal commands combinations (%d)*", count);
	if (count == 1) {
		if (action & 0x8040) {
			s += sprintf (s, "TS ");
			/*
			 * ddk_kd_print (("\n  All Timeslots Assignment:"));
			 */
			for (i = 0; i < M32X_UseTimeslots; i++) {
				M32X_TimeslotAssignment TS;

				TS.raw = b->PCM_ccb.Timeslots[i].raw;
				if (!TS.fields.tti || !TS.fields.rti) {
					s += sprintf (s, "%2d=", i);
					if (!TS.fields.tti)
						s += sprintf (s, "t%02d", TS.fields.tx_chan);
					if (!TS.fields.rti)
						s += sprintf (s, "r%02d", TS.fields.rx_chan);
					s += sprintf (s, " ");
				}
				/*
				 * ddk_dbg_print ("\n\t%02d - 0x%08X"
				 */
				/*
				 * " | rti = %d, rx_ch = %02d, rx_mask = 0x%02X"
				 */
				/*
				 * " | tti = %d, tx_ch = %02d, tx_mask = 0x%02X",
				 */
				/*
				 * i, TS.raw,
				 */
				/*
				 * TS.rti, TS.rx_chan, TS.rx_mask,
				 */
				/*
				 * TS.tti, TS.tx_chan, TS.tx_mask
				 */
				/*
				 * );
				 */
			}
		}
		if (action & 0x0040) {
			s += sprintf (s, "| all [0]=x%02X", b->PCM_ccb.Channels[0].flags_a.fields.command_nitbs);
			/*
			 * ddk_kd_print (("\n  All Channels Specification:"));
			 */
			/*
			 * for (unsigned i = 0; i < M32X_UseChannels; i++)
			 */
			/*
			 * M32X_DumpChannelSpecification (b, i);
			 */
		} else if (action & 0xC000) {
			s += sprintf (s, "cmd x%02X", b->PCM_ccb.Channels[channel].flags_a.fields.command_nitbs);
			/*
			 * ddk_kd_print (("\n  Selected c Specification:"));
			 */
			/*
			 * M32X_DumpChannelSpecification (b, channel);
			 */
		}
	}
	ddk_dbg_print ("peb: %s\n", buffer);
}

static __noinline void _print_io_chain (BOOLEAN Tx, M32X* b,
					M32X_Descriptor * pIo, PCI_PHYSICAL_ADDRESS Current, M32X_Descriptor * pHalt)
{
	unsigned count;
	M32X_Descriptor *p;

	for (count = 0, p = pIo; p->from && p->from != p;) {
		M32X_Descriptor *pp = p->from;

		if (pp->u.io.next != p->spa)
			break;
		p = pp;
		if (++count > 32) {
			ddk_dbg_print ("print_io_chain: Count (1) == %d\n", count);
			break;
		}
	}

	count = 0;
	while (1) {
		char buffer[64];
		char *s = buffer;

		if (p->spa == Current)
			*s++ = '^';
		if (p == pIo)
			*s++ = '-';
		if (p->u.io.u.fields.hold)
			*s++ = 'H';
		if (p->u.io.u.fields.hi)
			*s++ = 'I';
		if (Tx) {
			if (p->u.tx.u.fields.fe)
				*s++ = 'e';
			if (p->u.tx.u.fields.csm)
				*s++ = '!';
		} else {
			if (p->u.rx.u2.fields.fe)
				*s++ = 'e';
			if (p->u.rx.u2.fields.c)
				*s++ = 'c';
			if (p->u.rx.u2.fields.rof || p->u.rx.u2.fields.ra
			    || p->u.rx.u2.fields.lfd
			    || p->u.rx.u2.fields.nob
			    || p->u.rx.u2.fields.crco || p->u.rx.u2.fields.loss || p->u.rx.u2.fields.sf) {
				*s++ = 'X';
				if (p->u.rx.u2.fields.rof)
					*s++ = 'f';
				if (p->u.rx.u2.fields.ra)
					*s++ = 'a';
				if (p->u.rx.u2.fields.lfd)
					*s++ = 'l';
				if (p->u.rx.u2.fields.nob)
					*s++ = 'n';
				if (p->u.rx.u2.fields.crco)
					*s++ = 'r';
				if (p->u.rx.u2.fields.loss)
					*s++ = 'o';
				if (p->u.rx.u2.fields.sf)
					*s++ = 's';
			}
			if (p->u.rx.u2.fields.bno)
				s += sprintf (s, "%d", p->u.rx.u2.fields.bno);
		}
		if (p->u.io.data == 0)
			*s++ = '@';
		if (p->u.io.next == 0)
			*s++ = '|';
		*s = 0;

		/*
		 * ddk_dbg_print ("%s%08x.%s.%d", p != pHalt ? "" : "hlt", p->spa, buffer, p->io.no);
		 */
		if (p == pHalt)
			ddk_dbg_print ("hlt.%s.%d", buffer, p->u.io.u.fields.no);
		else
			ddk_dbg_print ("%08x.%s.%d", p->spa, buffer, p->u.io.u.fields.no);

		if (p->u.io.next == p->spa || p->u.io.next == 0 || p->u.io.data == 0)
			break;
		ddk_dbg_print (" => ");
		p = MX32_pa2desc (b, p->u.io.next);
		if (++count > 32) {
			ddk_dbg_print ("print_io_chain: Count (2) == %d\n", count);
			break;
		}
	}
	ddk_dbg_print ("\n");
}

static __noinline void print_io_chain (BOOLEAN Tx, M32X* b,
				       M32X_Descriptor * pIo, PCI_PHYSICAL_ADDRESS Current, M32X_Descriptor * pHalt)
{
	M32X_Descriptor *p = 0;

	if (pIo == 0)
		pIo = pHalt;
	ddk_dbg_print ("%cx_ph: ", Tx ? 't' : 'r');
	if (Current) {
		p = MX32_pa2desc (b, Current);
		_print_io_chain (Tx, b, p, Current, pHalt);
	} else
		ddk_dbg_print ("^0\n");
	if (p != pIo) {
		ddk_dbg_print ("%cx_lg: ", Tx ? 't' : 'r');
		_print_io_chain (Tx, b, pIo, Current, pHalt);
	}
}

static __noinline void print_io_chain_all (M32X* b)
{
	M32X_Descriptor *pIo;
	int c;

	ddk_dbg_print ("=== Tx-Io-Chains:\n");
	for (c = 0; c < M32X_UseChannels; c++) {
		ddk_dbg_print ("cd%d >> ", c);
		pIo = 0;
		if (b->txq[c].first)
			pIo = b->txq[c].first->Io.descFirst;
		print_io_chain (1, b, pIo, b->PCM_ccb.CurrentTxDescriptors[c], b->last_tx[c]);
	}
	ddk_dbg_print ("=== Rx-Io-Chains:\n");
	for (c = 0; c < M32X_UseChannels; c++) {
		ddk_dbg_print ("cd%d << ", c);
		pIo = 0;
		if (b->rxq[c].first)
			pIo = b->rxq[c].first->Io.descFirst;
		print_io_chain (0, b, pIo, b->PCM_ccb.CurrentRxDescriptors[c], b->halt_rx[c]);
	}
}

static void total_dump (M32X* b)
{
	unsigned i;

	ddk_dbg_print ("\n****************** total dump begin **************************\n");
	M32X_Bar1_DumpNonZeroWriteables ((M32X_RegistersBar1 *)
					 b->RegistersBar1_Hardware);

	ddk_dbg_print ("=== All Channels Specification:");
	for (i = 0; i < M32X_UseChannels; i++)
		M32X_DumpChannelSpecification (b, i);
	ddk_dbg_print ("\n");

	ddk_dbg_print ("=== All Timeslots Assignment:");
	for (i = 0; i < M32X_UseTimeslots; i++) {
		M32X_TimeslotAssignment TS;

		TS.raw = b->PCM_ccb.Timeslots[i].raw;
		ddk_dbg_print ("\n\t%02d - 0x%08X"
			  " | rti = %d, rx_ch = %02d, rx_mask = 0x%02X"
			  " | tti = %d, tx_ch = %02d, tx_mask = 0x%02X", i,
			  TS.raw, TS.fields.rti, TS.fields.rx_chan,
			  TS.fields.rx_mask, TS.fields.tti, TS.fields.tx_chan, TS.fields.tx_mask);
	}
	ddk_dbg_print ("\n");

	print_io_chain_all (b);
	ddk_dbg_print ("\n******************* total dump end ***************************\n");
}

static void M32X_DumpBuffer_a (void *VirtualAddress, unsigned Length)
{
	/*
	 * TRAP;
	 */
	unsigned j, i;
	u8 *buffer = (u8 *) VirtualAddress;

	for (j = 0; j < Length; j++)
		if (		/*buffer[j] != 0 */
			   /*
			    * && (buffer[j] != 0x7F || (j + 1 < Length && buffer[j + 1] != 0x7F))
			    */
			   /*
			    * && buffer[j] != 0xFF
			    */
			   1) {
			ddk_kd_print (("   Dump+%d:", j));
			for (i = 0; i < 32 && j < Length; j++)
				if (	/*buffer[j] != 0 */
					   /*
					    * && (buffer[j] != 0x7F || (j + 1 < Length && buffer[j + 1] != 0x7F))
					    */
					   /*
					    * && buffer[j] != 0xFF
					    */
					   1) {
					ddk_kd_print ((" %02X", buffer[j]));
					buffer[j] = 0xFF;
					i++;
				} else
					break;
			ddk_kd_print (("\n"));
			break;
		}
}

static void M32X_DumpBuffer_b (M32X* b, PCI_PHYSICAL_ADDRESS PhysicalAddress, unsigned Length)
{
	u8 *VirtualAddress = ((u8 *) b) + PhysicalAddress - b->pa;

	M32X_DumpBuffer_a (VirtualAddress, Length);
}

/*
 * Multichannel Network Interface Controller for HDLC (MUNICH32)
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
 * $Id: munich32x.c,v 1.56 2009-07-10 11:34:12 ly Exp $
 */

#define M32X_SELECTED_RATE M32X_RATE_2048
#define M32X_CLOCK 2048000.0
#define M32X_FRAME_DURATION		(32 * 8 / M32X_CLOCK)
#define M32X_PAUSE_SAFE_SCALE 1.2
#define M32X_ACTION_TIMEOUT_FACTOR 16	/* в фреймах E1, максимум 16, это
					 * примерно = PCI_CLK / 65535 (2
					 * ms) */

#define M32X_LBI_CLOCK_MODE 0	/* LBI-clock == PCI-clock */
#define M32X_LBI_CLOCK M32X_PCI_CLOCK
#define M32X_LBI_TICK_DURATION	(1.0 / M32X_LBI_CLOCK)
#define M32X_SAFE_PCI_ADDRESS 0xFFFFF000ul	/* 0x000FF000ul */

#define M32X_PA(field) \
	(b->pa + ddk_offsetof (M32X, field))

#ifndef M32X_BEFORE_CALLBACK
#	define M32X_BEFORE_CALLBACK(b)
#endif

#ifndef M32X_AFTER_CALLBACK
#	define M32X_AFTER_CALLBACK(b)
#endif

#define FLAG_DEEP_RX	1
#define FLAG_DEEP_TX	2
#define FLAG_DEEP_ACT	4
#define FLAG_DEEP_REQ	8
#define FLAG_DEEP_IRQ	16

static __forceinline void M32X_CopyTimeslotAssignment (M32X* b)
{
	int i;

	b->cm.TimeslotsChanges = 0;
	i = 32 - 1; do {
		b->PCM_ccb.Timeslots[i] = b->TargetTimeslots[i];
	} while (--i >= 0);
}

static __forceinline char M32X_IsPointerBetween (void *Pointer, void *Begin, void *End)
{
	return ((u8 *) Pointer) >= ((u8 *) Begin)
		&& ((u8 *) Pointer) < ((u8 *) End);
}

static __forceinline char M32X_IsValidDescriptor (M32X* b, M32X_dma_t * desc)
{
	return M32X_IsPointerBetween (desc, &b->DescriptorsPool[0], ARRAY_END (b->DescriptorsPool));
}

static __forceinline M32X_dma_t *MX32_pa2desc (M32X* b, u32 pa)
{
	M32X_dma_t *pResult;

	ddk_assert (pa != 0);
	pResult = (M32X_dma_t *) (((u8 *) b) + pa - b->pa);
	ddk_assert (M32X_IsValidDescriptor (b, pResult));
	return pResult;
}

#if DDK_DEBUG
#	include "munich32x-debug.h"
#endif

#if ddk_assert_check

may_static int MX32_is_valid_rx_cmd (unsigned cmd)
{
	switch (cmd & M32X_RECEIVE_BITS) {
		case M32X_RECEIVE_OFF:
		case M32X_RECEIVE_ABORT:
		case M32X_RECEIVE_INIT:
		case M32X_RECEIVE_CLEAR:
		case M32X_RECEIVE_JUMP:
		case M32X_RECEIVE_FAST_ABORT:
			return 1;
	}
	ddk_dbg_print ("MX32_is_valid_Rx_cmd: cmd = 0x%02X\n", cmd);
	ddk_trap();
	return 0;
}

may_static int MX32_is_valid_tx_cmd (unsigned cmd)
{
	switch (cmd & M32X_TRANSMIT_BITS) {
		case M32X_TRANSMIT_OFF:
		case M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD:
		case M32X_TRANSMIT_ABORT:
		case M32X_TRANSMIT_INIT:
		case M32X_TRANSMIT_CLEAR:
		case M32X_TRANSMIT_JUMP:
		case M32X_TRANSMIT_FAST_ABORT:
		return 1;
	}
	ddk_dbg_print ("MX32_is_valid_Tx_cmd: cmd = 0x%02X\n", cmd);
	ddk_trap();
	return 0;
}

#endif

may_static void M32X_Bar1_Clear (M32X_RegistersBar1 * RegistersBar1_Target)
{
	ddk_assert (sizeof (M32X_RegistersBar1) == M32X_RegistersBar1Size);
	ddk_memset (RegistersBar1_Target, 0, M32X_RegistersBar1Size);
}

static __forceinline void M32X_Bar1_Write (M32X_RegistersBar1 *
					   RegistersBar1_Source, volatile M32X_RegistersBar1 * RegistersBar1_Hardware)
{
	ddk_assert (sizeof (M32X_RegistersBar1) == M32X_RegistersBar1Size);

#define REWRITE_BAR1_REG(item) \
	RegistersBar1_Hardware->item = RegistersBar1_Source->item;

	REWRITE_BAR1_REG (CONF_rw_00.all);
	/*
	 * REWRITE_BAR1_REG (CMD_w_04); dedicated
	 * REWRITE_BAR1_REG (STAT_rw_08); special
	 */
	REWRITE_BAR1_REG (IMASK_rw_0C);
	/*
	 * REWRITE_BAR1_REG (reserved_10);
	 */
	REWRITE_BAR1_REG (PIQBA_rw_14);
	REWRITE_BAR1_REG (PIQL_rw_18);
	/*
	 * REWRITE_BAR1_REG (reserved_1C);
	 */

	REWRITE_BAR1_REG (MODE1_rw_20.all);
	REWRITE_BAR1_REG (MODE2_rw_24.all);
	REWRITE_BAR1_REG (CCBA_rw_28);
	REWRITE_BAR1_REG (TXPOLL_rw_2C);
	REWRITE_BAR1_REG (TIQBA_rw_30);
	REWRITE_BAR1_REG (TIQL_rw_34);
	REWRITE_BAR1_REG (RIQBA_rw_38);
	REWRITE_BAR1_REG (RIQL_rw_3C);

	REWRITE_BAR1_REG (LCONF_rw_40);
	REWRITE_BAR1_REG (LCCBA_rw_44);
	/*
	 * REWRITE_BAR1_REG (reserved_48);
	 * REWRITE_BAR1_REG (LTRAN_w_4C); dedicated
	 */
	REWRITE_BAR1_REG (LTIQBA_rw_50);
	REWRITE_BAR1_REG (LTIQL_rw_54);
	REWRITE_BAR1_REG (LRIQBA_rw_58);
	REWRITE_BAR1_REG (LRIQL_rw_5C);
	REWRITE_BAR1_REG (LREG0_rw_60);
	REWRITE_BAR1_REG (LREG1_rw_64);
	REWRITE_BAR1_REG (LREG2_rw_68);
	REWRITE_BAR1_REG (LREG3_rw_6C);
	REWRITE_BAR1_REG (LREG4_rw_70);
	REWRITE_BAR1_REG (LREG5_rw_74);
	REWRITE_BAR1_REG (LREG6_rw_78);
	/*
	 * REWRITE_BAR1_REG (LSTAT_r_7C); readonly
	 */

	REWRITE_BAR1_REG (GPDIR_rw_80);
	REWRITE_BAR1_REG (GPDATA_rw_84);
	REWRITE_BAR1_REG (GPOD_rw_88);
	/*
	 * REWRITE_BAR1_REG (reserved_8C);
	 */

	REWRITE_BAR1_REG (SSCCON_rw_90);
	REWRITE_BAR1_REG (SSCBR_rw_94);
	REWRITE_BAR1_REG (SSCTB_rw_98);
	/*
	 * REWRITE_BAR1_REG (SSCRB_r_9C); readonly
	 */
	REWRITE_BAR1_REG (SSCCSE_rw_A0);
	REWRITE_BAR1_REG (SSCIM_rw_A4);
	/*
	 * REWRITE_BAR1_REG (reserved_A8);
	 */
	/*
	 * REWRITE_BAR1_REG (reserved_AC);
	 */

	REWRITE_BAR1_REG (IOMCON1_rw_B0);
	REWRITE_BAR1_REG (IOMCON2_rw_B4);
	/*
	 * REWRITE_BAR1_REG (IOMSTAT_r_B8); readonly
	 */
	/*
	 * REWRITE_BAR1_REG (reserved_BC);
	 */
	REWRITE_BAR1_REG (IOMCIT0_rw_C0);
	REWRITE_BAR1_REG (IOMCIT1_rw_C4);
	/*
	 * REWRITE_BAR1_REG (IOMCIR0_r_C8); readonly
	 * REWRITE_BAR1_REG (IOMCIR1_r_CC); readonly
	 * REWRITE_BAR1_REG (IOMTMO_rw_D0); too hot...
	 * REWRITE_BAR1_REG (IOMRMO_r_D4); readonly
	 * REWRITE_BAR1_REG (reserved_D8);
	 * REWRITE_BAR1_REG (reserved_DC);
	 */

	REWRITE_BAR1_REG (MBCMD_rw_E0);
	REWRITE_BAR1_REG (MBDATA1_rw_E4);
	REWRITE_BAR1_REG (MBDATA2_rw_E8);
	REWRITE_BAR1_REG (MBDATA3_rw_EC);
	REWRITE_BAR1_REG (MBDATA4_rw_F0);
	REWRITE_BAR1_REG (MBDATA5_rw_F4);
	REWRITE_BAR1_REG (MBDATA6_rw_F8);
	REWRITE_BAR1_REG (MBDATA7_rw_FC);

#undef REWRITE_BAR1_REG
}

may_static void M32X_ch2ts (M32X* b, ddk_bitops_t Mask, unsigned c)
{
	int i = 32 - 1;

	do {
		if (ddk_bit_test (Mask, i)) {
			b->uta[i].TxChannel = (u8) c;
			b->uta[i].RxChannel = (u8) c;
			b->uta[i].TxFillmask = 0xFFu;
			b->uta[i].RxFillmask = 0xFFu;
		} else {
			if (b->uta[i].TxChannel == (u8) c)
				b->uta[i].TxChannel = 0xFFu;
			if (b->uta[i].RxChannel == (u8) c)
				b->uta[i].RxChannel = 0xFFu;
		}
	} while (--i >= 0);
}

M32X_INTERFACE_CALL char M32X_IsRxOnlyTMA (M32X* b)
{
#ifdef CRONYX_LYSAP
	return 0;
#else
	u32 RxMask = b->cm.rx;
	int i = 0;

	if (RxMask == 0 || (RxMask & ~b->cm.tx) != 0)
		return 0;
	do {
		if ((RxMask & 1)
		    && b->PCM_ccb.chan[i].flags_a.fields.mode /* != M32X_MODE_TRANSPARENT_A */)
			return 0;
		i++;
	} while (RxMask >>= 1);
	return 1;
#endif
}

may_static unsigned M32X_BuildTimestotAssignment (M32X* b)
{
	unsigned Result = 0;
	int i;

	b->cm.TimeslotsChanges = 0;
	i = 32 - 1;
	do {
		register M32X_TimeslotAssignment n;

		n.raw = 0;

		if (b->uta[i].RxChannel < M32X_UseChannels && b->uta[i].RxFillmask != 0) {
			n.fields.rx_chan |= b->uta[i].RxChannel;
			n.fields.rx_mask |= b->uta[i].RxFillmask;
		} else {
			n.fields.rti |= 1;
			n.fields.rx_chan |= 32 - 1;
		}

		if (b->uta[i].TxChannel < M32X_UseChannels && b->uta[i].TxFillmask != 0) {
			n.fields.tx_chan |= b->uta[i].TxChannel;
			n.fields.tx_mask |= b->uta[i].TxFillmask;
		} else {
			n.fields.tti |= 1;
			n.fields.tx_chan |= 32 - 1;
		}

		b->TargetTimeslots[i].raw = n.raw;
		if (n.raw != b->PCM_ccb.Timeslots[i].raw) {
			ddk_bit_set (b->cm.TimeslotsChanges, n.fields.tx_chan);
			ddk_bit_set (b->cm.TimeslotsChanges, n.fields.rx_chan);
			ddk_bit_set (b->cm.TimeslotsChanges, b->PCM_ccb.Timeslots[i].fields.tx_chan);
			ddk_bit_set (b->cm.TimeslotsChanges, b->PCM_ccb.Timeslots[i].fields.rx_chan);
			Result = 1;
		}
	} while (--i >= 0);
	return Result;
}

static __forceinline void M32X_Channel_ClearConfig_a (M32X_ChannelSpecification * cs)
{
	register M32X_ChannelSpecification_flags_a local;

	local.all = 0;
	local.fields.command_nitbs |= M32X_RECEIVE_OFF | M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD;

	cs->flags_a.all = local.all;
	cs->flags_b.all = 0;
}

static __forceinline void M32X_Channel_ClearConfig_b (M32X* b, unsigned c)
{
	M32X_Channel_ClearConfig_a (&b->PCM_ccb.chan[c]);
}

static __forceinline void M32X_Pause_256bits (M32X* b, unsigned DueFrames)
{
	unsigned duration;
	register M32X_RegistersBar1_CMD cmd;

	ddk_assert (b->as == M32X_ACTION_VOID);
	ddk_assert ((b->RegistersBar1_Hardware->STAT_rw_08 & (M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA)) == 0);

	/*
	 * clear previous timer acknowledge
	 */
	b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA;

	/*
	 * set & start timeout timer
	 * for DueFrames == 1 this is ~4125.4, but real pause would be 4127 (+2) ticks,
	 * it is about 256.1 bits at 2048 Mbps
	 */
	duration = ((DueFrames << 3) + DueFrames) << 9;	/* = DueFrames * (4096 + 512) */
	ddk_assert (duration > 1);
	if (duration > 0xFFFFul)
		duration = 0xFFFFul;

	cmd.all = 0;
	cmd.fields.timv |= duration;
	cmd.fields.timr |= 1;

	b->as = M32X_PAUSE_PENDING;
	b->RegistersBar1_Hardware->CMD_w_04 = cmd.all;
}

may_static void M32X_Stall_256bits (M32X* b, unsigned DueFrames)
{
	/* LY: each loop take at least 100 ns,
	   1250 = 125 mks = 256 bits */
	unsigned Count = 1250 * DueFrames * 2;

	M32X_Pause_256bits (b, DueFrames);
	do {
		if (--Count == 0)
			break;
		ddk_memory_barrier ();
		ddk_yield_cpu ();
	} while ((b->RegistersBar1_Hardware->STAT_rw_08 & M32X_STAT_TI) == 0 && b->as == M32X_PAUSE_PENDING);

	/*
	 * stop timeout timer
	 */
	b->RegistersBar1_Hardware->CMD_w_04 = 0;
	b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA;
	b->as = M32X_ACTION_VOID;
}

static __forceinline void M32X_SubmitAction (M32X* b, u32 action)
{
	unsigned duration;
	register M32X_RegistersBar1_CMD cmd;

	ddk_assert ((b->RegistersBar1_Hardware->STAT_rw_08 & (M32X_STAT_PCMF | M32X_STAT_PCMA)) == 0);

#if DDK_DEBUG
	M32X_DumpAction (b, action);
	/*
	 * M32X_DumpActionLite (b, action);
	 */
#endif
	ddk_flush_cpu_writecache ();

	/*
	 * clear previous action acknowledge
	 */
	b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_PCMF | M32X_STAT_PCMA | M32X_STAT_TI;

	/*
	 * write to PCM_CCB
	 */
	b->PCM_ccb.Action.all = action;

	/*
	 * kick M32X
	 */
	cmd.all = 0;
	cmd.fields.arpcm |= 1;

	/*
	 * set & start timeout timer
	 * we expect that any action request will be completed in a M32X_ACTION_TIMEOUT_FACTOR PCM frames.
	 * the 5 frames is about 20625 PCI-clocks.
	 */
	duration = (unsigned) (M32X_FRAME_DURATION * M32X_ACTION_TIMEOUT_FACTOR / M32X_LBI_TICK_DURATION + 1.5);
	/*
	 * ddk_assert (duration <= 0xFFFFul);
	 */
	ddk_assert (duration > 1);
	if (duration > 0xFFFFul)
		duration = 0xFFFFul;
	cmd.fields.timv |= duration;
	cmd.fields.timr |= 1;	/* experimental, normally must be present */

	b->as = M32X_ACTION_PENDING;
	b->RegistersBar1_Hardware->CMD_w_04 = cmd.all;
}

may_static char M32X_SubmitActionAndWait (M32X* b, u32 action)
{
	unsigned status;

	ddk_memory_barrier ();
	b->RegistersBar1_Hardware->STAT_rw_08 = (u32)~0ul;
	M32X_SubmitAction (b, action);
	for (;;) {
		ddk_yield_dmabus ();

		/*
		 * each read take about 300 ns
		 */
		status = b->RegistersBar1_Hardware->STAT_rw_08;

		/*
		 * does have any action related status ?
		 */
		if (status & (M32X_STAT_PCMF | M32X_STAT_PCMA | M32X_STAT_TI)) {
			b->as = M32X_ACTION_VOID;
			/*
			 * stop timeout timer
			 */
			b->RegistersBar1_Hardware->CMD_w_04 = 0;
			/*
			 * clear all acknowledges
			 */
			b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA;

			if (status & M32X_STAT_PCMA) {
				ddk_kd_print (("M32X: action ok\n"));
				return 1;
			}

			if (status & M32X_STAT_PCMF) {
				ddk_kd_print (("M32X: action failed\n"));
				return 0;
			}

			if (status & M32X_STAT_TI) {
				ddk_kd_print (("M32X: action timeout (by timer), stat = 0x%X, cmd = 0x%X\n", status,
					       b->RegistersBar1_Hardware->CMD_w_04));
				return 0;
			}
		}
	}
}

static __forceinline u32 M32X_BuildAction (char full_reset,
					   char channel_full,
					   char channel_spec,
					   unsigned channel_number, char new_interrupt_queue, unsigned loop_mode)
{
	register M32X_CCB_Action action;

	action.all = 0;

	if (full_reset)
		action.fields.res |= 1;
	else if (channel_full) {
		action.fields.in |= 1;
		action.fields.cnum |= channel_number;
	} else if (channel_spec) {
		action.fields.ico |= 1;
		action.fields.cnum |= channel_number;
	}

	if (new_interrupt_queue)
		action.fields.ia |= 1;
	if (loop_mode)
		action.fields.loop |= loop_mode;

	return action.all;
}

M32X_INTERFACE_CALL void M32X_SetPhonyFill_lsbf (M32X* b, unsigned cn, u32 pattern)
{
#if MX32_PHONY_STUB_FILL
	if (cn < 32)
		*(u32*)(b->tx_stub_buffer + cn * M32X_TxStubBufferSizePerChannel) = pattern;
#endif
}

static __forceinline void M32X_MakeHaltTx (M32X* b, volatile M32X_dma_t * desc, unsigned cn)
{
	desc->entry.ir = 0;
	desc->from = 0;
#if MX32_PHONY_STUB_FILL
	desc->u.tx.data = M32X_PA (tx_stub_buffer) + cn * M32X_TxStubBufferSizePerChannel;
	if (! b->tx_running[cn] || b->PCM_ccb.chan[cn].flags_a.fields.mode /* != M32X_MODE_TRANSPARENT_A */) {
		desc->u.tx.u.all = M32X_IO_HOLD | M32X_IO_FE | ((M32X_TxStubBufferSizePerChannel - 4) << M32X_IO_NO_S);
		ddk_flush_cpu_writecache ();
		desc->u.io.next = 0;
	} else {
		desc->u.tx.u.all = M32X_IO_HI | ((M32X_TxStubBufferSizePerChannel - 4) << M32X_IO_NO_S);
		ddk_flush_cpu_writecache ();
		desc->u.io.next = desc->pa;
	}
#else
	desc->u.tx.data = M32X_PA (tx_stub_buffer);
	desc->u.tx.u.all = M32X_IO_HOLD | M32X_IO_FE | ((M32X_TxStubBufferSizePerChannel - 4) << M32X_IO_NO_S);
	ddk_flush_cpu_writecache ();
	desc->u.io.next = 0;
#endif
}

static __forceinline void M32X_MakeHaltRx (M32X* b, volatile M32X_dma_t * desc)
{
	desc->u.tx.data = M32X_PA (rx_stub_buffer);
	desc->u.io.next = desc->pa;
	desc->u.rx.u1.all_1 = M32X_IO_HI | ((M32X_RxStubBufferSize - 4) << M32X_IO_NO_S);
	desc->u.rx.u2.all_2 = 0;
	ddk_flush_cpu_writecache ();
	desc->entry.ir = 0;
	desc->from = 0;
}

may_static M32X_dma_t *M32X_AllocDescriptor (M32X* b)
{
	M32X_dma_t *desc = b->ffd;

	if (likely (desc->entry.__next != 0)) {
		ddk_assert (M32X_IsValidDescriptor (b, desc));
		b->ffd = desc->entry.__next;
		if (desc->u.io.next) {
			M32X_dma_t *chain = MX32_pa2desc (b, desc->u.io.next);

			if (chain->from == desc)
				chain->from = 0;
			desc->u.io.next = 0;	/* must be !cleared! here and updated later */
		}
		desc->from = 0;
		desc->u.io.all_2 = 0;
		return desc;
	}
	return 0;
}

static __forceinline void M32X_FreeDescriptor (M32X* b, M32X_dma_t * desc)
{
	ddk_assert (M32X_IsValidDescriptor (b, desc));

	/*
	 * Important!!!
	 * the desc->io.next field must be unchanged (alive) here,
	 * because it (will be) used by other routines for going on descriptors chains by physical addresses.
	 */
	ddk_assert (desc->u.io.next);

	desc->entry.__next = 0;
	b->lfd->entry.__next = desc;
	b->lfd = desc;
}

may_static void M32X_FreeDescriptorRx (M32X* b, M32X_ir_t * ir)
{
	if (likely (ir->Io.desc != 0)) {
		M32X_FreeDescriptor (b, ir->Io.desc);
		ir->Io.desc = 0;
	}
}

may_static void M32X_FreeDescriptorTx (M32X* b, M32X_ir_t * ir)
{
	if (likely (ir->Io.desc != 0)) {
		ddk_assert (ir->ur);
		ddk_assert (ir->ur->Io.cn < M32X_UseChannels);
		if (likely (ir->Io.desc != b->last_tx[ir->ur->Io.cn]))
			M32X_FreeDescriptor (b, ir->Io.desc);
		else
			M32X_MakeHaltTx (b, ir->Io.desc, ir->ur->Io.cn);
		ir->Io.desc = 0;
	}
}

may_static void M32X_PutLastTx (M32X* b, unsigned c, M32X_dma_t * desc)
{
	volatile M32X_dma_t *last;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ddk_assert (desc->u.tx.next == 0);
	ddk_assert (desc->u.tx.u.fields.hold);
	last = b->last_tx[c];
	ddk_assert (last != desc);

	last->u.tx.next = desc->pa;
	last->u.tx.u.all &= ~M32X_IO_HOLD;
	ddk_flush_cpu_writecache ();
	b->RegistersBar1_Hardware->TXPOLL_rw_2C &= ~(1ul << c);

	b->last_tx[c] = desc;
	desc->from = (M32X_dma_t *) last;

	if (last->entry.ir == 0)
		M32X_FreeDescriptor (b, (M32X_dma_t *) last);
}

may_static M32X_ir_t *M32X_AllocInternalRequest (M32X* b)
{
	M32X_ir_t *ir;

	ir = 0;
	if (likely (!ddk_queue_isempty (&b->free_ir))) {
		ir = ddk_queue_get (&b->free_ir, M32X_ir_t, Manage.entry);
		ddk_queue_entry_detach (&ir->Manage.entry);
		ir->Manage.RefCounter = 0;
		ir->ur = 0;
		ir->Io.desc = 0;
		ddk_queue_entry_init (&ir->Manage.entry);
		ddk_queue_entry_init (&ir->Io.entry);
#if ddk_assert_check
		ir->Manage.Total = 0xCC;
		ir->Manage.Current = 0xDD;
#endif
	}
	return ir;
}

static __forceinline void M32X_FreeInternalRequest (M32X* b, M32X_ir_t * ir)
{
	ddk_queue_put (&b->free_ir, &ir->Manage.entry);
}

may_static void M32X_Steps_Clear (M32X_ir_t * ir)
{
#if ddk_assert_check
	ddk_assert (ir->Manage.Total == 0xCC);
	ddk_assert (ir->Manage.Current == 0xDD);
	ir->Manage.Total = 0xAA;
	ir->Manage.Current = 0xBB;
#endif
	//ir->Manage.Total = 0;

	ddk_memset (ir->Manage.Steps, 0, sizeof (ir->Manage.Steps));
}

may_static void M32X_Steps_End (M32X_ir_t * ir)
{
	unsigned i;

	ddk_assert (ir->Manage.Total == 0xAA);
	ddk_assert (ir->Manage.Current == 0xBB);
	for (i = 0; ir->Manage.Steps[i].all != 0 && i < M32X_MaxActionsPerRequest; i++) {
		if (ir->Manage.Steps[i].fields.rx || ir->Manage.Steps[i].fields.tx) {
			ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[i].fields.code));
			ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[i].fields.code));
		}
	}
	ir->Manage.Total = i;
}

may_static void M32X_Steps_Fill (M32X* b, M32X_ir_t * ir, unsigned c)
{
	M32X_InternalStep *pStep;
	unsigned FillCommand = 0;	// M32X_RECEIVE_CLEAR | M32X_TRANSMIT_CLEAR;

	if (!b->rx_running[c])
		FillCommand = M32X_RECEIVE_OFF;

	if (!b->tx_running[c])
		FillCommand |= M32X_TRANSMIT_OFF;

	/*
	 * fill unpair tail of rx/tx commands queue
	 */
	pStep = ir->Manage.Steps;
	while (pStep->all != 0) {
		if (pStep->fields.tx == 0 && pStep->fields.rx != 0) {
			ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
			pStep->fields.code |= FillCommand & M32X_TRANSMIT_BITS;
			ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
		}

		if (pStep->fields.rx == 0 && pStep->fields.tx != 0) {
			ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
			pStep->fields.code |= FillCommand & M32X_RECEIVE_BITS;
			ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
		}

		pStep++;
		ddk_assert (pStep < ARRAY_END (ir->Manage.Steps));
	}
}

may_static void M32X_Steps_Add (M32X_ir_t * ir, enum M32X_STEPS Operation, unsigned Command)
{
	unsigned i;

	ddk_assert (ir->Manage.Total == 0xAA);
	ddk_assert (ir->Manage.Current == 0xBB);
	for (i = 0; i < M32X_MaxActionsPerRequest; i++) {
		M32X_InternalStep *pStep = &ir->Manage.Steps[i];

		switch (Operation) {
			case M32X_STEP_TX:
				ddk_assert ((Command & ~M32X_TRANSMIT_BITS) == 0);
				if (pStep->fields.tx || pStep->fields.code >= M32X_CLK_OFF)
					continue;
				ddk_assert ((pStep->fields.code & M32X_TRANSMIT_BITS) == 0);
				ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
				ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
				pStep->fields.tx = 1;
				pStep->fields.code |= Command;
				ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
				ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
				break;

			case M32X_STEP_RX:
				ddk_assert ((Command & ~M32X_RECEIVE_BITS) == 0);
				if (pStep->fields.rx || pStep->fields.code >= M32X_CLK_OFF)
					continue;
				ddk_assert ((pStep->fields.code & M32X_RECEIVE_BITS) == 0);
				ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
				ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
				pStep->fields.rx = 1;
				pStep->fields.code |= Command;
				ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
				ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
				break;

			case M32X_STEP_PAUSE:
			case M32X_STEP_PAUSE_LONG:
				if (pStep->fields.rx || pStep->fields.tx
				    || pStep->fields.code || pStep->fields.config_clear || pStep->fields.config_new)
					continue;
				pStep->fields.pause_count |= (Operation == M32X_STEP_PAUSE) ? 1 : (u32)~0ul;
				break;

			case M32X_STEP_CONFIG_CLEAR:
				if (pStep->fields.tx || pStep->fields.rx || pStep->fields.code)
					continue;
				pStep->fields.config_clear = 1;
				break;

			case M32X_STEP_CONFIG_NEW:
				if (pStep->fields.tx || pStep->fields.rx || pStep->fields.code)
					continue;
				pStep->fields.config_new = 1;
				break;

			case M32X_STEP_MULTI_OFF:
			case M32X_STEP_MULTI_ABORT:
			case M32X_STEP_MULTI_RESTORE:
			case M32X_STEP_SET_LOOP_INT:
			case M32X_STEP_SET_LOOP_EXT:
			case M32X_STEP_SET_LOOP_OFF:
			case M32X_CLK_OFF:
			case M32X_CLK_ON:
				if (pStep->fields.tx || pStep->fields.rx || pStep->fields.code)
					continue;
				pStep->fields.code |= Operation;
				break;

			default:
				ddk_assert (0);
				ddk_assume (0);
		}
		ddk_kd_print (("tau32-step-add: %08X[%d], %d, %d\n", ir, i, Operation, Command));
		return;
	}
	ddk_kd_print (("tau32: action-steps-overflow\n"));
	ddk_assert (0);
}

may_static void M32X_Steps_TxStop (M32X* b, M32X_ir_t * ir, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	if (likely (b->tx_running[c])) {
		M32X_Steps_Add (ir, M32X_STEP_TX, M32X_TRANSMIT_ABORT);
		M32X_Steps_Add (ir, M32X_STEP_PAUSE, 0);
		M32X_Steps_Add (ir, M32X_STEP_TX, M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD);
		M32X_Steps_Add (ir, M32X_STEP_PAUSE_LONG, 0);
		b->tx_running[c] = 0;
		if (likely (!b->rx_should_running[c])) {
			M32X_Steps_Add (ir, M32X_STEP_CONFIG_CLEAR, 0);
			M32X_Steps_Add (ir, M32X_STEP_TX, M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD);
		}
	}
}

may_static void M32X_Steps_RxStop (M32X* b, M32X_ir_t * ir, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	if (likely (b->rx_running[c])) {
		M32X_Steps_Add (ir, M32X_STEP_RX, M32X_RECEIVE_ABORT);
		M32X_Steps_Add (ir, M32X_STEP_PAUSE, 0);
		M32X_Steps_Add (ir, M32X_STEP_RX, M32X_RECEIVE_OFF);
		M32X_Steps_Add (ir, M32X_STEP_PAUSE_LONG, 0);
		b->rx_running[c] = 0;
		if (likely (!b->tx_should_running[c])) {
			M32X_Steps_Add (ir, M32X_STEP_CONFIG_CLEAR, 0);
			M32X_Steps_Add (ir, M32X_STEP_RX, M32X_RECEIVE_OFF);
		}
	}
}

may_static void M32X_Steps_TxStart (M32X* b, M32X_ir_t * ir, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	if (likely (!b->tx_running[c])) {
		if (likely (!b->rx_running[c])) {
			M32X_Steps_Add (ir, M32X_STEP_CONFIG_NEW, 0);
			M32X_Steps_Add (ir, M32X_STEP_RX, M32X_RECEIVE_OFF);
		}
		M32X_Steps_Add (ir, M32X_STEP_TX, M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD);
		b->tx_running[c] = 1;
		M32X_Steps_Add (ir, M32X_STEP_TX, M32X_TRANSMIT_INIT);
	}
}

may_static void M32X_Steps_RxStart (M32X* b, M32X_ir_t * ir, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	if (likely (!b->rx_running[c])) {
		if (likely (!b->tx_running[c])) {
			M32X_Steps_Add (ir, M32X_STEP_CONFIG_NEW, 0);
			M32X_Steps_Add (ir, M32X_STEP_TX, M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD);
		}
		M32X_Steps_Add (ir, M32X_STEP_RX, M32X_RECEIVE_OFF);
		b->rx_running[c] = 1;
		M32X_Steps_Add (ir, M32X_STEP_RX, M32X_RECEIVE_INIT);
	}
}

may_static void M32X_Request_FireLoop (M32X* b)
{
	M32X_ir_t *ir;

	if (likely ((b->DeepFlags & FLAG_DEEP_REQ) == 0)) {
		b->DeepFlags |= FLAG_DEEP_REQ;
		while (!ddk_queue_isempty (&b->rq_in)) {
			ir = ddk_queue_get (&b->rq_in, M32X_ir_t, Manage.entry);
			ddk_queue_entry_detach (&ir->Manage.entry);
			M32X_Request_Start (b, ir);
		}
		b->DeepFlags &= ~FLAG_DEEP_REQ;
	}
}

static __forceinline char M32X_IsChannelNeedCommit (M32X* b, unsigned c)
{
	if ((b->tx_should_running[c] ^ b->tx_running[c])
	| (b->rx_should_running[c] != b->rx_running[c]))
		return 1;
	if (b->tx_should_running[c] | b->rx_should_running[c]) {
		if (b->current_config[c] != b->target_config[c])
			return 1;
		if (ddk_bit_test (b->cm.TimeslotsChanges, c))
			return 1;
	}
	return 0;
}

static __forceinline void M32X_Commit_PrepareAll (M32X* b, M32X_ir_t * ir)
{
	int i = M32X_UseChannels - 1;

	do {
		if (b->rx_running[i] || b->tx_running[i]) {
			/*
			 * we need ABORT with PAUSE, only if any channel does rx/tx
			 */
			M32X_Steps_Add (ir, M32X_STEP_MULTI_ABORT, 0);
			M32X_Steps_Add (ir, M32X_STEP_PAUSE_LONG, 0);
			break;
		}
	} while (--i >= 0);

	/*
	 * TX/RX off
	 */
	M32X_Steps_Add (ir, M32X_STEP_CONFIG_CLEAR, 0);
	M32X_Steps_Add (ir, M32X_STEP_MULTI_OFF, 0);
	M32X_Steps_Add (ir, M32X_STEP_PAUSE, 0);

	i = M32X_UseChannels - 1;
	do {
		if (b->rx_should_running[i]
		    || b->tx_should_running[i]) {
			/*
			 * restore channels state, only if any channel does rx/tx
			 */
			M32X_Steps_Add (ir, M32X_STEP_CONFIG_NEW, 0);
			M32X_Steps_Add (ir, M32X_STEP_MULTI_OFF, 0);
			M32X_Steps_Add (ir, M32X_STEP_PAUSE_LONG, 0);
			M32X_Steps_Add (ir, M32X_STEP_MULTI_RESTORE, 0);
			M32X_Steps_Add (ir, M32X_STEP_PAUSE_LONG, 0);
			break;
		}
	} while (--i >= 0);
}

static __forceinline void M32X_Commit_PrepareOneChannel (M32X* b, M32X_ir_t * ir, unsigned c)
{
	/*
	 * ok, we can & must do this by M32X/ICO action
	 */
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ir->Manage.c = c;
	if (b->current_config[c] != b->target_config[c]
	    || ddk_bit_test (b->cm.TimeslotsChanges, c)) {
		M32X_Steps_RxStop (b, ir, c);
		M32X_Steps_TxStop (b, ir, c);
		M32X_Steps_Fill (b, ir, c);
	}

	if (b->rx_should_running[c])
		M32X_Steps_RxStart (b, ir, c);
	else
		M32X_Steps_RxStop (b, ir, c);

	if (b->tx_should_running[c])
		M32X_Steps_TxStart (b, ir, c);
	else
		M32X_Steps_TxStop (b, ir, c);

	M32X_Steps_Fill (b, ir, c);
	M32X_Steps_Add (ir, M32X_STEP_PAUSE, 0);
}

may_static void M32X_Request_Start (M32X* b, M32X_ir_t * ir)
{
#if ddk_assert_check
	unsigned count = 0;
#endif
	M32X_ur_t *ur = ir->ur;

	ddk_assert (ir->Manage.RefCounter == 0);
	ddk_assert (ir->ur->pInternal == ir);
	ddk_assert (ir->ur->Command & __M32X_ValidCommands_mask);
	ddk_assert ((ir->ur->Command & ~__M32X_ValidCommands_mask) == 0);

	ir->Manage.RefCounter++;
	do {
#ifdef M32X_USER_PROCESS_REQUEST
		if (!M32X_USER_PROCESS_REQUEST (b, ur, ir))
#endif
		{
			int i, c = ur->Io.cn;

#ifdef CRONYX_LYSAP
			ddk_assert (c == 0);
			c = 0;
#endif

			if (!(ur->Command & (M32X_Tx_Stop | M32X_Rx_Stop))
			    || (ur->Command & (M32X_Tx_Start | M32X_Rx_Start))) {
				if (ur->Command & M32X_Configure_Loop) {
					ur->Command &= ~M32X_Configure_Loop;
					b->TargetLoopmode = ur->Io.DigitalLoop != 0;
					b->HaveChanges = 1;
				}

				if (ur->Command & M32X_Timeslots_Complete) {
					ur->Command &= ~M32X_Timeslots_Complete;
					i = 32 - 1;
					do
						b->uta[i] = ur->Io.TimeslotsAssignment.Complete[i];
					while (--i >= 0);
					b->HaveChanges |= M32X_BuildTimestotAssignment (b);
				}

				if (ur->Command & TAU32_Timeslots_Map) {
					ur->Command &= ~TAU32_Timeslots_Map;
					i = M32X_UseChannels - 1;
					do
						M32X_ch2ts (b, ur->Io.TimeslotsAssignment.Map[i], i);
					while (--i >= 0);
					b->HaveChanges |= M32X_BuildTimestotAssignment (b);
				}

				if (ur->Command & M32X_Timeslots_Channel) {
					ur->Command &= ~M32X_Timeslots_Channel;
					M32X_ch2ts (b, ur->Io.ChannelConfig.AssignedTsMask, c);
					b->HaveChanges |= M32X_BuildTimestotAssignment (b);
				}

				if (ur->Command & M32X_Configure_Setup) {
					ur->Command &= ~M32X_Configure_Setup;
					b->target_config[c] = ur->Io.ChannelConfig.Config;
					b->HaveChanges = 1;
				}
			}

			if (ur->Command & M32X_Tx_Start) {
				ur->Command &= ~M32X_Tx_Start;
				b->tx_should_running[c] = 1;
				b->HaveChanges = 1;
			}

			if (ur->Command & M32X_Rx_Start) {
				ur->Command &= ~M32X_Rx_Start;
				b->rx_should_running[c] = 1;
				b->HaveChanges = 1;
			}

			if (ur->Command & M32X_Tx_Data) {
				ur->Command &= ~M32X_Tx_Data;
				ur->Io.Tx.Transmitted = 0;
				ir->Manage.RefCounter++;
				ddk_queue_put (&b->txq[c], &ir->Io.entry);
				M32X_Tx_FireLoopChan (b, c);
			}

			if (ur->Command & M32X_Rx_Data) {
				ur->Command &= ~M32X_Rx_Data;
				ur->Io.Rx.Received = 0;
				ur->Io.Rx.FrameEnd = 0;
				ir->Manage.RefCounter++;
				ddk_queue_put (&b->rxq[c], &ir->Io.entry);
				M32X_Rx_FireLoopChan (b, c);
			}

			if (ir->Manage.RefCounter <= 1) {
				if (ur->Command & M32X_Tx_Stop) {
					ur->Command &= ~M32X_Tx_Stop;
					b->tx_should_running[c] = 0;
					b->HaveChanges = 1;
				}

				if (ur->Command & M32X_Rx_Stop) {
					ur->Command &= ~M32X_Rx_Stop;
					b->rx_should_running[c] = 0;
					b->HaveChanges = 1;
				}
			}

			if ((ur->Command & M32X_Configure_Commit)
			    && b->HaveChanges) {
				unsigned ToDo = 0;

				b->HaveChanges = 0;
				i = M32X_UseChannels - 1;
				do {
					if (M32X_IsChannelNeedCommit (b, i)) {
						c = i;
						if (++ToDo > 1)
							/*
							 * more than one channel todo, do it by M32X/IN action
							 */
							break;
					}
				}
				while (--i >= 0);

				if (ToDo > 0 || b->CurrentLoopmode != b->TargetLoopmode) {
					M32X_Steps_Clear (ir);
					if (ToDo > 1 || b->cm.TimeslotsChanges != 0)
						M32X_Commit_PrepareAll (b, ir);
					else if (ToDo)
						M32X_Commit_PrepareOneChannel (b, ir, c);

					if (b->CurrentLoopmode != b->TargetLoopmode)
						M32X_Steps_Add (ir,
								b->TargetLoopmode ?
								M32X_STEP_SET_LOOP_INT : M32X_STEP_SET_LOOP_OFF, 0);

					M32X_Steps_End (ir);
					M32X_Actions_QueueOrStart (b, ir);
#ifdef M32X_USER_NOTIFY_NEW_CHCFG
					M32X_USER_NOTIFY_NEW_CHCFG (b);
#endif
				}
			}
		}
		ddk_assert (++count < 1024);
	}
	while (ir->Manage.RefCounter == 1 && ir->ur->Command & (__M32X_SelfCommands | __M32X_UserCommands));

	M32X_Request_TryComplete (b, ir);
}

may_static void M32X_request_pause (M32X* b, M32X_ir_t * ir, unsigned DueFrames)
{
	unsigned i, step;

	ddk_assert (DueFrames > 0 && DueFrames <= M32X_MaxActionsPerRequest * 15);

	i = 0;
	do {
		if (DueFrames <= 15)
			step = DueFrames;
		else if (DueFrames <= 30)
			step = DueFrames / 2;
		else
			step = 15;
		ir->Manage.Steps[i].all = 0;
		ir->Manage.Steps[i].fields.pause_count |= step;
		i++;
	} while (DueFrames -= step);
	ir->Manage.Total = i;
	M32X_Actions_QueueOrStart (b, ir);
}

may_static void M32X_NotifyError (M32X* b, int Item, unsigned Code)
{
	if (likely (b->uc->pErrorNotifyCallback != 0 && Item < M32X_UseChannels)) {
		M32X_BEFORE_CALLBACK (b);
		b->uc->pErrorNotifyCallback (b->uc, Item, Code);
		M32X_AFTER_CALLBACK (b);
	}
}
may_static void M32X_AutoAction_Rx (M32X* b, unsigned c, unsigned Command)
{
	M32X_ir_t *ir;

	ddk_assert ((Command & ~M32X_RECEIVE_BITS) == 0);
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif

	M32X_NotifyError (b, c, M32X_WARN_RX_JUMP);
	ir = b->AutoActions[c];
	if (ir == 0) {
		ir = M32X_AllocInternalRequest (b);
		if (unlikely (ir == 0)) {
			M32X_NotifyError (b, c, M32X_ERROR_ALLOCATION);
			return;
		}
		b->AutoActions[c] = ir;
		if (!b->tx_running[c])
			Command |= M32X_TRANSMIT_OFF;
		ir->Manage.c = c;
		ir->Manage.Steps[0].all = 0;
		ir->Manage.Steps[0].fields.code |= Command;
		ir->Manage.Steps[0].fields.rx |= 1;
		ir->Manage.Total = 1;
		ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[0].fields.code));
		ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[0].fields.code));
		M32X_Actions_QueueOrStart (b, ir);
	} else {
		ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[0].fields.code));
		ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[0].fields.code));
		ir->Manage.Steps[0].fields.code =
			(M32X_TRANSMIT_BITS & ir->Manage.Steps[0].fields.code) | Command;
		ir->Manage.Steps[0].fields.rx |= 1;
		ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[0].fields.code));
		ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[0].fields.code));
	}
	b->PCM_ccb.CurrentRxDescriptors[c] = b->PCM_ccb.chan[c].frda;
}

may_static void M32X_AutoAction_Tx (M32X* b, unsigned c, unsigned Command)
{
	M32X_ir_t *ir;

	ddk_assert ((Command & ~M32X_TRANSMIT_BITS) == 0);
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif

	M32X_NotifyError (b, c, M32X_WARN_TX_JUMP);
	ir = b->AutoActions[c];
	if (ir == 0) {
		ir = M32X_AllocInternalRequest (b);
		if (unlikely (ir == 0)) {
			M32X_NotifyError (b, c, M32X_ERROR_ALLOCATION);
			return;
		}
		b->AutoActions[c] = ir;
		if (!b->rx_running[c])
			Command |= M32X_RECEIVE_OFF;
		ir->Manage.c = c;
		ir->Manage.Steps[0].all = 0;
		ir->Manage.Steps[0].fields.code |= Command;
		ir->Manage.Steps[0].fields.tx |= 1;
		ir->Manage.Total = 1;
		ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[0].fields.code));
		ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[0].fields.code));
		M32X_Actions_QueueOrStart (b, ir);
	} else {
		ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[0].fields.code));
		ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[0].fields.code));
		ir->Manage.Steps[0].fields.code =
			(M32X_RECEIVE_BITS & ir->Manage.Steps[0].fields.code) | Command;
		ir->Manage.Steps[0].fields.tx |= 1;
		ddk_assert (MX32_is_valid_tx_cmd (ir->Manage.Steps[0].fields.code));
		ddk_assert (MX32_is_valid_rx_cmd (ir->Manage.Steps[0].fields.code));
	}
	b->PCM_ccb.CurrentTxDescriptors[c] = b->PCM_ccb.chan[c].ftda;
}

may_static void M32X_Actions_Complete (M32X* b)
{
	M32X_ir_t *ir;

	ir = ddk_queue_get (&b->rq_a, M32X_ir_t, Manage.entry);
	ddk_assert (ir->Manage.RefCounter > 0);
	ddk_queue_entry_detach (&ir->Manage.entry);
	M32X_Request_TryComplete (b, ir);
}

may_static void M32X_Tx_Complete (M32X* b, unsigned c)
{
	M32X_ir_t *ir;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ir = ddk_queue_get (&b->txq[c], M32X_ir_t, Io.entry);
	ddk_assert (ir->Manage.RefCounter > 0);
	ddk_queue_entry_detach (&ir->Io.entry);
	M32X_FreeDescriptorTx (b, ir);
	M32X_Request_TryComplete (b, ir);
}

may_static void M32X_Rx_Complete (M32X* b, unsigned c)
{
	M32X_ir_t *ir;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ir = ddk_queue_get (&b->rxq[c], M32X_ir_t, Io.entry);
	ddk_assert (ir->Manage.RefCounter > 0);
	ddk_queue_entry_detach (&ir->Io.entry);
	M32X_FreeDescriptorRx (b, ir);
	M32X_Request_TryComplete (b, ir);
}

may_static void M32X_InvokeCallback (M32X* b, M32X_ur_t * ur)
{
#if defined (_NTDDK_)
	if (ur->CallbackDpc.DeferredRoutine) {
		if (ur->CallbackDpc.DeferredContext)
			KeInsertQueueDpc (&ur->CallbackDpc, b->uc, ur);
		else {
			M32X_BEFORE_CALLBACK (b);
			((TAU32_RequestCallback) ur->CallbackDpc.DeferredRoutine) (b->uc, ur);
			M32X_AFTER_CALLBACK (b);
		}
	}
#else
	if (ur->pCallback) {
		M32X_BEFORE_CALLBACK (b);
		ur->pCallback (b->uc, ur);
		M32X_AFTER_CALLBACK (b);
	}
#endif
}

#if defined (_NTDDK_)
may_static void M32X_InvokeCallbackSafe (M32X* b, M32X_ur_t * ur)
{
	KIRQL Irql;

	KeRaiseIrql (DISPATCH_LEVEL, &Irql);
	M32X_InvokeCallback (b, ur);
	KeLowerIrql (Irql);
}
#else
#	define M32X_InvokeCallbackSafe M32X_InvokeCallback
#endif

may_static void M32X_Request_TryComplete (M32X* b, M32X_ir_t * ir)
{
	ddk_assert (ir->Manage.RefCounter > 0);
	if (--ir->Manage.RefCounter == 0) {
		M32X_ur_t *ur = ir->ur;

		if (ur) {
			if (ur->Command & (__M32X_SelfCommands | __M32X_UserCommands)) {
				/*
				 * it is not all commands processed, so we should not dequeue or complete request.
				 */
				M32X_Request_Start (b, ir);
				return;
			}
			if (ur->pInternal != ir) {
				/*
				 * cancellation waiting linked list
				 */
#if ddk_assert_check
				unsigned count = 0;
#endif
				do {
					M32X_ur_t *next = (M32X_ur_t *) ur->pInternal;

					ur->pInternal = 0;
					M32X_InvokeCallback (b, ur);
					ur = next;
					ddk_assert (++count < 1024);
				}
				while (ur != 0);
			} else {
				ur->pInternal = 0;
				M32X_InvokeCallback (b, ur);
			}
		}

		M32X_FreeInternalRequest (b, ir);
		M32X_Request_FireLoop (b);
	}
}

may_static void M32X_Actions_QueueOrStart (M32X* b, M32X_ir_t * ir)
{
	ddk_assert (ir->Manage.Total > 0);
	ddk_assert (ir->Manage.Total < ARRAY_LENGTH (ir->Manage.Steps));
	ir->Manage.RefCounter++;
	ir->Manage.Current = 0;
	ddk_assert (!ddk_queue_iscontain (&b->rq_a, &ir->Manage.entry));
	ddk_queue_put (&b->rq_a, &ir->Manage.entry);
	M32X_Actions_FireLoop (b);
}

may_static void M32X_Channel_BuildConfig (M32X* b, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
	b->PCM_ccb.chan[c].flags_a.all = b->PCM_ccb.chan[c].flags_b.all = 0;

	b->PCM_ccb.chan[c].flags_b.fields.itbs |= 2;
	if (b->target_config[c] & M32X_data_inversion)
		b->PCM_ccb.chan[c].flags_a.fields.inv |= 1;

	b->PCM_ccb.chan[c].flags_a.fields.cimask |= 0 | M32X_CIMASK_FE2	/* mask Tx-FE2 interrupt, using HI instead */
		| M32X_CIMASK_FIT	/* mask Tx-FI interrupt, using HI instead */
		| M32X_CIMASK_IFC;	/* mask Idle/Flag change interrupt, it is unneeded */

	switch (b->target_config[c] & M32X_channel_mode_mask) {
		case M32X_HDLC:
			b->PCM_ccb.chan[c].flags_a.fields.mode |= M32X_MODE_HDLC;

			if (b->target_config[c] & M32X_hdlc_adjustment)
				b->PCM_ccb.chan[c].flags_a.fields.fa |= 1;
			if (b->target_config[c] & M32X_hdlc_nocrc)
				b->PCM_ccb.chan[c].flags_a.fields.tflag_cs |= 1;
			if (b->target_config[c] & M32X_hdlc_crc32)
				b->PCM_ccb.chan[c].flags_a.fields.crc |= 1;
			if (b->target_config[c] & M32X_hdlc_interframe_fill)
				b->PCM_ccb.chan[c].flags_a.fields.iftf |= 1;
			break;

		case M32X_V110_x30:
			b->PCM_ccb.chan[c].flags_a.fields.mode |= M32X_MODE_V110;
			b->PCM_ccb.chan[c].flags_a.fields.trv |=
				(b->target_config[c] & M32X_v110_x30_tr_mask) >> M32X_v110_x30_tr_shift;
			break;

		case M32X_TMA:
			b->PCM_ccb.chan[c].flags_a.fields.mode |= M32X_MODE_TRANSPARENT_A;
			if (b->target_config[c] & M32X_tma_flag_filtering) {
				b->PCM_ccb.chan[c].flags_a.fields.fa |= 1;
				b->PCM_ccb.chan[c].flags_a.fields.tflag_cs |=
					(b->target_config[c] & M32X_tma_flags_mask) >> M32X_tma_flags_shift;
			}
			if (b->target_config[c] & M32X_tma_nopack)
				b->PCM_ccb.chan[c].flags_a.fields.crc |= 1;
			break;

		case M32X_TMB:
		case M32X_TMR:
			b->PCM_ccb.chan[c].flags_a.fields.mode |= M32X_MODE_TRANSPARENT_B;
			if (b->target_config[c] & M32X_channel_mode_mask)
				b->PCM_ccb.chan[c].flags_a.fields.crc |= 1;
			break;

		default:
			ddk_assert (0);
			ddk_assume (0);
	}
	b->current_config[c] = b->target_config[c];
}

static __forceinline u32 M32X_BuildAction_ManageChannel (unsigned ChanelNumber, char reload_timeslots)
{
	return reload_timeslots ? M32X_BuildAction (0, 1, 0, ChanelNumber, 0, M32X_NO_LOOP)
		: M32X_BuildAction (0, 0, 1, ChanelNumber, 0, M32X_NO_LOOP);
}

static __forceinline u32 M32X_BuildAction_ConfigureAll (void)
{
	return M32X_BuildAction (1, 0, 0, 0, 0, M32X_NO_LOOP);
}

static __forceinline u32 M32X_BuildAction_SetLoop (enum M32X_LOOP_COMMANDS LoopMode)
{
	return M32X_BuildAction (0, 0, 0, 0, 0, LoopMode);
}

static __forceinline void M32X_Actions_NotifyTimeout (M32X* b)
{
	ddk_kd_print (("PEB: action timeout!!!\n"));
	M32X_NotifyError (b, -1, M32X_ERROR_TIMEOUT);
}

static __forceinline void M32X_Actions_NotifyFault (M32X* b)
{
	ddk_kd_print (("PEB: action failed!!!\n"));
	M32X_NotifyError (b, -1, M32X_ERROR_FAIL);
}

static __forceinline void M32X_UpdateRunningMask (M32X* b, unsigned c, ddk_bitops_t * pTxMask, ddk_bitops_t * pRxMask)
{
	unsigned Command = b->PCM_ccb.chan[c].flags_a.fields.command_nitbs;
	switch (Command & M32X_TRANSMIT_BITS) {
		case M32X_TRANSMIT_OFF:
		case M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD:
		case M32X_TRANSMIT_ABORT:
			ddk_bit_clear (*pTxMask, c);
			break;
		case M32X_TRANSMIT_INIT:
			ddk_bit_set (*pTxMask, c);
			break;
		case M32X_TRANSMIT_CLEAR:
		case M32X_TRANSMIT_JUMP:
		case M32X_TRANSMIT_FAST_ABORT:
			break;
		default:
			ddk_kd_print (("Command = %02X\n", Command));
			ddk_assert (0);
			ddk_assume (0);
	}

	switch (Command & M32X_RECEIVE_BITS) {
		case M32X_RECEIVE_OFF:
		case M32X_RECEIVE_ABORT:
			ddk_bit_clear (*pRxMask, c);
			break;
		case M32X_RECEIVE_INIT:
			ddk_bit_set (*pRxMask, c);
			break;
		case M32X_RECEIVE_CLEAR:
		case M32X_RECEIVE_JUMP:
		case M32X_RECEIVE_FAST_ABORT:
			break;
		default:
			ddk_kd_print (("Command = %02X\n", Command));
			ddk_assert (0);
			ddk_assume (0);
	}
}

may_static void M32X_UpdateRunningMaskAllOff (M32X* b)
{
	if (b->cm.rx | b->cm.tx) {
		b->cm.rx = 0;
		b->cm.tx = 0;
#ifdef M32X_USER_NOTIFY_NEW_CHRUN
		M32X_USER_NOTIFY_NEW_CHRUN (b);
#endif
	}
}

may_static void M32X_Actions_FireLoop (M32X* b)
{
	int i;
	M32X_ir_t *ir;

#if ddk_assert_check
	unsigned count = 0;
#endif

	if (unlikely (b->DeepFlags & FLAG_DEEP_ACT))
		return;

	b->DeepFlags |= FLAG_DEEP_ACT;
	while (1) {
		switch (b->as) {
			case M32X_ACTION_OK:
				if (!ddk_queue_isempty (&b->rq_a)) {
					ir = ddk_queue_get (&b->rq_a, M32X_ir_t, Manage.entry);
					ddk_assert (ir->Manage.Total > 0);
					ddk_assert (ir->Manage.Total < ARRAY_LENGTH (ir->Manage.Steps));
					ddk_assert (ir->Manage.Current <= ir->Manage.Total);
					if (ir->Manage.Current > 0) {
						M32X_InternalStep *pStep = &ir->Manage.Steps[ir->Manage.Current - 1];
						ddk_bitops_t TxMask = b->cm.tx;
						ddk_bitops_t RxMask = b->cm.rx;

						if (pStep->fields.tx || pStep->fields.rx)
							M32X_UpdateRunningMask (b, ir->Manage.c, &TxMask, &RxMask);
						else if (pStep->fields.code == M32X_STEP_MULTI_RESTORE) {
							i = M32X_UseChannels - 1;
							do
								M32X_UpdateRunningMask (b, i, &TxMask, &RxMask);
							while (--i >= 0);
						}
						if (b->cm.rx != RxMask || b->cm.tx != TxMask) {
							b->cm.rx = RxMask;
							b->cm.tx = TxMask;
#ifdef M32X_USER_NOTIFY_NEW_CHRUN
							M32X_USER_NOTIFY_NEW_CHRUN (b);
#endif
						}
					}
				}
				b->as = M32X_ACTION_VOID;
				/*
				 * no break here
				 */

			case M32X_ACTION_VOID:
				/*
				 * just starting new action
				 */
				if (ddk_queue_isempty (&b->rq_a)) {
					/* stop timer to avoid extra interrupt */
					b->RegistersBar1_Hardware->CMD_w_04 = 0;
					goto done;
				}
				ir = ddk_queue_get (&b->rq_a, M32X_ir_t, Manage.entry);
				ddk_assert (ir->Manage.Total > 0);
				ddk_assert (ir->Manage.Total < ARRAY_LENGTH (ir->Manage.Steps));
				ddk_assert (ir->Manage.Current <= ir->Manage.Total);
				if (ir->Manage.Current >= ir->Manage.Total) {
					M32X_Actions_Complete (b);
				} else {
					while (ir->Manage.Current < ir->Manage.Total) {
						M32X_InternalStep *pStep = &ir->Manage.Steps[ir->Manage.Current];

						if (pStep->fields.pause_count) {
							M32X_Pause_256bits (b, pStep->fields.pause_count);
							pStep->fields.pause_count = 0;
							goto DoneStep;
						}
						ir->Manage.Current++;

						if (pStep->fields.rx || pStep->fields.tx) {
							/*
							 * ICO or IN action
							 */
							unsigned c = ir->Manage.c;

							ddk_assert (MX32_is_valid_tx_cmd (pStep->fields.code));
							ddk_assert (MX32_is_valid_rx_cmd (pStep->fields.code));
							ddk_assert (c < M32X_UseChannels);
							if (ir == b->AutoActions[c])
								b->AutoActions[c] = 0;

							if (pStep->fields.config_clear) {
								ddk_kd_print (("M32X-ico: config_clear\n"));
								M32X_Channel_ClearConfig_b (b, c);
							}

							if (pStep->fields.config_new) {
								/*
								 * update config
								 */
								ddk_kd_print (("M32X-ico: config_new\n"));
								M32X_Channel_BuildConfig (b, c);
							}

							b->PCM_ccb.chan[c].flags_a.fields.command_nitbs =
								pStep->fields.code;
							if (ddk_bit_test (b->cm.TimeslotsChanges, c)) {
								M32X_CopyTimeslotAssignment (b);
								M32X_SubmitAction (b, M32X_BuildAction_ManageChannel (c, 1));
#ifdef M32X_USER_NOTIFY_NEW_TSAS
								M32X_USER_NOTIFY_NEW_TSAS (b);
#endif
							} else
								M32X_SubmitAction (b, M32X_BuildAction_ManageChannel (c, 0));
							goto DoneStep;
						} else {
							/*
							 * RES action
							 */

							if (pStep->fields.config_clear) {
								/*
								 * clear config
								 */
								ddk_kd_print (("M32X-res: config_clear\n"));
								i = M32X_UseChannels - 1;
								do
									M32X_Channel_ClearConfig_b (b, i);
								while (--i >= 0);
							}

							if (pStep->fields.config_new) {
								/*
								 * update config
								 */
								ddk_kd_print (("M32X-res: config_new\n"));
								i = M32X_UseChannels - 1;
								do
									if (b->tx_should_running[i]
									    || b->rx_should_running[i])
										M32X_Channel_BuildConfig (b, i);
								while (--i >= 0) ;
								M32X_CopyTimeslotAssignment (b);
#ifdef M32X_USER_NOTIFY_NEW_TSAS
								M32X_USER_NOTIFY_NEW_TSAS (b);
#endif
							}

							switch (pStep->fields.code) {
								case M32X_STEP_SET_LOOP_OFF:
									ddk_kd_print (("M32X: set_loop_off\n"));
									M32X_SubmitAction (b,
											   M32X_BuildAction_SetLoop
											   (M32X_LOOP_OFF));
									b->CurrentLoopmode = 0;
									// ddk_dbg_print ("loop-off\n");
									goto DoneStep;

								case M32X_STEP_SET_LOOP_INT:
									ddk_kd_print (("M32X: set_loop_int\n"));
									M32X_SubmitAction (b,
											   M32X_BuildAction_SetLoop
											   (M32X_LOOP_FULL_INT));
									b->CurrentLoopmode = 1;
									// ddk_dbg_print ("loop-int\n");
									goto DoneStep;

								case M32X_STEP_SET_LOOP_EXT:
									ddk_kd_print (("M32X: set_loop_ext\n"));
									M32X_SubmitAction (b,
											   M32X_BuildAction_SetLoop
											   (M32X_LOOP_FULL_EXT));
									// ddk_dbg_print ("loop-ext\n");
									goto DoneStep;

								case M32X_STEP_MULTI_ABORT:
									ddk_kd_print (("M32X-res: abort\n"));
									i = M32X_UseChannels - 1;
									do {
										unsigned command = M32X_RECEIVE_OFF;

										if (b->rx_running[i])
											command = M32X_RECEIVE_ABORT;

										if (b->tx_running[i])
											command |= M32X_TRANSMIT_ABORT;
										else
											command |= M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD;

										b->PCM_ccb.chan[i].flags_a.fields.command_nitbs = command;
									}
									while (--i >= 0);
									M32X_UpdateRunningMaskAllOff (b);
									// ddk_dbg_print ("multi-abort\n");
									break;

								case M32X_STEP_MULTI_OFF:
									ddk_kd_print (("M32X-res: off\n"));
									i = M32X_UseChannels - 1;
									do
										b->PCM_ccb.chan[i].flags_a.fields.command_nitbs =
											M32X_RECEIVE_OFF |M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD;
									while (--i >= 0);
									M32X_UpdateRunningMaskAllOff (b);
									// ddk_dbg_print ("multi-off\n");
									break;

								case M32X_STEP_MULTI_RESTORE:
									ddk_kd_print (("M32X-res: restore\n"));
									i = M32X_UseChannels - 1;
									do {
										unsigned command;
										M32X_dma_t *n;

										n = M32X_Rx_GetFirst (b, i);
										b->PCM_ccb.chan[i].frda = n ? n->pa : b->halt_rx[i]->pa;

										b->rx_running[i] = b->rx_should_running[i];
										command = M32X_RECEIVE_OFF;
										if (b->rx_should_running[i]) {
											b->PCM_ccb.CurrentRxDescriptors[i] = b->PCM_ccb.chan[i].frda;
											ddk_bit_set (b->RxFireMask, i);
											command = M32X_RECEIVE_INIT;
										}

										n = M32X_Tx_GetFirst (b, i);
										b->PCM_ccb.chan[i].ftda = n ? n->pa : b->last_tx[i]->pa;

										b->tx_running[i] = b->tx_should_running[i];
										if (b->tx_should_running[i]) {
											b->PCM_ccb.CurrentTxDescriptors[i] = b->PCM_ccb.chan[i].ftda;
											ddk_bit_set (b->TxFireMask, i);
											command |= M32X_TRANSMIT_INIT;
										} else
											command |= M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD;

										b->PCM_ccb.chan[i].flags_a.fields.command_nitbs = command;
									}
									while (--i >= 0);
									// ddk_dbg_print ("multi-restore\n");
									break;

								case 0:
									/*
									 * in some cases we need a <nop>
									 */
									// ddk_dbg_print ("noop\n");
									continue;

								case M32X_CLK_OFF:
									TAU32_SetClkOff (b);
									goto DoneStep;
								case M32X_CLK_ON:
									TAU32_SetClkOn (b);
									goto DoneStep;
								default:
									ddk_kd_print (("multi-BAD?\n"));
									ddk_assert (0);
									ddk_assume (0);
									continue;
							}

							M32X_SubmitAction (b, M32X_BuildAction_ConfigureAll ());
							goto DoneStep;
						}
					}
				DoneStep:
					M32X_HandleInterrupt (b);
				}
				break;

			case M32X_ACTION_FAIL:
				b->as = M32X_ACTION_VOID;
				M32X_Actions_NotifyFault (b);
				if (!ddk_queue_isempty (&b->rq_a)) {
					ir = ddk_queue_get (&b->rq_a, M32X_ir_t, Manage.entry);
					if (ir->ur)
						ir->ur->ErrorCode |= M32X_ERROR_FAIL;
					M32X_Actions_Complete (b);
				}
				break;

			case M32X_ACTION_TIMEOUT:
				b->as = M32X_ACTION_VOID;
				M32X_Actions_NotifyTimeout (b);
#if DDK_DEBUG
				total_dump (b);
#endif
				if (!ddk_queue_isempty (&b->rq_a)) {
					ir = ddk_queue_get (&b->rq_a, M32X_ir_t, Manage.entry);
					if (ir->ur)
						ir->ur->ErrorCode |= M32X_ERROR_TIMEOUT;
					M32X_Actions_Complete (b);
				}
				break;

			case M32X_PAUSE_PENDING:
				/*
				 * ddk_kd_print (("M32X_PAUSE_PENDING\n"));
				 */
				goto done;

			case M32X_ACTION_PENDING:
				/*
				 * ddk_kd_print (("M32X_ACTION_PENDING\n"));
				 */
				goto done;

			default:
				ddk_assert (0);
				ddk_assume (0);
		}
		ddk_assert (++count < 1024);
	}
done:
	b->DeepFlags &= ~FLAG_DEEP_ACT;
}

may_static void M32X_Actions_HandleInterrupt (M32X* b, unsigned status)
{
	if (b->as == M32X_PAUSE_PENDING) {
		if (unlikely ((status & (M32X_STAT_PCMF | M32X_STAT_PCMA | M32X_STAT_TI)) != M32X_STAT_TI)) {
			ddk_kd_print (("M32X: pause error, status = 0x%X, (TIMEOUT is expected)\n", status));
			b->as = M32X_ACTION_FAIL;
		} else {
			ddk_kd_print (("pause ok\n"));
			b->as = M32X_ACTION_OK;
		}
	} else {
		ddk_assert (b->as == M32X_ACTION_PENDING);
		if (likely (status & M32X_STAT_PCMA)) {
			ddk_assert ((status & M32X_STAT_PCMF) == 0);
			b->as = M32X_ACTION_OK;
		} else if (status & M32X_STAT_PCMF) {
			ddk_assert ((status & M32X_STAT_PCMA) == 0);
			b->as = M32X_ACTION_FAIL;
		} else {
			ddk_assert ((status & M32X_STAT_TI) != 0);
			b->RegistersBar1_Hardware->CMD_w_04 = 0;
			ddk_yield_dmabus ();
			b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_PCMF | M32X_STAT_PCMA | M32X_STAT_TI;
			ddk_yield_dmabus ();
			b->as = M32X_ACTION_TIMEOUT;
		}
	}
	M32X_Actions_FireLoop (b);
}

may_static enum M32X_START_RESULTS M32X_Start1 (M32X* b)
{
	/*
	 * check M32X timer, future we will just expect than it is OK
	 */
	unsigned left_count, error_count = 0;
	u32 action;

	/*
	 * clear status
	 */
	b->RegistersBar1_Hardware->CMD_w_04 = 0;
	b->RegistersBar1_Hardware->STAT_rw_08 = (u32)~0ul;
	if (b->RegistersBar1_Hardware->STAT_rw_08 & M32X_STAT_TI) {
		ddk_kd_print (("M32X-Reset: step 1, wrong status 0x%X\n", b->RegistersBar1_Hardware->STAT_rw_08));
		return M32X_RR_INTERRUPT;
	}

	if (ddk_cli_avail ()) {
		left_count = 32;
		do {
			ddk_irql_t context;
			unsigned loop_count = 0;

			ddk_cli (context);
			/*
			 * start timer with counter = 0xEE + 2 PCI clocks
			 */
			b->RegistersBar1_Hardware->CMD_w_04 = 0x00EE0010;
			do {
				/*
				 * each read take about 4 <= N <= 32 PCI clocks
				 */
				if (b->RegistersBar1_Hardware->STAT_rw_08 & M32X_STAT_TI)
					break;
				else
					loop_count++;
			}
			while (loop_count < 32);

			b->RegistersBar1_Hardware->CMD_w_04 = 0;
			b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_TI;
			ddk_sti (context);

			if (loop_count < 4 || loop_count >= 32)
				if (error_count > 2) {
					ddk_kd_print (("M32X-Reset: on-chip timer test failed (probe %d, loop_count = %d, expected 4 < n < 32, status = 0x%X)\n", 32 - left_count, loop_count, b->RegistersBar1_Hardware->STAT_rw_08));
					return M32X_RR_TIMER;
				} else
					error_count++;
			else
				error_count = 0;

			if (b->RegistersBar1_Hardware->STAT_rw_08 & M32X_STAT_TI) {
				ddk_kd_print (("M32X-Reset: step 2, wrong status 0x%X\n",
					       b->RegistersBar1_Hardware->STAT_rw_08));
				return M32X_RR_INTERRUPT;
			}
		}
		while (--left_count);
	}

	action = M32X_BuildAction (0, 0, 0, 0, 0, M32X_NO_LOOP);
	if (!M32X_SubmitActionAndWait (b, action)) {
		ddk_kd_print (("M32X-Reset: 'noop kick' action unsuccessful, status 0x%X, retry\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
		if (!M32X_SubmitActionAndWait (b, action)) {
			ddk_kd_print (("M32X-Reset: 'noop kick' action unsuccessful, status 0x%X, retry #2\n",
				       b->RegistersBar1_Hardware->STAT_rw_08));
			if (!M32X_SubmitActionAndWait (b, action)) {
				ddk_kd_print (("M32X-Reset: 'noop kick' action failed, status 0x%X, M32X may failed\n",
					       b->RegistersBar1_Hardware->STAT_rw_08));
				return M32X_RR_ACTION;
			}
		}
	}

	action = M32X_BuildAction (0, 0, 0, 0, 1, M32X_NO_LOOP);
	if (!M32X_SubmitActionAndWait (b, action)) {
		ddk_kd_print (("M32X-Reset: 'set new interrupt queue' action unsuccessful, status 0x%X, retry\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
		if (!M32X_SubmitActionAndWait (b, action)) {
			ddk_kd_print (("M32X-Reset: 'set new interrupt queue' action unsuccessful, status 0x%X, retry #2\n", b->RegistersBar1_Hardware->STAT_rw_08));
			if (!M32X_SubmitActionAndWait (b, action)) {
				ddk_kd_print (("M32X-Reset: 'set new interrupt queue' action unsuccessful, status 0x%X, M32X may failed\n", b->RegistersBar1_Hardware->STAT_rw_08));
				return M32X_RR_ACTION;
			}
		}
	}
	return M32X_RR_OK;
}

may_static enum M32X_START_RESULTS M32X_Start2 (M32X* b)
{
	u32 action;

	b->RegistersBar1_Hardware->MODE1_rw_20.fields.ren |= 1;	/* rx enable */
	action = M32X_BuildAction_SetLoop (M32X_LOOP_OFF);
	if (!M32X_SubmitActionAndWait (b, action)) {
		ddk_kd_print (("M32X-Reset: 'all loopback off' action unsuccessful, status 0x%X, retry\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
		if (!M32X_SubmitActionAndWait (b, action)) {
			ddk_kd_print (("M32X-Reset: 'all loopback off' action unsuccessful, status 0x%X, assume M32X failed\n", b->RegistersBar1_Hardware->STAT_rw_08));
			return M32X_RR_ACTION;
		}
	}
	M32X_Stall_256bits (b, 4);
	b->RegistersBar1_Hardware->MODE1_rw_20.fields.rid &= 0;	/* rx-int enable */

	M32X_CopyTimeslotAssignment (b);
	action = M32X_BuildAction_ConfigureAll ();
	if (!M32X_SubmitActionAndWait (b, action)) {
		ddk_kd_print (("M32X-Reset: 'configuration reset & reload' action unsuccessful, status 0x%X, retry\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
		if (!M32X_SubmitActionAndWait (b, action)) {
			ddk_kd_print (("M32X-Reset: 'configuration reset & reload' action unsuccessful, status 0x%X, assume M32X failed\n", b->RegistersBar1_Hardware->STAT_rw_08));
			return M32X_RR_ACTION;
		}
	}
	M32X_Stall_256bits (b, 4);

	M32X_HandleInterrupt (b);
	return M32X_RR_OK;
}

may_static M32X_dma_t *M32X_Tx_GetFirst (M32X* b, unsigned c)
{
	unsigned gap = 0;
	M32X_ir_t *ir;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif

	ddk_queue_foreach (ir, &b->txq[c], M32X_ir_t, Io.entry) {
		M32X_dma_t *desc = ir->Io.desc;

		if (unlikely (desc == 0)) {
			M32X_ur_t *ur = ir->ur;

			desc = M32X_AllocDescriptor (b);
			if (unlikely (desc == 0)) {
				ur->ErrorCode |= M32X_ERROR_ALLOCATION;
				M32X_NotifyError (b, c, M32X_ERROR_ALLOCATION);
				M32X_Tx_Complete (b, c);
				break;
			}

			desc->u.tx.u.all = M32X_IO_HOLD | M32X_IO_HI | (ur->Io.Tx.DataLength << M32X_IO_NO_S);
			desc->u.io.data = ur->Io.Tx.PhysicalDataAddress;
#ifndef CRONYX_LYSAP
			if (ur->Command & M32X_Tx_NoCrc)
				desc->u.tx.u.all |= M32X_TX_CSM;
			if ((b->current_config[c] & M32X_fr_tx_auto) != 0 || (ur->Command & M32X_Tx_FrameEnd) != 0) {
				desc->u.tx.u.all |= M32X_IO_FE;
				if ((b->current_config[c] & (TAU32_channel_mode_mask
							     | TAU32_hdlc_shareflags)) == TAU32_HDLC)
					desc->u.tx.u.all |= 1;	/* fnum */
			}
#endif
			ir->Io.desc = desc;
			desc->entry.ir = ir;
			M32X_PutLastTx (b, c, desc);
		}
		if (++gap == M32X_IoGapLength)
			break;
	}

	if (!gap)
		return 0;

	return ddk_queue_get (&b->txq[c], M32X_ir_t, Io.entry)->Io.desc;
}

may_static void M32X_Tx_FireLoopAll (M32X* b)
{
	if (!(b->DeepFlags & FLAG_DEEP_TX) && b->TxFireMask)
		__M32X_Tx_FireLoop (b, ddk_ffs (b->TxFireMask));
}

may_static void M32X_Tx_FireLoopChan (M32X* b, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ddk_bit_set (b->TxFireMask, c);
	if (!(b->DeepFlags & FLAG_DEEP_TX))
		__M32X_Tx_FireLoop (b, c);
}

may_static void __M32X_Tx_FireLoopChan (M32X* b, unsigned c)
{
#if ddk_assert_check
	unsigned count = 0;
#endif
	M32X_dma_t *n, *desc, *next, *dma;
	u32 dma_pa;
	M32X_dma_t *halt_tx = b->last_tx[c];

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	n = M32X_Tx_GetFirst (b, c);
	dma_pa = b->PCM_ccb.CurrentTxDescriptors[c];

	if (n == 0) {
		/*
		 * nothing to transmit
		 */
		if (dma_pa != halt_tx->pa) {
			/*
			 * tx-engine is on the tail of previsious undone chain.
			 * This is possible, when request cancelled or tx-error occured
			 */

			/*
			 * mark than we issue abort/jump from <dma>
			 */
			b->PCM_ccb.chan[c].ftda = halt_tx->pa;
			if (b->tx_running[c])
				M32X_AutoAction_Tx (b, c, M32X_TRANSMIT_JUMP);
#if MX32_PHONY_STUB_FILL
		} else if (halt_tx->u.tx.u.all & M32X_IO_HI /*b->PCM_ccb.chan[c].flags_a.fields.mode == M32X_MODE_TRANSPARENT_A*/) {
			halt_tx->u.tx.u.all &= ~M32X_IO_HI;
			M32X_NotifyError (b, c, M32X_ERROR_TX_UNDERFLOW);
#endif
		}
		goto done;
	}

	/*
	 * update ftda is needed for asynchronous action processing.
	 */
	b->PCM_ccb.chan[c].ftda = n->pa;

	/*
	 * we expect that the dma tx-descriptor is our n-first, or our next and it is not the halt_tx,
	 * or dma.next pointed to the our n-first.
	 * if these it not 1, then we need issue tx-jump or tx-fast-abort command.
	 */

	if (dma_pa) {
		if (dma_pa == n->pa) {
			/*
			 * our n-first is already running,
			 * we sure not need jump.
			 */
			goto done;
		}

		dma = MX32_pa2desc (b, dma_pa);
		if (n->from == dma) {
			if (dma->u.io.next == n->pa) {
				/*
				 * dma tx-descriptor.next pointed to our n-first, so it is just the next.
				 * also, we sure not need jump.
				 */
				goto done;
			} else {
				/*
				 * it is probably abort/cancel or other exception.
				 * we assume that the tx-list is broken and the M32X
				 * was not transmitted our n-first, and will does not transmit it without a kick.
				 */
				n->from = 0;
			}
		}

		if (n->from) {
#if ddk_assert_check
			unsigned count = 0;
#endif
			/*
			 * now it is n-first is properly linked from another.
			 * we assume that M32X not need kick if dma is reachable
			 * from n-first by tx-list, and each item of tx-list is properly linked.
			 */
			for (desc = n; !desc->u.io.u.fields.hold; desc = next) {
				if (desc->u.io.next == dma->pa) {
					/*
					 * we reach the end
					 */
					goto done;
				}

				next = MX32_pa2desc (b, desc->u.io.next);
				if (next->from != desc) {
					/*
					 * the chain is broken, M32X need a kick
					 */
					break;
				}
				ddk_assert (++count < 1024);
			}

			/*
			 * at this point dma is unreachable from n-first.
			 * it is probably in exception (abort/break) condition,
			 * or if n-first is reachable for dma, but more than one step.
			 * we assume we need jump
			 */
		} else {
			/*
			 * the n-first is not linked from anywere,
			 * we need jump
			 */
		}

		/*
		 * if dma tx-descriptor is halt_tx
		 * then
		 *  tx-engine is on the halt_tx descriptor,
		 *  we need set ftda and issue the tx-jump command.
		 *  - we can issue TX_JUMP (or not ?)
		 * else
		 *  tx-engine is on the tail of previsious undone descriptor.
		 *  This is possible, when request cancelled or tx-error occured.
		 *  - we need set ftda and issue tx-abort command.
		 */

		/*
		 * mark than we issue abort/jump from <dma>
		 */
		dma->from = 0;
		desc = MX32_pa2desc (b, dma->u.io.next);
		desc->from = 0;
	}

	/*
	 * mark that we issue jump/abort to <n-first>
	 */

#if ddk_assert_check
	for (desc = n; !desc->u.tx.u.fields.hold;) {
		ddk_assert (b->last_tx[c] != desc);
		ddk_assert (desc->u.io.next != n->pa);
		desc = MX32_pa2desc (b, desc->u.io.next);
		ddk_assert (++count < 1024);
	}
	ddk_assert (desc == b->last_tx[c]);
	if (desc != b->last_tx[c])
		M32X_PutLastTx (b, c, desc);
#endif

	if (b->tx_running[c]) {
		if (dma_pa != halt_tx->pa)
			M32X_AutoAction_Tx (b, c, M32X_TRANSMIT_FAST_ABORT);
	} else
		n->from = n;
done:
	ddk_bit_clear (b->TxFireMask, c);
}

may_static void __M32X_Tx_FireLoop (M32X* b, unsigned c)
{
#if ddk_assert_check
	unsigned count = 0;
#endif

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ddk_assert (!(b->DeepFlags & FLAG_DEEP_TX));
	ddk_assert (ddk_bit_test (b->TxFireMask, c));

	b->DeepFlags |= FLAG_DEEP_TX;
	for (;;) {
		ddk_assert (++count < M32X_MaxRequests);
		__M32X_Tx_FireLoopChan (b, c);
		if (!b->TxFireMask)
			break;
#ifndef CRONYX_LYSAP
		c = ddk_ffs (b->TxFireMask);
#endif
	}
	b->DeepFlags &= ~FLAG_DEEP_TX;
}

may_static void M32X_Tx_HandleInterrupts (M32X* b)
{
	unsigned c, ErrorCode, LoopLeft;
	M32X_ir_t *ir;
	M32X_dma_t *desc;

	LoopLeft = M32X_InterruptQueueSize * 3;
	do {
		M32X_TxInterruptQueueItem vector;

		if (M32X_InterruptQueueGap) {
			unsigned gapped_tail = b->tiqp + M32X_InterruptQueueSize - M32X_InterruptQueueGap;

			ddk_assert (b->tiq[gapped_tail & (M32X_InterruptQueueSize - 1)] == 0);
			if (unlikely (b->tiq[gapped_tail & (M32X_InterruptQueueSize - 1)] != 0)) {
				b->TxFireMask = M32X_ALLMASK;
				M32X_NotifyError (b, -1, M32X_ERROR_INT_OVER_TX);
			}
		}
		vector.all = b->tiq[b->tiqp];
		if (unlikely (vector.all == 0)) {
			if (unlikely (LoopLeft == M32X_InterruptQueueSize * 3)) {
				/*
				 * avoid int-queue-overlow
				 */
				unsigned i = b->tiqp;

				do {
					i = (i + 1) & (M32X_InterruptQueueSize - 1);
					vector.all = b->tiq[i];
					if (vector.all) {
						b->tiqp = i;
						goto follow;
					}
				}
				while (i != b->tiqp);
			}
			break;
		}

follow:
		b->tiq[b->tiqp] = 0;
		b->tiqp = (b->tiqp + 1) & (M32X_InterruptQueueSize - 1);
		if (unlikely (vector.fields.designator != M32X_IV_TRANSMIT)) {
			ddk_kd_print (("M32X: transmit interrupt, ignore strange vector 0x%X\n", vector.all));
			ddk_assert (0);
			goto endloop;
		}
#ifdef CRONYX_LYSAP
		ddk_assert (vector.fields.channel_number == 0);
		if (unlikely (vector.fields.channel_number))
			goto endloop;
		c = 0;
#else
		c = vector.fields.channel_number;
#endif
		ir = 0;
		if (likely (! ddk_queue_isempty (&b->txq[c])))
			ir = ddk_queue_get (&b->txq[c], M32X_ir_t, Io.entry);
		if (unlikely (vector.fields.fo || vector.fields.err)) {
			ErrorCode = 0;
			/*
			 * we have some error
			 */
			if (vector.fields.fo) {
				ddk_kd_print (("M32X: transmit underrun (unable access to bus/data) channel %d\n",
					       vector.fields.channel_number));
				ErrorCode |= M32X_ERROR_BUS;
			} else if (vector.fields.fi && vector.fields.err) {
				ddk_kd_print (("M32X: transmit underrun (no data provided) channel %d\n",
					       vector.fields.channel_number));
				ErrorCode |= M32X_ERROR_TX_UNDERFLOW;
			} else {
				ddk_kd_print (("M32X: transmit unknown error (0x%X) channel %d\n", vector.all,
					       vector.fields.channel_number));
				ErrorCode |= M32X_ERROR_TX_PROTOCOL;
			}
			if (ir)
				ir->ur->ErrorCode |= ErrorCode;
			M32X_NotifyError (b, c, ErrorCode);
		}

		if (unlikely (!vector.fields.hi && !vector.fields.fi && !vector.fields.fe2)) {
			/*
			 * no tx-done condition, if so we don't logicaly branch to next tx-descriptor.
			 * just continue;
			 */
			//ddk_dbg_print ("M32X-Tx-Int: vector[%d] 0x%lX /"
			//  " ch %d: fo = %d, err = %d, hi = %d, fi = %d, fe2 = %d, late_stop = %d\n",
			//  b->tiqp, vector.all,
			//  vector.fields.channel_number,
			//  vector.fields.fo, vector.fields.err,
			//  vector.fields.hi, vector.fields.fi,
			//  vector.fields.fe2,
			//  vector.fields.late_stop);
			goto endloop;
		}
		if (unlikely (ir == 0))
			goto mark;

		desc = ir->Io.desc;
		ddk_assert (desc != 0);
		if (unlikely (desc == 0))
			goto mark;

		/*
		 * we have at least one tx-done descriptor
		 */
		ir->ur->Io.Tx.Transmitted = desc->u.tx.u.fields.no;

#ifndef CRONYX_LYSAP
		if (likely (!desc->u.io.u.fields.hold))
			while (unlikely (desc->pa == b->PCM_ccb.CurrentTxDescriptors[c]
			&& !desc->u.io.u.fields.hold))
				ddk_yield_dmabus ();
#endif

		M32X_Tx_Complete (b, c);
mark:
		ddk_assert (c < M32X_UseChannels);
		ddk_bit_set (b->TxFireMask, c);
endloop:;
	} while (--LoopLeft);
	M32X_Tx_FireLoopAll (b);
}

may_static unsigned M32X_Rx_GatherErrors_a (M32X_RxDescriptorFlags2 RxFlags2, M32X* b, unsigned c)
{
	unsigned ErrorCode = 0;

	if (RxFlags2.fields.loss)
		ErrorCode |= M32X_ERROR_RX_SYNC;
	if (RxFlags2.fields.crco
	    && !(b->PCM_ccb.chan[c].flags_a.fields.tflag_cs
		 && b->PCM_ccb.chan[c].flags_a.fields.mode == M32X_MODE_HDLC))
		ErrorCode |= M32X_ERROR_RX_CRC;
	if (RxFlags2.fields.nob)
		ErrorCode |= M32X_ERROR_RX_FRAME;
	if (RxFlags2.fields.lfd)
		ErrorCode |= M32X_ERROR_RX_LONG;
	if (RxFlags2.fields.ra)
		ErrorCode |= M32X_ERROR_RX_ABORT;
	if (RxFlags2.fields.rof)
		ErrorCode |= M32X_ERROR_RX_OVERFLOW;
	if (RxFlags2.fields.sf && (ErrorCode || RxFlags2.fields.bno < 2))
		ErrorCode |= M32X_ERROR_RX_SHORT;
	//if (ErrorCode) {
	//  ddk_dbg_print ("PEB-rx-Errors: sf = %d, loss = %d, crco = %d, nob = %d, lfd = %d, ra = %d, rof = %d, fe = %d\n", RxFlags2.fields.sf, RxFlags2.fields.loss, RxFlags2.fields.crco, RxFlags2.fields.nob, RxFlags2.fields.lfd, RxFlags2.fields.ra, RxFlags2.fields.rof, RxFlags2.fields.fe);
	//}
	return ErrorCode;
}

may_static unsigned M32X_Rx_GatherErrors_b (M32X* b, unsigned c)
{
#if ddk_assert_check
	unsigned count = 0;
#endif
	M32X_dma_t *desc;
	unsigned ErrorCode = 0;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	desc = MX32_pa2desc (b, b->PCM_ccb.chan[c].frda);
	do {
		if (desc == b->halt_rx[c])
			break;

		if (desc->u.rx.u2.fields.c)
			ErrorCode |= M32X_Rx_GatherErrors_a (desc->u.rx.u2, b, c);

		desc = MX32_pa2desc (b, desc->u.io.next);
		ddk_assert (++count < 1024);
	}
	while (desc->pa != b->PCM_ccb.CurrentRxDescriptors[c]
	       && desc->pa != desc->u.io.next);

	return ErrorCode;
}

may_static void M32X_Rx_CheckOverflow (M32X* b, unsigned c)
{
	M32X_RxDescriptorFlags2 RxFlags2;

#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif

	RxFlags2.all_2 = b->halt_rx[c]->u.rx.u2.all_2;
	if (unlikely (RxFlags2.all_2 & M32X_RX_C)) {
		unsigned ErrorCode;

		b->halt_rx[c]->u.rx.u2.all_2 = 0;
		ErrorCode = M32X_Rx_GatherErrors_a (RxFlags2, b, c);
		if (RxFlags2.all_2 & (M32X_IO_FE | M32X_IO_NO))
			ErrorCode |= M32X_ERROR_RX_OVERFLOW;
		if (unlikely (ErrorCode))
			M32X_NotifyError (b, c, ErrorCode);
	}
}

may_static void MX32_RxDone (M32X* b, int c)
{
	unsigned ErrorCode;
	M32X_ir_t *ir;
	M32X_dma_t *desc;
	M32X_ur_t *ur;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ir = ddk_queue_get (&b->rxq[c], M32X_ir_t, Io.entry);
	desc = ir->Io.desc;
	ur = ir->ur;
	ddk_assert (desc);
	ddk_assert (ur);

	ErrorCode = M32X_Rx_GatherErrors_a (desc->u.rx.u2, b, c);
	if (unlikely (ErrorCode)) {
		ur->ErrorCode |= ErrorCode;
		M32X_NotifyError (b, c, ErrorCode);
	}

	if (likely (desc != b->halt_rx[c])) {
		ur->Io.Rx.Received = desc->u.rx.u2.fields.bno;
		ur->Io.Rx.FrameEnd = desc->u.rx.u2.fields.fe;
		if (unlikely (!ur->Io.Rx.FrameEnd && (b->current_config[c] & M32X_fr_rx_fitcheck))) {
			/*
			 * dma frame was not fit into rx-buffer
			 */
			ur->ErrorCode |= M32X_ERROR_RX_UNFIT;
			M32X_NotifyError (b, c, M32X_ERROR_RX_UNFIT);
		}

		if (likely (!desc->u.io.u.fields.hold))
			while (unlikely (desc->pa == b->PCM_ccb.CurrentRxDescriptors[c]))
				ddk_yield_dmabus ();

		M32X_Rx_Complete (b, c);
	}
}

may_static void MX32_RxWatchdog (M32X* b)
{
	int c;
	if (! b->RxWatchdogMask)
		return;

#ifdef CRONYX_LYSAP
	c = 0;
#else
	c = ddk_ffs (b->RxWatchdogMask);
#endif
	for (;;) {
		if (b->rx_should_running[c] && !ddk_queue_isempty (&b->rxq[c])) {
			M32X_ir_t *ir = ddk_queue_get (&b->rxq[c], M32X_ir_t, Io.entry);
			M32X_dma_t *desc = ir->Io.desc;

			if (desc) {
				if (desc->u.rx.u2.fields.c) {
					ddk_bit_clear (b->RxWatchdogMask, c);
					MX32_RxDone (b, c);
					ddk_bit_set (b->RxFireMask, c);
				}
			} else
				ddk_bit_clear (b->RxWatchdogMask, c);
		} else
			ddk_bit_clear (b->RxWatchdogMask, c);

		if (! b->RxWatchdogMask)
			return;

		do {
			if (++c >= M32X_UseChannels)
				return;
		} while (! ddk_bit_test (b->RxWatchdogMask, c));
	}
}

may_static void M32X_Rx_HandleInterrupts (M32X* b)
{
	int c, i, ErrorCode, LoopLeft;
	M32X_ir_t *ir;
	M32X_dma_t *desc;

	LoopLeft = M32X_InterruptQueueSize * 3;
	do {
		M32X_RxInterruptQueueItem vector;

		if (M32X_InterruptQueueGap) {
			unsigned gapped_tail = b->riqp + M32X_InterruptQueueSize - M32X_InterruptQueueGap;

			ddk_assert (b->riq[gapped_tail & (M32X_InterruptQueueSize - 1)] == 0);
			if (unlikely (b->riq[gapped_tail & (M32X_InterruptQueueSize - 1)] != 0)) {
				b->RxFireMask = M32X_ALLMASK;
				M32X_NotifyError (b, -1, M32X_ERROR_INT_OVER_RX);
			}
		}
		vector.all = b->riq[b->riqp];
		if (unlikely (vector.all == 0)) {
			if (unlikely (LoopLeft == M32X_InterruptQueueSize * 3)) {
				/*
				 * avoid int-queue-overlow
				 */
				b->RxWatchdogMask = M32X_ALLMASK;
				MX32_RxWatchdog (b);
				i = b->riqp;
				do {
					i = (i + 1) & (M32X_InterruptQueueSize - 1);
					vector.all = b->riq[i];
					if (vector.all) {
						b->riqp = i;
						goto follow;
					}
				} while (i != b->riqp);
			}
			break;
		}

follow:
		b->riq[b->riqp] = 0;
		b->riqp = (b->riqp + 1) & (M32X_InterruptQueueSize - 1);
		if (unlikely (vector.fields.designator != M32X_IV_RECEIVE)) {
			ddk_kd_print (("M32X: receive interrupt, ignore strange vector 0x%X\n", vector.all));
			ddk_assert (0);
			goto endloop;
		}
#ifdef CRONYX_LYSAP
		ddk_assert (vector.fields.channel_number == 0);
		if (unlikely (vector.fields.channel_number))
			goto endloop;
		c = 0;
#else
		c = vector.fields.channel_number;
#endif
		if (unlikely (vector.fields.fo))
			M32X_NotifyError (b, c, M32X_ERROR_BUS);

		if (b->rx_running[c])
			M32X_Rx_CheckOverflow (b, c);

		if (unlikely (ddk_queue_isempty (&b->rxq[c]))) {
			/*
			 * we does't have any request
			 */
			ErrorCode = M32X_Rx_GatherErrors_b (b, c);
			if (unlikely (ErrorCode))
				M32X_NotifyError (b, c, ErrorCode);
			goto endloop;
		}

		ir = ddk_queue_get (&b->rxq[c], M32X_ir_t, Io.entry);
		desc = ir->Io.desc;
		if (unlikely (desc == 0))
			goto mark;

		ddk_assert (desc->entry.ir == ir);
		/*
		 * we may have one rx-done descriptor
		 */

		if (vector.fields.hi && !desc->u.rx.u2.fields.c)
			ddk_yield_dmabus ();

		if (likely (desc->u.rx.u2.fields.c))
			MX32_RxDone (b, c);
		else {
			ddk_kd_print (("M32X: deferred dma-rx on chan %u\n", c));
			ddk_bit_set (b->RxWatchdogMask, c);
		}

mark:
		ddk_assert (c < M32X_UseChannels);
		ddk_bit_set (b->RxFireMask, c);
endloop:;
	}
	while (--LoopLeft);
	M32X_Rx_FireLoopAll (b);
}

may_static M32X_dma_t *M32X_Rx_GetFirst (M32X* b, unsigned c)
{
	M32X_ir_t *ir;
	M32X_dma_t *prev = 0;
	unsigned gap = 0;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ddk_queue_foreach (ir, &b->rxq[c], M32X_ir_t, Io.entry) {
		M32X_dma_t *desc = ir->Io.desc;

		if (unlikely (desc == 0)) {
			M32X_ur_t *ur = ir->ur;

			desc = M32X_AllocDescriptor (b);
			if (unlikely (desc == 0)) {
				ur->ErrorCode |= M32X_ERROR_ALLOCATION;
				M32X_NotifyError (b, c, M32X_ERROR_ALLOCATION);
				M32X_Rx_Complete (b, c);
				break;
			}
			ir->Io.desc = desc;
			desc->entry.ir = ir;
			desc->u.io.data = (u32) ddk_dma_addr2phys (ur->Io.Rx.PhysicalDataAddress);
			ddk_assert (desc->u.io.data == ddk_dma_addr2phys (ur->Io.Rx.PhysicalDataAddress));
			desc->u.io.next = b->halt_rx[c]->pa;
			desc->u.rx.u1.all_1 = M32X_IO_HI | (ur->Io.Rx.BufferLength << M32X_IO_NO_S);
			ddk_flush_cpu_writecache ();
			if (likely (prev != 0)) {
				ddk_assert (prev->u.io.next == b->halt_rx[c]->pa);
				prev->u.io.next = desc->pa;
				desc->from = prev;
			}
		}
		prev = desc;
		if (++gap == M32X_IoGapLength)
			break;
	};

	if (!gap)
		return 0;

	return ddk_queue_get (&b->rxq[c], M32X_ir_t, Io.entry)->Io.desc;
}

may_static void M32X_Rx_FireLoopAll (M32X* b)
{
	if (!(b->DeepFlags & FLAG_DEEP_RX) && b->RxFireMask)
		__M32X_Rx_FireLoop (b, ddk_ffs (b->RxFireMask));
}

may_static void M32X_Rx_FireLoopChan (M32X* b, unsigned c)
{
	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ddk_bit_set (b->RxFireMask, c);
	if ((b->DeepFlags & FLAG_DEEP_RX) == 0)
		__M32X_Rx_FireLoop (b, c);
}

may_static void __M32X_Rx_FireLoopChan (M32X* b, unsigned c)
{
	u32 dma_pa;
	M32X_dma_t *n, *desc, *dma, *next;

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	dma_pa = b->PCM_ccb.CurrentRxDescriptors[c];
	n = M32X_Rx_GetFirst (b, c);
	if (n == 0) {
		/*
		 * no space for receive
		 */
		ddk_kd_print (("M32X-rx-Fire: no space for receive\n"));
		n = b->halt_rx[c];
		if (dma_pa != n->pa) {
			/*
			 * rx-engine is on the tail of previsious undone chain.
			 * This is possible, when request cancelled or rx-error occured.
			 * We sould issue rx-fast-abort command
			 */
			if (dma_pa) {
				desc = MX32_pa2desc (b, dma_pa);
				desc->from = 0;
				desc = MX32_pa2desc (b, desc->u.io.next);
				desc->from = 0;
			}
			n->u.rx.u2.all_2 = 0;
			b->PCM_ccb.chan[c].frda = n->pa;
			if (b->rx_running[c]) {
				unsigned Command = M32X_RECEIVE_JUMP;

				if (b->PCM_ccb.chan[c].flags_a.fields.mode & 1)
					Command = M32X_RECEIVE_FAST_ABORT;
				M32X_AutoAction_Rx (b, c, Command);
			}
		}
		goto done;
	}

	/*
	 * update frda is needed for asynchronous action processing
	 */
	b->PCM_ccb.chan[c].frda = n->pa;

	/*
	 * we expect that the dma rx-descriptor is our n-first, or our next and it is not the halt_rx,
	 * or dma.next pointed to the our n-first.
	 * if these it not 1, then we need issue rx-jump or rx-fast-abort command.
	 */

	if (dma_pa) {
		M32X_Rx_CheckOverflow (b, c);

		if (dma_pa == n->pa) {
			/*
			 * our n-first is already running,
			 * we sure not need jump.
			 */
			/*
			 * LY: we must not complete request here immediately (if n->u.rx.u2.fields.c != 0)
			 * without walk over the b->riq[]!!!
			 */
			goto done;
		}

		dma = MX32_pa2desc (b, dma_pa);
		if (n->from == dma) {
			ddk_assert (dma->u.io.next == n->pa);
			if (dma->u.io.next == n->pa) {
				/*
				 * dma rx-descriptor.next pointed to our n-first, so it is just the next.
				 * also, we sure not need jump.
				 */
				goto done;
			}
			/*
			 * it is probably abort/cancel or other exception.
			 * we assume that the rx-list is broken and the M32X
			 * was not receiveted our n-first, and will does not receive it without a kick.
			 */
			n->from = 0;
		}

		if (n->from != 0) {
#if ddk_assert_check
			unsigned count = 0;
#endif
			/*
			 * now it is n-first is properly linked from anoher.
			 * we assume that M32X not need kick if dma is reachable
			 * from n-first by rx-list, and each item of rx-list is properly linked
			 * and compleeted.
			 */
			for (desc = n; desc->u.rx.u2.fields.c; desc = next) {
				if (desc->u.io.next == dma->pa) {
					/*
					 * we reach the end
					 */
					goto done;
				}

				next = MX32_pa2desc (b, desc->u.io.next);
				if (next->from != desc) {
					/*
					 * the chain is broken, M32X need a kick
					 */
					break;
				}

				ddk_assert (++count < M32X_MaxRequests);
			}

			/*
			 * at this point dma is unreachable from n-first.
			 * it is probably in exception (abort/break) or race-with-M32X condition,
			 * or if n-first is reachable for dma, but more than one step.
			 * we assume M32X need a kick
			 */
		} else {
			/*
			 * the n-first is not linked from anywere,
			 * M32X need a kick
			 */
		}

		/*
		 * if dma rx-descriptor is halt_rx
		 * then
		 *  rx-engine is on the halt_rx descriptor,
		 *  we need set frda and issue the rx-jump command.
		 *  - we can issue RX_JUMP (or not ?)
		 * else
		 *  rx-engine is on the tail of previsious undone descriptor.
		 *  This is possible, when request cancelled or rx-error occured.
		 *  - we need set frda and issue rx-abort command.
		 */

		/*
		 * mark than we issue abort/jump from <dma>
		 */
		dma->from = 0;
		desc = MX32_pa2desc (b, dma->u.io.next);
		desc->from = 0;
	}

	/*
	 * mark that we issue jump/abort to <n-first>
	 */
	n->from = n;
	if (b->rx_running[c]) {
		unsigned Command = M32X_RECEIVE_JUMP;	// M32X_RECEIVE_INIT;

		if (dma_pa != b->halt_rx[c]->pa && (b->PCM_ccb.chan[c].flags_a.fields.mode & 1))
			Command = M32X_RECEIVE_FAST_ABORT;
		M32X_AutoAction_Rx (b, c, Command);
	}
done:
	ddk_bit_clear (b->RxFireMask, c);
}

may_static void __M32X_Rx_FireLoop (M32X* b, unsigned c)
{
#if ddk_assert_check
	unsigned count = 0;
#endif

	ddk_assert (c < M32X_UseChannels);
#ifdef CRONYX_LYSAP
	ddk_assert (c == 0);
	c = 0;
#endif
	ddk_assert (!(b->DeepFlags & FLAG_DEEP_RX));
	ddk_assert (ddk_bit_test (b->RxFireMask, c));

	b->DeepFlags |= FLAG_DEEP_RX;
	for (;;) {
		ddk_assert (++count < M32X_MaxRequests);
		__M32X_Rx_FireLoopChan (b, c);
		if (!b->RxFireMask)
			break;
#ifndef CRONYX_LYSAP
		c = ddk_ffs (b->RxFireMask);
#endif
	}
	b->DeepFlags &= ~FLAG_DEEP_RX;
}

/*--------------------------------------------------------------------------------------------------------------------- */

#if defined (_NTDDK_)
#	pragma code_seg (push)
#	pragma code_seg ("PAGE")
#endif

M32X_INTERFACE_CALL void M32X_Initialize (M32X* b,
					  void *Bar1VirtualAddress, u32 pa, M32X_UserContext * uc)
{
	int i;
	M32X_RegistersBar1 RegistersBar1;

	ddk_assert (pa % 4 == 0);
	ddk_assert (sizeof (M32X_TimeslotAssignment) == 4);
	ddk_assert (sizeof (M32X_ccb) == 908);
	ddk_assert (sizeof (M32X_LBI_ccb) == 52);
	ddk_assert (sizeof (M32X) % sizeof (unsigned) == 0);
	ddk_assert (sizeof (M32X_RxDescriptor) == sizeof (M32X_TxDescriptor));
	ddk_assert (sizeof (M32X_RxDescriptor) == 16);
	ddk_assert (M32X_RxStubBufferSize % 4 == 0 && M32X_RxStubBufferSize > 4 && M32X_RxStubBufferSize <= 8192);
	ddk_assert (M32X_TxStubBufferSizePerChannel % 4 == 0 && M32X_TxStubBufferSizePerChannel > 4 && M32X_TxStubBufferSizePerChannel <= 8192);
	ddk_assert (M32X_UseChannels > 0 && M32X_UseChannels <= 32);
	ddk_assert (32 > 0 && 32 <= 32);
	ddk_assert (M32X_IoGapLength >= 3);

	ddk_memset (b, 0, sizeof (M32X));
	ddk_memset (b->tx_stub_buffer, ~0, sizeof (b->tx_stub_buffer));

	b->RegistersBar1_Hardware = (M32X_RegistersBar1 *) Bar1VirtualAddress;
	b->pa = pa;
	b->uc = uc;

	ddk_queue_init (&b->free_ir);
	ddk_queue_init (&b->rq_in);
	ddk_queue_init (&b->rq_a);
	i = M32X_UseChannels - 1;
	do {
		ddk_queue_init (&b->txq[i]);
		ddk_queue_init (&b->rxq[i]);
	} while (--i >= 0);

	/*
	 * LY: disable all interrupts, clear pending interrupts
	 */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0;	/* LY: disable LBI IRQ */
	b->RegistersBar1_Hardware->LREG6_rw_78 = 0xFFu;
	b->RegistersBar1_Hardware->IMASK_rw_0C = (u32)~0ul;
	b->RegistersBar1_Hardware->STAT_rw_08 = (u32)~0ul;

	M32X_Bar1_Clear (&RegistersBar1);

	/*
	 * LY: we need timer by LBI-clock
	 */
	RegistersBar1.CONF_rw_00.fields.cst |= M32X_LBI_CLOCK_MODE;

	/*
	 * LY: LBI clock = PCI clock/1 = 33Mhz
	 */
	RegistersBar1.CONF_rw_00.fields.lcd |= 0;

	ddk_assert (M32X_PA (PCM_ccb) % 4 == 0);
	b->PCM_CCB_PhysicalAddress = M32X_PA (PCM_ccb);
	ddk_assert (M32X_PA (PCM_CCB_PhysicalAddress) % 4 == 0);
	RegistersBar1.CCBA_rw_28 = M32X_PA (PCM_CCB_PhysicalAddress);

	ddk_assert (M32X_PA (tiq) % 4 == 0);
	RegistersBar1.TIQBA_rw_30 = M32X_PA (tiq);
	RegistersBar1.TIQL_rw_34 = sizeof (b->tiq) / 64 - 1;

	ddk_assert (M32X_PA (riq) % 4 == 0);
	RegistersBar1.RIQBA_rw_38 = M32X_PA (riq);
	RegistersBar1.RIQL_rw_3C = sizeof (b->riq) / 64 - 1;

	ddk_assert (M32X_PA (piq) % 4 == 0);
	ddk_assert (sizeof (b->piq) / 16 > 0);
	RegistersBar1.PIQBA_rw_14 = M32X_PA (piq);
	RegistersBar1.PIQL_rw_18 = sizeof (b->piq) / 16 - 1;

	RegistersBar1.TXPOLL_rw_2C = (u32)~0ul;

	ddk_assert (M32X_PA (rx_stub_buffer) % 4 == 0);
	ddk_assert (sizeof (b->rx_stub_buffer) >= 128);
	RegistersBar1.LTIQBA_rw_50 = RegistersBar1.LRIQBA_rw_58 = M32X_PA (rx_stub_buffer);
	RegistersBar1.LTIQL_rw_54 = RegistersBar1.LRIQL_rw_5C = 1;

	ddk_assert (M32X_PA (LBI_ccb) % 4 == 0);
	b->LBI_CCB_PhysicalAddress = M32X_PA (LBI_ccb);
	ddk_assert (M32X_PA (LBI_ccb) % 4 == 0);
	RegistersBar1.LCCBA_rw_44 = M32X_PA (LBI_CCB_PhysicalAddress);

	/*
	 * LY: disable IOM-2
	 */
	RegistersBar1.IOMCON1_rw_B0 = 0x080C0AFFu;
	RegistersBar1.IOMCON2_rw_B4 = 0x00000007u;

#ifdef M32X_USER_CONFIG
	M32X_USER_CONFIG (RegistersBar1);
#else
	RegistersBar1.MODE1_rw_20.fields.mfl |= 4096;	/* max frame size */
	RegistersBar1.MODE1_rw_20.fields.rbs |= 4;	/* rx bit shift */
	RegistersBar1.MODE1_rw_20.fields.rts |= 0;	/* rx timeslot */
	RegistersBar1.MODE1_rw_20.fields.tbs |= 4;	/* tx bit shift */
	RegistersBar1.MODE1_rw_20.fields.tts |= 0;	/* tx timeslot */
	RegistersBar1.MODE1_rw_20.fields.pcm |= M32X_SELECTED_RATE;

	RegistersBar1.MODE2_rw_24.fields.tsr |= 1;	/* tsp sampled on rising
							 * txclk */
	RegistersBar1.MODE2_rw_24.fields.rsf |= 0;	/* rsp sampled on rising
							 * rxclk */
	RegistersBar1.MODE2_rw_24.fields.txr |= 0;	/* txdata sampled on
							 * rising txclk */
	RegistersBar1.MODE2_rw_24.fields.rxf |= 0;	/* rxdata sampled on
							 * rising rxclk */
#endif
	RegistersBar1.MODE1_rw_20.fields.ren &= 0;	/* rx disable */
	RegistersBar1.MODE1_rw_20.fields.rid |= 1;	/* rx-int disable */

	/*
	 * prepare descriptors (list of free items)
	 */
	i = ARRAY_LENGTH (b->DescriptorsPool) - 1;
	do {
		ddk_assert (M32X_PA (DescriptorsPool[i]) % 4 == 0);
		b->DescriptorsPool[i].pa = M32X_PA (DescriptorsPool[i]);
		b->DescriptorsPool[i].entry.__next = &b->DescriptorsPool[i + 1];
	}
	while (--i >= 0);

	b->ffd = &b->DescriptorsPool[0];
	b->lfd = &b->DescriptorsPool[ARRAY_LENGTH (b->DescriptorsPool) - 1];
	b->lfd->entry.__next = 0;

	/*
	 * prepare requests (list of free items)
	 */
	i = ARRAY_LENGTH (b->RequestsPool) - 1;
	do
		ddk_queue_put (&b->free_ir, &b->RequestsPool[i].Manage.entry);
	while (--i >= 0);

	/*
	 * initialize channels structure
	 */
	ddk_assert (M32X_PA (rx_stub_buffer) % 4 == 0);
	i = 32 - 1; do {
		M32X_ChannelSpecification *cs;
		M32X_dma_t *descRx;
		M32X_dma_t *descTx;

		ddk_assert (M32X_PA (halt_rx[i]) % 4 == 0);
		cs = &b->PCM_ccb.chan[i];

		descRx = M32X_AllocDescriptor (b);
		M32X_MakeHaltRx (b, descRx);
		cs->frda = descRx->pa;

		descTx = M32X_AllocDescriptor (b);
		M32X_MakeHaltTx (b, descTx, i);
		cs->ftda = descTx->pa;

		if (i >= M32X_UseChannels && M32X_UseChannels < 32) {
			/*
			 * disable channel
			 */
			cs->flags_a.fields.cimask |= 0xFF;	/* disable all interrupts */
			cs->flags_a.fields.command_nitbs |= M32X_RECEIVE_OFF | M32X_TRANSMIT_OFF | M32X_TRANSMIT_HOLD;
		} else {
			/*
			 * rx halt descriptor
			 */
			b->halt_rx[i] = descRx;

			/*
			 * tx halt descriptor
			 */
			b->last_tx[i] = descTx;

			M32X_Channel_ClearConfig_a (cs);
		}
	} while (--i >= 0);

	/*
	 * reset timeslots Assignment
	 */
	i = 32 - 1; do {
		b->PCM_ccb.Timeslots[i].fields.rti |= 1;
		b->PCM_ccb.Timeslots[i].fields.tti |= 1;
		b->PCM_ccb.Timeslots[i].fields.rx_chan |= 32 - 1;
		b->PCM_ccb.Timeslots[i].fields.tx_chan |= 32 - 1;
	} while (--i >= 0);

	/*
	 * default timeslots Assignment
	 */
	i = 32 - 1; do {
		b->uta[i].RxChannel = b->uta[i].TxChannel = (u8) (i - 1);
		b->uta[i].RxFillmask = b->uta[i].TxFillmask = 0xFFu;
	} while (--i > 0);

	/*
	 * disable timeslot 0
	 */
	b->uta[0].RxChannel = b->uta[0].TxChannel = 0xFFu;
	b->uta[0].RxFillmask = b->uta[0].TxFillmask = 0;

	M32X_BuildTimestotAssignment (b);

	/*
	 * disable all interrupts, clear pending interrupts
	 */
	RegistersBar1.LCONF_rw_40 = 0;	/* disable LBI IRQ */
	RegistersBar1.LREG6_rw_78 = 0xFFu;
	RegistersBar1.IMASK_rw_0C = (u32)~0ul;
	RegistersBar1.STAT_rw_08 = (u32)~0ul;

#if DDK_DEBUG
	M32X_Bar1_DumpNonZeroWriteables (&RegistersBar1);
#endif
	M32X_Bar1_Write (&RegistersBar1, b->RegistersBar1_Hardware);

	/*
	 * clear pending interrupts one more
	 */
	b->RegistersBar1_Hardware->STAT_rw_08 = (u32)~0ul;
}

may_static void M32X_DestructiveCancelRequest (M32X* b, M32X_ir_t * ir)
{
	M32X_ur_t *ur = ir->ur;

	if (ur) {
		ur->ErrorCode |= M32X_ERROR_CANCELLED;
		if (ur->pInternal != ir) {
			/*
			 * cancellation waiting linked list
			 */
#if ddk_assert_check
			unsigned count = 0;
#endif
			do {
				M32X_ur_t *next = (M32X_ur_t *) ur->pInternal;

				ur->pInternal = 0;
				M32X_InvokeCallbackSafe (b, ur);
				ur = next;
				ddk_assert (++count < M32X_MaxRequests);
			}
			while (ur != 0);
		} else {
			ur->pInternal = 0;
			M32X_InvokeCallbackSafe (b, ur);
		}
		ir->ur = 0;
	}
}

M32X_INTERFACE_CALL void M32X_DestructiveHalt (M32X* b, char CancelRequests)
{
	int i;
	M32X_ChannelSpecification *cs;

#if DDK_DEBUG
	total_dump (b);
#endif

	/*
	 * disable all interrupts, clear pending interrupts
	 */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0;	/* disable LBI IRQ */
	b->RegistersBar1_Hardware->LREG6_rw_78 = 0xFFu;
	b->RegistersBar1_Hardware->IMASK_rw_0C = (u32)~0ul;
	b->RegistersBar1_Hardware->STAT_rw_08 = (u32)~0ul;

	/*
	 * disable IOM-2
	 */
	b->RegistersBar1_Hardware->IOMCON1_rw_B0 = 0x080C0AFFu;
	b->RegistersBar1_Hardware->IOMCON2_rw_B4 = 0x00000007u;

	/*
	 * disable all timeslots
	 */
	i = 32 - 1;
	do {
		b->PCM_ccb.Timeslots[i].fields.rti = 1;
		b->PCM_ccb.Timeslots[i].fields.tti = 1;
	} while (--i >= 0);

	/*
	 * stop all channels
	 */
	cs = &b->PCM_ccb.chan[0];
	i = 32 - 1;
	do
		M32X_Channel_ClearConfig_a (cs++);
	while (--i >= 0);

	/*
	 * issue noop-1
	 */
	if (!M32X_SubmitActionAndWait (b, M32X_BuildAction (0, 0, 0, 0, 0, M32X_NO_LOOP))) {
		ddk_kd_print (("M32X-Halt: 'shutdown/noop-1' action failed, result 0x%X\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
	}

	/*
	 * issue full reset
	 */
	if (!M32X_SubmitActionAndWait (b, M32X_BuildAction (1, 0, 0, 0, 0, M32X_NO_LOOP))) {
		ddk_kd_print (("M32X-Halt: 'shutdown/RES' action failed, result 0x%X\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
	}

	/*
	 * issue noop-2
	 */
	if (!M32X_SubmitActionAndWait (b, M32X_BuildAction (0, 0, 0, 0, 0, M32X_NO_LOOP))) {
		ddk_kd_print (("M32X-Halt: 'shutdown/noop-2' action failed, result 0x%X\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
	}

	b->RegistersBar1_Hardware->PIQBA_rw_14 = 0 + M32X_PA (rx_stub_buffer);
	b->RegistersBar1_Hardware->TIQBA_rw_30 = 4 + M32X_PA (rx_stub_buffer);
	b->RegistersBar1_Hardware->RIQBA_rw_38 = 8 + M32X_PA (rx_stub_buffer);
	b->RegistersBar1_Hardware->LTIQBA_rw_50 = 12 + M32X_PA (rx_stub_buffer);
	b->RegistersBar1_Hardware->LRIQBA_rw_58 = 16 + M32X_PA (rx_stub_buffer);

	b->RegistersBar1_Hardware->PIQL_rw_18 = 1;	/* 1 is safe value, 0 - may be mean 1024! */
	b->RegistersBar1_Hardware->TIQL_rw_34 = 1;
	b->RegistersBar1_Hardware->RIQL_rw_3C = 1;
	b->RegistersBar1_Hardware->LTIQL_rw_54 = 1;
	b->RegistersBar1_Hardware->LRIQL_rw_5C = 1;

	/*
	 * issue noop-3
	 */
	if (!M32X_SubmitActionAndWait (b, M32X_BuildAction (0, 0, 0, 0, 0, M32X_NO_LOOP))) {
		ddk_kd_print (("M32X-Halt: 'shutdown/noop-3' action failed, result %d/0x%X\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
	}

	/*
	 * issue new IA
	 */
	if (!M32X_SubmitActionAndWait (b, M32X_BuildAction (0, 0, 0, 0, 1, M32X_NO_LOOP))) {
		ddk_kd_print (("M32X-Halt: 'shutdown/IA' action failed, result %d/0x%X\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
	}

	/*
	 * issue noop-4
	 */
	if (!M32X_SubmitActionAndWait (b, M32X_BuildAction (0, 0, 0, 0, 0, M32X_NO_LOOP))) {
		ddk_kd_print (("M32X-Halt: 'shutdown/noop-4' action failed, result 0x%X\n",
			       b->RegistersBar1_Hardware->STAT_rw_08));
	}

	ddk_kd_print (("M32X-Halt: *PIQBA_rw_14 = %lX\n", ((u32 *) b->rx_stub_buffer)[0]));
	ddk_kd_print (("M32X-Halt: *TIQBA_rw_30 = %lX\n", ((u32 *) b->rx_stub_buffer)[1]));
	ddk_kd_print (("M32X-Halt: *RIQBA_rw_38 = %lX\n", ((u32 *) b->rx_stub_buffer)[2]));
	ddk_kd_print (("M32X-Halt: *LTIQBA_rw_50 = %lX\n", ((u32 *) b->rx_stub_buffer)[3]));
	ddk_kd_print (("M32X-Halt: *LRIQBA_rw_58 = %lX\n", ((u32 *) b->rx_stub_buffer)[4]));

	b->RegistersBar1_Hardware->PIQBA_rw_14 = M32X_SAFE_PCI_ADDRESS;
	b->RegistersBar1_Hardware->TIQBA_rw_30 = M32X_SAFE_PCI_ADDRESS;
	b->RegistersBar1_Hardware->RIQBA_rw_38 = M32X_SAFE_PCI_ADDRESS;
	b->RegistersBar1_Hardware->LTIQBA_rw_50 = M32X_SAFE_PCI_ADDRESS;
	b->RegistersBar1_Hardware->LRIQBA_rw_58 = M32X_SAFE_PCI_ADDRESS;

	/*
	 * clear config and hush pending interrupts one more
	 */
	b->RegistersBar1_Hardware->CONF_rw_00.all = 0;
	b->RegistersBar1_Hardware->CMD_w_04 = 0;
	b->RegistersBar1_Hardware->MODE1_rw_20.all = 0;
	b->RegistersBar1_Hardware->MODE2_rw_24.all = 0;
	b->RegistersBar1_Hardware->STAT_rw_08 = (u32)~0ul;

	if (CancelRequests) {
		M32X_ir_t *ir;

#if ddk_assert_check
		unsigned count = 0;
#endif

		while (!ddk_queue_isempty (&b->rq_in)) {
			ir = ddk_queue_get (&b->rq_in, M32X_ir_t, Manage.entry);
			ddk_queue_entry_detach (&ir->Manage.entry);
			M32X_DestructiveCancelRequest (b, ir);
			ddk_assert (++count < M32X_MaxRequests);
		}
		while (!ddk_queue_isempty (&b->rq_a)) {
			ir = ddk_queue_get (&b->rq_a, M32X_ir_t, Manage.entry);
			ddk_queue_entry_detach (&ir->Manage.entry);
			M32X_DestructiveCancelRequest (b, ir);
			ddk_assert (++count < M32X_MaxRequests);
		}

		i = M32X_UseChannels - 1;
		do {
			while (!ddk_queue_isempty (&b->txq[i])) {
				ir = ddk_queue_get (&b->txq[i], M32X_ir_t, Io.entry);
				ddk_queue_entry_detach (&ir->Io.entry);
				M32X_DestructiveCancelRequest (b, ir);
				ddk_assert (++count < M32X_MaxRequests);
			}
			while (!ddk_queue_isempty (&b->rxq[i])) {
				ir = ddk_queue_get (&b->rxq[i], M32X_ir_t, Io.entry);
				ddk_queue_entry_detach (&ir->Io.entry);
				M32X_DestructiveCancelRequest (b, ir);
				ddk_assert (++count < M32X_MaxRequests);
			}
		}
		while (--i >= 0);
	}

	/*
	 * LY: clear *this
	 */
	ddk_memset (b, 0, sizeof (M32X));
}

#if defined (_NTDDK_)
#	pragma code_seg (pop)
#endif

M32X_INTERFACE_CALL char M32X_SubmitRequest (M32X* b, M32X_ur_t * ur)
{
	int TaskCount = 0;
	M32X_ir_t *ir;

	ur->ErrorCode = 0;
	ur->pInternal = 0;

	/*
	 * verify request
	 */
#if defined (_NTDDK_)
	if (ur->CallbackDpc.DeferredRoutine == 0
#else
	if (ur->pCallback == 0
#endif
	    || (ur->Command & __M32X_ValidCommands_mask) == 0 || (ur->Command & ~__M32X_ValidCommands_mask) != 0)
		return 0;

	if (ur->Command & TAU32_Tx_Data) {
		ddk_assert (ur->Io.Tx.PhysicalDataAddress);
		TaskCount++;
	}

	if (ur->Command & TAU32_Rx_Data) {
		ddk_assert (ur->Io.Rx.PhysicalDataAddress);
		TaskCount++;
	}

	if (ur->Command & M32X_Configure_Setup)
		TaskCount++;

	if (ur->Command & M32X_Timeslots_Complete)
		TaskCount++;

	if (ur->Command & M32X_Timeslots_Map)
		TaskCount++;

	if (ur->Command & M32X_Timeslots_Channel)
		TaskCount++;

	if (ur->Command & M32X_Configure_Loop)
		TaskCount++;

	if (__M32X_UserCommands && (ur->Command & __M32X_UserCommands) != 0)
		TaskCount++;

	if (TaskCount > 1) {
		ddk_kd_print (("M32X-SubmitRequest: 0x%x is rejected: receive data or transmit data, but not both\n",
			       ur));
		return 0;
	}

	ir = M32X_AllocInternalRequest (b);
	if (ir == 0) {
		ur->ErrorCode |= M32X_ERROR_ALLOCATION;
		M32X_NotifyError (b, -1, M32X_ERROR_ALLOCATION);
		return 0;
	}
#ifdef M32X_USER_VERIFY_REQUEST
	if (!M32X_USER_VERIFY_REQUEST (b, ur, ir))
		goto ballout;
#endif

	/*
	 * we allow the 'noop' command
	 */

	if ((ur->Command & M32X_Rx_Data) != 0 && (ur->Command & M32X_Tx_Data) != 0) {
		ddk_kd_print (("M32X-SubmitRequest: 0x%x is rejected: receive data or transmit data, but not both\n",
			       ur));
		goto ballout;
	}

	if ((ur->Command & (M32X_Configure_Setup | __M32X_Tx_mask | __M32X_Rx_mask | M32X_Timeslots_Channel)) != 0
#ifdef CRONYX_LYSAP
	    && ur->Io.cn) {
#else
	    && ur->Io.cn >= 32) {
#endif
		ddk_kd_print (("M32X-SubmitRequest: 0x%x is rejected: channel number is invalid\n", ur));
		goto ballout;
	}

	if (ur->Command & M32X_Tx_Data) {
		ur->Io.Tx.Transmitted = 0;
		if (ur->Io.Tx.PhysicalDataAddress == 0) {
			ddk_kd_print (("M32X-SubmitRequest: 0x%x is rejected: address is invalid\n", ur));
			goto ballout;
		}
		if (ur->Io.Tx.DataLength > 4096 || ur->Io.Tx.DataLength == 0) {
			ddk_kd_print (("M32X-SubmitRequest: 0x%x is rejected: transmit buffer length is invalid\n",
				       ur));
			goto ballout;
		}
	}

	if (ur->Command & M32X_Rx_Data) {
		ur->Io.Rx.Received = 0;
		if (ur->Io.Rx.BufferLength < 4 || ur->Io.Rx.BufferLength > 4096
		    || (ur->Io.Rx.BufferLength & 3) != 0
		    || ddk_dma_addr2phys (ur->Io.Rx.PhysicalDataAddress) == 0
			|| (ddk_dma_addr2phys (ur->Io.Rx.PhysicalDataAddress) & 3) != 0) {
			ddk_kd_print (("M32X-SubmitRequest: 0x%x is rejected: receive buffer length/address is invalid\n", ur));
			goto ballout;
		}
	}

	ir->ur = ur;
	ur->pInternal = ir;

	ddk_queue_put (&b->rq_in, &ir->Manage.entry);
	M32X_Request_FireLoop (b);
	M32X_HandleInterrupt (b);
	return 1;

ballout:
	M32X_FreeInternalRequest (b, ir);
	return 0;
}

M32X_INTERFACE_CALL char M32X_CancelRequest (M32X* b, M32X_ur_t * ur, char BreakIfRunning)
{
	M32X_ir_t *ir;

	ddk_kd_print ((">> M32X: cancel request %x - %x\n", ur->pInternal, ur));

	ir = (M32X_ir_t *) ur->pInternal;
	if (!M32X_IsPointerBetween (ir, &b->RequestsPool[0], ARRAY_END (b->RequestsPool))) {
		ddk_kd_print (("<< M32X: request %x - %x NOT cancelled (1)\n", ur->pInternal, ur));
		return 0;
	}

	ddk_assert (ir->ur == ur);
	if (ir->ur != ur) {
		ddk_kd_print (("<< M32X: request %x - %x NOT cancelled (2)\n", ur->pInternal, ur));
		return 0;
	}
#ifdef M32X_USER_CANCEL_REQUEST
	M32X_USER_CANCEL_REQUEST (b, ur, ir, BreakIfRunning);
#endif

	if (ir->Manage.RefCounter == 0) {
#if ddk_assert_check
		unsigned i;
#endif

		/*
		 * request is only in inbound queue
		 */
		ddk_assert (!ddk_queue_iscontain (&b->rq_a, &ir->Manage.entry));
		ddk_assert (ddk_queue_iscontain (&b->rq_in, &ir->Manage.entry));
#if ddk_assert_check
		for (i = 0; i < M32X_UseChannels; i++) {
			ddk_assert (!ddk_queue_iscontain (&b->rxq[i], &ir->Io.entry));
		}
#endif
		ddk_assert (ir->Io.desc == 0);
		ir->ur = 0;
		ddk_queue_entry_detach (&ir->Manage.entry);
		M32X_FreeInternalRequest (b, ir);
		M32X_Request_FireLoop (b);
		ddk_kd_print (("<< M32X: request %x - %x cancelled (4.1)\n", ur->pInternal, ur));
		ur->ErrorCode |= M32X_ERROR_CANCELLED;
		ur->pInternal = 0;
		return 1;
	}

	ddk_assert (!ddk_queue_iscontain (&b->rq_in, &ir->Manage.entry));
	if (!BreakIfRunning && ir->Io.desc) {
		ddk_assert (ir->Manage.RefCounter > 0);
		ddk_kd_print (("<< M32X: request %x - %x NOT cancelled (5)\n", ur->pInternal, ur));
		return 0;
	}

	if (ddk_queue_iscontain (&b->rq_a, &ir->Manage.entry)) {
		ddk_assert (ir->Manage.RefCounter > 0);
		if (!BreakIfRunning) {
			ddk_kd_print (("<< M32X: request %x - %x NOT cancelled (6)\n", ur->pInternal, ur));
			return 0;
		}
	}

	ur->ErrorCode |= M32X_ERROR_CANCELLED;
	if (ur->Io.cn < M32X_UseChannels) {
		/*
		 * check for tx/rx queues
		 */
		if (ddk_queue_iscontain (&b->rxq[ur->Io.cn], &ir->Io.entry)) {
			/*
			 * cancel rx-request
			 */
			ddk_assert (ir->Manage.RefCounter > 0);
			ir->Manage.RefCounter--;
			if (ir->Io.desc) {
				M32X_dma_t *descTo;

				ir->Io.desc->u.io.data = M32X_PA (rx_stub_buffer);
				ir->Io.desc->u.io.u.fields.no = 4;
				/*
				 * remove rx-descriptors from rx-gap-list
				 */
				descTo = MX32_pa2desc (b, ir->Io.desc->u.io.next);
				ddk_assert (M32X_IsValidDescriptor (b, descTo)
					    || descTo == b->halt_rx[ur->Io.cn]);

				descTo->from = 0;
				if (ir->Io.desc->from) {
					M32X_dma_t *descFrom = ir->Io.desc->from;

					ddk_assert (M32X_IsValidDescriptor (b, descFrom));
					descFrom->u.io.next = descTo->pa;
					descTo->from = descFrom;
				}
				M32X_FreeDescriptorRx (b, ir);
			}
			ddk_queue_entry_detach (&ir->Io.entry);
			M32X_Rx_FireLoopChan (b, ur->Io.cn);
		}

		if (ddk_queue_iscontain (&b->txq[ur->Io.cn], &ir->Io.entry)) {
			/*
			 * cancel tx-request
			 */
			ddk_assert (ir->Manage.RefCounter > 0);
			ir->Manage.RefCounter--;
			if (ir->Io.desc) {
				M32X_dma_t *descTo = 0;
				M32X_dma_t *descFrom = ir->Io.desc->from;

				/*
				 * remove tx-descriptors from tx-gap-list
				 */
				if (!ir->Io.desc->u.tx.u.fields.hold) {
					ddk_assert (ir->Io.desc->u.io.next);
					descTo = MX32_pa2desc (b, ir->Io.desc->u.io.next);
					ddk_assert (M32X_IsValidDescriptor (b, descTo)
						    || descTo == b->last_tx[ur->Io.cn]);

					descTo->from = 0;
					if (descFrom) {
						ddk_assert (M32X_IsValidDescriptor (b, descFrom));
						descFrom->u.io.next = descTo->pa;
						descTo->from = descFrom;
					}
				} else if (descFrom) {
					descFrom->u.tx.u.fields.hold |= 1;
					descFrom->u.io.next = 0;
					M32X_PutLastTx (b, ur->Io.cn, descFrom);
				}
				M32X_FreeDescriptorTx (b, ir);
			}
			ddk_queue_entry_detach (&ir->Io.entry);
			M32X_Tx_FireLoopChan (b, ur->Io.cn);
		}

		if (b->AutoActions[ur->Io.cn]) {
			/*
			 * cancellation request is running or queued
			 */
			ir->ur = 0;
			if (ir->Manage.RefCounter == 0)
				M32X_FreeInternalRequest (b, ir);

			/*
			 * append linked list by pInternal
			 * ddk_dbg_print ("M32X: append cancellation waiting list\n");
			 */
			ur->pInternal = b->AutoActions[ur->Io.cn]->ur;
			b->AutoActions[ur->Io.cn]->ur = ur;
			ddk_kd_print (("<< M32X: request %x - %x cancellation pending (6)\n", ur->pInternal, ur));
			return 0;
		}
	}

	ir->ur = 0;
	if (ir->Manage.RefCounter == 0)
		M32X_FreeInternalRequest (b, ir);

	ddk_kd_print (("<< M32X: request %x - %x cancelled (6)\n", ur->pInternal, ur));
	ur->pInternal = 0;

	M32X_HandleInterrupt (b);
	return 1;
}

M32X_INTERFACE_CALL char M32X_IsInterruptPending (M32X* b)
{
	return b->RegistersBar1_Hardware->STAT_rw_08 != 0;
}

M32X_INTERFACE_CALL char M32X_HandleInterrupt (M32X* b)
{
	unsigned loopcount;
	u32 status;

	if (unlikely (b->DeepFlags & FLAG_DEEP_IRQ))
		return 0;

	status = b->RegistersBar1_Hardware->STAT_rw_08;
	if (status == 0)
		return 0;

	loopcount = 0;
	b->DeepFlags |= FLAG_DEEP_IRQ;
loop:
	do {
		ddk_yield_dmabus ();
		/*
		 * LY: acknowledge
		 */
#ifdef M32X_USER_INTERRUPT_BITS
		b->RegistersBar1_Hardware->STAT_rw_08 = status & ~M32X_USER_INTERRUPT_BITS;
#else
		b->RegistersBar1_Hardware->STAT_rw_08 = status;
#endif

		if (status & (M32X_STAT_PCMF | M32X_STAT_PCMA | M32X_STAT_TI)) {
			/*
			 * LY: stop timer & cancel action (if timeout)
			 */
			b->RegistersBar1_Hardware->CMD_w_04 = 0;
			b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA;
			ddk_kd_print (("M32X-Interrupt: action status 0x%X\n",
				       status & (M32X_STAT_PCMF | M32X_STAT_PCMA | M32X_STAT_TI)));
			M32X_Actions_HandleInterrupt (b, status);
		} else
			/*
			 * LY: experimental
			 */
			if (unlikely ((b->as == M32X_ACTION_PENDING)
				      && (b->RegistersBar1_Hardware->CMD_w_04 & 1) == 0)) {
				/*
				 * LY: assume that is a bug in M32X, and so, an action is done
				 */
				ddk_yield_dmabus ();
				if ((b->RegistersBar1_Hardware->STAT_rw_08 & (M32X_STAT_PCMF | M32X_STAT_PCMA)) == 0) {
					b->RegistersBar1_Hardware->CMD_w_04 = 0;
					b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA;
					M32X_Actions_HandleInterrupt (b, M32X_STAT_PCMA);
				}
			}

		if (status & (M32X_STAT_PTI | M32X_STAT_TSPA)) {
			if (unlikely (status & M32X_STAT_TSPA))
				M32X_NotifyError (b, -1, M32X_ERROR_TSP);
			M32X_Tx_HandleInterrupts (b);
		}

		if (status & (M32X_STAT_PRI | M32X_STAT_RSPA)) {
			if (unlikely (status & M32X_STAT_RSPA))
				M32X_NotifyError (b, -1, M32X_ERROR_RSP);
			M32X_Rx_HandleInterrupts (b);
		}
#if defined (M32X_USER_INTERRUPT_BITS) && defined (M32X_USER_HANDLE_INTERRUPT)
		if (status & M32X_USER_INTERRUPT_BITS) {
			M32X_USER_HANDLE_INTERRUPT (b, status);
			b->RxWatchdogMask = M32X_ALLMASK;
		}
#endif

		if (unlikely (++loopcount > M32X_MaxInterruptsLoop)) {
			M32X_NotifyError (b, -1, M32X_ERROR_INT_STORM);
			break;
		}
		status = b->RegistersBar1_Hardware->STAT_rw_08;
	} while (status);

	M32X_Rx_FireLoopAll (b);
	M32X_Tx_FireLoopAll (b);

	status = b->RegistersBar1_Hardware->STAT_rw_08;
	if (status && likely (loopcount < M32X_MaxInterruptsLoop))
		goto loop;

	MX32_RxWatchdog (b);
	b->DeepFlags &= ~FLAG_DEEP_IRQ;
	return 1;
}

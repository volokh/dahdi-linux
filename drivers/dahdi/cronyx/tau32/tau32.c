/*
 * Cronyx Tai-32 specific functions)
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
 * $Id: tau32.c,v 1.52 2009-01-16 15:32:37 ly Exp $
 */

#define GPP_INIT(GPP) GPP = &b->RegistersBar1_Hardware->GPDATA_rw_84
#define GPP_DATA(GPP)((GPP)[0])
#define GPP_DIR(GPP)((GPP)[-1])
#define GPP_DATA_BYTE(GPP)(((volatile u8 *)(GPP))[1])

#define GPP_1KDATA	0x0001
#define GPP_DCLK	0x0002
#define GPP_NCONFIG	0x0004
#define GPP_NSTATUS	0x0008
#define GPP_CONFDONE	0x0010
#define GPP_1KLOAD	0x0020
#define GPP_IOW		0x0040
#define GPP_IOR		0x0080

/*
* LY: take 700 ns
*/
static __forceinline unsigned TAU32_in_byte (volatile u32 * GPP, unsigned reg)
{
	unsigned addr, result;

	ddk_assert (reg >= 0 && reg <= 0x1F);
	addr = reg & 0x1F;

	/*
	 * set address, /OIW = 1, /IOR = 1, /1KLOAD = 1
	 */
	GPP_DATA (GPP) = addr | GPP_1KLOAD | GPP_IOW | GPP_IOR;

	/*
	 * set bus mode (address, /IOW, /IOR, /1KLOAD) = out, data = in
	 */
	GPP_DIR (GPP) = 0x00FF;

	/*
	 * set address, /OIW = 1, /IOR = 0, /1KLOAD = 1
	 */
	GPP_DATA (GPP) = addr | GPP_1KLOAD | GPP_IOW;

	/*
	 * just delay 100 ns
	 */
	GPP_DIR (GPP) = 0x00FF;

	/*
	 * load data
	 */
	result = GPP_DATA_BYTE (GPP);
	/*
	 * unsigned result = (GPP_DATA (GPP) >> 8) & 0xFF;
	 */

	/*
	 * leave address, set /OIW = 1, /IOR = 1, /1KLOAD = 1
	 */
	GPP_DATA (GPP) = addr | GPP_1KLOAD | GPP_IOW | GPP_IOR;

	/*
	 * set bus (/IOW, /IOR, /1KLOAD) = out, (address, data) = in
	 */
	GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;

	return result;
}

/*
* LY: take 600 ns
*/
static __forceinline void TAU32_out_byte (volatile u32 * GPP, unsigned reg, unsigned val)
{
	unsigned addr_data;

	ddk_assert (reg >= 0 && reg <= 0x1F);
	ddk_assert (val >= 0 && val <= 0xFF);
	addr_data = (reg & 0x1F) | (val << 8);

	/*
	 * set address & data, /OIW = 1, /IOR = 1, /1KLOAD = 1
	 */
	GPP_DATA (GPP) = addr_data | GPP_1KLOAD | GPP_IOW | GPP_IOR;

	/*
	 * set bus mode (address, data, /IOW, /IOR, /1KLOAD) = out
	 */
	GPP_DIR (GPP) = 0xFFFFFFFF;

	/*
	 * leave address, set /OIW = 0, /IOR = 1, /1KLOAD = 1
	 */
	GPP_DATA (GPP) = addr_data | GPP_1KLOAD | GPP_IOR;

	/*
	 * just delay 100 ns
	 */
	GPP_DIR (GPP) = 0xFFFFFFFF;

	/*
	 * leave address, set /OIW = 1, /IOR = 1, /1KLOAD = 1
	 */
	GPP_DATA (GPP) = addr_data | GPP_1KLOAD | GPP_IOW | GPP_IOR;

	/*
	 * set bus (/IOW, /IOR, /1KLOAD) = out, (address, data) = in
	 */
	GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;
}

static __forceinline unsigned TAU32_chip2cs (unsigned chip)
{
	if (chip == 0)
		return T32_CS0;
	if (chip == 1)
		return T32_CS1;
	if (chip == 2)
		return T32_CS2;
	ddk_assume (0);
	ddk_assert (0);
	return 0xFFu;
}

static __forceinline void TAU32_write_register_inl (unsigned chip, volatile u32 * GPP, unsigned reg, unsigned val)
{
	ddk_assert (reg >= 0 && reg <= 0xFF);
	ddk_assert (val >= 0 && val <= 0xFF);

	TAU32_out_byte (GPP, TAU32_chip2cs (chip), reg);
	TAU32_out_byte (GPP, T32_DAT, val);
}

static __forceinline unsigned TAU32_read_register_inl (unsigned chip, volatile u32 * GPP, unsigned reg)
{
	ddk_assert (reg >= 0 && reg < 0xC0);

	TAU32_out_byte (GPP, TAU32_chip2cs (chip), reg);
	return TAU32_in_byte (GPP, T32_DAT);
}

static __forceinline unsigned TAU32_read_status_inl (unsigned chip, volatile u32 * GPP, unsigned reg)
{
	unsigned result;

	ddk_assert (reg >= 0 && reg < 0xC0);

	/*
	 * lock bits
	 */
	TAU32_out_byte (GPP, TAU32_chip2cs (chip), reg);
	TAU32_out_byte (GPP, T32_DAT, 0xFF);

	/*
	 * get values
	 */
	TAU32_out_byte (GPP, TAU32_chip2cs (chip), reg);
	result = TAU32_in_byte (GPP, T32_DAT);

	/*
	 * unlock bits
	 */
	TAU32_out_byte (GPP, TAU32_chip2cs (chip), reg);
	TAU32_out_byte (GPP, T32_DAT, result);

	return result;
}

// LY: b->TsTestMode = 1
// LY: первая фаза проверки - контролируем номера таймслотов в данных по приёму.
// LY: выключаем мультиплексор-фреймер CAS, для сохранения данных внутри таймслотов.
// LY: включаем "нумерацию" таймслотов для проверки сдвига по-приёму.
//
// LY: b->TsTestMode = 2
// LY: вторая фаза - клиентский код генерирует ключевую payload для передачи и проверяет прием.
// LY: выключаем мультиплексор-фреймер CAS, для сохранения данных внутри таймслотов.
// LY: выключаем "нумерацию" таймслотов для проверки сдвига по-приёму.
//
// LY: b->TsTestMode = 0
// LY: переходим в нормальный режим работы.
may_static void TAU32_write_cr (TAU32_Controller *b, volatile u32 *GPP, unsigned cr)
{
	if (b->TsTestMode)
		cr &= ~CR_CASEN;
	TAU32_out_byte (GPP, T32_CR, cr);
}

may_static void TAU32_write_umr0 (TAU32_Controller *b, volatile u32 *GPP, unsigned umr0)
{
	if (b->TsTestMode == 1)
		umr0 = UMR0_PEB_CHECK | UMR_FIFORST;
	TAU32_out_byte (GPP, T32_UMR0, umr0);
}

/*----------------------------------------------------------------------------- */

may_static void TAU32_write_register_cs0 (volatile u32 * GPP, unsigned reg, unsigned val)
{
	TAU32_write_register_inl (0, GPP, reg, val);
}

may_static unsigned TAU32_read_register_cs0 (volatile u32 * GPP, unsigned reg)
{
	return TAU32_read_register_inl (0, GPP, reg);
}

may_static unsigned TAU32_read_status_cs0 (volatile u32 * GPP, unsigned reg)
{
	return TAU32_read_status_inl (0, GPP, reg);
}

may_static void TAU32_write_register32_cs0 (volatile u32 * GPP, unsigned reg, u32 val)
{
	TAU32_write_register_inl (0, GPP, reg + 0, val & 0xFFu);
	TAU32_write_register_inl (0, GPP, reg + 1, (val >> 8) & 0xFFu);
	TAU32_write_register_inl (0, GPP, reg + 2, (val >> 16) & 0xFFu);
	TAU32_write_register_inl (0, GPP, reg + 3, val >> 24);
}

may_static u32 TAU32_read_register32_cs0 (volatile u32 * GPP, unsigned reg)
{
	u32 result = TAU32_read_register_inl (0, GPP, reg + 0);

	result += TAU32_read_register_inl (0, GPP, reg + 1) << 8;
	result += TAU32_read_register_inl (0, GPP, reg + 2) << 16;
	result += TAU32_read_register_inl (0, GPP, reg + 3) << 24;
	return result;
}

/*----------------------------------------------------------------------------- */

may_static void TAU32_write_register_cs1 (volatile u32 * GPP, unsigned reg, unsigned val)
{
	TAU32_write_register_inl (1, GPP, reg, val);
}

may_static unsigned TAU32_read_register_cs1 (volatile u32 * GPP, unsigned reg)
{
	return TAU32_read_register_inl (1, GPP, reg);
}

may_static unsigned TAU32_read_status_cs1 (volatile u32 * GPP, unsigned reg)
{
	return TAU32_read_status_inl (1, GPP, reg);
}

may_static void TAU32_write_register32_cs1 (volatile u32 * GPP, unsigned reg, u32 val)
{
	TAU32_write_register_inl (1, GPP, reg + 0, val & 0xFFu);
	TAU32_write_register_inl (1, GPP, reg + 1, (val >> 8) & 0xFFu);
	TAU32_write_register_inl (1, GPP, reg + 2, (val >> 16) & 0xFFu);
	TAU32_write_register_inl (1, GPP, reg + 3, val >> 24);
}

may_static u32 TAU32_read_register32_cs1 (volatile u32 * GPP, unsigned reg)
{
	u32 result = TAU32_read_register_inl (1, GPP, reg + 0);

	result += TAU32_read_register_inl (1, GPP, reg + 1) << 8;
	result += TAU32_read_register_inl (1, GPP, reg + 2) << 16;
	result += TAU32_read_register_inl (1, GPP, reg + 3) << 24;
	return result;
}

/*----------------------------------------------------------------------------- */

may_static void TAU32_write_register_both (TAU32_Controller * b, unsigned reg, unsigned val)
{
	volatile u32 *GPP;

	ddk_assert (b->ModelType > TAU32_UNKNOWN);
	ddk_assert (reg >= 0 && reg <= 0xFF);
	ddk_assert (val >= 0 && val <= 0xFF);

	GPP_INIT (GPP);

	ddk_assert (reg >= 0 && reg <= 0xFF);
	ddk_assert (val >= 0 && val <= 0xFF);

	TAU32_out_byte (GPP, T32_CS01, reg);
	TAU32_out_byte (GPP, T32_DAT, val);
}

may_static void TAU32_clear_ds (TAU32_Controller * b)
{
	unsigned i = 0;

	do {
		TAU32_write_register_both (b, i, (i == DS_LICR) ? LICR_POWERDOWN : 0);
	} while (++i < 256);
}

#if DDK_DEBUG
may_static const char *dallas_registers[0xC0] = {
	/*
	 * 00
	 */ "VCR1", "VCR2", "CRCCR1", "CRCCR2", "EBCR1", "EBCR2", "SR1",
	"SR2",
	"RIR", "_09", "_0A", "_0B", "_0C", "_0D", "_0E", "IDR",
	/*
	 * 10
	 */ "RCR1", "RCR2", "TCR1", "TCR2", "CCR1", "TEST1", "IMR1",
	"IMR2",
	"LICR", "TEST2", "CCR2", "CCR3", "TSaCR", "CCR6", "SSR", "RNAF",
	/*
	 * 20
	 */ "TAF", "TNAF", "TCBR1", "TCBR2", "TCBR3", "TCBR4", "TIR1",
	"TIR2",
	"TIR3", "TIR4", "TIDR", "RCBR1", "RCBR2", "RCBR3", "RCBR4", "RAF",

	/*
	 * 30
	 */ "RS[1]", "RS[2]", "RS[3]", "RS[4]", "RS[5]", "RS[6]", "RS[7]",
	"RS[8]",
	"RS[9]", "RS[10]", "RS[11]", "RS[12]", "RS[13]", "RS[14]",
	"RS[15]",
	"RS[16]",
	/*
	 * 40
	 */ "TS[1]", "TS[2]", "TS[3]", "TS[4]", "TS[5]", "TS[6]", "TS[7]",
	"TS[8]",
	"TS[9]", "TS[10]", "TS[11]", "TS[12]", "TS[13]", "TS[14]",
	"TS[15]",
	"TS[16]",
	/*
	 * 50
	 */ "TSiAF", "TSiNAF", "TRA", "TSa4", "TSa5", "TSa6", "TSa7",
	"TSa8",
	"RSiAF", "RSiNAF", "RRA", "RSa4", "RSa5", "RSa6", "RSa7", "RSa8",

	/*
	 * 60
	 */ "TC[1]", "TC[2]", "TC[3]", "TC[4]", "TC[5]", "TC[6]", "TC[7]",
	"TC[8]",
	"TC[9]", "TC[10]", "TC[11]", "TC[12]", "TC[13]", "TC[14]",
	"TC[15]",
	"TC[16]",
	/*
	 * 70
	 */ "TC[17]", "TC[18]", "TC[19]", "TC[20]", "TC[21]", "TC[22]",
	"TC[23]", "TC[24]",
	"TC[25]", "TC[26]", "TC[27]", "TC[28]", "TC[29]", "TC[30]",
	"TC[31]",
	"TC[32]",
	/*
	 * 80
	 */ "RC[1]", "RC[2]", "RC[3]", "RC[4]", "RC[5]", "RC[6]", "RC[7]",
	"RC[8]",
	"RC[9]", "RC[10]", "RC[11]", "RC[12]", "RC[13]", "RC[14]",
	"RC[15]",
	"RC[16]",
	/*
	 * 90
	 */ "RC[17]", "RC[18]", "RC[19]", "RC[20]", "RC[21]", "RC[22]",
	"RC[23]", "RC[24]",
	"RC[25]", "RC[26]", "RC[27]", "RC[28]", "RC[29]", "RC[30]",
	"RC[31]",
	"RC[32]",

	/*
	 * A0
	 */ "TCC1", "TCC2", "TCC3", "TCC4", "RCC1", "RCC2", "RCC3", "RCC4",
	"CCR4", "TDS0M", "CCR5", "RDS0M", "TEST3", "_AD", "_AE", "_AF",
	/*
	 * B0
	 */ "HCR", "HSR", "HIMR", "RHIR", "RHFR", "IBO", "THIR", "THFR",
	"RDC1", "RDC2", "TDC1", "TDC2", "_BC", "_BD", "_BE", "_BF"
		/*
		 * C0
		 */
		/*
		 * D0
		 */
		/*
		 * E0
		 */
		/*
		 * F0
		 */
};

may_static void TAU32_DumpDallas (unsigned chip, TAU32_Controller * b)
{
	int i;
	volatile u32 *GPP;

	ddk_kd_print (("TAU32_Controller: DumpDallas_cs%d\n", chip));

	GPP_INIT (GPP);
	for (i = 0; i < 0xC0; i += 4)
		ddk_kd_print (("%6s.%02X = %02X %6s.%02X = %02X %6s.%02X = %02X %6s.%02X = %02X\n",
			       dallas_registers[i + 0], i + 0, chip ? TAU32_read_register_cs1 (GPP, i + 0) :
			       TAU32_read_register_cs0 (GPP, i + 0), dallas_registers[i + 1], i + 1,
			       chip ? TAU32_read_register_cs1 (GPP, i + 1) : TAU32_read_register_cs0 (GPP, i + 1),
			       dallas_registers[i + 2], i + 2, chip ? TAU32_read_register_cs1 (GPP,  i + 2) :
			       TAU32_read_register_cs0 (GPP, i + 2), dallas_registers[i + 3], i + 3,
			       chip ? TAU32_read_register_cs1 (GPP, i + 3) : TAU32_read_register_cs0 (GPP, i + 3)
			      ));
}
#endif /* DDK_DEBUG */

static __forceinline char TAU32_config2dallas (unsigned InterfaceConfig,
					       u32 UnframedMask, TAU32_DallasModeRegistersLite * pDallas)
{
	/*
	 * clocks is always input
	 */
	pDallas->tcr1 = TCR1_TSI;
	pDallas->rcr1 = RCR1_RSI;

	pDallas->ccr2 = CCR2_EC625 | CCR2_TRCLK;
	pDallas->ccr3 = CCR3_TBCS | (ENABLE_E1_TES ? CCR3_TESE : 0);
	pDallas->ccr1 = 0;
	if (!(InterfaceConfig & TAU32_tx_ami))
		pDallas->ccr1 |= CCR1_THDB3;
	if (!(InterfaceConfig & TAU32_rx_ami)) {
		pDallas->ccr1 |= CCR1_RHDB3;
		pDallas->ccr2 |= CCR2_CNTCV;	/* LY: count code violations
						 * instead of bpv */
		pDallas->ccr3 |= CCR3_RCLA;	/* LY: 2048 zeros insted of 255 for AMI */
	}

	pDallas->licr = LICR_JA_RX;
	if (InterfaceConfig & TAU32_ja_tx)
		pDallas->licr = LICR_JA_TX /* | LICR_JA_LOW */ ;	/* LY: transmit
									 * side, 32 bit
									 * depth */
	pDallas->ccr4 = 0;
	pDallas->tcr2 = 0;
	pDallas->rcbr = 0;
	pDallas->umr = 0;
	pDallas->tsacr = 0;
#if DDK_DEBUG
	pDallas->ccr2 &= ~CCR2_EC625;	/* LY: if DDK_DEBUG set timer to 1 sec */
#endif
	pDallas->test3 = 0;

	if ((InterfaceConfig & TAU32_fas_io)
	    && (InterfaceConfig & TAU32_fas8_io)) {
		ddk_kd_print ((" (5) "));
		return 0;
	}

	if ((InterfaceConfig & (TAU32_fas_io | TAU32_fas8_io))
	    && (InterfaceConfig & TAU32_crc4_mf_tx)) {
		ddk_kd_print ((" (6) "));
		return 0;
	}

	switch (InterfaceConfig & TAU32_line_mode_mask) {
		case TAU32_LineOff:
			pDallas->licr = LICR_POWERDOWN;
			pDallas->imr1 = 0;
			pDallas->imr2 = 0;
			return 1;
		case TAU32_LineNormal:
			break;
		case TAU32_LineLoopInt:
			pDallas->ccr4 |= CCR4_LLB;
			pDallas->licr &= ~LICR_JA_TX;
			// pDallas->licr |= LICR_POWERDOWN;
			break;
		case TAU32_LineLoopExt:
			pDallas->ccr4 |= CCR4_RLB;
			return 1;
		case TAU32_LineAIS:
			pDallas->tcr1 |= TCR1_TUA1;
			break;
		default:
			ddk_kd_print ((" (4) "));
			return 0;
	}

	pDallas->imr1 = (ENABLE_E1_RES ? SR1_RSLIP : 0) | SR1_RUA1 | SR1_RCL | SR1_RLOS;
	pDallas->imr2 = SR2_SEC | SR2_LOTC | (ENABLE_E1_TES ? SR2_TSLIP : 0);
	if (InterfaceConfig & TAU32_fas_io) {
		pDallas->imr2 |= SR2_RAF | SR2_TAF;
		pDallas->tcr1 |= TCR1_TSIS;	/* Si from TAF/TNAF */
	}

	if (InterfaceConfig & TAU32_fas8_io) {
		pDallas->imr2 |= SR2_TMF | SR2_RCMF;
		pDallas->tsacr = 0xFF;
	}

	if (InterfaceConfig & TAU32_cas_io)
		pDallas->imr2 |= SR2_TMF | SR2_RMF;

	if (InterfaceConfig & TAU32_monitor)
		pDallas->test3 = 0x70;	/* 0x70 == 30 dB, 0x72 == 12 dB */

	if (InterfaceConfig & TAU32_higain)
		pDallas->licr |= LICR_HIGAIN;

	if ((InterfaceConfig & TAU32_framing_mode_mask) > TAU32_unframed) {
		pDallas->rcr1 |= RCR1_FRC;
		if (InterfaceConfig & TAU32_scrambler) {
			ddk_kd_print ((" (9) "));
			return 0;
		}

		switch (InterfaceConfig & TAU32_framing_mode_mask) {
			case TAU32_framed_no_cas:
				if (InterfaceConfig & (TAU32_cas_all_ones | TAU32_cas_fe	/* |
												 * TAU32_cas_io
												 */
						       | TAU32_not_auto_dmra | TAU32_dmra)) {
					ddk_kd_print ((" (8) "));
					return 0;
				}
				pDallas->ccr1 |= CCR1_CCS;
				pDallas->imr1 |= SR1_RRA;
				break;
			case TAU32_framed_cas_set:
				pDallas->tcr1 |= TCR1_T16S;
				pDallas->imr1 |= SR1_RRA | SR1_RDMA /* | SR1_RSA0 | SR1_RSA1 */ ;
				break;
			case TAU32_framed_cas_pass:
				pDallas->imr1 |= SR1_RRA | SR1_RDMA /* | SR1_RSA0 | SR1_RSA1 */ ;
				break;
			case TAU32_framed_cas_cross:
				pDallas->ccr3 |= CCR3_THSE | CCR3_TCBFS;
				pDallas->imr1 |= SR1_RRA | SR1_RDMA /* | SR1_RSA0 | SR1_RSA1 */ ;
				break;
			default:
				ddk_kd_print ((" (10) "));
				return 0;
		}
		pDallas->ccr2 |= CCR2_LOFA1;
		if ((InterfaceConfig & TAU32_not_auto_ra) == 0)
			pDallas->ccr2 |= CCR2_AUTORA;
		pDallas->tsacr &= ~0x20;

		if (InterfaceConfig & TAU32_crc4_mf_rx) {
			pDallas->ccr1 |= CCR1_RCRC4;
			if (InterfaceConfig & TAU32_crc4_mf_tx)
				pDallas->tcr2 |= TCR2_AEBE;
		}
		if (InterfaceConfig & TAU32_crc4_mf_tx) {
			pDallas->ccr1 |= CCR1_TCRC4;
			pDallas->tsacr &= ~0xC0;	/* don't pass SiAF & SiNAF from data */
		}

		if (InterfaceConfig & TAU32_cas_fe)
			pDallas->ccr2 |= CCR2_RFE;

		if (InterfaceConfig & TAU32_sa_bypass)
			pDallas->tcr1 |= TCR1_TFPT;
		else if ((InterfaceConfig & TAU32_si_bypass) == 0)
			pDallas->tcr1 |= TCR1_TSIS;

		if (InterfaceConfig & TAU32_cas_all_ones)
			pDallas->tcr1 |= TCR1_TSA1;
	} else {
		unsigned Bandwidth;

		if (InterfaceConfig &
		    (TAU32_crc4_mf | TAU32_cas_all_ones | TAU32_cas_fe |
		     TAU32_cas_io | TAU32_fas_io | TAU32_fas8_io |
		     TAU32_auto_ais | TAU32_not_auto_ra | TAU32_not_auto_dmra | TAU32_ra | TAU32_dmra)) {
			ddk_kd_print ((" (7) "));
			return 0;
		}
		pDallas->rcr1 |= RCR1_SYNCD;
		pDallas->tcr1 |= TCR1_TFPT;
		pDallas->ccr1 |= CCR1_CCS;

		if ((InterfaceConfig & TAU32_framing_mode_mask) == TAU32_unframed)
			UnframedMask = 0xFFFFFFFFul;

		ddk_kd_print (("\nUnframedMask = %08X\n", UnframedMask));
		pDallas->rcbr = UnframedMask;

		for (Bandwidth = 0; UnframedMask; UnframedMask >>= 1)
			Bandwidth += UnframedMask & 1;

		switch (InterfaceConfig & TAU32_framing_mode_mask) {
			case TAU32_unframed_64:
				if (Bandwidth != 1) {
					ddk_kd_print ((" (11/1) "));
					return 0;
				}
				pDallas->umr = UMR_64;
				break;
			case TAU32_unframed_128:
				if (Bandwidth != 2) {
					ddk_kd_print ((" (11/2) "));
					return 0;
				}
				pDallas->umr = UMR_128;
				break;
			case TAU32_unframed_256:
				if (Bandwidth != 4) {
					ddk_kd_print ((" (11/4) "));
					return 0;
				}
				pDallas->umr = UMR_256;
				break;
			case TAU32_unframed_512:
				if (Bandwidth != 8) {
					ddk_kd_print ((" (11/8) "));
					return 0;
				}
				pDallas->umr = UMR_512;
				break;
			case TAU32_unframed_1024:
				if (Bandwidth != 16) {
					ddk_kd_print ((" (11/16) "));
					return 0;
				}
				pDallas->umr = UMR_1024;
				break;
			case TAU32_unframed_2048:
				if (Bandwidth != 32) {
					ddk_kd_print ((" (11/32) "));
					return 0;
				}
				pDallas->umr = UMR_2048;
				break;
			default:
				ddk_kd_print ((" (12) "));
				return 0;
		}
		if (InterfaceConfig & TAU32_scrambler)
			pDallas->umr |= UMR_SCR;
	}

	ddk_kd_print ((" TAU32_config2dallas==ok "));
	return 1;
}

static __forceinline char TAU32_IsTriggerAlarmed (unsigned FifoId, TAU32_Fifo * pFifo)
{
	unsigned FillLevel = (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE;

	return ((FifoId == TAU32_FifoId_FasTx || FifoId == TAU32_FifoId_CasTx)
		&& FillLevel <= pFifo->TriggerLevel)
		|| ((FifoId == TAU32_FifoId_FasRx || FifoId == TAU32_FifoId_CasRx)
		    && FillLevel >= pFifo->TriggerLevel);
}

may_static void TAU32_NotifyLostIrqE1 (TAU32_Fifo * pFifo, TAU32_Controller * b, int Interface)
{
	pFifo->pSlipCount[0]++;
	b->uc->E1IntLostCount++;
	M32X_NotifyError (b, Interface, TAU32_ERROR_INT_E1LOST);
}

static __forceinline void TAU32_CheckTrigger (unsigned FifoId,
					      unsigned PeriodLimitBits,
					      TAU32_Fifo * pFifo, TAU32_Controller * b, int Interface)
{
	u32 TickCounter;
	volatile u32 *GPP;

	GPP_INIT (GPP);

	TickCounter = TAU32_read_tsclow (GPP);
	if (pFifo->Mark) {
		if (TickCounter - pFifo->Mark > PeriodLimitBits)
			TAU32_NotifyLostIrqE1 (pFifo, b, Interface);
	}
	pFifo->Mark = TickCounter;

	if (pFifo->TriggerActivated && TAU32_IsTriggerAlarmed (FifoId, pFifo)
	    && pFifo->Trigger) {
		unsigned FillLevel = (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE;

		pFifo->TriggerActivated = 0;
		Unlock (&b->Lock);
		pFifo->Trigger (b->uc, Interface, FifoId, (FifoId == TAU32_FifoId_FasTx || FifoId == TAU32_FifoId_CasTx)
				? TAU32_FIFO_SIZE - FillLevel : FillLevel);
		Lock (&b->Lock);
	}
}

static __forceinline void TAU32_UpdateTrigger (unsigned FifoId, TAU32_Fifo * pFifo)
{
	if (pFifo->Trigger && pFifo->TriggerActivated == 0 && !TAU32_IsTriggerAlarmed (FifoId, pFifo))
		pFifo->TriggerActivated = 1;
}

static __forceinline void TAU32_SetTrigger (TAU32_Fifo * pFifo, unsigned Level, TAU32_FifoTrigger Trigger)
{
	pFifo->TriggerActivated = 0;
	pFifo->TriggerLevel = Level;
	if ((pFifo->Trigger = Trigger) != 0)
		pFifo->TriggerActivated = 1;
}

#define __TAU32_ProcessDallasStatus(chip, b) { \
	Status##chip = TAU32_E1OFF; \
	if (likely (b->e1_config[chip])) { \
		int i; \
		volatile u32 *GPP; \
		unsigned rir, ssr, sr1, sr2; \
		GPP_INIT (GPP); \
		rir = TAU32_read_status_cs##chip (GPP, DS_RIR); \
		Status##chip = 0; \
		if (rir & RIR_JALT) \
			Status##chip |= TAU32_JITTER; \
 		ssr = TAU32_read_register_cs##chip (GPP, DS_SSR); \
 		sr1 = TAU32_read_status_cs##chip (GPP, DS_SR1); \
		TAU32_write_register_cs##chip (GPP, DS_IMR1, b->ds_saved[chip].imr1 & ~(ENABLE_E1_RES ? sr1 & ~SR1_RSLIP : sr1)); \
		if (unlikely (sr1 & SR1_RUA1)) \
			/* AIS is inhibit many other conditions */ \
			Status##chip |= TAU32_RUA1; \
		else { \
			if (likely ((b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_unframed)) { \
				if ((ssr & SSR_SYNC_FAS) /*|| (rir & RIR_RESYNC_FAS)*/) \
					Status##chip |= TAU32_RFAS; \
				if (sr1 & SR1_RRA) \
					Status##chip |= TAU32_RRA; \
				if (sr1 & SR1_RLOS) \
					Status##chip |= TAU32_RLOS; \
				if ((b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_framed_no_cas) { \
					if ((ssr & SSR_SYNC_CAS) /*|| (rir & RIR_RESYNC_CAS)*/) \
						Status##chip |= TAU32_RCAS; \
					if (sr1 & SR1_RDMA) \
						Status##chip |= TAU32_RDMA; \
					switch (sr1 & (SR1_RSA1 | SR1_RSA0)) { \
						case SR1_RSA1: \
							Status##chip |= TAU32_RSA1; \
							break; \
						case SR1_RSA0: \
							Status##chip |= TAU32_RSA0; \
							break; \
						case SR1_RSA0 | SR1_RSA1: \
							/* LY: just signaling change */ \
							if (b->e1_config[chip] & TAU32_strict_cas) { \
								u8 byte = TAU32_read_register_inl (chip, GPP, DS_RS); \
								if (byte > 15 || byte == 0) \
									goto bad_cas##chip; \
								i = 15; do { \
									byte = TAU32_read_register_inl (chip, GPP, DS_RS + i); \
									if (byte < 16 || (byte & 0x0F) == 0) { \
								bad_cas##chip: \
										Status##chip |= TAU32_BCAS; \
										break; \
									} \
								} while (--i); \
							} \
							break; \
						case 0: \
							break; \
					} \
				} \
				if (b->e1_config[chip] & TAU32_crc4_mf_rx) { \
					if (ssr >> 4 >= 13) \
						Status##chip |= TAU32_RCRC4LONG; \
					if ((ssr & SSR_SYNC_CRC4) /*|| (rir & RIR_RESYNC_CRC)*/) \
						Status##chip |= TAU32_RCRC4; \
				} \
			} \
			if (sr1 & SR1_RCL) \
				Status##chip |= TAU32_RCL; \
		} \
		if (ENABLE_E1_RES && (sr1 & SR1_RSLIP) /*|| (rir & (RIR_RES_FULL | RIR_RES_EMPTY))*/) { \
			Status##chip |= TAU32_RSLIP; \
			b->uc->InterfacesInfo[chip].ReceiveSlips++; \
			/* TAU32_write_register_cs##chip (GPP, DS_CCR5, CCR5_RESA); */ \
			/* TAU32_write_register_cs##chip (GPP, DS_CCR5, 0); */ \
		} \
		sr2 = TAU32_read_status_cs##chip (GPP, DS_SR2); \
		TAU32_write_register_cs##chip (GPP, DS_IMR2, b->ds_saved[chip].imr2 & ~(sr2 & SR2_LOTC)); \
		if (sr2 & SR2_LOTC) \
			Status##chip |= TAU32_LOTC; \
		if (b->e1_config[chip] & TAU32_fas_io) { \
			if (sr2 & SR2_RAF) { \
				/* read fas signaling info fifo */ \
				TAU32_Fifo *pFifo = b->E1Fifos[chip][TAU32_FifoId_FasRx]; \
				pFifo->Buffer[pFifo->Head % TAU32_FIFO_SIZE] = (u8) TAU32_read_register_cs##chip (GPP, DS_RNAF); \
				if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE >= TAU32_FIFO_SIZE - 1) { \
					pFifo->pSlipCount[0]++; \
					pFifo->Tail++; \
				} \
				pFifo->Head++; \
				TAU32_CheckTrigger (TAU32_FifoId_FasRx, 512, pFifo, b, chip); \
			} \
			if (sr2 & SR2_TAF) { \
				/* write fas signaling from fifo */ \
				TAU32_Fifo *pFifo = b->E1Fifos[chip][TAU32_FifoId_FasTx]; \
				if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE >= 1) { \
					unsigned byte = pFifo->Buffer[pFifo->Tail % TAU32_FIFO_SIZE]; \
					TAU32_write_register_cs##chip (GPP, DS_TNAF, b->ds_saved[chip].tnaf = (u8) (byte | 0x40)); \
					pFifo->Tail++; \
				} else \
					pFifo->pSlipCount[0]++; \
				TAU32_CheckTrigger (TAU32_FifoId_FasTx, 512, pFifo, b, chip); \
			} \
		} else if (b->e1_config[chip] & TAU32_fas8_io) { \
			if (sr2 & SR2_RCMF) { \
				/* read fas signaling of entire crc4_mf info fifo */ \
				TAU32_Fifo *pFifo = b->E1Fifos[chip][TAU32_FifoId_FasRx]; \
				i = 7; do \
					pFifo->Buffer[(pFifo->Head + i) % TAU32_FIFO_SIZE] = (u8) TAU32_read_register_cs##chip (GPP, DS_RSIAF + i); \
				while (--i >= 0); \
				if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE >= TAU32_FIFO_SIZE - 8) { \
					pFifo->pSlipCount[0]++; \
					pFifo->Tail += 8; \
				} \
				pFifo->Head += 8; \
				TAU32_CheckTrigger (TAU32_FifoId_FasRx, 4096, pFifo, b, chip); \
			} \
			if (sr2 & SR2_TMF) { \
				/* write fas signaling of entire crc4_mf from fifo */ \
				TAU32_Fifo *pFifo = b->E1Fifos[chip][TAU32_FifoId_FasTx]; \
				if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE >= 8) { \
 					i = 7; do \
						TAU32_write_register_cs##chip (GPP, DS_TSIAF + i, pFifo->Buffer[(pFifo->Tail + i) % TAU32_FIFO_SIZE]); \
					while (--i >= 0); \
					pFifo->Tail += 8; \
				} else \
					pFifo->pSlipCount[0]++; \
				TAU32_CheckTrigger (TAU32_FifoId_FasTx, 4096, pFifo, b, chip); \
			} \
		} \
		if (b->e1_config[chip] & TAU32_cas_io) { \
			if (sr2 & SR2_RMF) { \
				/* read cas signaling into fifo */ \
				TAU32_Fifo *pFifo = b->E1Fifos[chip][TAU32_FifoId_CasRx]; \
				i = 15; do \
					pFifo->Buffer[(pFifo->Head + i) % TAU32_FIFO_SIZE] = (u8) TAU32_read_register_cs##chip (GPP, DS_RS + i); \
				while (--i >= 0); \
				if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE >= TAU32_FIFO_SIZE - 16) { \
					pFifo->pSlipCount[0]++; \
					pFifo->Tail += 16; \
				} \
				pFifo->Head += 16; \
				TAU32_CheckTrigger (TAU32_FifoId_CasRx, 4096, pFifo, b, chip); \
			} \
			if (sr2 & SR2_TMF) { \
				/* write cas signaling from fifo */ \
				TAU32_Fifo *pFifo = b->E1Fifos[chip][TAU32_FifoId_CasTx]; \
				if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE >= 16) { \
					i = 15; do \
						TAU32_write_register_cs##chip (GPP, DS_TS + i, \
							b->ds_saved[chip].ts[i] = pFifo->Buffer[(pFifo->Tail + i) % TAU32_FIFO_SIZE]); \
					while (--i >= 0); \
					pFifo->Tail += 16; \
				} else \
					pFifo->pSlipCount[0]++; \
				TAU32_CheckTrigger (TAU32_FifoId_CasTx, 4096, pFifo, b, chip); \
			} \
		} \
		if (ENABLE_E1_TES && (sr2 & SR2_TSLIP) /*|| (rir & (RIR_TES_FULL | RIR_TES_EMPTY))*/) { \
			Status##chip |= TAU32_TSLIP; \
			b->uc->InterfacesInfo[chip].TransmitSlips++; \
			/* TAU32_write_register_cs##chip (GPP, DS_CCR5, CCR5_TESA); */ \
			/* TAU32_write_register_cs##chip (GPP, DS_CCR5, 0); */ \
		} \
		if (sr2 & SR2_SEC) { \
			b->uc->InterfacesInfo[chip].RxViolations += \
				(TAU32_read_register_cs##chip (GPP, DS_VCR1) << 8) + TAU32_read_register_cs##chip (GPP, DS_VCR2); \
			if ((b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_unframed) { \
				unsigned crcr1, ebcr1; \
				crcr1 = TAU32_read_register_cs##chip (GPP, DS_CRCCR1); \
				if (b->e1_config[chip] & TAU32_crc4_mf_rx) \
					b->uc->InterfacesInfo[chip].Crc4Errors += \
					((crcr1 & 3) << 8) + TAU32_read_register_cs##chip (GPP, DS_CRCCR2); \
				ebcr1 = TAU32_read_register_cs##chip (GPP, DS_EBCR1); \
				if (b->e1_config[chip] & (TAU32_crc4_mf_rx | TAU32_crc4_mf_tx)) \
					b->uc->InterfacesInfo[chip].FarEndBlockErrors += \
					((ebcr1 & 3) << 8) + TAU32_read_register_cs##chip (GPP, DS_EBCR2); \
				b->uc->InterfacesInfo[chip].FasErrors += \
					(ebcr1 >> 2) + ((crcr1 & ~3) << 6); \
			} \
			b->uc->InterfacesInfo[chip].TickCounter++; \
		} \
	} \
	Status_Delta##chip = Status##chip ^ b->E1[chip].Status; \
}

may_static void TAU32_ProcessDallasesStatus (TAU32_Controller * b)
{
	unsigned Status_Delta0, Status0, Status_Delta1, Status1;

	__TAU32_ProcessDallasStatus (0, b);
	__TAU32_ProcessDallasStatus (1, b);

	if (Status_Delta0 | Status_Delta1) {
		b->uc->InterfacesInfo[0].Status = b->E1[0].Status = Status0;
		b->uc->InterfacesInfo[1].Status = b->E1[1].Status = Status1;
		TAU32_decalogue (b, TAU32_E1_ALL);
		if (b->uc->pStatusNotifyCallback) {
			Unlock (&b->Lock);
			if (Status_Delta0)
				b->uc->pStatusNotifyCallback (b->uc, 0, Status_Delta0);
			if (Status_Delta1)
				b->uc->pStatusNotifyCallback (b->uc, 1, Status_Delta1);
			Lock (&b->Lock);
		}
	}
}

/*----------------------------------------------------------------------------- */

may_static void TAU32_InitCross (TAU32_Controller * b, volatile u32 * GPP)
{
	int i;

	/*
	 * build & update DXC-CM
	 */
	TAU32_out_byte (GPP, T32_CMAR, 0);
	i = 0;
	do {
		b->CrossMatrix_FlipBuffers[0][i] = (u8) i;
		b->CrossMatrix_FlipBuffers[1][i] = (u8) i;
		TAU32_out_byte (GPP, T32_CM, i);
	} while (++i < TAU32_CROSS_WIDTH);

	if (b->ModelType != TAU32_LITE) {
		TAU32_out_byte (GPP, T32_CMAR, 128);
		i = 0;
		do {
			b->CrossMatrix_FlipBuffers[0][i + 128] = (u8) i;
			b->CrossMatrix_FlipBuffers[1][i + 128] = (u8) i;
			TAU32_out_byte (GPP, T32_CM, i);
		} while (++i < TAU32_CROSS_WIDTH);
	}

	/*
	 * DXC-CM swap
	 */
	TAU32_write_cr (b, GPP, b->cr_saved | CR_CMSWAP);
}

static __forceinline void TAU32_KickSlaveInterrupt (TAU32_Controller * b)
{
	/*
	 * b->RegistersBar1_Hardware->LCONF_rw_40 = 0; // disable LBI IRQ
	 * set HE1 = 1, /IRQ from Dallases
	 * 1100 0000 0rr0 0000 000i 0000 0001 1111
	 */

	/* new config & reset */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC000101Ful;
	/* commit LBI-interrupt */
	b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_LBII;
	/* -reset, +enable */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC060101Ful;
}

void TAU32_CALL_TYPE TAU32_EnableInterrupts (TAU32_Controller * b)
{
	u32 disable_mask;

	LockTest (&b->Lock);

	disable_mask =
		~(M32X_STAT_PCMA | M32X_STAT_PCMF | M32X_STAT_RSPA |
		  M32X_STAT_TSPA | M32X_STAT_TI | M32X_STAT_LBII | M32X_STAT_PRI | M32X_STAT_PTI);
	if (M32X_IsRxOnlyTMA (b))
		disable_mask |= M32X_STAT_PRI | M32X_STAT_LBII;	/* LY: disable RX
								 * and E1
								 * interrupt in
								 * TMA (aka phony)
								 * * mode */
	b->RegistersBar1_Hardware->IMASK_rw_0C = disable_mask;
	b->RegistersBar1_Hardware->LREG6_rw_78 = 0u;
	TAU32_KickSlaveInterrupt (b);
}

void TAU32_CALL_TYPE TAU32_DisableInterrupts (TAU32_Controller * b)
{
	/*
	 * b->RegistersBar1_Hardware->LCONF_rw_40 = 0;
	 */
	b->RegistersBar1_Hardware->IMASK_rw_0C = ~(u32)0;
	ddk_interrupt_sync (b->uc->InterruptObject);
	LockTest (&b->Lock);
}

may_static void TAU32_ForceProcessDallasesStatus (TAU32_Controller * b)
{
	TAU32_KickSlaveInterrupt (b);
	TAU32_ProcessReadStatus (b);
	TAU32_ProcessDallasesStatus (b);
	TAU32_HandleSlaveInterrupt (b);
}

void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_BeforeReset (TAU32_UserContext * uc)
{
	if (uc) {
		volatile M32X_RegistersBar1 *RegistersBar1_Hardware =
			(volatile M32X_RegistersBar1 *) uc->PciBar1VirtualAddress;
		if (RegistersBar1_Hardware) {
			RegistersBar1_Hardware->IMASK_rw_0C = ~(u32)0;
			RegistersBar1_Hardware->LCONF_rw_40 = 0u;
			RegistersBar1_Hardware->CONF_rw_00.all = 0u;
			RegistersBar1_Hardware->LREG6_rw_78 = ~(u32)0;
			RegistersBar1_Hardware->CMD_w_04 = 0u;
		}
	}
}

may_static int TAU32_HandleSlaveInterrupt (TAU32_Controller * b)
{
#if 1
	/* disable IRQ: new config & reset */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC000101Ful;
	/* commit LBI-interrupt */
	b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_LBII;
	TAU32_ProcessReadStatus (b);
	TAU32_ProcessDallasesStatus (b);
	/* enable IRQ: -reset, +enable */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC060101Ful;
	return 1;
#else
	int Count = M32X_MaxInterruptsLoop;

	do {
		/* disable IRQ: new config & reset */
		b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC000101Ful;
		/* commit LBI-interrupt */
		b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_LBII;

		TAU32_ProcessReadStatus (b);
		TAU32_ProcessDallasesStatus (b);
		if (--Count == 0)
			goto ballout;

		/* enable IRQ: -reset, +enable */
		b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC060101Ful;
	} while (b->RegistersBar1_Hardware->LSTAT_r_7C & 2);
	return 1;

ballout:
	/* disable LBI INT */
	b->RegistersBar1_Hardware->IMASK_rw_0C |= M32X_STAT_LBII;
	/* new config & reset */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0;
	M32X_NotifyError (b, -2, M32X_ERROR_INT_STORM);
	return -1;
#endif
}

char TAU32_CALL_TYPE TAU32_IsInterruptPending (TAU32_Controller * b)
{
	return M32X_IsInterruptPending (b);
}

may_static unsigned TAU32_ProcessReadStatus (TAU32_Controller * b)
{
	volatile u32 *GPP;
	unsigned AdapterStatus_Delta, AdapterStatus, Status;

	GPP_INIT (GPP);
	Status = TAU32_in_byte (GPP, T32_SR);

	AdapterStatus = b->AdapterStatus & ~TAU32_FRLOMF;
	if ((Status & SR_LOF) && (b->cr_saved & CR_CASEN)) {
		AdapterStatus |= TAU32_FRLOMF;
		if (!(b->AdapterStatus & TAU32_FRLOMF))
			b->uc->CasIoLofCount++;
	}
	//if (Status & SR_UMFERR0) {
	//      ddk_dbg_print ("SR.UMFERR0\n");
	//}
	//if (Status & SR_UMFERR1) {
	//      ddk_dbg_print ("SR.UMFERR1\n");
	//}
	//if (Status & SR_BLKERR) {
	//      ddk_dbg_print ("SR.BLKERR\n");
	//}

	if (Status & SR_CMDONE) {
		AdapterStatus &= ~TAU32_CROSS_PENDING;
		if (AdapterStatus & TAU32_CROSS_WAITING) {
			TAU32_UpdateCross (b, GPP);
			TAU32_decalogue (b, TAU32_E1_ALL);
			AdapterStatus = (AdapterStatus | TAU32_CROSS_PENDING) & ~TAU32_CROSS_WAITING;
		}
	}

	AdapterStatus_Delta = b->AdapterStatus ^ AdapterStatus;
	if (AdapterStatus_Delta) {
		b->uc->AdapterStatus = b->AdapterStatus = AdapterStatus;
		if (b->uc->pStatusNotifyCallback) {
			Unlock (&b->Lock);
			b->uc->pStatusNotifyCallback (b->uc, -1, AdapterStatus_Delta);
			Lock (&b->Lock);
		}
	}

	return Status;
}

char TAU32_CALL_TYPE TAU32_HandleInterrupt (TAU32_Controller * b)
{
	char result;

	LockOrTrap (&b->Lock);
	result = M32X_HandleInterrupt (b);
	Unlock (&b->Lock);
	return result;
}

char TAU32_CALL_TYPE TAU32_SubmitRequest (TAU32_Controller * b, TAU32_UserRequest * ur)
{
	char result;

	Lock (&b->Lock);
	result = M32X_SubmitRequest (b, ur);
	Unlock (&b->Lock);
	return result;
}

char TAU32_CALL_TYPE TAU32_CancelRequest (TAU32_Controller * b, TAU32_UserRequest * ur, char BreakIfRunning)
{
	char result;

	Lock (&b->Lock);
	result = M32X_CancelRequest (b, ur, BreakIfRunning);
	if (result) {
		ddk_kd_print (("TAU32_CancelRequest (%X)\n", ur));
	}
	Unlock (&b->Lock);
	return result;
}

#if 0
char TAU32_CALL_TYPE TAU32_SetVoiceCompression (TAU32_Controller * b, TAU32_VoiceCompression VoiceCompression)
{
	if (VoiceCompression != TAU32_NoCompression)
		return 0;
	Lock (&b->Lock);
	/*
	 * TODO
	 */
	Unlock (&b->Lock);
	return 1;
}

char TAU32_CALL_TYPE TAU32_SetTimestotsFilling (TAU32_Controller * b, TAU32_TimeslotFill * pFilling)
{
	int i = TAU32_TIMESLOTS - 1;

	do {
		if (pFilling[i] != TAU32_DataOnly && pFilling[i] != TAU32_VoiceOnly)
			return 0;
	}
	while (--i >= 0);
	Lock (&b->Lock);
	/*
	 * TODO
	 */
	Unlock (&b->Lock);
	return 1;
}

char TAU32_CALL_TYPE TAU32_SetTimestotsFillingEx (TAU32_Controller * b, TAU32_TimeslotFill Fill, u32 AffectedMask)
{
	if (Fill != TAU32_DataOnly && Fill != TAU32_VoiceOnly)
		return 0;
	Lock (&b->Lock);
	/*
	 * TODO
	 */
	Unlock (&b->Lock);
	return 1;
}

#endif

#define CrossIdleAddress	0x7Fu
#define CrossReverseFlag	0x80u

static __forceinline char TAU32_BuildCrossCompare (int CrossBase, int CrossWidth, u8 * pNewCrossMatrix,
						   u32 ReverseMask, u8 * pActiveCrossMatrix, u8 * pPendingCrossMatrix)
{
	char Result = 0;
	int Route;
	int i = CrossBase;
	int CrossEnd = CrossBase + CrossWidth;

	do {
		if (i == 96)
			i = 128;

		Route = pNewCrossMatrix ? pNewCrossMatrix[i]
			: (pActiveCrossMatrix[i] & ~CrossReverseFlag);
		if (Route >= CrossWidth)
			Route = CrossIdleAddress;

		if (ReverseMask) {
			if (ReverseMask & 1)
				Route |= CrossReverseFlag;
			ReverseMask >>= 1;
		}

		if (Result || Route != pActiveCrossMatrix[i]) {
			pPendingCrossMatrix[i] = (u8) Route;
			if (unlikely (!Result)) {
				int j;

				for (j = 0; j < i; j++)
					pPendingCrossMatrix[j] = pActiveCrossMatrix[j];
				Result = 1;
			}
		}
	} while (++i < CrossEnd);

	return Result;
}

may_static char TAU32_CrossFullCompare (TAU32_Controller * b)
{
	int i = 0;

	do
		if (((unsigned *) b->CrossMatrix_Pending)[i] != ((unsigned *) b->pCrossMatrix_Active)[i])
			return 1;
	while (++i < sizeof (TAU32_CrossMatrixImp) / sizeof (unsigned)) ;
	return 0;
}

may_static void TAU32_UpdateCross (TAU32_Controller * b, volatile u32 * GPP)
{
	u8 *pSwap;
	int CrossEnd;
	int NextAddress = -1;
	int i = 0;

	CrossEnd = 128 + 96;
	if (b->ModelType == TAU32_LITE)
		CrossEnd = 64;

	do {
		if (i == 96)
			i = 128;
		if (b->CrossMatrix_Pending[i] != b->pCrossMatrix_Shadow[i]) {
			if (NextAddress != i)
				TAU32_out_byte (GPP, T32_CMAR, i);
			TAU32_out_byte (GPP, T32_CM, b->pCrossMatrix_Shadow[i] = b->CrossMatrix_Pending[i]);
			NextAddress = i + 1;
		}
	}
	while (++i < CrossEnd);
	TAU32_write_cr (b, GPP, b->cr_saved | CR_CMSWAP);

	pSwap = b->pCrossMatrix_Shadow;
	b->pCrossMatrix_Shadow = b->pCrossMatrix_Active;
	b->pCrossMatrix_Active = pSwap;
}

char TAU32_CALL_TYPE TAU32_SetCrossMatrix (TAU32_Controller * b, u8 * pCrossMatrix, u32 ReverseMask)
{
	Lock (&b->Lock);

	if (TAU32_BuildCrossCompare
	    (0, (b->ModelType == TAU32_LITE) ? 64 : 96, pCrossMatrix,
	     ReverseMask, b->pCrossMatrix_Active, b->CrossMatrix_Pending)) {
		volatile u32 *GPP;

		GPP_INIT (GPP);
		if (b->AdapterStatus & TAU32_CROSS_PENDING) {
			/*
			 * LY: some updates are loaded into and now pending on the chip,
			 * we must wait for done-pending for safe update cross-matrix.
			 */
			if (!(b->AdapterStatus & TAU32_CROSS_WAITING)) {
				b->uc->AdapterStatus = b->AdapterStatus |= TAU32_CROSS_WAITING;
				if (b->uc->pStatusNotifyCallback) {
					Unlock (&b->Lock);
					b->uc->pStatusNotifyCallback (b->uc, -1, TAU32_CROSS_WAITING);
					Lock (&b->Lock);
				}
			}
		} else {
			/*
			 * LY: no any updates are pending on chip,
			 * we can safe upload it now.
			 */
			b->uc->AdapterStatus = b->AdapterStatus |= TAU32_CROSS_PENDING;
			TAU32_UpdateCross (b, GPP);
			TAU32_decalogue (b, TAU32_E1_ALL);
		}
	} else {
		if ((b->AdapterStatus & TAU32_CROSS_WAITING)
		    && !TAU32_CrossFullCompare (b)) {
			/*
			 * LY: no any differences from dmaly active cross-matrix,
			 * we need cancel any waiting updates.
			 */
			b->uc->AdapterStatus = b->AdapterStatus &= ~TAU32_CROSS_WAITING;
			if (b->uc->pStatusNotifyCallback) {
				Unlock (&b->Lock);
				b->uc->pStatusNotifyCallback (b->uc, -1, TAU32_CROSS_WAITING);
				Lock (&b->Lock);
			}
		}
	}

	Unlock (&b->Lock);
	return 1;
}

char TAU32_CALL_TYPE TAU32_SetCrossMatrixCas (TAU32_Controller * b, u8 * pCrossMatrix)
{
	unsigned NewCr;
	int NeedDecalogue = 0;
	volatile u32 *GPP;

	if (b->ModelType == TAU32_LITE)
		return !pCrossMatrix;

	Lock (&b->Lock);

	NewCr = b->cr_saved & ~CR_CASEN;
	if (pCrossMatrix)
		NewCr |= CR_CASEN;
	if (b->cr_saved != NewCr) {
		GPP_INIT (GPP);
		TAU32_write_cr (b, GPP, b->cr_saved = NewCr);
		NeedDecalogue = 1;
	}

	if (pCrossMatrix) {
		if (TAU32_BuildCrossCompare
		    (128, 96, pCrossMatrix - 128, 0, b->pCrossMatrix_Active, b->CrossMatrix_Pending)) {
			GPP_INIT (GPP);
			if (b->AdapterStatus & TAU32_CROSS_PENDING) {
				/*
				 * LY: some updates are loaded into and now pending on the chip,
				 * we must wait for done-pending for safe update cross-matrix.
				 */
				if (!(b->AdapterStatus & TAU32_CROSS_WAITING)) {
					b->uc->AdapterStatus = b->AdapterStatus |= TAU32_CROSS_WAITING;
					if (b->uc->pStatusNotifyCallback) {
						Unlock (&b->Lock);
						b->uc->pStatusNotifyCallback (b->uc, -1, TAU32_CROSS_WAITING);
						Lock (&b->Lock);
					}
				}
			} else {
				/*
				 * LY: no any updates are pending on chip,
				 * we can safe upload it now.
				 */
				b->uc->AdapterStatus = b->AdapterStatus |= TAU32_CROSS_PENDING;
				TAU32_UpdateCross (b, GPP);
				NeedDecalogue = 1;
			}
		} else {
			if ((b->AdapterStatus & TAU32_CROSS_WAITING)
			    && !TAU32_CrossFullCompare (b)) {
				/*
				 * LY: no any differences from dmaly active cross-matrix,
				 * we need cancel any waiting updates.
				 */
				b->uc->AdapterStatus = b->AdapterStatus &= ~TAU32_CROSS_WAITING;
				if (b->uc->pStatusNotifyCallback) {
					Unlock (&b->Lock);
					b->uc->pStatusNotifyCallback (b->uc, -1, TAU32_CROSS_WAITING);
					Lock (&b->Lock);
				}
			}
		}
	}

	if (NeedDecalogue)
		TAU32_decalogue (b, TAU32_E1_ALL);
	Unlock (&b->Lock);
	return 1;
}

void TAU32_CALL_TYPE TAU32_LedBlink (TAU32_Controller * b)
{
	volatile u32 *GPP;

	Lock (&b->Lock);
	b->uc->AdapterStatus = b->AdapterStatus ^= TAU32_LED;
	b->cr_saved ^= CR_LED;
	ddk_kd_print (("TAU32_Controller:: Led = %s\n", (b->cr_saved & CR_LED) ? "ON" : "OFF"));
	GPP_INIT (GPP);
	TAU32_write_cr (b, GPP, b->cr_saved);
	Unlock (&b->Lock);
}

void TAU32_CALL_TYPE TAU32_LedSet (TAU32_Controller * b, char On)
{
	unsigned NewCr;

	Lock (&b->Lock);
	NewCr = b->cr_saved & ~CR_LED;
	if (On)
		NewCr |= CR_LED;
	if (b->cr_saved != NewCr) {
		volatile u32 *GPP;
		unsigned AdapterStatus = b->AdapterStatus & ~TAU32_LED;

		if (NewCr & CR_LED)
			AdapterStatus |= TAU32_LED;
		b->uc->AdapterStatus = b->AdapterStatus = AdapterStatus;
		GPP_INIT (GPP);
		TAU32_write_cr (b, GPP, b->cr_saved = NewCr);
	}
	Unlock (&b->Lock);
}

u64 TAU32_CALL_TYPE TAU32_ProbeGeneratorFrequency (u64 frequency)
{
	u32 factor, l = 0, h = (u32) (frequency >> 32);

	if (((int) h) <= 0)
		h = 2048000;
	else if (h < 2048000 - 5000)
		h = 2048000 - 5000;
	else if (h >= 2048000 + 5000)
		h = 2048000 + 5000;
	else
		l = (u32) frequency;
	frequency = (((u64) h) << 32) + l;

	/*
	 * factor = frequency{32.32} / 2^24 * 2.048 = frequency / 65536 / 125
	 */
	frequency += 65536 * 125 / 2;
	factor = ddk_edivu (frequency >> 16, 125);

	/*
	 * frequency = factor / 2.048 * 2^24 = factor * 65536 * 125
	 */
	return ddk_emulu (factor, 125 * 65536);
}

u64 TAU32_CALL_TYPE TAU32_SetGeneratorFrequency (TAU32_Controller * b, u64 frequency)
{
	volatile u32 *GPP;
	u32 factor, h = (u32) (frequency >> 32);

	if (((int) h) <= 0)
		goto reset;
	else if (h < 2048000 - 5000)
		goto lo_limit;
	else if (h >= 2048000 + 5000)
		goto hi_limit;

process:
	/*
	 * factor = frequency / 2^24 * 2.048 = frequency / 65536 / 125
	 */
	frequency += 65536 * 125 / 2;
	factor = ddk_edivu (frequency >> 16, 125);
	ddk_assert (factor >= 1 && factor <= 0x80000000ul);

	Lock (&b->Lock);
	if (b->LyGenFactor != factor) {
		b->LyGenFactor = factor;
		GPP_INIT (GPP);
		TAU32_setup_lygen (GPP, factor);
	}
	Unlock (&b->Lock);

	/*
	 * frequency = factor / 2.048 * 2^24 = factor * 65536 * 125
	 */
	return ddk_emulu (factor, 125 * 65536);

reset:
	frequency = ((u64) 2048000ul) << 32;
	goto process;
lo_limit:
	frequency = ((u64) 2048000ul - 5000) << 32;
	goto process;
hi_limit:
	frequency = ((u64) 2048000ul + 5000) << 32;
	goto process;
}

may_static void TAU32_Sync2Cr (TAU32_Controller * b)
{
	unsigned NewCr = b->cr_saved & ~CR_CLK_MASK;

	switch (b->sync) {
		case TAU32_SYNC_LYGEN:
			NewCr |= CR_CLK_LYGEN;
			break;

		case TAU32_SYNC_RCV_A:
			if (b->ds_saved[0].ccr4 & CCR4_LLB /* == TAU32_LineLoopInt */ )
				goto internal_sync;
			NewCr |= CR_CLK_RCV0;
			break;

		case TAU32_SYNC_RCV_B:
			if (b->ds_saved[1].ccr4 & CCR4_LLB /* == TAU32_LineLoopInt */ )
				goto internal_sync;
			NewCr |= CR_CLK_RCV1;
			break;

		default:
		case TAU32_SYNC_INTERNAL:
		      internal_sync:
			NewCr |= CR_CLK_INT;
			break;
	}

	if (b->cr_saved != NewCr) {
		volatile u32 *GPP;

		GPP_INIT (GPP);
		TAU32_write_cr (b, GPP, b->cr_saved = NewCr);
	}
}

char TAU32_CALL_TYPE TAU32_SetSyncMode (TAU32_Controller * b, unsigned Mode)
{
	if (b->ModelType == TAU32_LITE && Mode == TAU32_SYNC_RCV_B)
		return 0;

	if (Mode > TAU32_SYNC_LYGEN)
		return 0;

	Lock (&b->Lock);
	b->sync = Mode;
	TAU32_Sync2Cr (b);
	Unlock (&b->Lock);
	return 1;
}

char TAU32_CALL_TYPE TAU32_UpdateIdleCodes (TAU32_Controller * b, int Interface, u32 TimeslotMask, u8 Code)
{
	char HasChanges = 0;
	int i;

	if ((Interface != TAU32_E1_ALL && Interface > TAU32_E1_B)
	    || TimeslotMask == 0)
		return 0;

	if (b->ModelType == TAU32_LITE && Interface > TAU32_E1_A)
		return 0;

	Lock (&b->Lock);
	i = 0;
	do {
		if (TimeslotMask & 1) {
			if (Interface == TAU32_E1_ALL || Interface == TAU32_E1_A)
				if (b->idle_codes[0][i] != Code) {
					b->idle_codes[0][i] = Code;
					HasChanges = 1;
				}

			if (b->ModelType != TAU32_LITE)
				if (Interface == TAU32_E1_ALL || Interface == TAU32_E1_B)
					if (b->idle_codes[1][i] != Code) {
						b->idle_codes[1][i] = Code;
						HasChanges = 1;
					}
		}
		++i;
	}
	while ((TAU32_TIMESLOTS == 32 || i < TAU32_TIMESLOTS)
	       && (TimeslotMask >>= 1) != 0);

	if (HasChanges)
		TAU32_decalogue (b, Interface);
	Unlock (&b->Lock);
	return 1;
}

char TAU32_CALL_TYPE TAU32_SetIdleCodes (TAU32_Controller * b, u8 * pIdleCodes)
{
	char HasChanges = 0;
	int i = (b->ModelType == TAU32_LITE) ? TAU32_TIMESLOTS - 1 : TAU32_TIMESLOTS * 2 - 1;

	do
		if (pIdleCodes[i] > 0xF && pIdleCodes[i] != 0xFF)
			return 0;
	while (--i >= 0) ;

	Lock (&b->Lock);
	i = (b->ModelType == TAU32_LITE) ? TAU32_TIMESLOTS - 1 : TAU32_TIMESLOTS * 2 - 1;
	do
		if (pIdleCodes[i] <= 0xF && b->idle_codes[i / TAU32_TIMESLOTS][i % TAU32_TIMESLOTS] != pIdleCodes[i]) {
			b->idle_codes[i / TAU32_TIMESLOTS][i % TAU32_TIMESLOTS]
				= pIdleCodes[i];
			HasChanges = 1;
		}
	while (--i >= 0) ;

	if (HasChanges)
		TAU32_decalogue (b, TAU32_E1_ALL);

	Unlock (&b->Lock);
	return 1;
}

static __forceinline int TAU32_FifoPutAppend (unsigned FifoId, TAU32_Fifo * pFifo, u8 * pBuffer, unsigned Length)
{
	unsigned i;

	/*
	 * ddk_kd_print (">> fifo_put_append: h = %d, t = %d, l = %d\n", pFifo->Head, pFifo->Tail, pFifo->Head - pFifo->Tail);
	 */
	if (TAU32_FIFO_SIZE - (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE < Length) {
		/*
		 * ddk_kd_print ("<< fifo_put_append: -1\n");
		 */
		return -1;
	}

	for (i = 0; i < Length; i++)
		pFifo->Buffer[(pFifo->Head + i) % TAU32_FIFO_SIZE] = pBuffer[i];

	pFifo->Head += Length;
	TAU32_UpdateTrigger (FifoId, pFifo);

	/*
	 * ddk_kd_print ("<< fifo_put_append: %d\n", (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE);
	 */
	return TAU32_FIFO_SIZE - (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE;
}

static __forceinline int TAU32_FifoPutAhead (unsigned FifoId, TAU32_Fifo * pFifo, u8 * pBuffer, unsigned Length)
{
	unsigned i;

	/*
	 * ddk_kd_print (">> fifo_put_ahead: h = %d, t = %d, l = %d\n", pFifo->Head, pFifo->Tail, pFifo->Head - pFifo->Tail);
	 */
	if (TAU32_FIFO_SIZE - (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE < Length) {
		/*
		 * ddk_kd_print ("<< fifo_put_ahead: -1\n");
		 */
		return -1;
	}

	pFifo->Tail -= Length;
	for (i = 0; i < Length; i++)
		pFifo->Buffer[(pFifo->Tail + i) % TAU32_FIFO_SIZE] = pBuffer[i];

	TAU32_UpdateTrigger (FifoId, pFifo);
	/*
	 * ddk_kd_print ("<< fifo_put_ahead: %d\n", (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE);
	 */
	return TAU32_FIFO_SIZE - (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE;
}

static __forceinline int TAU32_FifoGet (unsigned FifoId, TAU32_Fifo * pFifo, u8 * pBuffer, unsigned Length)
{
	unsigned i;

	/*
	 * ddk_kd_print (">> fifo_get: h = %d, t = %d, l = %d\n", pFifo->Head, pFifo->Tail, pFifo->Head - pFifo->Tail);
	 */
	if ((pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE < Length) {
		/*
		 * ddk_kd_print ("<< fifo_get: -1\n");
		 */
		return -1;
	}

	for (i = 0; i < Length; i++)
		pBuffer[i] = pFifo->Buffer[(pFifo->Tail + i) % TAU32_FIFO_SIZE];

	pFifo->Tail += Length;
	TAU32_UpdateTrigger (FifoId, pFifo);
	/*
	 * ddk_kd_print ("<< fifo_get: %d\n", (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE);
	 */
	return (pFifo->Head - pFifo->Tail) % TAU32_FIFO_SIZE;
}

static __forceinline void TAU32_FifoClear (unsigned FifoId, TAU32_Fifo * pFifo)
{
	pFifo->Head = 0;
	pFifo->Tail = 0;
	pFifo->Mark = 0;
	if (pFifo->pSlipCount)
		pFifo->pSlipCount[0] = 0;
	TAU32_UpdateTrigger (FifoId, pFifo);
}

int TAU32_CALL_TYPE TAU32_FifoPutCasAppend (TAU32_Controller * b, int Interface, u8 * pBuffer, unsigned Length)
{
	int result = -2;

	Lock (&b->Lock);
	switch (Interface) {
		case TAU32_E1_A:
			result = TAU32_FifoPutAppend (TAU32_FifoId_CasTx,
						      &b->E1[0].Fifos[TAU32_FifoId_CasTx], pBuffer, Length);
			break;
		case TAU32_E1_B:
			if (b->ModelType != TAU32_LITE)
				result = TAU32_FifoPutAppend (TAU32_FifoId_CasTx,
							      &b->E1[1].Fifos[TAU32_FifoId_CasTx], pBuffer, Length);
			break;
		case TAU32_E1_ALL:
			result = TAU32_FifoPutAppend (TAU32_FifoId_CasTx,
						      &b->E1[0].Fifos[TAU32_FifoId_CasTx], pBuffer, Length);
			if (b->ModelType != TAU32_LITE) {
				int result2 = TAU32_FifoPutAppend (TAU32_FifoId_CasTx,
								   &b->E1[1].Fifos[TAU32_FifoId_CasTx],
								   pBuffer,
								   Length);

				if (result2 < result)
					result = result2;
			}
	}
	Unlock (&b->Lock);
	return result;
}

int TAU32_CALL_TYPE TAU32_FifoPutCasAhead (TAU32_Controller * b, int Interface, u8 * pBuffer, unsigned Length)
{
	int result = -2;

	Lock (&b->Lock);
	switch (Interface) {
		case TAU32_E1_A:
			result = TAU32_FifoPutAhead (TAU32_FifoId_CasTx,
						     &b->E1[0].Fifos[TAU32_FifoId_CasTx], pBuffer, Length);
			break;
		case TAU32_E1_B:
			if (b->ModelType != TAU32_LITE)
				result = TAU32_FifoPutAhead (TAU32_FifoId_CasTx,
							     &b->E1[1].Fifos[TAU32_FifoId_CasTx], pBuffer, Length);
			break;
		case TAU32_E1_ALL:
			result = TAU32_FifoPutAhead (TAU32_FifoId_CasTx,
						     &b->E1[0].Fifos[TAU32_FifoId_CasTx], pBuffer, Length);
			if (b->ModelType != TAU32_LITE) {
				int result2 = TAU32_FifoPutAhead (TAU32_FifoId_CasTx,
								  &b->E1[1].Fifos[TAU32_FifoId_CasTx],
								  pBuffer,
								  Length);

				if (result2 < result)
					result = result2;
			}
	}
	Unlock (&b->Lock);
	return result;
}

int TAU32_CALL_TYPE TAU32_FifoGetCas (TAU32_Controller * b, int Interface, u8 * pBuffer, unsigned Length)
{
	int result = -2;

	Lock (&b->Lock);
	switch (Interface) {
		case TAU32_E1_A:
			result = TAU32_FifoGet (TAU32_FifoId_CasRx,
						&b->E1[0].Fifos[TAU32_FifoId_CasRx], pBuffer, Length);
			break;
		case TAU32_E1_B:
			if (b->ModelType != TAU32_LITE)
				result = TAU32_FifoGet (TAU32_FifoId_CasRx,
							&b->E1[1].Fifos[TAU32_FifoId_CasRx], pBuffer, Length);
			break;
	}
	Unlock (&b->Lock);
	return result;
}

int TAU32_CALL_TYPE TAU32_FifoPutFasAppend (TAU32_Controller * b, int Interface, u8 * pBuffer, unsigned Length)
{
	int result = -2;

	Lock (&b->Lock);
	switch (Interface) {
		case TAU32_E1_A:
			result = TAU32_FifoPutAppend (TAU32_FifoId_FasTx,
						      &b->E1[0].Fifos[TAU32_FifoId_FasTx], pBuffer, Length);
			break;
		case TAU32_E1_B:
			if (b->ModelType != TAU32_LITE)
				result = TAU32_FifoPutAppend (TAU32_FifoId_FasTx,
							      &b->E1[1].Fifos[TAU32_FifoId_FasTx], pBuffer, Length);
			break;
		case TAU32_E1_ALL:
			result = TAU32_FifoPutAppend (TAU32_FifoId_FasTx,
						      &b->E1[0].Fifos[TAU32_FifoId_FasTx], pBuffer, Length);
			if (b->ModelType != TAU32_LITE) {
				int result2 = TAU32_FifoPutAppend (TAU32_FifoId_FasTx,
								   &b->E1[1].Fifos[TAU32_FifoId_FasTx],
								   pBuffer,
								   Length);

				if (result2 < result)
					result = result2;
			}
	}
	Unlock (&b->Lock);
	return result;
}

int TAU32_CALL_TYPE TAU32_FifoPutFasAhead (TAU32_Controller * b, int Interface, u8 * pBuffer, unsigned Length)
{
	int result = -2;

	Lock (&b->Lock);
	switch (Interface) {
		case TAU32_E1_A:
			result = TAU32_FifoPutAhead (TAU32_FifoId_FasTx,
						     &b->E1[0].Fifos[TAU32_FifoId_FasTx], pBuffer, Length);
			break;
		case TAU32_E1_B:
			if (b->ModelType != TAU32_LITE)
				result = TAU32_FifoPutAhead (TAU32_FifoId_FasTx,
							     &b->E1[1].Fifos[TAU32_FifoId_FasTx], pBuffer, Length);
			break;
		case TAU32_E1_ALL:
			result = TAU32_FifoPutAhead (TAU32_FifoId_FasTx,
						     &b->E1[0].Fifos[TAU32_FifoId_FasTx], pBuffer, Length);
			if (b->ModelType != TAU32_LITE) {
				int result2 = TAU32_FifoPutAhead (TAU32_FifoId_FasTx,
								  &b->E1[1].Fifos[TAU32_FifoId_FasTx],
								  pBuffer,
								  Length);

				if (result2 < result)
					result = result2;
			}
	}
	Unlock (&b->Lock);
	return result;
}

int TAU32_CALL_TYPE TAU32_FifoGetFas (TAU32_Controller * b, int Interface, u8 * pBuffer, unsigned Length)
{
	int result = -2;

	Lock (&b->Lock);
	switch (Interface) {
		case TAU32_E1_A:
			result = TAU32_FifoGet (TAU32_FifoId_FasRx,
						&b->E1[0].Fifos[TAU32_FifoId_FasRx], pBuffer, Length);
			break;
		case TAU32_E1_B:
			if (b->ModelType != TAU32_LITE)
				result = TAU32_FifoGet (TAU32_FifoId_FasRx,
							&b->E1[1].Fifos[TAU32_FifoId_FasRx], pBuffer, Length);
			break;
	}
	Unlock (&b->Lock);
	return result;
}

#define __TAU32_ds_defaults(chip, b) { \
	unsigned i; \
	volatile u32 *GPP; \
	b->ds_saved[chip].imr2 = 0; \
	TAU32_FifoClear (TAU32_FifoId_CasTx, b->E1Fifos[chip][TAU32_FifoId_CasTx]); \
	TAU32_FifoClear (TAU32_FifoId_FasTx, b->E1Fifos[chip][TAU32_FifoId_FasTx]); \
	GPP_INIT (GPP); \
	/* fas */ \
	TAU32_write_register_cs##chip (GPP, DS_TAF, 0x9B); \
	TAU32_write_register_cs##chip (GPP, DS_TNAF, b->ds_saved[chip].tnaf = 0xDF); \
	/* cas */ \
	TAU32_write_register_cs##chip (GPP, DS_TS, b->ds_saved[chip].ts[0] = 0x0B); \
	i = 15; do \
		TAU32_write_register_cs##chip (GPP, DS_TS + i, b->ds_saved[chip].ts[i] = 0xFF); \
	while (--i); \
	/* si+ra+sa by crc4_fm */ \
	TAU32_write_register_cs##chip (GPP, DS_TSIAF, 0xFF); \
	TAU32_write_register_cs##chip (GPP, DS_TSINAF, 0xFF); \
	TAU32_write_register_cs##chip (GPP, DS_TRA, 0); \
	TAU32_write_register_cs##chip (GPP, DS_TSA4, 0xFF); \
	TAU32_write_register_cs##chip (GPP, DS_TSA5, 0xFF); \
	TAU32_write_register_cs##chip (GPP, DS_TSA6, 0xFF); \
	TAU32_write_register_cs##chip (GPP, DS_TSA7, 0xFF); \
	TAU32_write_register_cs##chip (GPP, DS_TSA8, 0xFF); \
	i = 32; do { \
		/* default idle code */ \
		TAU32_write_register_cs##chip (GPP, DS_TC + i - 1, \
		b->idle_codes[chip][i - 1] = b->ds_saved[chip].tc[i - 1] = 0xFF /*0xD5*/); \
		/* default fail code */ \
		TAU32_write_register_cs##chip (GPP, DS_RC + i - 1, 0xFF); \
	} while (--i); \
	/* signaling for timeslots 0 & 16 from TS1 in hardware signaling mode */ \
	TAU32_write_register32_cs##chip (GPP, DS_TCBR, b->ds_saved[chip].tcbr = 0x03); \
	TAU32_write_register32_cs##chip (GPP, DS_TCC, b->ds_saved[chip].tcc = ~(u32)0); \
	TAU32_write_register32_cs##chip (GPP, DS_RCC, b->ds_saved[chip].rcc = ~(u32)0); \
}

may_static void TAU32_ds_defaults_cs0 (TAU32_Controller * b)
{
	__TAU32_ds_defaults (0, b);
}

may_static void TAU32_ds_defaults_cs1 (TAU32_Controller * b)
{
	__TAU32_ds_defaults (1, b);
}

#define __TAU32_ds_prepare(chip, b, ur, ir) { \
	ir->Tau32.Tasks |= DoUpdate << chip; \
	if ((ur->Io.InterfaceConfig.Config & TAU32_line_mode_mask) != TAU32_LineOff) { \
		if ((b->e1_config[chip] & TAU32_line_mode_mask) == TAU32_LineOff) \
			ir->Tau32.Tasks |= (DoStart | DoReset | DoResync | DoClear) << chip; \
		\
		if (((ur->Io.InterfaceConfig.Config & TAU32_line_mode_mask) == TAU32_LineLoopInt) \
		!= ((b->e1_config[chip] & TAU32_line_mode_mask) == TAU32_LineLoopInt)) \
			ir->Tau32.Tasks |= (DoReset | DoResync | DoClear) << chip; \
		\
		if (((ur->Io.InterfaceConfig.Config & TAU32_framing_mode_mask) <= TAU32_unframed) \
		!= ((b->e1_config[chip] & TAU32_framing_mode_mask) <= TAU32_unframed)) \
			ir->Tau32.Tasks |= (DoResync | DoClear) << chip; \
		\
		if ((b->e1_config[chip] ^ ur->Io.InterfaceConfig.Config) & (TAU32_framing_mode_mask | TAU32_crc4_mf_rx)) \
			ir->Tau32.Tasks |= DoClear << chip; \
	} else { \
		ir->Tau32.Tasks |= DoStop << chip; \
		if ((b->e1_config[chip] & TAU32_line_mode_mask) != TAU32_LineOff) \
		ir->Tau32.Tasks |= DoClear << chip; \
	} \
}

may_static char TAU32_VerifyUserRequest (TAU32_Controller * b, M32X_ur_t * ur, M32X_ir_t * ir)
{
	if (ur->Command & TAU32_Configure_E1) {
		ddk_kd_print (("TAU32_ConfigureE1\n" "  E1 = %d (%s)\n"
			"  %8s %d (%s)\n" "  %8s %d (%s)\n" "  %8s %d\n"
			"  %8s %d\n" "  %8s %d\n" "  %8s %d\n"
			"  %8s %d\n" "  %8s %d\n" "  %8s %d\n"
			"  %8s %d\n" "  %8s %d\n" "  %8s %d\n"
			"  %8s %d\n", ur->Io.InterfaceConfig.Interface,
			ur->Io.InterfaceConfig.Interface ==
			TAU32_E1_A ? "A" : ur->Io.InterfaceConfig.
			Interface ==
			TAU32_E1_B ? "B" : ur->Io.InterfaceConfig.
			Interface == TAU32_E1_ALL ? "ALL" : "unknown",
			"line_mode",
			ur->Io.InterfaceConfig.
			Config & TAU32_line_mode_mask,
			((ur->Io.InterfaceConfig.
			Config & TAU32_line_mode_mask) <=
			TAU32_LineOff) ? "power-off" : ((ur->Io.InterfaceConfig.Config & TAU32_line_mode_mask)
			== TAU32_LineNormal)
			? "Normal"
			: ((ur->Io.InterfaceConfig.
			Config & TAU32_line_mode_mask) ==
			TAU32_LineAIS) ? "AIS" : ((ur->Io.InterfaceConfig.Config & TAU32_line_mode_mask)
			== TAU32_LineLoopInt)
			? "Loopback Int"
			: ((ur->Io.InterfaceConfig.
			Config & TAU32_line_mode_mask) ==
			TAU32_LineLoopExt) ? "Loopback Ext" :
		"unknown", "framing_mode",
			ur->Io.InterfaceConfig.
			Config & TAU32_framing_mode_mask,
			((ur->Io.InterfaceConfig.
			Config & TAU32_framing_mode_mask) <=
			TAU32_unframed) ? "unframed" : ((ur->Io.InterfaceConfig.
			Config & TAU32_framing_mode_mask)
			== TAU32_framed_no_cas)
			? "G.703, no CAS"
			: ((ur->Io.InterfaceConfig.
			Config & TAU32_framing_mode_mask) ==
			TAU32_framed_cas_set) ? "G.703, set CAS"
			: ((ur->Io.InterfaceConfig.Config & TAU32_framing_mode_mask)
			== TAU32_framed_cas_pass)
			? "G.703, pass CAS"
			: ((ur->Io.InterfaceConfig.
			Config & TAU32_framing_mode_mask) ==
			TAU32_framed_cas_cross) ? "G.703, cross CAS" :
		"unknown", "monitor",
			(ur->Io.InterfaceConfig.Config & TAU32_monitor) !=
			0, "higain",
			(ur->Io.InterfaceConfig.Config & TAU32_higain) !=
			0, "crc4_mf",
			(ur->Io.InterfaceConfig.Config & TAU32_crc4_mf) !=
			0, "sa_bypass",
			(ur->Io.InterfaceConfig.
			Config & TAU32_sa_bypass) != 0, "si_bypass",
			(ur->Io.InterfaceConfig.
			Config & TAU32_si_bypass) != 0, "cas_fe",
			(ur->Io.InterfaceConfig.Config & TAU32_cas_fe) !=
			0, "ais_on_los",
			(ur->Io.InterfaceConfig.
			Config & TAU32_ais_on_los) != 0, "cas_all_ones",
			(ur->Io.InterfaceConfig.
			Config & TAU32_cas_all_ones) != 0, "cas_io",
			(ur->Io.InterfaceConfig.Config & TAU32_cas_io) !=
			0, "fas_io",
			(ur->Io.InterfaceConfig.Config & TAU32_fas_io) !=
			0, "fas8_io",
			(ur->Io.InterfaceConfig.Config & TAU32_fas8_io) !=
			0, "auto_ais", (ur->Io.InterfaceConfig.Config & TAU32_auto_ais) != 0));

		if (ur->Io.InterfaceConfig.Interface != TAU32_E1_ALL && ur->Io.InterfaceConfig.Interface > TAU32_E1_B) {
			ddk_kd_print ((" (1) "));
			return 0;
		}

		if (b->ModelType == TAU32_LITE && ur->Io.InterfaceConfig.Interface > TAU32_E1_A) {
			ddk_kd_print ((" (2) "));
			return 0;
		}

		if (!TAU32_config2dallas
			(ur->Io.InterfaceConfig.Config, ur->Io.InterfaceConfig.UnframedTsMask, &ir->Tau32.NewRegisters)) {
				ddk_kd_print ((" (3) "));
				return 0;
			}

			ir->Tau32.Tasks = 0;
			if (ur->Io.InterfaceConfig.Interface == TAU32_E1_ALL || ur->Io.InterfaceConfig.Interface == TAU32_E1_A)
				__TAU32_ds_prepare (0, b, ur, ir);
			if ((ur->Io.InterfaceConfig.Interface == TAU32_E1_ALL || ur->Io.InterfaceConfig.Interface == TAU32_E1_B)
				&& b->ModelType != TAU32_LITE)
				__TAU32_ds_prepare (1, b, ur, ir);
	}

	return 1;
}

#define __TAU32_ds_update(chip, b, ur, ir, GPP, tau32_status) { \
	u8 tcr2; \
	ddk_kd_print (("--update cs%d\n", chip)); \
	if (b->ds_saved[chip].ccr1 != ir->Tau32.NewRegisters.ccr1) \
		TAU32_write_register_cs##chip (GPP, DS_CCR1, b->ds_saved[chip].ccr1 = ir->Tau32.NewRegisters.ccr1); \
	if (b->ds_saved[chip].ccr2 != ir->Tau32.NewRegisters.ccr2) \
		TAU32_write_register_cs##chip (GPP, DS_CCR2, b->ds_saved[chip].ccr2 = ir->Tau32.NewRegisters.ccr2); \
	if (b->ds_saved[chip].ccr3 != ir->Tau32.NewRegisters.ccr3) \
		TAU32_write_register_cs##chip (GPP, DS_CCR3, b->ds_saved[chip].ccr3 = ir->Tau32.NewRegisters.ccr3); \
	if (b->ds_saved[chip].ccr4 != ir->Tau32.NewRegisters.ccr4) { \
		TAU32_write_register_cs##chip (GPP, DS_CCR4, b->ds_saved[chip].ccr4 = ir->Tau32.NewRegisters.ccr4); \
		TAU32_Sync2Cr (b); \
	} \
	if (b->ds_saved[chip].licr != ir->Tau32.NewRegisters.licr) { \
		b->ds_saved[chip].licr = ir->Tau32.NewRegisters.licr; \
		if (tau32_status & ((chip == 0) ? SR_TP0 : SR_TP1)) \
			TAU32_write_register_cs##chip (GPP, DS_LICR, ir->Tau32.NewRegisters.licr | LICR_LB120P); \
		else \
			TAU32_write_register_cs##chip (GPP, DS_LICR, ir->Tau32.NewRegisters.licr | LICR_LB75P); \
	} \
	if (b->ds_saved[chip].tcr1 != ir->Tau32.NewRegisters.tcr1) \
		TAU32_write_register_cs##chip (GPP, DS_TCR1, b->ds_saved[chip].tcr1 = ir->Tau32.NewRegisters.tcr1); \
	tcr2 = ir->Tau32.NewRegisters.tcr2; \
	/* if (b->SaCross_Saved & ((chip == 0) ? SA_EN_0 : SA_EN_1)) \
		tcr2 |= 0xF8; */ \
	if (b->ds_saved[chip].tcr2 != tcr2) \
		TAU32_write_register_cs##chip (GPP, DS_TCR2, b->ds_saved[chip].tcr2 = tcr2); \
	if (b->ds_saved[chip].rcr1 != ir->Tau32.NewRegisters.rcr1) \
		TAU32_write_register_cs##chip (GPP, DS_RCR1, b->ds_saved[chip].rcr1 = ir->Tau32.NewRegisters.rcr1); \
	if (b->ds_saved[chip].test3 != ir->Tau32.NewRegisters.test3) \
		TAU32_write_register_cs##chip (GPP, DS_TEST3, b->ds_saved[chip].test3 = ir->Tau32.NewRegisters.test3); \
	if (b->ds_saved[chip].tsacr != ir->Tau32.NewRegisters.tsacr) \
		TAU32_write_register_cs##chip (GPP, DS_TSACR, b->ds_saved[chip].tsacr = ir->Tau32.NewRegisters.tsacr); \
	if (b->ds_saved[chip].umr != ir->Tau32.NewRegisters.umr \
	|| b->ds_saved[chip].rcbr != ir->Tau32.NewRegisters.rcbr) { \
		u32 other_rcbr = b->ds_saved[chip ^ 1].rcbr & ~ir->Tau32.NewRegisters.rcbr; \
		if (b->ds_saved[chip ^ 1].rcbr != other_rcbr) { \
			if (chip) \
				TAU32_write_register32_cs0 (GPP, DS_RCBR, b->ds_saved[0].rcbr = other_rcbr); \
			else \
				TAU32_write_register32_cs1 (GPP, DS_RCBR, b->ds_saved[1].rcbr = other_rcbr); \
		} \
		TAU32_write_register32_cs##chip (GPP, DS_RCBR, b->ds_saved[chip].rcbr = ir->Tau32.NewRegisters.rcbr); \
		b->ds_saved[chip].umr = ir->Tau32.NewRegisters.umr; \
		if (chip == 0) \
			TAU32_write_umr0 (b, GPP, b->ds_saved[0].umr | UMR_FIFORST); \
		else \
			TAU32_out_byte (GPP, T32_UMR1, b->ds_saved[chip].umr | UMR_FIFORST); \
	} \
	if ((ur->Io.InterfaceConfig.Config & TAU32_line_mode_mask) != TAU32_LineOff) { \
		b->e1_config[chip] = ur->Io.InterfaceConfig.Config; \
		if (b->ds_saved[chip].imr1 != ir->Tau32.NewRegisters.imr1 \
			|| b->ds_saved[chip].imr2 != ir->Tau32.NewRegisters.imr2) \
			ir->Tau32.Tasks |= DoStart << chip; \
	} else \
		/* e1_config is also used as if-disable flag */ \
		b->e1_config[chip] = 0; \
}

may_static char TAU32_ProcessUserRequest (TAU32_Controller * b, M32X_ur_t * ur, M32X_ir_t * ir)
{
	if (ur->Command & TAU32_Configure_E1) {
		volatile u32 *GPP;

#if defined (SR_TP0) && defined (SR_TP1)
		unsigned tau32_status = TAU32_ProcessReadStatus (b);

		b->uc->CableTypeJumpers = TAU32_COAX_A + TAU32_COAX_B;
		if (tau32_status & SR_TP0)
			b->uc->CableTypeJumpers += TAU32_TP_A - TAU32_COAX_A;
		if (tau32_status & SR_TP1)
			b->uc->CableTypeJumpers += TAU32_TP_B - TAU32_COAX_B;
#endif

		GPP_INIT (GPP);
		if (ir->Tau32.Tasks & (DoUpdate << 0))
			__TAU32_ds_update (0, b, ur, ir, GPP, tau32_status);

		if (ir->Tau32.Tasks & (DoUpdate << 1))
			__TAU32_ds_update (1, b, ur, ir, GPP, tau32_status);

		if (ir->Tau32.Tasks & ((DoUpdate << 0) | (DoUpdate << 1))) {
			ir->Tau32.Tasks &= ~((DoUpdate << 0) | (DoUpdate << 1));
			TAU32_decalogue (b, TAU32_E1_ALL);
		}

		/*
		 * disable interrupts
		 */
		if (ir->Tau32.Tasks & (DoStop << 0)) {
			ddk_kd_print (("--di cs0\n"));
			/*
			 * disable interrupt
			 */
			TAU32_write_register_cs0 (GPP, DS_IMR1, 0);
			TAU32_write_register_cs0 (GPP, DS_IMR2, 0);
			/*
			 * clear signalling fifos and setup defaults
			 */
			TAU32_ds_defaults_cs0 (b);
		}
		if (ir->Tau32.Tasks & (DoStop << 1)) {
			ddk_kd_print (("--di cs1\n"));
			/*
			 * disable interrupt
			 */
			TAU32_write_register_cs1 (GPP, DS_IMR1, 0);
			TAU32_write_register_cs1 (GPP, DS_IMR2, 0);
			/*
			 * clear signalling fifos and setup defaults
			 */
			TAU32_ds_defaults_cs1 (b);
		}
		ir->Tau32.Tasks &= ~((DoStop << 0) | (DoStop << 1));

#if 0
		if (ir->Tau32.Tasks & (DoReset0 << 0)) {
			ddk_kd_print (("--reset0 cs0\n"));
			/*
			 * TODO ?
			 */
		}
		if (ir->Tau32.Tasks & (DoReset0 << 1)) {
			ddk_kd_print (("--reset0 cs1\n"));
			/*
			 * TODO ?
			 */
		}
#endif
		if (ir->Tau32.Tasks & ((DoReset0 << 0) | (DoReset0 << 1))) {
			ir->Tau32.Tasks &= ~((DoReset0 << 0) | (DoReset0 << 1));
			/*
			 * pause for power-up/new-config
			 */
			ddk_kd_print (("--pause for reset0\n"));
			M32X_request_pause (b, ir, 4);
			return 1;
		}

		/*
		 * begin reset line interface & elastic stores
		 */
		if (ir->Tau32.Tasks & (DoReset1 << 0)) {
			ddk_kd_print (("--reset1 cs0\n"));
			TAU32_write_register_cs0 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs0 (GPP, DS_CCR6, 0);
			TAU32_write_register_cs0 (GPP, DS_CCR5, CCR5_LIRST);
			TAU32_write_register_cs0 (GPP, DS_CCR6, CCR6_RESR | CCR6_TESR);
		}
		if (ir->Tau32.Tasks & (DoReset1 << 1)) {
			ddk_kd_print (("--reset1 cs1\n"));
			TAU32_write_register_cs1 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs1 (GPP, DS_CCR6, 0);
			TAU32_write_register_cs1 (GPP, DS_CCR5, CCR5_LIRST);
			TAU32_write_register_cs1 (GPP, DS_CCR6, CCR6_RESR | CCR6_TESR);
		}
		if (ir->Tau32.Tasks & ((DoReset1 << 0) | (DoReset1 << 1))) {
			ir->Tau32.Tasks &= ~((DoReset1 << 0) | (DoReset1 << 1));
			ddk_kd_print (("--pause for reset1\n"));
			M32X_request_pause (b, ir, 16 * 4);
			return 1;
		}

		/*
		 * done reset line interface & elastic stores
		 */
		if (ir->Tau32.Tasks & (DoReset2 << 0)) {
			ddk_kd_print (("--reset2 cs0\n"));
			TAU32_write_register_cs0 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs0 (GPP, DS_CCR6, 0);
		}
		if (ir->Tau32.Tasks & (DoReset2 << 1)) {
			ddk_kd_print (("--reset2 cs1\n"));
			TAU32_write_register_cs1 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs1 (GPP, DS_CCR6, 0);
		}
		if (ir->Tau32.Tasks & ((DoReset2 << 0) | (DoReset2 << 1))) {
			ir->Tau32.Tasks &= ~((DoReset2 << 0) | (DoReset2 << 1));
			/*
			 * pause
			 */
			ddk_kd_print (("--pause for reset2\n"));
			M32X_request_pause (b, ir, 16 * 4);
			return 1;
		}

		/*
		 * resync framer
		 */
		if (ir->Tau32.Tasks & (DoResync << 0)) {
			/*
			 * force resync
			 */
			ddk_kd_print (("--resync cs0\n"));
			TAU32_write_register_cs0 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs0 (GPP, DS_CCR5, CCR5_RESA | CCR5_TESA);
			TAU32_write_register_cs0 (GPP, DS_RCR1, b->ds_saved[0].rcr1 | RCR1_RESYNC);
			TAU32_write_register_cs0 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs0 (GPP, DS_RCR1, b->ds_saved[0].rcr1);
		}
		if (ir->Tau32.Tasks & (DoResync << 1)) {
			/*
			 * force resync
			 */
			ddk_kd_print (("--resync cs1\n"));
			TAU32_write_register_cs1 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs1 (GPP, DS_CCR5, CCR5_RESA | CCR5_TESA);
			TAU32_write_register_cs1 (GPP, DS_RCR1, b->ds_saved[1].rcr1 | RCR1_RESYNC);
			TAU32_write_register_cs1 (GPP, DS_CCR5, 0);
			TAU32_write_register_cs1 (GPP, DS_RCR1, b->ds_saved[1].rcr1);
		}
		if (ir->Tau32.Tasks & ((DoResync << 0) | (DoResync << 1))) {
			ir->Tau32.Tasks &= ~((DoResync << 0) | (DoResync << 1));
			/*
			 * pause
			 */
			ddk_kd_print (("--pause for resync\n"));
			M32X_request_pause (b, ir, 16 * 4);
			return 1;
		}

		/*
		 * clear status
		 */
		if (ir->Tau32.Tasks & (DoClear << 0)) {
			ddk_kd_print (("--clear cs0\n"));
			TAU32_read_status_cs0 (GPP, DS_SR1);
			TAU32_read_status_cs0 (GPP, DS_SR2);
			TAU32_read_status_cs0 (GPP, DS_RIR);
			b->uc->InterfacesInfo[0].Status = b->E1[0].Status = 0;
			TAU32_FifoClear (TAU32_FifoId_CasRx, b->E1Fifos[0][TAU32_FifoId_CasRx]);
			TAU32_FifoClear (TAU32_FifoId_FasRx, b->E1Fifos[0][TAU32_FifoId_FasRx]);
		}
		if (ir->Tau32.Tasks & (DoClear << 1)) {
			ddk_kd_print (("--clear cs1\n"));
			TAU32_read_status_cs1 (GPP, DS_SR1);
			TAU32_read_status_cs1 (GPP, DS_SR2);
			TAU32_read_status_cs1 (GPP, DS_RIR);
			b->uc->InterfacesInfo[1].Status = b->E1[1].Status = 0;
			TAU32_FifoClear (TAU32_FifoId_CasRx, b->E1Fifos[1][TAU32_FifoId_CasRx]);
			TAU32_FifoClear (TAU32_FifoId_FasRx, b->E1Fifos[1][TAU32_FifoId_FasRx]);
		}

		/*
		 * enable interrupts
		 */
		if (ir->Tau32.Tasks & (DoStart << 0)) {
			ddk_kd_print (("--ei cs0\n"));
			/*
			 * b->cs0_enabled = 1;
			 */
			TAU32_write_register_cs0 (GPP, DS_IMR1, b->ds_saved[0].imr1 = ir->Tau32.NewRegisters.imr1);
			TAU32_write_register_cs0 (GPP, DS_IMR2, b->ds_saved[0].imr2 = ir->Tau32.NewRegisters.imr2);
		}
		if (ir->Tau32.Tasks & (DoStart << 1)) {
			ddk_kd_print (("--ei cs1\n"));
			/*
			 * /b->cs1_enabled = 1;
			 */
			TAU32_write_register_cs1 (GPP, DS_IMR1, b->ds_saved[1].imr1 = ir->Tau32.NewRegisters.imr1);
			TAU32_write_register_cs1 (GPP, DS_IMR2, b->ds_saved[1].imr2 = ir->Tau32.NewRegisters.imr2);
		}
		if (ir->Tau32.Tasks & ((DoClear << 0) | (DoClear << 1) | (DoStart << 0) | (DoStart << 1))) {
			TAU32_decalogue (b, TAU32_E1_ALL);
			TAU32_ForceProcessDallasesStatus (b);
			ir->Tau32.Tasks &= ~((DoClear << 0) | (DoClear << 1) | (DoStart << 0) | (DoStart << 1));
		}

		if (ir->Tau32.Tasks == 0) {
			ur->Command &= ~TAU32_Configure_E1;
#if DDK_DEBUG
			/*
			 * TAU32_DumpDallas (b);
			 */
			/*
			 * TAU32_DumpDallas << 1(b);
			 */
#endif
		}
	}
	return 0;
}

u32 TAU32_CALL_TYPE TAU32_Diag (TAU32_Controller * b, unsigned Operation, u32 Data)
{
	volatile u32 *GPP;
	ddk_bitops_t Result = 0;

	Lock (&b->Lock);
	GPP_INIT (GPP);
	switch (Operation) {
		case TAU32_CRONYX_PS:
			/*
			 * set GPP to state that is safe for i/o
			 */
			GPP_DIR (GPP) = 0x0000;
			GPP_DATA (GPP) = 0xFFFFFFFF;
			GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;
			GPP_DIR (GPP) = 0x0000;
			/*
			 * no break here
			 */
		case TAU32_CRONYX_P:
			/*
			 * just read GPP bits
			 */
			Result = (GPP_DATA (GPP) & 0xFFFF) | (GPP_DIR (GPP) << 16);
			break;
		case TAU32_CRONYX_PA:
			/*
			 * GPP i/o, direction then data
			 */
			GPP_DIR (GPP) = Data >> 16;
			GPP_DATA (GPP) = Data;
			Result = GPP_DATA (GPP);
			break;
		case TAU32_CRONYX_PB:
			/*
			 * GPP i/o, data then direction
			 */
			GPP_DATA (GPP) = Data;
			GPP_DIR (GPP) = Data >> 16;
			Result = GPP_DATA (GPP);
			break;
		case TAU32_CRONYX_O:
			/*
			 * write TAU32 register
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			break;
		case TAU32_CRONYX_U:
			/*
			 * write TAU32 register
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			/*
			 * no break here
			 */
		case TAU32_CRONYX_I:
			/*
			 * read TAU32 register
			 */
			Result = TAU32_in_byte (GPP, (Data >> 16) & 0xFF) << 8;
			break;
		case TAU32_CRONYX_R:
			/*
			 * complete read address
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			Result = TAU32_in_byte (GPP, T32_DAT);
			break;
		case TAU32_CRONYX_W:
			/*
			 * complete write address
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			TAU32_out_byte (GPP, T32_DAT, Data & 0xFF);
			break;
		case TAU32_CRONYX_RW:
			/*
			 * read and then write address
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			Result = TAU32_in_byte (GPP, T32_DAT);
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			TAU32_out_byte (GPP, T32_DAT, Data & 0xFF);
			break;
		case TAU32_CRONYX_WR:
			/*
			 * write and then read address
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			TAU32_out_byte (GPP, T32_DAT, Data & 0xFF);
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			Result = TAU32_in_byte (GPP, T32_DAT);
			break;
		case TAU32_CRONYX_S:
			/*
			 * read address in dallas 'status' protocol
			 */
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			TAU32_out_byte (GPP, T32_DAT, Data & 0xFF);
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			Result = TAU32_in_byte (GPP, T32_DAT);
			TAU32_out_byte (GPP, (Data >> 16) & 0xFF, (Data >> 8) & 0xFF);
			TAU32_out_byte (GPP, T32_DAT, Result & Data & 0xFF);
			break;
		case TAU32_CRONYX_G:
			/*
			 * setup generator
			 */
			if (Data > 2097151999ul)
				Result = ~(u32)0;
			else {
				/*
				 * factor = frequency{24.8} * 2.048 = frequency * 256 / 125
				 */
				Result = (u32) ((ddk_emulu (Data, TAU32_GEN_MAGIC_048) + 0x80000000ul) >> 32);
				Result += Data + Data;
			}
			TAU32_setup_lygen (GPP, Result);
			TAU32_write_cr (b, GPP, b->cr_saved = (b->cr_saved & ~CR_CLK_MASK) | CR_CLK_LYGEN);
			break;

		case TAU32_CRONYX_T:
			Result = 0;
			if (!(b->AdapterStatus & (TAU32_CROSS_WAITING | TAU32_CROSS_PENDING))
			    && b->as == M32X_ACTION_VOID && ddk_queue_isempty (&b->rq_in)
			    && ddk_queue_isempty (&b->rq_a)) {
				int i = 31;

				do
					if (b->PCM_ccb.Timeslots[i].fields.rti == 0 &&
					    b->PCM_ccb.Timeslots[i].fields.rx_chan == (Data & 31))
						ddk_bit_set (Result, i);
				while (--i >= 0);

				b->TsTestMode = Data >> 5;
				// LY: b->TsTestMode = 1
				// LY: первая фаза проверки - контролируем номера таймслотов в данных по приёму.
				// LY: выключаем мультиплексор-фреймер CAS, для сохранения данных внутри таймслотов.
				// LY: включаем "нумерацию" таймслотов для проверки сдвига по-приёму.
				//
				// LY: b->TsTestMode = 2
				// LY: вторая фаза - клиентский код генерирует ключевую payload для передачи и проверяет прием.
				// LY: выключаем мультиплексор-фреймер CAS, для сохранения данных внутри таймслотов.
				// LY: выключаем "нумерацию" таймслотов для проверки сдвига по-приёму.
				//
				// LY: b->TsTestMode = 0
				// LY: переходим в нормальный режим работы.
				TAU32_write_cr (b, GPP, b->cr_saved);
				TAU32_write_umr0 (b, GPP, b->ds_saved[0].umr | UMR_FIFORST);
			}
			break;
	}
	Unlock (&b->Lock);
	return Result;
}

char TAU32_CALL_TYPE TAU32_SetSaCross (TAU32_Controller * b, TAU32_SaCross SaCross)
{
	u8 Value = 0;

	if (SaCross.InterfaceA > TAU32_SaAllZeros || SaCross.InterfaceB > TAU32_SaAllZeros)
		return 0;

	if (!SaCross.SystemEnableTs0 && (SaCross.InterfaceA == TAU32_SaSystem || SaCross.InterfaceB == TAU32_SaSystem))
		return 0;

	if (b->ModelType == TAU32_LITE)
		if (SaCross.InterfaceA == TAU32_SaIntB || SaCross.InterfaceB != TAU32_SaDisable)
			return 0;

	if (SaCross.SystemEnableTs0)
		Value |= SA_ENPEB;
	if (SaCross.InterfaceA)
		Value |= SA_EN_0 | ((SaCross.InterfaceA - 1) << SA_SHIFT_0);
	if (SaCross.InterfaceB)
		Value |= SA_EN_1 | ((SaCross.InterfaceB - 1) << SA_SHIFT_1);

	if (Value != b->SaCross_Saved) {
		volatile u32 *GPP;

		Lock (&b->Lock);
		GPP_INIT (GPP);
		TAU32_out_byte (GPP, T32_SACR, b->SaCross_Saved = Value);
		TAU32_decalogue (b, TAU32_E1_ALL);
		Unlock (&b->Lock);
	}
	return 1;
}

/*----------------------------------------------------------------------------- */

may_static const u32 ts2cas_mask[32] = {
	1u << 0, 1u << 2, 1u << 4, 1u << 6,
	1u << 8, 1u << 10, 1u << 12, 1u << 14,
	1u << 16, 1u << 18, 1u << 20, 1u << 22,
	1u << 24, 1u << 26, 1u << 28, 1u << 30,
	1u << 1, 1u << 3, 1u << 5, 1u << 7,
	1u << 9, 1u << 11, 1u << 13, 1u << 15,
	1u << 17, 1u << 19, 1u << 21, 1u << 23,
	1u << 25, 1u << 27, 1u << 29, 1u << 31
};

#define AlarmCode	0xFFu
#define CasIdleCode	0xDDu
#define CasAlarmCode	0xFFu
#define LinkFailMask	(TAU32_RCL | TAU32_RFAS | TAU32_E1OFF)
#define CasFailMask	(LinkFailMask | TAU32_RSA1 | TAU32_RCAS /* | TAU32_RSA0 */)

#define __TAU32_decalogue_ts(chip, b, ts, ts_mask, peb_tx_mask, unframed_ts_mask, cas_subst_mask, tx_idle_mask, tx_fail_mask) { \
	u8 cas_code, slot_code; \
	unsigned source_ts, source_cas; \
	volatile u32 *GPP; \
	if (ts == 0 && (b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_unframed) \
		/* ts0/fas is always done */ \
		goto done_##chip; \
	if (ts == 16 && (b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_framed_no_cas) \
		/* ts16/cas is always done */ \
		goto done_##chip; \
	cas_code = CasIdleCode; \
 	slot_code = b->idle_codes[chip][ts]; \
	source_ts = b->pCrossMatrix_Active[ts + 32 * (chip + 1)] & 0x7F; \
	if (source_ts == CrossIdleAddress) \
		/* void tx-to E1/0, insert idle code */ \
		tx_idle_mask |= ts_mask; \
	else if (source_ts < 32) { \
		/* source_ts from peb-tx */ \
		if (!ddk_bit_test (peb_tx_mask, source_ts) || (ts_mask & unframed_ts_mask) != 0) \
			/* tx is disabled or acquired by unframed engine, insert idle code */ \
			tx_fail_mask |= ts_mask; \
	} else if (source_ts < 64) { \
		/* source_ts from E1/0 */ \
		if ((b->ds_saved[0].umr & UMR_ENABLE) != 0 \
			|| (ManagedStatusE1[0] & LinkFailMask)) { \
				/* source_ts is disabled or in the fail state, insert alarm code */ \
				slot_code = AlarmCode; \
				tx_fail_mask |= ts_mask; \
			} \
	} else { \
		/* source_ts from E1/1 */ \
		if ((b->ds_saved[1].umr & UMR_ENABLE) != 0 \
			|| (ManagedStatusE1[1] & LinkFailMask)) { \
				/* source_ts is disabled or in the fail state, insert alarm code */ \
				slot_code = AlarmCode; \
				tx_fail_mask |= ts_mask; \
			} \
	} \
	source_cas = source_ts; \
	if (b->cr_saved & CR_CASEN) \
		source_cas = b->pCrossMatrix_Active[128 + ts + 32 * (chip + 1)]; \
	if (source_cas == CrossIdleAddress) \
		/* void tx-to E1/0, insert idle code */ \
		cas_subst_mask |= ts2cas_mask[ts]; \
	else if (source_cas < 32) { \
		/* source_cas from peb-tx */ \
		if (!ddk_bit_test(peb_tx_mask, source_cas) \
			|| (ts_mask & unframed_ts_mask) != 0) \
			/* tx is disabled or acquired by unframed engine, insert idle code */ \
			cas_subst_mask |= ts2cas_mask[ts]; \
		if ((b->e1_config[chip] & TAU32_framing_mode_mask) >= TAU32_framed_cas_pass) { \
			if (((b->e1_config[chip] & TAU32_framing_mode_mask) == TAU32_framed_cas_cross && !(b->cr_saved & CR_CASEN)) \
				|| !(peb_tx_mask & (1u << 16)) || (unframed_ts_mask & (1u << 16)) != 0) { \
					/* cas from peb by framer, but peb-tx is disabled or acquired by unframed engine */ \
					/* insert alarm code for cas */ \
					cas_code = CasAlarmCode; \
					cas_subst_mask |= ts2cas_mask[ts]; \
				} \
		} \
 	} else if (source_cas < 64) { \
		/* source_cas from E1/0 */ \
		if ((b->ds_saved[0].umr & UMR_ENABLE) != 0 \
			|| (ManagedStatusE1[0] & LinkFailMask)) { \
				/* source_cas is disabled or in the fail state, insert alarm code */ \
				cas_code = CasAlarmCode; \
				cas_subst_mask |= ts2cas_mask[ts]; \
			} else if ((ManagedStatusE1[0] & CasFailMask) \
				&& (b->e1_config[0] & TAU32_cas_fe) == 0) { \
					/* source_cas does not have cas and cas-freeze disabled, insert cas alarm code */ \
					cas_code = CasAlarmCode; \
					cas_subst_mask |= ts2cas_mask[ts]; \
				} \
	} else { \
		/* source_cas from E1/1 */ \
		if ((b->ds_saved[1].umr & UMR_ENABLE) != 0 \
			|| (ManagedStatusE1[1] & LinkFailMask)) { \
				/* source_cas is disabled or in the fail state, insert alarm code */ \
				cas_code = CasAlarmCode; \
				cas_subst_mask |= ts2cas_mask[ts]; \
			} else if ((ManagedStatusE1[1] & CasFailMask) \
				&& (b->e1_config[1] & TAU32_cas_fe) == 0) { \
					/* source_cas does not have cas and cas-freeze disabled, insert cas alarm code */ \
					cas_code = CasAlarmCode; \
					cas_subst_mask |= ts2cas_mask[ts]; \
				} \
	} \
	GPP_INIT (GPP); \
	if (ts & 0x0Fu) /* skip ts0 & ts16 */ { \
		/* update signalling for E1 */ \
		unsigned n = ts; \
		if (n < 16) { \
			cas_code = (b->ds_saved[chip].ts[n] & 0xF0u) | (cas_code & 0x0Fu); \
		} else { \
			n -= 16; \
			cas_code = (b->ds_saved[chip].ts[n] & 0x0Fu) | (cas_code & 0xF0u); \
		} \
		if (b->ds_saved[chip].ts[n] != cas_code) \
			TAU32_write_register_cs##chip (GPP, DS_TS + n, b->ds_saved[chip].ts[n] = cas_code); \
	} \
	/* update tx-subst code for E1 */ \
	if (slot_code != b->ds_saved[chip].tc[ts]) \
		TAU32_write_register_cs##chip (GPP, DS_TC + ts, b->ds_saved[chip].tc[ts] = slot_code); \
	done_##chip:; \
}

#define __TAU32_decalogue_if(chip, b, peb_tx_mask, unframed_ts_mask, cas_subst_mask, tx_idle_mask, tx_fail_mask) { \
	volatile u32 *GPP; \
	u8 tcr1, tcr2, ts0; \
 	ddk_bitops_t tx_subst_mask, rx_subst_mask; \
	GPP_INIT (GPP); \
 	/* update transmit side signaling re-insertion mask (has effect only in cas-cross mode) */ \
	if (cas_subst_mask != b->ds_saved[chip].tcbr) \
		TAU32_write_register32_cs##chip (GPP, DS_TCBR, b->ds_saved[chip].tcbr = cas_subst_mask); \
 	/* update Sa control */ \
	tcr2 = b->ds_saved[chip].tcr2 & 0x07u; \
	if (b->SaCross_Saved & ((chip == 0) ? SA_EN_0 : SA_EN_1)) { \
		tcr2 |= 0xF8u; \
		switch ((b->SaCross_Saved >> ((chip == 0) ? SA_SHIFT_0 : SA_SHIFT_1)) & 3) { \
			case SA_SRC_PEB: \
				/* Sa from peb */ \
				if (!(b->SaCross_Saved & SA_ENPEB) \
					|| (unframed_ts_mask & (1u << 0)) != 0 \
					|| !(peb_tx_mask & (1u << 0))) \
					tcr2 &= 0x07u; \
				break; \
			case SA_SRC_0: \
				/* Sa from E1/0 */ \
				if ((b->ds_saved[0].umr & UMR_ENABLE) != 0 \
					|| (ManagedStatusE1[0] & LinkFailMask)) \
					tcr2 &= 0x07u; \
				break; \
			case SA_SRC_1: \
				/* Sa from E1/1 */ \
				if ((b->ds_saved[1].umr & UMR_ENABLE) != 0 \
					|| (ManagedStatusE1[1] & LinkFailMask)) \
					tcr2 &= 0x07u; \
				break; \
			case SA_SRC_ZERO: \
				/* Sa all zeros */ \
				break; \
			default: \
				ddk_assert (0); \
				ddk_assume (0); \
		} \
	} \
	if (b->ds_saved[chip].tcr2 != tcr2) \
		TAU32_write_register_cs##chip (GPP, DS_TCR2, b->ds_saved[chip].tcr2 = tcr2); \
	tcr1 = b->ds_saved[chip].tcr1 & ~TCR1_TUA1; \
	/* update transmit side re-insertion mask */ \
	tx_subst_mask = tx_fail_mask | tx_idle_mask; \
	if (tx_subst_mask != b->ds_saved[chip].tcc) \
 		TAU32_write_register32_cs##chip (GPP, DS_TCC, b->ds_saved[chip].tcc = tx_subst_mask); \
	/* test auto-ais condition */ \
	if ((b->e1_config[chip] & TAU32_ais_on_los) && (ManagedStatusE1[chip] & TAU32_RCL)) \
		tcr1 |= TCR1_TUA1; \
	if (b->ds_saved[chip].umr & UMR_ENABLE) { \
		if ((b->ds_saved[chip].rcbr & ~peb_tx_mask)) \
			tcr1 |= TCR1_TUA1; \
	} else { \
		if ((b->e1_config[chip] & TAU32_ais_on_lof) && (ManagedStatusE1[chip] & TAU32_RFAS)) \
			tcr1 |= TCR1_TUA1; \
		if (b->e1_config[chip] & TAU32_auto_ais) { \
			u32 ais_mask = tx_subst_mask; \
			if ((b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_unframed) \
				ais_mask |= 0x00000001ul; \
			/* if ((b->e1_config[chip] & TAU32_framing_mode_mask) > TAU32_framed_no_cas) \
				ais_mask |= 0x00010000ul; */ \
			if (ais_mask == 0xFFFFFFFFul) \
				tcr1 |= TCR1_TUA1; \
		} \
	} \
	if ((b->e1_config[chip] & TAU32_line_mode_mask) == TAU32_LineAIS) \
		tcr1 |= TCR1_TUA1; \
	if (b->ds_saved[chip].tcr1 != tcr1) \
		TAU32_write_register_cs##chip (GPP, DS_TCR1, b->ds_saved[chip].tcr1 = tcr1); \
	rx_subst_mask = 0; \
	ts0 = b->ds_saved[chip].ts[0] & ~0x04u; \
	if (! (b->e1_config[chip] & TAU32_not_auto_dmra) \
		&& (ManagedStatusE1[chip] & CasFailMask)) { \
			/* set auto remote multiframe alarm */ \
			ts0 |= 0x04u; \
			/* replace received cas (may be occur only if cas enabled) */ \
			rx_subst_mask = 1u << 16u; \
		} \
		if (b->e1_config[chip] & TAU32_dmra) \
			ts0 |= 0x04u; \
		/* update remote multiframe alarm bit */ \
		if (b->ds_saved[chip].ts[0] != ts0) \
			TAU32_write_register_cs##chip (GPP, DS_TS, b->ds_saved[chip].ts[0] = ts0); \
		if (b->e1_config[chip] & TAU32_not_auto_ra) { \
			/* update remote alarm bit (if auto control disabled) */ \
			u8 tnaf = b->ds_saved[chip].tnaf & ~0x20; \
			if (b->e1_config[chip] & TAU32_ra) \
 				tnaf |= 0x20u; \
			if (tnaf != b->ds_saved[chip].tnaf) \
				TAU32_write_register_cs##chip (GPP, DS_TNAF, b->ds_saved[chip].tnaf = tnaf); \
		} \
		if (ManagedStatusE1[chip] & LinkFailMask) \
			/* may be: this should be (partially) done by dallas CCR2.3 RSERC */ \
			rx_subst_mask = 0xFFFFFFFFul; \
		/* update receive side re-insertion for E1/0 */ \
		if (rx_subst_mask != b->ds_saved[chip].rcc) \
			TAU32_write_register32_cs##chip (GPP, DS_RCC, b->ds_saved[chip].rcc = rx_subst_mask); \
		/* ddk_dbg_print ("E1/%d rx_subst_mask = 0x%lX, tx_subst_mask = 0x%lX, cas_subst_mask = 0x%lX\n", chip, rx_subst_mask, tx_subst_mask, cas_subst_mask); */ \
}

may_static void TAU32_decalogue (TAU32_Controller * b, int Interfaces)
{
	ddk_bitops_t cas_subst_mask[2], tx_fail_mask[2], tx_idle_mask[2];
	ddk_bitops_t unframed_ts_mask, peb_tx_mask;
	int ts;
	unsigned ManagedStatusE1[2];

	/*
	 * clear RCL if local loop-back mode
	 */
	ManagedStatusE1[0] = b->E1[0].Status;
	if ((b->e1_config[0] & TAU32_line_mode_mask) == TAU32_LineLoopInt)
		ManagedStatusE1[0] &= ~(LinkFailMask | CasFailMask);
	ManagedStatusE1[1] = b->E1[1].Status;
	if ((b->e1_config[1] & TAU32_line_mode_mask) == TAU32_LineLoopInt)
		ManagedStatusE1[1] &= ~(LinkFailMask | CasFailMask);

	cas_subst_mask[0] = cas_subst_mask[1] = 0x03;	/* 0x3 == ts0 | ts16 */
	tx_fail_mask[0] = tx_fail_mask[1] = 0;
	tx_idle_mask[0] = tx_idle_mask[1] = 0;
	unframed_ts_mask = b->ds_saved[0].rcbr | b->ds_saved[1].rcbr;
	peb_tx_mask = 0;

	ts = 31;
	do
		if (!b->PCM_ccb.Timeslots[ts].fields.tti) {
			unsigned channel = b->PCM_ccb.Timeslots[ts].fields.tx_chan;

			if (b->tx_should_running[channel]
			    && ddk_bit_test (b->cm.tx, channel))
				ddk_bit_set (peb_tx_mask, ts);
		}
	while (--ts >= 0) ;

	ts = 31;
	do {
		ddk_bitops_t ts_mask = 1u << ts;

		if ((b->ds_saved[0].umr & UMR_ENABLE) == 0)
			if (Interfaces == TAU32_E1_ALL || Interfaces == TAU32_E1_A)
				__TAU32_decalogue_ts (0, b, ts, ts_mask,
						      peb_tx_mask,
						      unframed_ts_mask,
						      cas_subst_mask[0], tx_idle_mask[0], tx_fail_mask[0]);

		if (b->ModelType != TAU32_LITE)
			if ((b->ds_saved[1].umr & UMR_ENABLE) == 0)
				if (Interfaces == TAU32_E1_ALL || Interfaces == TAU32_E1_B)
					__TAU32_decalogue_ts (1, b, ts,
							      ts_mask,
							      peb_tx_mask,
							      unframed_ts_mask,
							      cas_subst_mask[1], tx_idle_mask[1], tx_fail_mask[1]);
	} while (--ts >= 0);

	if (Interfaces == TAU32_E1_ALL || Interfaces == TAU32_E1_A)
		__TAU32_decalogue_if (0, b, peb_tx_mask, unframed_ts_mask,
				      cas_subst_mask[0], tx_idle_mask[0], tx_fail_mask[0]);

	if (b->ModelType != TAU32_LITE)
		if (Interfaces == TAU32_E1_ALL || Interfaces == TAU32_E1_B)
			__TAU32_decalogue_if (1, b, peb_tx_mask,
					      unframed_ts_mask, cas_subst_mask[1], tx_idle_mask[1], tx_fail_mask[1]);
}

/*----------------------------------------------------------------------------- */

char TAU32_CALL_TYPE TAU32_SetFifoTrigger (TAU32_Controller * b,
					   int Interface, unsigned FifoId, unsigned Level, TAU32_FifoTrigger Trigger)
{
	if (Interface != TAU32_E1_ALL && Interface > TAU32_E1_B)
		return 0;

	if (b->ModelType == TAU32_LITE && Interface > TAU32_E1_A)
		return 0;

	if (FifoId > TAU32_FifoId_FasTx || Level >= TAU32_FIFO_SIZE)
		return 0;

	Lock (&b->Lock);

	if (Interface == TAU32_E1_ALL || Interface == TAU32_E1_A)
		TAU32_SetTrigger (b->E1Fifos[0][FifoId], Level, Trigger);

	if (b->ModelType != TAU32_LITE)
		if (Interface == TAU32_E1_ALL || Interface == TAU32_E1_B)
			TAU32_SetTrigger (b->E1Fifos[1][FifoId], Level, Trigger);

	Unlock (&b->Lock);
	return 1;
}

may_static u32 TAU32_read_tsclow (volatile u32 * GPP)
{
	u32 Result;

	TAU32_out_byte (GPP, T32_TLOAD, 0);
	/* LY: valid for any endian-order */
	Result = TAU32_in_byte (GPP, T32_TLOAD);
	Result = (Result << 8) | TAU32_in_byte (GPP, T32_TLOAD);
	Result = (Result << 8) | TAU32_in_byte (GPP, T32_TLOAD);
	Result = (Result << 8) | TAU32_in_byte (GPP, T32_TLOAD);
	return Result;
}

may_static void TAU32_setup_lygen (volatile u32 * GPP, u32 factor)
{
	/* LY: valid for any endian-order */
	TAU32_out_byte (GPP, T32_GLOAD3, (factor >> 24) & 0xFFu);
	TAU32_out_byte (GPP, T32_GLOAD2, (factor >> 16) & 0xFFu);
	TAU32_out_byte (GPP, T32_GLOAD1, (factor >> 8) & 0xFFu);
	TAU32_out_byte (GPP, T32_GLOAD0, factor & 0xFFu);
}

/*
* LY: take 600 + 700 * 8 = 6200 ns = 6.2 us
*/
void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_ReadTsc (TAU32_Controller * b, TAU32_tsc * pResult)
{
	volatile u32 *GPP;
	u8 *bytes = (u8 *) pResult;
	unsigned i;

	Lock (&b->Lock);
	GPP_INIT (GPP);

	TAU32_out_byte (GPP, T32_TLOAD, 0);
	if (DDK_BIG_ENDIAN) {
		i = 0; do
			bytes[i] = (u8) TAU32_in_byte (GPP, T32_TLOAD);
		while (++i < 8);
	} else {
		i = 8; do
			bytes[i - 1] = (u8) TAU32_in_byte (GPP, T32_TLOAD);
		while (--i);
	}
	Unlock (&b->Lock);
}

void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_SetPhonyStubFill_lsbf (TAU32_Controller * b, unsigned Channel, u32 FourBytesPattern)
{
#if !defined (CRONYX_LYSAP) && defined (TAU32_CHANNELS)
	if (Channel < TAU32_CHANNELS)
		M32X_SetPhonyFill_lsbf (b, Channel, FourBytesPattern);
#else
	if (Channel == 0)
		M32X_SetPhonyFill_lsbf (b, 0, FourBytesPattern);
#endif
}

may_static void TAU32_SetClkOn (TAU32_Controller * b)
{
	volatile u32 *GPP;

	GPP_INIT (GPP);
	TAU32_write_cr (b, GPP, b->cr_saved &= ~CR_CLKBLOCK);
}

may_static void TAU32_SetClkOff (TAU32_Controller * b)
{
	volatile u32 *GPP;

	GPP_INIT (GPP);
	TAU32_write_cr (b, GPP, b->cr_saved |= CR_CLKBLOCK);
}

/*----------------------------------------------------------------------------- */

#if defined (_NTDDK_)
#	pragma section ("PAGE", read, execute)
#	pragma section ("PAGECONS", read)
#	pragma code_seg (push)
#	pragma const_seg (push)
#	pragma code_seg ("PAGE")
#	pragma const_seg ("PAGECONS")
#endif

#define UNPACK_INIT(init_ptr) \
	const u8 *__unpack_ptr = (init_ptr) + 2; \
	int __unpack_count = 0; \
	unsigned __unpack_byte = 0; \
	unsigned UnpackedLength = (init_ptr)[0] +((init_ptr)[1] << 8)

#define UNPACK_BYTE(result)  {				\
	if (__unpack_count > 0)				\
		--__unpack_count;			\
	else {						\
		__unpack_byte = *__unpack_ptr++;	\
		if (__unpack_byte == 0)			\
		__unpack_count = *__unpack_ptr++;	\
	}						\
	result = __unpack_byte;				\
}

may_static const u8 firmware_tau32[] = {
#	include "firmware-tau32.inc"
};

may_static const u8 firmware_tau32_lite[] = {
#	include "firmware-tau32-lite.inc"
};

static __forceinline unsigned DownloadFirmware (volatile u32 * GPP, const u8 * pFirmware, unsigned FirmwareSize)
{
	unsigned i, byte;
	char bPacked = 1;

	UNPACK_INIT (pFirmware);
	if (UnpackedLength >= 0xFFFF) {
		UnpackedLength = FirmwareSize;
		bPacked = 0;
	} else if (UnpackedLength < 100 || UnpackedLength >= 0x10000) {
		ddk_kd_print (("TAU32_Controller: invalid firmware\n"));
		GPP_DATA (GPP) = 0xFFFFFFFF;
		GPP_DIR (GPP) = 0;
		return TAU32_IE_FIRMWARE;
	}

	i = 0;
	/*
	 * delay up to 2.5 us (ALTERA need at least 2.0)
	 */
	/*
	 * set /1KLOAD = 0, nCONFIG = 1
	 */
	do
		/*
		 * each pci-write take at least 100ns
		 */
		GPP_DATA (GPP) = GPP_NCONFIG;
	while (++i < 25);

	/*
	 * wait for: nSTATUS == 1, CONF_DONE == 0
	 */
	while ((GPP_DATA (GPP) & (GPP_NSTATUS | GPP_CONFDONE)) != GPP_NSTATUS) {
		/*
		 * each pci-read take at least 100ns
		 */
		/*
		 * no more than 100 us (ALTERA promist 2.5 < x < ~50)
		 */
		if (++i > 1000 + 25) {
			ddk_kd_print (("TAU32_Controller: bad status (2), hardware failure\n"));
			GPP_DATA (GPP) = 0xFFFFFFFF;
			GPP_DIR (GPP) = 0;
			return TAU32_IE_FIRMWARE;
		}
	}

	/*
	 * delay up to 7.5 us (ALTERA need at least 5.0)
	 */
	do
		/*
		 * each pci-write take at least 100ns
		 */
		GPP_DATA (GPP) = GPP_NCONFIG;
	while (++i < 25 + 75);

	do {
		/*
		 * check for: nSTATUS == 1
		 */
		if ((GPP_DATA (GPP) & GPP_NSTATUS) == 0) {
			ddk_kd_print (("TAU32_Controller: bad nstatus (3).\n"));
			goto failed;
		}

		/*
		 * check for CONF_DONE
		 */
		if (GPP_DATA (GPP) & GPP_CONFDONE) {
			/*
			 * Ten extra clocks.
			 */
			i = 10;
			do {
				/*
				 * DCLK = 0, DATA = 0
				 */
				GPP_DATA (GPP) = GPP_NCONFIG | GPP_IOW | GPP_IOR;
				/*
				 * DCLK = 1, DATA = 0
				 */
				GPP_DATA (GPP) = GPP_NCONFIG | GPP_DCLK | GPP_IOW | GPP_IOR;
			}
			while (--i);

			/*
			 * DCLK = 0, DATA = 0
			 */
			GPP_DATA (GPP) = GPP_NCONFIG | GPP_IOW | GPP_IOR;
			/*
			 * check for: nSTATUS == 1, CONF_DONE == 1
			 */
			if ((GPP_DATA (GPP) & (GPP_NSTATUS | GPP_CONFDONE))
			    != (GPP_NSTATUS | GPP_CONFDONE)) {
				ddk_kd_print (("TAU32_Controller: bad nstatus (5).\n"));
				goto failed;
			}

			/*
			 * Succeeded.
			 */
			ddk_kd_print (("TAU32_Controller: firmware download succeeded, %d bytes remaining.\n",
				       UnpackedLength));

			/*
			 * set /1KLOAD = 1
			 */
			GPP_DATA (GPP) = 0xFFFFFFFF;
			GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;

			return TAU32_IE_OK;
		}

		if (bPacked)
			UNPACK_BYTE (byte)
				else
			byte = *pFirmware++;

		byte |= 0x100;
		do {
			/*
			 * DCLK = 0, DATA = bit
			 */
			unsigned value = (byte & 1) | GPP_NCONFIG;

			GPP_DATA (GPP) = value;
			/*
			 * DCLK = 1, DATA = bit
			 */
			GPP_DATA (GPP) = value | GPP_DCLK;
			/*
			 * go to the next bit
			 */
			byte >>= 1;
		}
		while (byte != 1);
	}
	while (--UnpackedLength);

	ddk_kd_print (("TAU32_Controller: bad confdone.\n"));

failed:
	ddk_kd_print (("TAU32_Controller: firmware downloading aborted, %d bytes remaining.\n", UnpackedLength));
	GPP_DATA (GPP) = 0xFFFFFFFF;
	GPP_DIR (GPP) = 0;

	return TAU32_IE_FIRMWARE;
}

static __forceinline void ResetAltera (volatile u32 * GPP)
{
	/*
	 * set all outputs to 1
	 */
	GPP_DATA (GPP) = 0xFFFFFFFF;

	/*
	 * set direction: output - 0,1,2,5, all other - input/Z-state
	 */
	GPP_DIR (GPP) = GPP_NCONFIG | GPP_1KLOAD | GPP_IOW | GPP_IOR | GPP_DCLK | GPP_1KDATA;

	/*
	 * set /1KLOAD = 0, nCONFIG = 1
	 */
	GPP_DATA (GPP) = GPP_IOW | GPP_IOR | GPP_NCONFIG;

	/*
	 * set /1KLOAD = 0, nCONFIG = 0
	 */
	GPP_DATA (GPP) = GPP_IOW | GPP_IOR;

	/*
	 * small pause, set IOW & IOR for input.
	 */
	GPP_DIR (GPP) = GPP_NCONFIG | GPP_1KLOAD | GPP_DCLK | GPP_1KDATA;
}

char TAU32_CALL_TYPE TAU32_Initialize (TAU32_UserContext * uc, char CronyxDiag)
{
	TAU32_Controller *b;
	const u8 *pFirmware;
	unsigned i, ModelId, FirmwareLength, dallas_id;
	const char *dallas_nik;
	volatile u32 *GPP;

	u8 *pEnd = ((u8 *) & uc->InterfacesInfo) + sizeof (uc->InterfacesInfo);
	u8 *pClear = (u8 *) & uc->Model;

	while (pClear < pEnd)
		*pClear++ = 0;

	b = uc->pControllerObject;
	ddk_assert (uc->ControllerObjectPhysicalAddress == (u32) ddk_dma_addr2phys (uc->ControllerObjectPhysicalAddress));
	M32X_Initialize (b, uc->PciBar1VirtualAddress, (u32) ddk_dma_addr2phys (uc->ControllerObjectPhysicalAddress), uc);

	GPP_INIT (GPP);
	ResetAltera (GPP);

	i = 0;
	/*
	 * wait for: nSTATUS == 0, CONF_DONE == 0
	 */
	while (GPP_DATA (GPP) & (GPP_NSTATUS | GPP_CONFDONE)) {
		/*
		 * each pci-read take at least 100ns
		 */
		/*
		 * no more than 1.5 us (ALTERA promist no more than 1.0 us)
		 */
		if (++i > 15) {
			ddk_kd_print (("TAU32_Controller: bad status (1), hardware failure\n"));
			GPP_DATA (GPP) = 0xFFFFFFFF;
			GPP_DIR (GPP) = 0;
			uc->InitErrors |= TAU32_IE_FIRMWARE;
			return 0;
		}
	}

	/*
	 * nCONFIG low pulse-width must be not less than 2.0 us
	 */
	do
		/*
		 * each pci-write take at least 100ns
		 */
		GPP_DATA (GPP) = 0;
	while (++i < 25);

	/*
	 * Detect the model of Adapter
	 */
	ModelId = GPP_DATA_BYTE (GPP) >> SF_SHIFT;
	switch (ModelId) {
		case SF_TAU32_B:
			uc->Model = b->ModelType = TAU32_BASE;
			uc->Interfaces = 2;
			ddk_kd_print (("TAU32_Controller: adapter model is TAU32_BASE_B\n"));

			pFirmware = firmware_tau32;
			FirmwareLength = sizeof (firmware_tau32);
			break;

		case SF_TAU32_LITE:
			uc->Model = b->ModelType = TAU32_LITE;
			uc->Interfaces = 1;
			ddk_kd_print (("TAU32_Controller: adapter model is TAU32_LITE\n"));

			pFirmware = firmware_tau32_lite;
			FirmwareLength = sizeof (firmware_tau32_lite);
			break;

#ifdef TAU32_ADPCM
		case SF_TAU32_ADPCM:
			uc->Model = b->ModelType = TAU32_ADPCM;
			uc->Interfaces = 2;
			ddk_kd_print (("TAU32_Controller: adapter model is TAU32_ADPCM\n"));
			/*
			 * this version of DDK does't support ADPCM
			 */
			uc->InitErrors |= TAU32_IE_MODEL | TAU32_IE_ADPCM;
			if (!CronyxDiag)
				return 0;

			pFirmware = firmware_tau32_adpcm;
			FirmwareLength = sizeof (firmware_tau32_adpcm);
			break;
#endif

		default:
			b->ModelType = TAU32_UNKNOWN;
			ddk_kd_print (("TAU32_Controller: adapter model is TAU32_UNKNOWN\n"));
			uc->InitErrors |= TAU32_IE_MODEL;
			return 0;
	}

#ifdef TAU32_CUSTOM_FIRMWARE
	if (uc->pCustomFirmware && uc->CustomFirmwareSize) {
		pFirmware = (const u8 *) uc->pCustomFirmware;
		FirmwareLength = uc->CustomFirmwareSize;
	}
#endif

	uc->InitErrors |= DownloadFirmware (GPP, pFirmware, FirmwareLength);
	if (uc->InitErrors)
		return 0;

	/*
	 * internal bus test
	 */
	GPP_DIR (GPP) = 0x00FF;
	i = 0xFF + 1;
	do {
		unsigned test, pattern;

		--i;
		GPP_DATA (GPP) = i | GPP_1KLOAD;
		test = GPP_DATA_BYTE (GPP);
		pattern = (i & ~0x20) | ((~i & 0x10) << 1);
		if (test != pattern) {
			ddk_kd_print (("TAU32_Controller: bus test failed 0x%02X/0x%02X\n", test, pattern));
			uc->DeadBits |= test ^ pattern;
			uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
		}
	} while (i);

	GPP_DATA (GPP) = 0xFFFFFFFF;
	GPP_DIR (GPP) = GPP_1KLOAD | GPP_IOW | GPP_IOR;
	TAU32_out_byte (GPP, T32_CR, CR_GENRES);
	TAU32_out_byte (GPP, T32_CR, CR_CLK_STOP);

	if (uc->InitErrors && !CronyxDiag)
		return 0;

	b->uc->CableTypeJumpers = TAU32_in_byte (GPP, T32_SR) & (SR_TP0 | SR_TP1);

#if defined (SR_TP0) && defined (SR_TP1)
	i = TAU32_in_byte (GPP, T32_SR);
	b->uc->CableTypeJumpers = TAU32_COAX_A + TAU32_COAX_B;
	if (i & SR_TP0)
		b->uc->CableTypeJumpers += TAU32_TP_A - TAU32_COAX_A;
	if (i & SR_TP1)
		b->uc->CableTypeJumpers += TAU32_TP_B - TAU32_COAX_B;
#else
	b->uc->CableTypeJumpers = TAU32_TP_A + TAU32_TP_B;
#endif /* defined (SR_TP0) && defined (SR_TP1) */

	/*
	 * double clear all Dallases's registers
	 */
	TAU32_clear_ds (b);
	TAU32_clear_ds (b);

	/*
	 * power off line
	 */
	TAU32_write_register_both (b, DS_LICR, LICR_POWERDOWN);

	/* LY: reset PLL */
	TAU32_write_register_both (b, DS_TEST3, 1);

	/*
	 * ddk_trap;
	 */
	for (i = 0; i < 64; i++) {
		u32 pattern_32_a, pattern_32_b, test_32;
		u8 test_8, pattern_8 = (u8) ((1u << (i & 7)) + i / 16);

		if (i & 8)
			pattern_8 = ~pattern_8;

		TAU32_write_register_cs0 (GPP, DS_TIDR, pattern_8);
		if (b->ModelType != TAU32_LITE)
			TAU32_write_register_cs1 (GPP, DS_TIDR, (u8)
						  ~ pattern_8);

		pattern_32_a = 1u << (i & 31);
		if (i & 32)
			pattern_32_a = ~pattern_32_a;

		pattern_32_b = (pattern_32_a << 3) | (pattern_32_a >> 29);
		TAU32_write_register32_cs0 (GPP, DS_TCBR, pattern_32_a);
		TAU32_write_register32_cs0 (GPP, DS_RCBR, pattern_32_b);
		if (b->ModelType != TAU32_LITE) {
			TAU32_write_register32_cs1 (GPP, DS_TCBR, ~pattern_32_a);
			TAU32_write_register32_cs1 (GPP, DS_RCBR, ~pattern_32_b);
		}

		test_8 = (u8) TAU32_read_register_cs0 (GPP, DS_TIDR);
		if (test_8 != pattern_8) {
			ddk_kd_print (("TAU32_Controller: DS21x54/0 register test failed 0x%02X/0x%02X\n", pattern_8,
				       test_8));
			uc->DeadBits |= (test_8 ^ pattern_8) << 16;
			uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
		}

		if (b->ModelType != TAU32_LITE) {
			test_8 = (u8)
				~ TAU32_read_register_cs1 (GPP, DS_TIDR);
			if (test_8 != pattern_8) {
				ddk_kd_print (("TAU32_Controller: DS21x54/1 register test failed 0x%02X/0x%02X\n",
					       pattern_8, ~test_8));
				uc->DeadBits |= (test_8 ^ pattern_8) << 24;
				uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
			}
		}

		test_32 = TAU32_read_register32_cs0 (GPP, DS_TCBR);
		if (test_32 != pattern_32_a) {
			ddk_kd_print (("TAU32_Controller: TCBR0 register test failed 0x%02X/0x%02X\n", pattern_32_a,
				       test_32));
			uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
		}
		test_32 = TAU32_read_register32_cs0 (GPP, DS_RCBR);
		if (test_32 != pattern_32_b) {
			ddk_kd_print (("TAU32_Controller: RCBR0 register test failed 0x%02X/0x%02X\n", pattern_32_b,
				       test_32));
			uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
		}
		if (b->ModelType != TAU32_LITE) {
			test_32 = ~TAU32_read_register32_cs1 (GPP, DS_TCBR);
			if (test_32 != pattern_32_a) {
				ddk_kd_print (("TAU32_Controller: TCBR1 register test failed 0x%02X/0x%02X\n",
					       pattern_32_a, ~test_32));
				uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
			}
			test_32 = ~TAU32_read_register32_cs1 (GPP, DS_RCBR);
			if (test_32 != pattern_32_b) {
				ddk_kd_print (("TAU32_Controller: RCBR1 register test failed 0x%02X/0x%02X\n",
					       pattern_32_b, ~test_32));
				uc->InitErrors |= TAU32_IE_INTERNAL_BUS;
			}
		}
	}
	if (uc->InitErrors && !CronyxDiag)
		return 0;

	ddk_kd_print (("TAU32_Controller: DS21x54 registers test passed\n"));
	dallas_id = TAU32_read_register_cs0 (GPP, DS_IDR);
	dallas_nik = 0;
	switch (dallas_id >> 4) {
		case 0x0:
			dallas_nik = "DS2152";
			break;
		case 0x1:
			dallas_nik = "DS21352";
			break;
		case 0x2:
			dallas_nik = "DS21552";
			break;
		case 0x8:
			dallas_nik = "DS2154";
			break;
		case 0x9:
			dallas_nik = "DS21354";
			break;
		case 0xA:
			dallas_nik = "DS21554";
			break;
		default:
			ddk_kd_print (("TAU32_Controller: unknown DS-chip (0x%02X)\n", dallas_id));
			uc->InitErrors |= TAU32_IE_E1_A;
			if (!CronyxDiag)
				return 0;
	}

	if (b->ModelType != TAU32_LITE && TAU32_read_register_cs1 (GPP, DS_IDR) != dallas_id) {
		uc->InitErrors |= TAU32_IE_E1_B;
		if (!CronyxDiag)
			return 0;
	}

	ddk_kd_print (("TAU32_Controller: DALLAS is %s revision %d\n", dallas_nik, dallas_id & 0x0F));

	for (i = 0; i < TAU32_FifoId_Max; i++) {
		b->E1Fifos[0][i] = &b->E1[0].Fifos[i];
		b->E1Fifos[0][i]->pSlipCount = &uc->InterfacesInfo[0].FifoSlip[i];
		b->E1Fifos[1][i] = &b->E1[1].Fifos[i];
		b->E1Fifos[1][i]->pSlipCount = &uc->InterfacesInfo[1].FifoSlip[i];
	}

	switch (M32X_Start1 (b)) {
		case M32X_RR_OK:
			break;
		case M32X_RR_TIMER:
			uc->InitErrors |= TAU32_IE_CLOCK;
			break;
		case M32X_RR_INTERRUPT:
		case M32X_RR_ACTION:
		default:
			uc->InitErrors |= TAU32_IE_HDLC;
			break;
	}
	if (uc->InitErrors && !CronyxDiag)
		return 0;

	/*
	 * clear SACR
	 */
	TAU32_out_byte (GPP, T32_SACR, 0);

	/*
	 * litte check for DXC-done flag
	 */
	if (TAU32_in_byte (GPP, T32_SR) & SR_CMDONE) {
		ddk_kd_print (("TAU32_DXC: error on test 1\n"));
		/*
		 * DXC-CM now bust be free
		 */
		uc->InitErrors |= TAU32_IE_DXC;
		if (!CronyxDiag)
			return 0;
	}

	/*
	 * first DXC-CM pass
	 */
	b->pCrossMatrix_Active = b->CrossMatrix_FlipBuffers[0];
	b->pCrossMatrix_Shadow = b->CrossMatrix_FlipBuffers[1];
	TAU32_InitCross (b, GPP);

	/*
	 * initial dallas config & reset
	 */
	TAU32_write_register_both (b, DS_CCR2, CCR2_TRCLK);

	/*
	 * wait at least 10 ms
	 */
	if ((uc->InitErrors & TAU32_IE_CLOCK) == 0)
		M32X_Stall_256bits (b, 81);

	/*
	 * clear all Dallases's registers again & power off line
	 */
	TAU32_clear_ds (b);
	b->ds_saved[0].licr = b->ds_saved[1].licr = LICR_POWERDOWN;

	/*
	 * sync input
	 */
	b->ds_saved[0].tcr1 = TCR1_TSI;
	TAU32_write_register_both (b, DS_TCR1, b->ds_saved[1].tcr1 = b->ds_saved[0].tcr1);
	b->ds_saved[0].rcr1 = RCR1_RSI | RCR1_SYNCD;
	TAU32_write_register_both (b, DS_RCR1, b->ds_saved[1].rcr1 = b->ds_saved[0].rcr1);

	/*
	 * 2048 clock, enable elastic stores
	 */
	TAU32_write_register_both (b, DS_RCR2, RCR2_RSCLKM | (ENABLE_E1_RES ? RCR2_RESE : 0));
	TAU32_write_register_both (b, DS_CCR3, CCR3_RCLA | CCR3_TBCS | (ENABLE_E1_TES ? CCR3_TESE : 0));

	TAU32_ds_defaults_cs0 (b);
	if (b->ModelType != TAU32_LITE)
		TAU32_ds_defaults_cs1 (b);

	/*
	 * reset line interfase
	 */
	TAU32_write_register_both (b, DS_CCR5, CCR5_LIRST);

	/*
	 * restore E1 clock (GRUN).
	 */
	TAU32_out_byte (GPP, T32_CR, b->cr_saved = CR_CLK_INT);
	if ((uc->InitErrors & TAU32_IE_CLOCK) == 0)
		M32X_Stall_256bits (b, 8);

	switch (M32X_Start2 (b)) {
		case M32X_RR_OK:
			break;
		case M32X_RR_TIMER:
			uc->InitErrors |= TAU32_IE_CLOCK;
			break;
		case M32X_RR_INTERRUPT:
		case M32X_RR_ACTION:
		default:
			uc->InitErrors |= TAU32_IE_HDLC;
			break;
	}
	if (uc->InitErrors && !CronyxDiag)
		return 0;

	if (dallas_id >= 0xA0 && dallas_id < 0x04) {
		ddk_kd_print ((">> TAU32_Controller: apply DALLAS 8MCLK fix\n"));
		TAU32_write_register_both (b, 0xAC, 1);
		/*
		 * wait at least 10 us
		 */
		if ((uc->InitErrors & TAU32_IE_CLOCK) == 0)
			M32X_Stall_256bits (b, 1);
		TAU32_write_register_both (b, 0xAC, 0);
		ddk_kd_print (("<< TAU32_Controller: apply DALLAS 8MCLK fix\n"));
	}

	/*
	 * reset elastic store.
	 */
	TAU32_write_register_both (b, DS_CCR6, CCR6_RESR | CCR6_TESR);
	/*
	 * TAU32_write_register_both (b, DS_CCR5, CCR5_RESA | CCR5_TESA);
	 */

	for (i = 0;; i++) {
		u32 TimestampStart, TimestampStop, TimestampTime;

		TimestampStart = TAU32_read_tsclow (GPP);
		/*
		 * wait for clock to stabilize
		 */
		if ((uc->InitErrors & TAU32_IE_CLOCK) == 0)
			M32X_Stall_256bits (b, 8);

		TimestampStop = TAU32_read_tsclow (GPP);
		TimestampTime = TimestampStop - TimestampStart;
		ddk_kd_print (("TAU32_read_tsclow: start %d, stop %d, time %d, expect ~%d\n", TimestampStart,
			       TimestampStop, TimestampTime, 256 * 8));
		if ((TimestampTime < 256 * 8 || TimestampTime > 256 * 99)
		    && i > 5) {
			ddk_kd_print (("TAU32_read_tsclow: error (%d)\n", i));
			uc->InitErrors |= TAU32_IE_CLOCK;
			if (!CronyxDiag)
				return 0;
		} else
			break;
	}

	/*
	 * second cm-done flag test
	 */
	if (!(TAU32_in_byte (GPP, T32_SR) & SR_CMDONE)
	    || (TAU32_in_byte (GPP, T32_SR) & SR_CMDONE)) {
		/*
		 * DXC-CM now must be set and then clean its done flag
		 */
		ddk_kd_print (("TAU32_DXC: error on test 2\n"));
		uc->InitErrors |= TAU32_IE_DXC;
		if (!CronyxDiag)
			return 0;
	}

	/*
	 * clear pending interrupts & reset LBI, for LBI-INT testing
	 */
	ddk_flush_cpu_writecache ();
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC000101Ful;	/* new config & reset */
	ddk_flush_cpu_writecache ();
	b->RegistersBar1_Hardware->STAT_rw_08 = ~(u32)0;
	ddk_flush_cpu_writecache ();
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0xC060101Ful;	/* -reset, +enable */
	ddk_flush_cpu_writecache ();

	/*
	 * second DXC-CM pass
	 */
	TAU32_InitCross (b, GPP);

	/*
	 * done reset
	 */
	TAU32_write_register_both (b, DS_CCR5, 0);
	TAU32_write_register_both (b, DS_CCR6, 0);
	if ((uc->InitErrors & TAU32_IE_CLOCK) == 0)
		M32X_Stall_256bits (b, 1);

	/*
	 * LBI-INT & trid cm-done flag test
	 */
	if (b->RegistersBar1_Hardware->LSTAT_r_7C != 2 || (b->RegistersBar1_Hardware->STAT_rw_08 & M32X_STAT_LBII) == 0) {
		if (!(TAU32_in_byte (GPP, T32_SR) & SR_CMDONE)
		    || (TAU32_in_byte (GPP, T32_SR) & SR_CMDONE)) {
			/*
			 * DXC-CM now bust be set and then clean its done flag
			 */
			ddk_kd_print (("TAU32_DXC: error on test 3\n"));
			uc->InitErrors |= TAU32_IE_DXC;
		} else
			uc->InitErrors |= TAU32_IE_XIRQ;
		if (!CronyxDiag)
			return 0;
	}

	if (TAU32_HandleSlaveInterrupt (b) <= 0) {
		uc->InitErrors |= TAU32_IE_XIRQ;
		if (!CronyxDiag)
			return 0;
	}

	b->RegistersBar1_Hardware->STAT_rw_08 = M32X_STAT_LBII;
	if (b->RegistersBar1_Hardware->LSTAT_r_7C || (b->RegistersBar1_Hardware->STAT_rw_08 & M32X_STAT_LBII)) {
		if (TAU32_in_byte (GPP, T32_SR) & SR_CMDONE) {
			/*
			 * DXC-CM now bust be set and then clean its done flag
			 */
			ddk_kd_print (("TAU32_DXC: error on test 4\n"));
			uc->InitErrors |= TAU32_IE_DXC;
		} else
			uc->InitErrors |= TAU32_IE_XIRQ;
		if (!CronyxDiag)
			return 0;
	}

	/*
	 * ХУФБОБЧМЙЧБЕН ЗЕОЕТБФПТ ЮБУФПФЩ ОБ 2048,000 KHz
	 */
	TAU32_setup_lygen (GPP, 1073741824ul);

#if DDK_DEBUG
	/*
	 * TAU32_DumpDallas (b);
	 */
#endif

	/*
	 * clear pending interrupts, e.g. RSPA and/or TSPA
	 */
	b->RegistersBar1_Hardware->STAT_rw_08 = ~(u32)0;

	/*
	 * enable-initialize lock/unlock
	 */
	ddk_atomic_set (b->Lock, 1);
	TAU32_decalogue (b, TAU32_E1_ALL);
	return 1;
}

void TAU32_CALL_TYPE TAU32_DestructiveHalt (TAU32_Controller * b, char CancelRequests)
{
	volatile u32 *GPP;

	/*
	 * b->InterruptsEnabled = 0;
	 */
	b->RegistersBar1_Hardware->IMASK_rw_0C = ~(u32)0;
	ddk_interrupt_sync (b->uc->InterruptObject);
	LockOrTrap (&b->Lock);

	/*
	 * disable LBI
	 */
	b->RegistersBar1_Hardware->LCONF_rw_40 = 0;

	/*
	 * disable E1 interrupts
	 */
	TAU32_write_register_both (b, DS_IMR1, 0);
	TAU32_write_register_both (b, DS_IMR2, 0);

	b->as = M32X_ACTION_VOID;
	b->RegistersBar1_Hardware->STAT_rw_08 = (M32X_STAT_TI | M32X_STAT_PCMF | M32X_STAT_PCMA);
	ddk_flush_cpu_writecache ();

	/*
	 * set AIS
	 */
	TAU32_write_register_both (b, DS_TCR1, TCR1_TUA1);
	M32X_Stall_256bits (b, 16);

	/*
	 * power down the line interface
	 */
	TAU32_write_register_both (b, DS_LICR, LICR_POWERDOWN);
	M32X_Stall_256bits (b, 16);

	/* LY: reset PLL */
	TAU32_write_register_both (b, 0xAC, 1);

	GPP_INIT (GPP);
	/*
	 * stop clock
	 */
	TAU32_out_byte (GPP, T32_CR, CR_CLK_STOP);

	ResetAltera (GPP);

	/*
	 * all input
	 */
	GPP_DIR (GPP) = 0;

	b->uc->Model = TAU32_UNKNOWN;
	b->uc->Interfaces = 0;
	M32X_DestructiveHalt (b, CancelRequests);
}

#if defined (_NTDDK_)
#	pragma code_seg (pop)
#	pragma const_seg (pop)
#endif

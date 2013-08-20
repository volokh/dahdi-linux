/*
* DDK (Driver Development Kit) for Cronyx Tau-PCI/32 adapter.
*
* Copyright (C) 2003-2008 Cronyx Engineering, http://www.cronyx.ru
* All rights reserved.
*
* Author: Leo Yuriev <ly@cronyx.ru>, http://leo.yuriev.ru
*
* $Id: tau32-ddk.h,v 1.33 2009-07-10 11:34:12 ly Exp $
*/

#ifndef DDK_PREPROC
#	include "ddk-host.h"
#	include "ddk-arch.h"
#endif /* DDK_PREPROC */

#define TAU32_CRONYX_P	0
#define TAU32_CRONYX_PS	1
#define TAU32_CRONYX_PA	2
#define TAU32_CRONYX_PB	3
#define TAU32_CRONYX_I	4
#define TAU32_CRONYX_O	5
#define TAU32_CRONYX_U	6
#define TAU32_CRONYX_R	7
#define TAU32_CRONYX_W	8
#define TAU32_CRONYX_RW	9
#define TAU32_CRONYX_WR	10
#define TAU32_CRONYX_S	11
#define TAU32_CRONYX_G	12
#define TAU32_CRONYX_T	13

#define TAU32_PCI_VENDOR_ID	0x110A
#define TAU32_PCI_DEVICE_ID	0x2101

/* TAU32_MODELS */
#define TAU32_ERROR		(-1)
#define TAU32_UNKNOWN		0
#define TAU32_BASE		1
#define TAU32_LITE		2

/* TAU32_INIT_ERRORS */
#define TAU32_IE_OK		0x0000u
#define TAU32_IE_FIRMWARE	0x0001u
#define TAU32_IE_MODEL		0x0002u
#define TAU32_IE_E1_A		0x0004u
#define TAU32_IE_E1_B		0x0008u
#define TAU32_IE_INTERNAL_BUS	0x0010u
#define TAU32_IE_HDLC		0x0020u
#define TAU32_IE_ADPCM		0x0040u
#define TAU32_IE_CLOCK		0x0080u
#define TAU32_IE_DXC		0x0100u
#define TAU32_IE_XIRQ		0x0200u

/* TAU32_INTERFACES */
#define TAU32_E1_ALL		(-1)
#define TAU32_E1_A		0
#define TAU32_E1_B		1

/* TAU32_LIMITS */
#if !defined (TAU32_CHANNELS) || (TAU32_CHANNELS < 1) || (TAU32_CHANNELS > 32)
#	undef TAU32_CHANNELS
#	ifdef CRONYX_LYSAP
#		define TAU32_CHANNELS	1
#	else
#		define TAU32_CHANNELS	32
#	endif
#endif

#define TAU32_TIMESLOTS		32
#define TAU32_MAX_INTERFACES	2
#define TAU32_MTU		4096
#define TAU32_IO_QUEUE		4
#define TAU32_IO_QUEUE_BYTES	128
#define TAU32_MAX_REQUESTS	1024
#define TAU32_MAX_BUFFERS	512
#define TAU32_FIFO_SIZE		256

/* TAU32_INTERFACE_STATUS_BITS */
#define TAU32_RCL	0x0001u /* receive carrier lost */
#define TAU32_RLOS	0x0002u /* receive sync lost */
#define TAU32_RUA1	0x0004u /* received unframed all ones */
#define TAU32_RRA	0x0008u /* receive remote alarm */
#define TAU32_RSA1	0x0010u /* receive signaling all ones */
#define TAU32_RSA0	0x0020u /* receive signaling all zeros */
#define TAU32_RDMA	0x0040u /* receive distant multiframe alarm */
#define TAU32_LOTC	0x0080u /* transmit clock lost */
#define TAU32_RSLIP	0x0100u /* receiver slip event */
#define TAU32_TSLIP	0x0200u /* transmitter slip event */
#define TAU32_RFAS	0x0400u /* receiver lost and searching for FAS */
#define TAU32_RCRC4	0x0800u /* receiver lost and searching for CRC4 MF */
#define TAU32_RCAS	0x1000u /* received lost and searching for CAS MF */
#define TAU32_JITTER	0x2000u /* jitter attenuator limit */
#define TAU32_RCRC4LONG	0x4000u /* G.706 400ms limit of searching for CRC4 */
#define TAU32_E1OFF	0x8000u /* E1 line power-off */
#define TAU32_RA	TAU32_RRA
#define TAU32_LOS	TAU32_RLOS
#define TAU32_AIS	TAU32_RUA1
#define TAU32_LOF	TAU32_RFAS
#define TAU32_AIS16	TAU32_RSA1
#define TAU32_LOMF	TAU32_RCAS
#define TAU32_BCAS	(TAU32_RSA1 | TAU32_RSA0) /* LY: bad CAS with cas-strict option */

/* TAU32_STATUS */
#define TAU32_FRLOMF		0x0001u /* CAS framer searching for CAS MF */
#define TAU32_CROSS_WAITING	0x0002u /* Connection memory swap waiting */
#define TAU32_CROSS_PENDING	0x0004u /* Connection memory swap pending */
#define TAU32_LED		0x0008u /* Led status (on/off) */

#define TAU32_CROSS_WIDTH	96
#define TAU32_CROSS_OFF		127
typedef u8 TAU32_CrossMatrix[TAU32_CROSS_WIDTH];

/* TAU32_FIFO_ID */
#define TAU32_FifoId_CasRx	0u
#define TAU32_FifoId_CasTx	1u
#define TAU32_FifoId_FasRx	2u
#define TAU32_FifoId_FasTx	3u
#define TAU32_FifoId_Max	4u

typedef struct tag_TAU32_E1_State {
	unsigned long TickCounter;
	unsigned long RxViolations;
	unsigned long Crc4Errors;
	unsigned long FarEndBlockErrors;
	unsigned long FasErrors;
	unsigned long TransmitSlips;
	unsigned long ReceiveSlips;
	unsigned long Status;
	unsigned long FifoSlip[TAU32_FifoId_Max];
} TAU32_E1_State;

typedef struct tag_TAU32_TimeslotAssignment {
	u8 TxChannel, RxChannel;
	u8 TxFillmask, RxFillmask;
} TAU32_TimeslotAssignment;

/* TAU32_INTERFACE_CONFIG_BITS */
#define TAU32_LineOff		(0ul << 0)
#define TAU32_LineLoopInt	(1ul << 0)
#define TAU32_LineLoopExt	(2ul << 0)
#define TAU32_LineNormal	(3ul << 0)
#define TAU32_LineAIS		(4ul << 0)
#define TAU32_line_mode_mask	0x0000000Ful
#define TAU32_unframed_64	(0ul << 4)
#define TAU32_unframed_128	(1ul << 4)
#define TAU32_unframed_256	(2ul << 4)
#define TAU32_unframed_512	(3ul << 4)
#define TAU32_unframed_1024	(4ul << 4)
#define TAU32_unframed_2048	(5ul << 4)
#define TAU32_unframed	TAU32_unframed_2048
#define TAU32_framed_no_cas	(6ul << 4)
#define TAU32_framed_cas_set	(7ul << 4)
#define TAU32_framed_cas_pass	(8ul << 4)
#define TAU32_framed_cas_cross	(9ul << 4)
#define TAU32_framing_mode_mask	0x000000F0ul
#define TAU32_monitor		0x00000100ul
#define TAU32_higain		0x00000200ul
#define TAU32_sa_bypass		0x00000400ul
#define TAU32_si_bypass		0x00000800ul
#define TAU32_cas_fe		0x00001000ul
#define TAU32_ais_on_los	0x00002000ul
#define TAU32_cas_all_ones	0x00004000ul
#define TAU32_cas_io		0x00008000ul
#define TAU32_fas_io		0x00010000ul
#define TAU32_fas8_io		0x00020000ul
#define TAU32_auto_ais		0x00040000ul
#define TAU32_not_auto_ra	0x00080000ul
#define TAU32_not_auto_dmra	0x00100000ul
#define TAU32_ra		0x00200000ul
#define TAU32_dmra		0x00400000ul
#define TAU32_scrambler		0x00800000ul
#define TAU32_tx_ami		0x01000000ul
#define TAU32_rx_ami		0x02000000ul
#define TAU32_ja_tx		0x04000000ul
#define TAU32_crc4_mf_tx	0x08000000ul
#define TAU32_crc4_mf_rx	0x10000000ul
#define TAU32_crc4_mf		(TAU32_crc4_mf_rx | TAU32_crc4_mf_tx)
#define TAU32_strict_cas	0x20000000ul
#define TAU32_ais_on_lof	0x40000000ul

/* TAU32_SA_CROSS_VALUES */
#define TAU32_SaDisable		0u
#define TAU32_SaSystem		1u
#define TAU32_SaIntA		2u
#define TAU32_SaIntB		3u
#define TAU32_SaAllZeros	4u

typedef struct tag_TAU32_SaCross {
	u8 InterfaceA, InterfaceB;
	u8 SystemEnableTs0;
} TAU32_SaCross;

#define TAU32_A_MASK		1
#define TAU32_TP_A		1
#define TAU32_COAX_A		0
#define TAU32_B_MASK		2
#define TAU32_TP_B		2
#define TAU32_COAX_B		0

/* TAU32_SYNC_MODES */
#define TAU32_SYNC_INTERNAL	0
#define TAU32_SYNC_RCV_A	1
#define TAU32_SYNC_RCV_B	2
#define TAU32_SYNC_LYGEN	3
#define TAU32_LYGEN_RESET	0

/* TAU32_CHANNEL_MODES */
#define TAU32_HDLC		0
#define TAU32_V110_x30		1
#define TAU32_TMA		2
#define TAU32_TMB		3
#define TAU32_TMR		4

/* TAU32_CHANNEL_CONFIG_BITS */
#define TAU32_channel_mode_mask		0x0000000Ful
#define TAU32_data_inversion		0x00000010ul
#define TAU32_fr_rx_splitcheck		0x00000020ul
#define TAU32_fr_rx_fitcheck		0x00000040ul
#define TAU32_fr_tx_auto		0x00000080ul
#define TAU32_hdlc_crc32		0x00000100ul
#define TAU32_hdlc_adjustment		0x00000200ul
#define TAU32_hdlc_interframe_fill	0x00000400ul
#define TAU32_hdlc_nocrc		0x00000800ul
#define TAU32_hdlc_shareflags		0x00001000ul
#define TAU32_tma_flag_filtering	0x00002000ul
#define TAU32_tma_nopack		0x00004000ul
#define TAU32_tma_flags_mask		0x00FF0000ul
#define TAU32_tma_flags_shift		16u
#define TAU32_v110_x30_tr_mask		0x03000000ul
#define TAU32_v110_x30_tr_shift		24u

//-----------------------------------------------------------------------------

#ifndef DDK_USERMODE_ONLY

#ifndef TAU32_CALLBACK_TYPE
#	define TAU32_CALLBACK_TYPE
#endif

#ifndef TAU32_CALL_TYPE
#	define TAU32_CALL_TYPE
#endif

#define TAU32_PCI_IO_BAR1_SIZE	0x0100
#define TAU32_PCI_RESET_ADDRESS 0x004C
#define TAU32_PCI_RESET_ON	0xF00F0000ul	/* 0xFFFFFFFFul */
#define TAU32_PCI_RESET_OFF	0
#define TAU32_PCI_RESET_LENGTH	4

/* TAU32_REQUEST_COMMANDS */
#define TAU32_Tx_Start			0x0001u
#define TAU32_Tx_Stop			0x0002u
#define TAU32_Tx_Data			0x0008u
#define TAU32_Rx_Start			0x0010u
#define TAU32_Rx_Stop			0x0020u
#define TAU32_Rx_Data			0x0080u
#define TAU32_Configure_Channel		0x0100u
#define TAU32_Timeslots_Complete	0x0200u
#define TAU32_Timeslots_Map		0x0400u
#define TAU32_Timeslots_Channel		0x0800u
#define TAU32_ConfigureDigitalLoop	0x1000u
#define TAU32_Configure_Commit		0x2000u
#define TAU32_Tx_FrameEnd		0x4000u
#define TAU32_Tx_NoCrc			0x8000u
#define TAU32_Configure_E1		0x0040u

/* TAU32_ERRORS */
#define TAU32_NOERROR			0x000000ul
#define TAU32_SUCCESSFUL		0x000000ul
#define TAU32_ERROR_ALLOCATION		0x000001ul /* not enough tx/rx/cmd descriptors */
#define TAU32_ERROR_BUS			0x000002ul /* PEB could not access to host memory by PCI bus for load/store information */
#define TAU32_ERROR_FAIL		0x000004ul /* PEB action request failed */
#define TAU32_ERROR_TIMEOUT		0x000008ul /* PEB action request timeout */
#define TAU32_ERROR_CANCELLED		0x000010ul
#define TAU32_ERROR_TX_UNDERFLOW	0x000020ul /* transmission underflow */
#define TAU32_ERROR_TX_PROTOCOL		0x000040ul /* reserved */
#define TAU32_ERROR_RX_OVERFLOW		0x000080ul
#define TAU32_ERROR_RX_ABORT		0x000100ul
#define TAU32_ERROR_RX_CRC		0x000200ul
#define TAU32_ERROR_RX_SHORT		0x000400ul
#define TAU32_ERROR_RX_SYNC		0x000800ul
#define TAU32_ERROR_RX_FRAME		0x001000ul
#define TAU32_ERROR_RX_LONG		0x002000ul
#define TAU32_ERROR_RX_SPLIT		0x004000ul /* frame has splitted between two requests during rx-gap allocation */
#define TAU32_ERROR_RX_UNFIT		0x008000ul /* frame can't be fit into request buffer */
#define TAU32_ERROR_TSP			0x010000ul
#define TAU32_ERROR_RSP			0x020000ul
#define TAU32_ERROR_INT_OVER_TX		0x040000ul
#define TAU32_ERROR_INT_OVER_RX		0x080000ul
#define TAU32_ERROR_INT_STORM		0x100000ul
#define TAU32_ERROR_INT_E1LOST		0x200000ul
#define TAU32_WARN_TX_JUMP		0x400000ul
#define TAU32_WARN_RX_JUMP		0x800000ul

typedef struct tag_TAU32_Controller TAU32_Controller;
typedef struct tag_TAU32_UserRequest TAU32_UserRequest;
typedef struct tag_TAU32_UserContext TAU32_UserContext;
typedef struct tag_TAU32_tsc TAU32_tsc;
typedef void (TAU32_CALLBACK_TYPE * TAU32_RequestCallback) (TAU32_UserContext *, TAU32_UserRequest *);
typedef void (TAU32_CALLBACK_TYPE * TAU32_NotifyCallback) (TAU32_UserContext *, int Item, unsigned NotifyBits);
typedef void (TAU32_CALLBACK_TYPE * TAU32_FifoTrigger) (TAU32_UserContext *, int Interface, unsigned FifoId, unsigned Level);

struct tag_TAU32_tsc {
	u32 osc, sync;
};

struct tag_TAU32_UserContext {
	/*
	* fields provided by user for for TAU32_Initiaize ()
	*/
	TAU32_Controller *pControllerObject;
	ddk_dma_addr_t ControllerObjectPhysicalAddress;
	void *PciBar1VirtualAddress;
	TAU32_NotifyCallback pErrorNotifyCallback;
	TAU32_NotifyCallback pStatusNotifyCallback;
#if defined (ddk_irqobj_t)
	ddk_irqobj_t InterruptObject;
#endif

#ifdef TAU32_CUSTOM_FIRMWARE
	void *pCustomFirmware;
	unsigned CustomFirmwareSize;
#endif
	/*
	* fields filled by TAU32_Initiaize ()
	*/
	int Model;
	int Interfaces;
	unsigned InitErrors;
	u32 DeadBits;
	/*
	* fields managed by DDK
	*/
	unsigned AdapterStatus;
	unsigned CableTypeJumpers;
	unsigned long CasIoLofCount;
	unsigned long E1IntLostCount;
	TAU32_E1_State InterfacesInfo[TAU32_MAX_INTERFACES];
	/*
	* fields which are't used by DDK, but nice for user
	*/
#ifdef TAU32_UserContext_Add
	TAU32_UserContext_Add
#endif
};

struct tag_TAU32_UserRequest {
	/*
	* required fields
	*/
	void *pInternal;	/* internal */
	unsigned Command;	/* in */
#if defined (_NTDDK_)
	KDPC CallbackDpc;
	void SetupCallback (PKDEFERRED_ROUTINE DeferredCallbackRoutine, void *pContext) {
		CallbackDpc.DeferredRoutine = DeferredCallbackRoutine;
		CallbackDpc.DeferredContext = 0;
	}
	void SetupCallback (TAU32_RequestCallback pCallback) {
		CallbackDpc.DeferredRoutine = (PKDEFERRED_ROUTINE) pCallback;
		CallbackDpc.DeferredContext = 0;
	}
#else
	TAU32_RequestCallback pCallback;	/* in */
#endif
	unsigned ErrorCode; /* out */
	union {
		unsigned cn, ChannelNumber; /* just common field */
		struct {
			unsigned Channel;	/* in */
			u32 Config;		/* in */
			u32 AssignedTsMask;	/* build channel from timeslots which is selected by mask */
		} ChannelConfig;

		struct {
			int Interface;
			u32 Config;		/* in */
			u32 UnframedTsMask;
		} InterfaceConfig;

		struct {
			unsigned Channel;	/* in */
			u32 PhysicalDataAddress;	/* in */
			unsigned DataLength;	/* in */
			unsigned Transmitted;	/* out */
		} Tx;

		struct {
			unsigned Channel;	/* in */
			ddk_dma_addr_t PhysicalDataAddress;	/* in */
			unsigned BufferLength;	/* in */
			unsigned Received;	/* out */
			char FrameEnd;	/* out */
		} Rx;

		char DigitalLoop;	/* in, loop by PEB */

		union {
			TAU32_TimeslotAssignment Complete[TAU32_TIMESLOTS];
			u32 Map[TAU32_CHANNELS];
		} TimeslotsAssignment;
	} Io;

	/*
	* fields which are't used by DDK, but nice for user
	*/
#ifdef TAU32_UserRequest_Add
	TAU32_UserRequest_Add
#endif
};

#define TAU32_IS_REQUEST_RUNNING(ur) ((ur)->pInternal != NULL)
#define TAU32_IS_REQUEST_NOT_RUNNING(ur) ((ur)->pInternal == NULL)

#ifndef TAU32_DDK_DLL
#	if defined (_NTDDK_)
#		ifdef TAU32_DDK_IMP
#			define TAU32_DDK_DLL __declspec (dllexport)
#		else
#			define TAU32_DDK_DLL __declspec (dllimport)
#		endif
#	else
#		define TAU32_DDK_DLL
#	endif
#endif

#ifdef __cplusplus
extern "C" {
#endif
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_BeforeReset (TAU32_UserContext *);
	char TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_Initialize (TAU32_UserContext *, char CronyxDiag);
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_DestructiveHalt (TAU32_Controller *, char CancelRequests);
	char TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_IsInterruptPending (TAU32_Controller *);
	char TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_HandleInterrupt (TAU32_Controller *);
	extern TAU32_DDK_DLL unsigned const TAU32_ControllerObjectSize;
	/*
	* LY: все функции ниже, могут реентерабельно вызываться из callback-ов
	*/
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_EnableInterrupts (TAU32_Controller *);
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_DisableInterrupts (TAU32_Controller *);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_SubmitRequest (TAU32_Controller *, TAU32_UserRequest *);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_CancelRequest (TAU32_Controller *, TAU32_UserRequest *,
		char BreakIfRunning);
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_LedBlink (TAU32_Controller *);
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_LedSet (TAU32_Controller *, char On);
	u64 TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_ProbeGeneratorFrequency (u64 Frequency);
	u64 TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_SetGeneratorFrequency (TAU32_Controller *, u64 Frequency);
	char TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_SetSyncMode (TAU32_Controller *, unsigned Mode);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_SetCrossMatrix (TAU32_Controller *,
		u8 * pCrossMatrix, u32 ReverseMask);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_SetCrossMatrixCas (TAU32_Controller *, u8 * pCrossMatrix);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_SetIdleCodes (TAU32_Controller *, u8 * pIdleCodes);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_UpdateIdleCodes (TAU32_Controller *,
		int Interface, u32 TimeslotMask, u8 IdleCode);
	char TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_SetSaCross (TAU32_Controller *, TAU32_SaCross SaCross);
	int TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_FifoPutCasAppend (TAU32_Controller *,
		int Interface, u8 * pBuffer, unsigned Length);
	int TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_FifoPutCasAhead (TAU32_Controller *,
		int Interface, u8 * pBuffer, unsigned Length);
	int TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_FifoGetCas (TAU32_Controller *,
		int Interface, u8 * pBuffer, unsigned Length);
	int TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_FifoPutFasAppend (TAU32_Controller *,
		int Interface, u8 * pBuffer, unsigned Length);
	int TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_FifoPutFasAhead (TAU32_Controller *,
		int Interface, u8 * pBuffer, unsigned Length);
	int TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_FifoGetFas (TAU32_Controller *,
		int Interface, u8 * pBuffer, unsigned Length);
	char TAU32_DDK_DLL TAU32_CALL_TYPE
		TAU32_SetFifoTrigger (TAU32_Controller *,
		int Interface, unsigned FifoId, unsigned Level, TAU32_FifoTrigger Trigger);
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_ReadTsc (TAU32_Controller *, TAU32_tsc *pResult);
	void TAU32_DDK_DLL TAU32_CALL_TYPE TAU32_SetPhonyStubFill_lsbf (TAU32_Controller *, unsigned Channel, u32 FourBytesPattern);
	/*
	* for Cronyx Engineering use only !!!
	*/
	u32 TAU32_CALL_TYPE TAU32_Diag (TAU32_Controller *,
		unsigned Operation, u32 Data);
#ifdef __cplusplus
}
#endif

#endif /* DDK_USERMODE_ONLY */

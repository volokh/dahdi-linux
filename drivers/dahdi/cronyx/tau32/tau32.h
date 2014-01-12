/*
 * Cronyx Tau-32
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
 * $Id: tau32.h,v 1.28 2008-11-09 20:22:14 ly Exp $
 */

/* Implementation */
typedef struct tag_TAU32_DallasModeRegistersLite {
	u8 ccr1, ccr2, ccr3, ccr4, licr, tcr1, tcr2, rcr1;
	u8 test3, imr1, imr2, tsacr;
	u8 umr;			/* it is not a part of dallas, but of tau32 */
	u32 tcbr, rcbr;
} TAU32_DallasModeRegistersLite;

typedef struct tag_TAU32_DallasModeRegistersFull {
	u8 ccr1, ccr2, ccr3, ccr4, licr, tcr1, tcr2, rcr1;
	u8 test3, imr1, imr2, tsacr;
	u8 umr;	/* LY: it isn't a part of dallas, but of tau32 */
	u32 tcbr, rcbr;
	u32 tcc, rcc;
	u8 tc[32], ts[16], tnaf;
} TAU32_DallasModeRegistersFull;

typedef struct tag_TAU32_Fifo {
	unsigned Head, Tail, Mark;
	unsigned long *pSlipCount;
	volatile u32 TriggerActivated;
	u32 TriggerLevel;
	TAU32_FifoTrigger Trigger;
	u8 Buffer[TAU32_FIFO_SIZE];
} TAU32_Fifo;

static __forceinline unsigned DownloadFirmware (volatile u32 * GPP, const u8 * pFirmware, unsigned FirmwareSize);
static __forceinline unsigned TAU32_in_byte (volatile u32 * GPP, unsigned reg);
static __forceinline void TAU32_out_byte (volatile u32 * GPP, unsigned reg, unsigned val);
static __forceinline void TAU32_write_register_inl (unsigned chip, volatile u32 * GPP, unsigned reg, unsigned val);
static __forceinline unsigned TAU32_read_register_inl (unsigned chip, volatile u32 * GPP, unsigned reg);
static __forceinline unsigned TAU32_read_status_inl (unsigned chip, volatile u32 * GPP, unsigned reg);

static void TAU32_write_register_cs0 (volatile u32 * GPP, unsigned reg, unsigned val);
static unsigned TAU32_read_register_cs0 (volatile u32 * GPP, unsigned reg);
static unsigned TAU32_read_status_cs0 (volatile u32 * GPP, unsigned reg);
static void TAU32_write_register32_cs0 (volatile u32 * GPP, unsigned reg, u32 val);
static u32 TAU32_read_register32_cs0 (volatile u32 * GPP, unsigned reg);
static void TAU32_ds_defaults_cs0 (TAU32_Controller * b);

static void TAU32_write_register_cs1 (volatile u32 * GPP, unsigned reg, unsigned val);
static unsigned TAU32_read_register_cs1 (volatile u32 * GPP, unsigned reg);
static unsigned TAU32_read_status_cs1 (volatile u32 * GPP, unsigned reg);
static void TAU32_write_register32_cs1 (volatile u32 * GPP, unsigned reg, u32 val);
static u32 TAU32_read_register32_cs1 (volatile u32 * GPP, unsigned reg);
static void TAU32_ds_defaults_cs1 (TAU32_Controller * b);

static void TAU32_write_register_both (TAU32_Controller * b, unsigned reg, unsigned val);
static void TAU32_clear_ds (TAU32_Controller * pControllerObject);

static __forceinline char TAU32_config2dallas (unsigned InterfaceConfig,
					       u32 UnframedMask, TAU32_DallasModeRegistersLite * pDallas);
static __forceinline void TAU32_KickSlaveInterrupt (TAU32_Controller * b);
static int TAU32_HandleSlaveInterrupt (TAU32_Controller * b);
static void TAU32_ForceProcessDallasesStatus (TAU32_Controller * pTau32);
static void TAU32_decalogue (TAU32_Controller * pControllerObject, int Interfaces);
static __forceinline int TAU32_FifoPutAppend (unsigned FifoId, TAU32_Fifo * pFifo, u8 * pBuffer, unsigned Length);
static __forceinline int TAU32_FifoPutAhead (unsigned FifoId, TAU32_Fifo * pFifo, u8 * pBuffer, unsigned Length);
static __forceinline int TAU32_FifoGet (unsigned FifoId, TAU32_Fifo * pFifo, u8 * pBuffer, unsigned Length);
static __forceinline void TAU32_FifoClear (unsigned FifoId, TAU32_Fifo * pFifo);
static __forceinline char TAU32_IsTriggerAlarmed (unsigned FifoId, TAU32_Fifo * pFifo);
static __forceinline void TAU32_CheckTrigger (unsigned FifoId,
					      unsigned PeriodLimitBits,
					      TAU32_Fifo * pFifo, TAU32_Controller * pTau32, int Interface);
static __forceinline void TAU32_UpdateTrigger (unsigned FifoId, TAU32_Fifo * pFifo);
static __forceinline void TAU32_SetTrigger (TAU32_Fifo * pFifo, unsigned Level, TAU32_FifoTrigger Trigger);
static void TAU32_UpdateCross (TAU32_Controller * pControllerObject, volatile u32 * GPP);
static u32 TAU32_read_tsclow (volatile u32 * GPP);
static void TAU32_setup_lygen (volatile u32 * GPP, u32 factor);
static unsigned TAU32_ProcessReadStatus (TAU32_Controller * pTau32);

/* Integration with the M32X code */
#define M32X_USER_HANDLE_INTERRUPT(b, InterruptStatus) TAU32_HandleSlaveInterrupt (b)
#define M32X_RequestCommands   TAU32_RequestCommands
#define M32X_Tx_Start      TAU32_Tx_Start
#define M32X_Tx_Stop       TAU32_Tx_Stop
#define M32X_Tx_Flush      0	/* TAU32_Tx_Flush - not yet implemented */
#define M32X_Tx_Data       TAU32_Tx_Data
#define M32X_Tx_FrameEnd     TAU32_Tx_FrameEnd
#define M32X_Tx_NoCrc      TAU32_Tx_NoCrc
#define __M32X_Tx_mask     (TAU32_Tx_Start | TAU32_Tx_Stop | TAU32_Tx_Data | TAU32_Tx_FrameEnd | TAU32_Tx_NoCrc)
#define M32X_Rx_Start      TAU32_Rx_Start
#define M32X_Rx_Stop       TAU32_Rx_Stop
#define M32X_Rx_Data       TAU32_Rx_Data
#define __M32X_Rx_mask     (TAU32_Rx_Start | TAU32_Rx_Stop | TAU32_Rx_Data)
#define M32X_Configure_Setup   TAU32_Configure_Channel
#define M32X_Timeslots_Complete  TAU32_Timeslots_Complete
#define M32X_Timeslots_Map   TAU32_Timeslots_Map
#define M32X_Timeslots_Channel TAU32_Timeslots_Channel
#define M32X_Configure_Commit  TAU32_Configure_Commit
#define M32X_Configure_Loop    TAU32_ConfigureDigitalLoop
#define __M32X_Configure_mask  (M32X_Configure_Setup | \
                    M32X_Timeslots_Complete | \
                    M32X_Timeslots_Map | \
                    M32X_Timeslots_Channel | \
                    M32X_Configure_Commit | \
                    M32X_Configure_Loop)
#define __M32X_ValidCommands_mask (__M32X_Tx_mask | \
                    __M32X_Rx_mask | \
                    __M32X_Configure_mask | \
                    __M32X_UserCommands)
#define __M32X_SelfCommands    (M32X_Tx_Start | \
                    M32X_Tx_Stop | \
                    M32X_Tx_Flush | \
                    M32X_Tx_Data | \
                    M32X_Rx_Start | \
                    M32X_Rx_Stop | \
                    M32X_Rx_Data | \
                    M32X_Configure_Setup | \
                    M32X_Timeslots_Complete | \
                    M32X_Timeslots_Channel | \
                    M32X_Configure_Loop)
#define __M32X_UserCommands    TAU32_Configure_E1

#define M32X_Errors        TAU32_Errors
#define M32X_NOERROR       TAU32_NOERROR
#define M32X_SUCCESSFUL      TAU32_SUCCESSFUL
#define M32X_ERROR_ALLOCATION  TAU32_ERROR_ALLOCATION
#define M32X_ERROR_BUS     TAU32_ERROR_BUS
#define M32X_ERROR_FAIL      TAU32_ERROR_FAIL
#define M32X_ERROR_TIMEOUT   TAU32_ERROR_TIMEOUT
#define M32X_ERROR_CANCELLED   TAU32_ERROR_CANCELLED
#define M32X_ERROR_TX_UNDERFLOW  TAU32_ERROR_TX_UNDERFLOW
#define M32X_ERROR_TX_PROTOCOL TAU32_ERROR_TX_PROTOCOL
#define M32X_ERROR_RX_OVERFLOW TAU32_ERROR_RX_OVERFLOW
#define M32X_ERROR_RX_ABORT    TAU32_ERROR_RX_ABORT
#define M32X_ERROR_RX_CRC    TAU32_ERROR_RX_CRC
#define M32X_ERROR_RX_SHORT    TAU32_ERROR_RX_SHORT
#define M32X_ERROR_RX_SYNC   TAU32_ERROR_RX_SYNC
#define M32X_ERROR_RX_FRAME    TAU32_ERROR_RX_FRAME
#define M32X_ERROR_RX_LONG   TAU32_ERROR_RX_LONG
#define M32X_ERROR_RX_UNFIT    TAU32_ERROR_RX_UNFIT
#define M32X_ERROR_TSP     TAU32_ERROR_TSP
#define M32X_ERROR_RSP     TAU32_ERROR_RSP
#define M32X_ERROR_INT_OVER_TX TAU32_ERROR_INT_OVER_TX
#define M32X_ERROR_INT_OVER_RX TAU32_ERROR_INT_OVER_RX
#define M32X_ERROR_INT_STORM   TAU32_ERROR_INT_STORM
#define M32X_WARN_TX_JUMP   TAU32_WARN_TX_JUMP
#define M32X_WARN_RX_JUMP   TAU32_WARN_RX_JUMP

#define M32X_User_ChannelModes TAU32_ChannelModes
#define M32X_HDLC        TAU32_HDLC
#define M32X_V110_x30      TAU32_V110_x30
#define M32X_TMA         TAU32_TMA
#define M32X_TMB         TAU32_TMB
#define M32X_TMR         TAU32_TMR

#define M32X_channel_mode_mask TAU32_channel_mode_mask
#define M32X_data_inversion    TAU32_data_inversion
#define M32X_fr_rx_fitcheck    TAU32_fr_rx_fitcheck
#define M32X_fr_tx_auto      TAU32_fr_tx_auto

#define M32X_hdlc_adjustment   TAU32_hdlc_adjustment
#define M32X_hdlc_interframe_fill TAU32_hdlc_interframe_fill
#define M32X_hdlc_crc32      TAU32_hdlc_crc32
#define M32X_hdlc_nocrc      TAU32_hdlc_nocrc

#define M32X_tma_flag_filtering  TAU32_tma_flag_filtering
#define M32X_tma_flags_mask    TAU32_tma_flags_mask
#define M32X_tma_flags_shift   TAU32_tma_flags_shift
#define M32X_tma_nopack      TAU32_tma_nopack

#define M32X_v110_x30_tr_mask  TAU32_v110_x30_tr_mask
#define M32X_v110_x30_tr_shift TAU32_v110_x30_tr_shift

#define M32X_uta TAU32_TimeslotAssignment
#define M32X_UseChannels     TAU32_CHANNELS
#define M32X_UseTimeslots    TAU32_TIMESLOTS
#define M32X_UserStatistics    TAU32_Statistics

#define M32X_RequestCallback   TAU32_RequestCallback
#define M32X_NotifyCallback    TAU32_NotifyCallback
#define M32X_ur_t     TAU32_UserRequest

#define M32X_CHANNELS      TAU32_CHANNELS
#define M32X           TAU32_Controller
#define tag_M32X         tag_TAU32_Controller
#define M32X_UserContext     TAU32_UserContext
#define M32X_USER_INTERRUPT_BITS  M32X_STAT_LBII
#define M32X_USER_VERIFY_REQUEST  TAU32_VerifyUserRequest
#define M32X_USER_PROCESS_REQUEST TAU32_ProcessUserRequest
#define M32X_USER_NOTIFY_NEW_TSAS  TAU32_DecalogueAndMask
#define M32X_USER_NOTIFY_NEW_CHRUN TAU32_DecalogueAndMask
#define M32X_USER_NOTIFY_NEW_CHCFG TAU32_DecalogueAndMask

#define M32X_INTERFACE_CALL static

static __forceinline void Lock (ddk_atomic_t* pLock)
{
	ddk_atomic_dec (*pLock);
}

static __forceinline void LockOrTrap (ddk_atomic_t* pLock)
{
	if (unlikely (! ddk_atomic_dec_and_test (*pLock)))
		ddk_trap ();
}

static __forceinline void Unlock (ddk_atomic_t* pLock)
{
	ddk_atomic_inc (*pLock);
}

static __forceinline void LockTest (ddk_atomic_t* pLock)
{
	if (unlikely (ddk_atomic_read (*pLock) != 1))
		ddk_trap ();
}

#define M32X_BEFORE_CALLBACK(b) Unlock (&b->Lock);
#define M32X_AFTER_CALLBACK(b) Lock (&b->Lock);

typedef u8 TAU32_CrossMatrixImp[128 + TAU32_CROSS_WIDTH];

#define M32X_USER_DATA \
  ddk_atomic_t Lock; \
  unsigned AdapterStatus; \
  int ModelType; \
  unsigned cr_saved, sync; \
  unsigned e1_config[2]; \
  TAU32_DallasModeRegistersFull ds_saved[2]; \
  u8 *pCrossMatrix_Shadow; \
  u8 *pCrossMatrix_Active; \
  unsigned SaCross_Saved, TsTestMode; \
  u32 LyGenFactor; \
  TAU32_Fifo *E1Fifos[2][4]; \
  struct \
  { \
    TAU32_Fifo Fifos[4]; \
    unsigned Status; \
  } E1[2]; \
  u8 idle_codes[2][32]; \
  TAU32_CrossMatrixImp CrossMatrix_Pending; \
  TAU32_CrossMatrixImp CrossMatrix_FlipBuffers[2];

#define M32X_USER_DATA_REQUEST \
  struct { \
    TAU32_DallasModeRegistersLite NewRegisters; \
    unsigned Tasks; \
  } Tau32;

enum TAU32_E1Config_Tasks {
	DoClear = 1u << 0,
	DoStop = 1u << 2,
	DoStart = 1u << 4,
	DoReset1 = 1u << 6,
	DoReset2 = 1u << 8,
	DoReset0 = 1u << 10,
	DoResync = 1u << 12,
	DoUpdate = 1u << 14,
	DoReset = DoReset1 | DoReset2 | DoReset0
};

struct tag_M32X_InternalRequest;
static char TAU32_VerifyUserRequest (TAU32_Controller * b, TAU32_UserRequest * ur, struct tag_M32X_InternalRequest
				     *ir);
static char TAU32_ProcessUserRequest (TAU32_Controller * b, TAU32_UserRequest * ur, struct tag_M32X_InternalRequest
				      *ir);

static void TAU32_SetClkOn (TAU32_Controller * b);
static void TAU32_SetClkOff (TAU32_Controller * b);
static void M32X_NotifyError (M32X* b, int Item, unsigned Code);

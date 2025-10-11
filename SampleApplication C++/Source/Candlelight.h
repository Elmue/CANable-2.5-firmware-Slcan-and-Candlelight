
// https://netcult.ch/elmue/CANable Firmware Update

#pragma once

#include "winusb.h"
#include "candlelight_def.h"
#include <afxmt.h>  // CCriticalSection

// Windows error codes are far below 50000

#define ERROR_INVALID_DEVICE    57010  // Not a Candlelight device
#define ERROR_INVALID_FIRMWARE  57011  // Not CANable 2.5 firmware
#define ERROR_CODE_IN_FEEDBACK  57012  // Check me_LastError for an explanation
#define ERROR_RX_FIFO_OVERFLOW  57013  // The application is polling ReceiveData() slower than USB IN packets arrive
#define ERROR_CORRUPT_IN_DATA   57014  // Corrupt USB IN packet received from the firmware

#define RX_FIFO_MAX_COUNT          30  // up to 30 USB packets can be buffered
#define RX_FIFO_BUF_SIZE          128  // max bytes that can be read from USB (should be multiple of max. endpoint packet size (=64))

typedef enum 
{
    LEVEL_Low,    // print error in grey
    LEVEL_Medium, // print error in yellow / orange
    LEVEL_High,   // print error in red
} eErrorLevel;

class Candlelight
{
public:
    Candlelight();
    ~Candlelight();
    // ------------------------------------
    DWORD    EnumDevices(BYTE u8_Interface, CStringArray* ps_DispNames, CStringArray* ps_DevicePaths);
    DWORD    Open(CString s_DevicePath);
    void     Close();
    DWORD    SetBitrate(bool b_FD, int s32_BRP, int s32_Seg1, int s32_Seg2, CString* ps_Display);
    DWORD    AddMaskFilter(bool b_29bit, DWORD u32_Filter, DWORD u32_Mask);
    DWORD    Start(eDeviceFlags e_Flags);
    // ------------------------------------
    DWORD    SendPacket(DWORD u32_ID, bool b_29bit, bool b_FDF, bool b_BRS, bool b_RTR, BYTE u8_Data[], int s32_DataLen, CString* ps_Packet, __int64* ps64_WinTimestamp);
    DWORD    ReceiveData(DWORD u32_Timeout, kHeader* pk_Header, DWORD u32_BufSize, __int64* ps64_WinTimestamp);
    // ------------------------------------
    __int64  GetWinTimestamp();
    CString  FormatTimestamp(kHeader* pk_Header, __int64 s64_WinTimestamp);
    CString  FormatHexBytes(BYTE u8_Data[], int s32_DataLen);
    CString  FormatRxPacket (kRxFrameElmue* pk_RxFrame);
    CString  FormatTxEcho   (kTxEchoElmue*  pk_Echo);
    CString  FormatCanErrors(kErrorElmue*   pk_Error, eErrorBusStatus* pe_BusStatus, eErrorLevel* pe_Level);
    CString  FormatLastError (DWORD u32_Error);
    CString  FormatBcdVersion(DWORD u32_Version);
    // ------------------------------------
    DWORD    Identify(bool b_Blink);
    DWORD    EnableBusLoadReport(BYTE u8_Interval);
    DWORD    EnterDfuMode();
    DWORD    DisableBootPin();
    DWORD    IsBootPinEnabled(bool* pb_Enabled);

    CString                  ms_Vendor;
    CString                  ms_Product;
    CString                  ms_Serial;
    BYTE                     mu8_EndpointIN;
    BYTE                     mu8_EndpointOUT;
    WORD                     mu16_MaxPackSizeIN;
    WORD                     mu16_MaxPackSizeOUT;
    bool                     mb_SupportsFD;
    bool                     mb_LegacyFirmware;
    USB_DEVICE_DESCRIPTOR    mk_DeviceDescr;
    kCapabilityClassic       mk_Capability;
    kCapabilityFD            mk_CapabilityFD;
    kDeviceVersion           mk_DeviceVersion;
    kBoardInfo               mk_BoardInfo;

private:
    DWORD    ReadStringDescriptor(BYTE u8_Index, WORD u16_LanguageID, CString* ps_String);
    DWORD    CtrlTransfer(eDirection e_Dir, BYTE u8_Request, WORD u16_Value, void* p_Data, DWORD u32_DataSize);
    CString  FormatFrame(DWORD u32_ID, BYTE u8_Flags, CString s_Data);
    DWORD    Reset();

    HANDLE                   mh_Device;
    WINUSB_INTERFACE_HANDLE  mh_WinUsb;
    BYTE                     mu8_Interface;
    bool                     mb_McuTimestamp;
    bool                     mb_BaudFDSet;
    bool                     mb_TxOverflow;
    bool                     mb_InitDone;
    bool                     mb_Started;
    eFeedback                me_LastError;
    __int64                  ms64_LastMcuStamp;    // the last MCU timestamp
    __int64                  ms64_McuRollOver;     // offset for 32 bit firmware timestamp
    __int64                  ms64_PerfTimeStart;   // offset for performance timer
    __int64                  ms64_ClockOffset;     // convert relative timestamp into clock time
    __int64                  ms64_StampOffset;     // offset of timestamp when the clock time was taken

    // ----------- ReadPipe Thread -----------

    static DWORD WINAPI ReadPipeThreadStatic(void* p_This);
    void ReadPipeThreadMember();

    struct kRxFifo
    {
        BYTE    mu8_Buffer[RX_FIFO_BUF_SIZE];
        DWORD   mu32_BytesRead;
        DWORD   mu32_Error;
        __int64 ms64_WinTimestamp;
    };

    kRxFifo                  mk_RxFifo[RX_FIFO_MAX_COUNT];  // must only be accessed in critical section
    int                      ms32_FifoReadIdx;              // must only be accessed in critical section
    int                      ms32_FifoCount;                // must only be accessed in critical section
    bool                     mb_AbortThread;
    bool                     mb_FifoOverflow;
    HANDLE                   mh_ThreadEvent;
    HANDLE                   mh_ReceiveEvent;
    CCriticalSection         mi_Critical;

    // --------------- Echo -----------------

    struct kEchoFrame
    {
        BYTE u8_TxMemory[sizeof(kTxFrameElmue) + 64];
    };

    kEchoFrame               mk_EchoFrames[256];
    BYTE                     mu8_EchoMarker;
};


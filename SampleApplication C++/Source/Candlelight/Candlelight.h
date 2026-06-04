
// https://netcult.ch/elmue/CANable Firmware Update

#pragma once

#include "WinUSB_def.h"
#include "Candlelight_def.h"
#include <afxmt.h>  // CCriticalSection

// Windows error codes are far below 50000

#define ERROR_INVALID_DEVICE    57010  // Not a Candlelight device
#define ERROR_INVALID_FIRMWARE  57011  // Not CANable 2.5 firmware
#define ERROR_CODE_IN_FEEDBACK  57012  // Check me_LastError for an explanation
#define ERROR_RX_FIFO_OVERFLOW  57013  // The application is polling ReceiveData() slower than USB IN packets arrive. In the demo app the reason may be the slow Windows console.
#define ERROR_CORRUPT_IN_DATA   57014  // Corrupt USB IN packet received from the firmware
#define ERROR_UPDATE_FIRMWARE   57015  // The user must update the firmware
#define ERROR_TOO_MANY_ERRORS   57016  // too many errors during WritePipe / ReadPipe

#define RX_FIFO_MAX_COUNT          30  // up to 30 USB packets can be buffered

typedef enum 
{
    LEVEL_Low,    // print error in grey
    LEVEL_Medium, // print error in yellow / orange
    LEVEL_High,   // print error in red
} eErrorLevel;

class cUsbDevice
{
public:
    CString ms_Interface;
    CString ms_Product;
    CString ms_SerialNo;
    CString ms_DevPath;
    int     ms32_Channel; // one-based

    cUsbDevice()
    {
        ms32_Channel = 0; // invalid
    }

    CString DisplayName()
    {
        // If a legacy Candlelight device does not expose a string in the Candlelight interface, 
        // Windows returns the Product string instead --> both are identical
        if (ms_Product == ms_Interface)
            return ms_Product;

        return ms_Product + L" - " + ms_Interface;
    }

    // Compare by Serial Number and then by Channel number for sorting
    int Compare(const cUsbDevice* pi_Dev2)
    {
        int s32_Diff = ms_SerialNo.Compare(pi_Dev2->ms_SerialNo);
        if (s32_Diff != 0)
            return s32_Diff;
            
        if (ms32_Channel > pi_Dev2->ms32_Channel)
            return 1;

        if (ms32_Channel < pi_Dev2->ms32_Channel)
            return -1;

        return 0; // This will never happen
    }
};

struct kDevInfo
{
    WCHAR                    ms_Vendor   [128];
    WCHAR                    ms_Product  [128];
    WCHAR                    ms_Serial   [128];
    WCHAR                    ms_Interface[128];
    BYTE                     mu8_EndpointIN;
    BYTE                     mu8_EndpointOUT;
    WORD                     mu16_MaxPackSizeIN;
    WORD                     mu16_MaxPackSizeOUT;
    bool                     mb_IsElmueSoft;
    bool                     mb_SupportsFD;
    BYTE                     mu8_Channel;
    USB_DEVICE_DESCRIPTOR    mk_DeviceDescr;
    kCapabilityClassic       mk_Capability;
    kCapabilityFD            mk_CapabilityFD;
    kDeviceVersion           mk_DeviceVersion;
    kBoardInfo               mk_BoardInfo;
};

// Remote fames store the DLC value in the first data byte
struct kCanPacket
{
    DWORD mu32_ID;
    BYTE  mu8_Data[64];
    BYTE  mu8_DataLen;
    bool  mb_29bit; // Extended ID.
    bool  mb_RTR;   // Remote Frame                     Only used if mb_FDF = false
    bool  mb_FDF;   // CAN FD Frame
    bool  mb_BRS;   // CAN FD Bit Rate Switching        Only used if mb_FDF = true
    bool  mb_ESI;   // CAN FD Error State Passive flag  Only used if mb_FDF = true
};

class Candlelight
{
public:
     Candlelight();
    ~Candlelight();
    // ------------------------------------
    DWORD    EnumDevices(bool b_Candlelight, CArray<cUsbDevice, cUsbDevice>* pi_Devices);
    DWORD    Open(CString s_DevicePath);
    void     Close();
    void     EnableTxEcho(bool b_Enable);
    DWORD    SetBitrate(bool b_FD, int s32_BRP, int s32_Seg1, int s32_Seg2, CString* ps_Display);
    DWORD    AddHostFilter(bool b_29bit, DWORD u32_Filter, DWORD u32_Mask);
    DWORD    SetBridgeFilter(BYTE u8_FilterIndex, BYTE u8_DestChannel, bool b_Enable, bool b_Block, bool b_29bit, DWORD u32_Filter, DWORD u32_Mask);
    DWORD    Start(eDeviceFlags e_Flags);
    // ------------------------------------
    DWORD      SendPacketBlob(kCanPacket* pk_Packets, int s32_Count, __int64* ps64_WinTimestamp);
    DWORD      SendPacket(kCanPacket* pk_CanPacket, __int64* ps64_WinTimestamp);
    DWORD      ReceiveData(DWORD u32_Timeout, kHeader** ppk_Header, __int64* ps64_RxTimestamp, bool* pb_Blob = NULL);
    kCanPacket RxFrameToCanPacket(kRxFrameElmue* pk_RxFrame);
    kCanPacket GetTxEchoPacket   (kTxEchoElmue*  pk_TxEcho);
    CString    ConvertStringFrame(kStringElmue*  pk_String);
    // ------------------------------------
    __int64  GetWinTimestamp();
    CString  FormatCanPacket(kCanPacket* pk_Packet);
    CString  FormatTimestamp(kHeader* pk_Header, __int64 s64_WinTimestamp);
    CString  FormatHexBytes(BYTE u8_Data[], int s32_DataLen);
    CString  FormatCanErrors(kErrorElmue*   pk_Error, eErrorBusStatus* pe_BusStatus, eErrorLevel* pe_Level);
    CString  FormatLastError (DWORD u32_Error);
    CString  FormatBcdVersion(DWORD u32_Version);
    // ------------------------------------
    DWORD    Identify(bool b_Blink);
    DWORD    EnableBusLoadReport(BYTE u8_Interval);
    DWORD    EnterDfuMode();
    DWORD    DisableBootPin();
    DWORD    IsBootPinEnabled(bool* pb_Enabled);
    DWORD    ReadFlash (BYTE u8_Segment, BYTE* u8_Buffer, DWORD u32_BufSize, DWORD* pu32_DataRead);
    DWORD    WriteFlash(BYTE u8_Segment, BYTE* u8_Buffer, DWORD u32_DataLen);
    // ------------------------------------
    inline kDevInfo GetDeviceInfo() { return mk_Info; }
    inline CString  GetDetails()    { return ms_Details; }

private:
    DWORD    EnumSerialNumbers(CMapStringToString* pi_Serials);
    DWORD    RegReadString(HKEY h_Class, const WCHAR* u16_Path, const WCHAR* u16_Entry, CString* ps_Value);
    DWORD    ReadStringDescriptor(BYTE u8_Index, WORD u16_LanguageID, WCHAR s_String[128]);
    DWORD    CtrlTransfer(eDirection e_Dir, BYTE u8_Request, WORD u16_Value, void* p_Data, DWORD u32_DataSize, DWORD* pu32_DataRead = NULL);
    DWORD    TxPacketToTxBytes(kCanPacket* pk_Packet, BYTE* u8_TxBuf, int s32_BufSize, int* ps32_Offset);
    DWORD    ReceiveFifo(DWORD u32_Timeout);
    DWORD    Reset();

    HANDLE                   mh_Device;
    WINUSB_INTERFACE_HANDLE  mh_WinUsb;
    kDevInfo                 mk_Info;
    CString                  ms_Details;
    BYTE                     mu8_Interface;
    BYTE                     mu8_Channel;
    bool                     mb_McuTimestamp;
    bool                     mb_BaudFDSet;
    bool                     mb_InitDone;
    bool                     mb_Started;
    bool                     mb_EnableTxEcho;
    DWORD                    mu32_TxOverflow;    
    DWORD                    mu32_RxPipeErrors;   
    DWORD                    mu32_TxPipeErrors;   
    eFeedback                me_LastError;
    __int64                  ms64_LastMcuStamp;    // the last MCU timestamp
    __int64                  ms64_McuRollOver;     // offset for 32 bit firmware timestamp
    __int64                  ms64_PerfTimeStart;   // offset for performance timer

    // ----------- ReadPipe Thread -----------

    static DWORD WINAPI ReadPipeThreadStatic(void* p_This);
    void ReadPipeThreadMember();

    struct kRxFifo
    {
        BYTE    mu8_Buffer[MAX_BLOB_SIZE];
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
    kRxFifo                  mk_BlobData;       // the last received blob or single frame
    DWORD                    mu32_BlobOffset;   // current read position in kRxFifo.mu8_Buffer
    int                      ms32_BlobFrames;   // count of remaining frames in mk_BlobData to be read

    // --------------- Echo -----------------

    BYTE                     mu8_EchoMarker;      // counter    1...255
    kCanPacket               mk_EchoPackets[256]; // Tx packets 1...255
};



// https://netcult.ch/elmue/CANable%20Firmware%20Update

#pragma once

//  The files WinLibrary.h and WinLibrary.cpp contain code for Windows.
//  Someone must re-write them for Linux
#if defined(_MSC_VER)
    #include "WinLibrary.h"
#elif defined(__linux__)
    #include "LinuxLibrary.h"
#else
    #error "Unknown compiler"
#endif

namespace CANable
{

enum eErrorLevel
{
    LEVEL_Low,    // print error in grey
    LEVEL_Medium, // print error in yellow / orange
    LEVEL_High,   // print error in red
};

// Remote fames store the DLC value in the first data byte
struct kCanPacket
{
    uint32_t mu32_ID;
    uint8_t  mu8_Data[64];
    uint8_t  mu8_DataLen;
    bool     mb_29bit; // Extended ID.
    bool     mb_RTR;   // Remote Frame                     Only used if mb_FDF = false
    bool     mb_FDF;   // CAN FD Frame
    bool     mb_BRS;   // CAN FD Bit Rate Switching        Only used if mb_FDF = true
    bool     mb_ESI;   // CAN FD Error State Passive flag  Only used if mb_FDF = true
};

struct kDetail
{
    wstring ms_Name;
    wstring ms_Value;

    kDetail(wstring s_Name, wstring s_Value)
    {
        ms_Name  = s_Name;
        ms_Value = s_Value;
    }

    // returns "USB Product:            Candlelight 2.5 - Multiboard"
    wstring Format(int s32_ColumnWidth)
    {
        wstring s_Out = ms_Name;
        s_Out += L":";
        s_Out.append(max(1, s32_ColumnWidth - s_Out.length()), ' ');
        s_Out += ms_Value;
        return s_Out;
    }
};

class Candlelight
{
public:
     Candlelight();
    ~Candlelight();
    // ------------------------------------
    uint32_t   Open(wstring s_DevicePath);
    void       Close();
    void       EnableTxEcho(bool b_Enable);
    uint32_t   SetBitrate(bool b_FD, int s32_BRP, int s32_Seg1, int s32_Seg2, wstring* ps_Display);
    uint32_t   AddHostFilter(bool b_29bit, uint32_t u32_Filter, uint32_t u32_Mask);
    uint32_t   SetBridgeFilter(uint8_t u8_FilterIndex, uint8_t u8_DestChannel, bool b_Enable, bool b_Block, bool b_29bit, uint32_t u32_Filter, uint32_t u32_Mask);
    uint32_t   Start(eDeviceFlags e_Flags);
    // ------------------------------------
    uint32_t   SendPacketBlob(kCanPacket* pk_Packets, int s32_Count, int64_t* ps64_OsTimestamp);
    uint32_t   SendPacket(kCanPacket* pk_CanPacket, int64_t* ps64_OsTimestamp);
    uint32_t   ReceiveData(uint32_t u32_Timeout, kHeader** ppk_Header, int64_t* ps64_RxTimestamp, bool* pb_Blob = NULL);
    kCanPacket RxFrameToCanPacket(kRxFrameElmue* pk_RxFrame);
    kCanPacket GetTxEchoPacket   (kTxEchoElmue*  pk_TxEcho);
    wstring    ConvertStringFrame(kStringElmue*  pk_String);
    // ------------------------------------
    wstring    FormatCanPacket(kCanPacket* pk_Packet);
    wstring    FormatTimestamp(kHeader* pk_Header, int64_t s64_OsTimestamp);
    wstring    FormatCanErrors(kErrorElmue*   pk_Error, eErrorBusStatus* pe_BusStatus, eErrorLevel* pe_Level);
    wstring    FormatLastError(uint32_t u32_Error);
    // ------------------------------------
    uint32_t   Identify(bool b_Blink);
    uint32_t   EnableBusLoadReport(uint8_t u8_Interval);
    uint32_t   EnterDfuMode();
    uint32_t   DisableBootPin();
    uint32_t   IsBootPinEnabled(bool* pb_Enabled);
    uint32_t   ReadFlash (uint8_t u8_Segment, uint8_t* u8_Buffer, uint32_t u32_BufSize, uint32_t* pu32_DataRead);
    uint32_t   WriteFlash(uint8_t u8_Segment, uint8_t* u8_Buffer, uint32_t u32_DataLen);
    // ------------------------------------
    inline vector<kDetail> GetDetails()     { return  mi_Details; }
    inline kDevInfo        GetDeviceInfo()  { return *mi_OsLibrary.DevInfo(); } // return a copy of the struct. mpk_Info may be NULL here!
    inline int64_t         GetOsTimestamp() { return  mi_OsLibrary.GetTimestamp(); }

private:
    uint32_t   CtrlTransfer(eDirection e_Dir, uint8_t u8_Request, uint16_t u16_Value, void* p_Data, uint32_t u32_DataSize, uint32_t* pu32_DataRead = NULL);
    uint32_t   TxPacketToTxBytes(kCanPacket* pk_Packet, uint8_t* u8_TxBuf, int s32_BufSize, int* ps32_Offset);
    uint32_t   Reset();

    OsLibrary                mi_OsLibrary;
    uint8_t                  mu8_Interface;
    kDevInfo*                mpk_Info;
    uint8_t                  mu8_EchoMarker;      // counter    1...255
    int64_t                  ms64_McuRollOver;    // offset for 32 bit firmware timestamp
    int64_t                  ms64_LastMcuStamp;   // the last MCU timestamp
    uint64_t                 mu64_TxOverflow;    
    uint32_t                 mu32_BlobOffset;     // current read position in kRxFifo.mu8_Buffer
    int                      ms32_BlobFrames;     // count of remaining frames in mk_BlobData to be read
    bool                     mb_BaudFDSet;
    bool                     mb_InitDone;
    bool                     mb_Started;
    bool                     mb_EnableTxEcho;
    eFeedback                me_LastError;
    kCanPacket               mk_EchoPackets[256]; // Tx packets 1...255
    vector<kDetail>          mi_Details;
    uint8_t                  mu8_Channel;
    bool                     mb_McuTimestamp;
    kUsbInPacket             mk_UsbInPacket;           // the last received blob or single frame
};

}; // namespace
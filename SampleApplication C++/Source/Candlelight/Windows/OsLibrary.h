
// https://netcult.ch/elmue/CANable%20Firmware%20Update

#pragma once

#include <tchar.h>
#include <conio.h>
#include <windows.h>

#include "../Utils.h"
#include "WinUSB_def.h"

// =======================================================================================================
//
//  This class contains code for Windows.
//  NOTE: This class uses WinUSB by purpose: 
//  The WinUSB driver is part of the operating system and installed 100% automatically 
//  when connecting the device for the first time.
//  On the other hand when using libusb on Windows the user would be forced to download 
//  and install a driver manually that has no advantage over WinUSB.
//
// =======================================================================================================

// Console colors
#define WHITE   (FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define GREY    (FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define CYAN    (FOREGROUND_GREEN | FOREGROUND_BLUE  | FOREGROUND_INTENSITY)
#define MAGENTA (FOREGROUND_RED   | FOREGROUND_BLUE  | FOREGROUND_INTENSITY)
#define YELLOW  (FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define LIME    (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define RED     (FOREGROUND_RED   | FOREGROUND_INTENSITY)
#define BLUE    (FOREGROUND_BLUE  | FOREGROUND_INTENSITY)
#define BROWN   (FOREGROUND_RED   | FOREGROUND_GREEN)
#define GREEN   (FOREGROUND_GREEN)

// up to 30 USB IN packets can be stoed in the Rx FIFO
#define RX_FIFO_MAX_COUNT   30  

namespace CANable
{

class OsLibrary
{
public:
    static uint32_t EnumDevices(bool b_GetCandlelight, vector<kUsbDevice>* pi_Devices);
    static string   GetErrorMessage(uint32_t u32_Error);
    static string   ToUtf8(wchar_t* s_Unicode, int s32_StrLen = -1);
    // Console
    static void     SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, string s_Title);
    static void     PrintConsole(uint16_t u16_Color, string s_Format, ...);
    static bool     CheckConsoleEnterPressed();
    static int      WaitConsoleChar();

     OsLibrary();
    ~OsLibrary();
    uint32_t    Open(kUsbDevice* pk_Device);
    uint32_t    StartPipes();
    void        Close();
    // USB transfer
    uint32_t    ControlTransfer(kSetup* pk_Setup, uint8_t* u8_Buffer, uint32_t* pu32_Transferred);
    uint32_t    ReadPipeIn(uint32_t u32_Timeout, kUsbInPacket* pk_UsbInPacket);
    uint32_t    WritePipeOut(uint8_t* u8_TxData, uint32_t u32_TxLen);
    // Time
    int64_t     GetTimestamp();
    // -------------------------
    inline bool      IsOpen()        { return mh_WinUsb != NULL && mb_ThreadRuns; }
    inline bool      HasPipeErrors() { return mu32_RxPipeErrors > 30 || mu32_TxPipeErrors > 30; }
    inline kDevInfo* DevInfo()       { return &mk_Info; }

private:
    static uint32_t        EnumSerialNumbers(cStringMap& i_Serials);
    static uint32_t        RegReadString(HKEY h_Class, const char* s8_Path, const char* s8_Entry, string* ps_Value);
    static uint32_t WINAPI PipeThreadStatic(void* p_This);

    void      PipeThreadMember();
    uint32_t  ReadStringDescriptor(uint8_t u8_Index, uint16_t u16_LanguageID, string* ps_String);

    HANDLE                   mh_Device;
    WINUSB_INTERFACE_HANDLE  mh_WinUsb;
    HANDLE                   mh_ReceiveEvent;
    HANDLE                   mh_ThreadEvent;

    int64_t                  ms64_PerfTimeStart; // offset for performance timer
    uint32_t                 mu32_RxPipeErrors;   
    uint32_t                 mu32_TxPipeErrors;   
    int                      ms32_FifoCount;     // must only be accessed in critical section
    int                      ms32_FifoReadIdx;   // must only be accessed in critical section
    bool                     mb_FifoOverflow;
    bool                     mb_AbortThread;
    bool                     mb_ThreadRuns;

    kDevInfo                 mk_Info;
    kUsbInPacket             mk_RxFifo[RX_FIFO_MAX_COUNT];  // must only be accessed in critical section
    CRITICAL_SECTION         mk_Critical;
};

}; // namespace
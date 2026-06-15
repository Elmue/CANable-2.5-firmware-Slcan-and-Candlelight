
// https://netcult.ch/elmue/CANable%20Firmware%20Update

#pragma once

#include "../Utils.h"

// =======================================================================================================
//
//  Study the class "Windows/OsLibrary.cpp" and re-write it for Linux, then send it to elmue@gmx.de
//  The WinUSB library must be replaced with the libusb library.
//  The thread and the Rx FIFO in the Windows class can probably be removed when using libusb.
//
// =======================================================================================================

// Console colors
#define WHITE   ... TODO
#define GREY    ... TODO
#define CYAN    ... TODO
#define MAGENTA ... TODO
#define YELLOW  ... TODO
#define LIME    ... TODO
#define RED     ... TODO
#define BLUE    ... TODO
#define BROWN   ... TODO
#define GREEN   ... TODO

namespace CANable
{

class OsLibrary
{
public:
    static uint32_t EnumDevices(bool b_Candlelight, vector<kUsbDevice>* pi_Devices);
    static string   GetErrorMessage(uint32_t u32_Error);
    // Console
    static void     SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, string s_Title);
    static void     PrintConsole(uint16_t u16_Color, string s_Format, ...);
    static bool     CheckConsoleEnterPressed();
    static int      WaitConsoleChar();

     OsLibrary();
    ~OsLibrary();
    uint32_t    Open(string s_DevicePath);
    uint32_t    StartPipes();
    void        Close();
    // USB transfer
    uint32_t    ControlTransfer(kSetup* pk_Setup, uint8_t* u8_Buffer, uint32_t u32_BufLen, uint32_t* pu32_Transferred);
    uint32_t    ReadPipeIn(uint32_t u32_Timeout, kUsbInPacket* pk_UsbInPacket);
    uint32_t    WritePipeOut(uint8_t* u8_Transmit, uint32_t u32_TxLen);
    // Time
    int64_t     GetTimestamp();
    // -------------------------
    inline bool      IsOpen()        { return ... TODO; } // return true if USB device is open and fully initialized
    inline bool      HasPipeErrors() { return ... TODO; } // return true if USB device disconnected or malfunctioning
    inline kDevInfo* DevInfo()       { return &mk_Info; }

private:
    kDevInfo   mk_Info;
};

}; // namespace
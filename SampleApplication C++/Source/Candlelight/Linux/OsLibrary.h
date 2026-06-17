
// https://netcult.ch/elmue/CANable%20Firmware%20Update

#pragma once

#include "../Utils.h"

// =======================================================================================================
//
//  I'am a Windows developer and I'm not interested in Linux.
//  However, I wrote this class for the Linux community with the help of Gemini.
//  This class has never been compiled and never been tested.
//  Finish and test this class on Linux, then send it to elmue@gmx.de
//
// =======================================================================================================

// Console colors
#define WHITE   0
#define GREY    1
#define CYAN    2
#define MAGENTA 3
#define YELLOW  4
#define LIME    5
#define RED     6
#define BLUE    7
#define BROWN   8
#define GREEN   9

namespace CANable
{

class OsLibrary
{
public:
    static uint32_t EnumDevices(bool b_GetCandlelight, vector<kUsbDevice>* pi_Devices);
    static string   GetErrorMessage(uint32_t u32_Error);
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
    inline bool      IsOpen()        { return mb_IsOpen; } 
    inline bool      HasPipeErrors() { return mu32_RxPipeErrors > 30 || mu32_TxPipeErrors > 30; }
    inline kDevInfo* DevInfo()       { return &mk_Info;  }

private:
    static string ReadSysfsString(string s_Path);
    static int    GetKeyboardInput(bool b_Blocking);

    kDevInfo              mk_Info;
    libusb_context*       mpi_UsbContext;
    libusb_device**       mppi_UsbDeviceList;
    libusb_device_handle* mpi_DevHandle;
    bool                  mb_IsOpen;
    uint32_t              mu32_RxPipeErrors;   
    uint32_t              mu32_TxPipeErrors;   
    int64_t               ms64_TimestampStart;
};

}; // namespace
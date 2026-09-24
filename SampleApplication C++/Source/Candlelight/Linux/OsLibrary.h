
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
    static string  GetErrorMessage(uint32_t u32_Error);
    static void    SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, string s_Title);
    static void    SwitchTerminalToNonCanonical();
    static void    RestoreTerminal();
    static void    PrintConsole(uint16_t u16_Color, string s_Format, ...);
    static bool    CheckConsoleEnterPressed();
    static int     WaitConsoleChar();
    static int64_t GetOsTimestamp();

     OsLibrary();
    ~OsLibrary();
    uint32_t    EnumDevices(bool b_GetCandlelight, vector<kUsbDevice>* pi_Devices);    
    uint32_t    Open(kUsbDevice* pk_Device);
    uint32_t    StartPipes();
    void        Close();
    // USB transfer
    uint32_t    ControlTransfer(kSetup* pk_Setup, void* p_Data, uint32_t* pu32_Transferred);
    uint32_t    ReadPipeIn(uint32_t u32_Timeout, kUsbInPacket* pk_UsbInPacket);
    uint32_t    WritePipeOut(uint8_t* u8_TxData, uint32_t u32_TxLen);
    // -------------------------
    inline bool      IsOpen()        { return mb_IsOpen; } 
    inline bool      HasPipeErrors() { return mu32_RxPipeErrors > 30 || mu32_TxPipeErrors > 30; }
    inline kDevInfo* GetDevInfo()    { return &mk_Info;  }

private:
    static int            GetKeyboardInput();

    static termios        mk_OldTermSettg;
    static bool           mb_OldTermValid;
    
    int                   ReadStringDescriptor(uint8_t u8_StrIndex, string* ps_String);
    
    kDevInfo              mk_Info;            // shared with Candlelight class
    libusb_context*       mpi_UsbContext;
    libusb_device**       mppi_UsbDeviceList;
    libusb_device_handle* mpi_DevHandle;
    bool                  mb_IsOpen;
    uint32_t              mu32_RxPipeErrors;   
    uint32_t              mu32_TxPipeErrors;   
};

}; // namespace

// https://netcult.ch/elmue/CANable Firmware Update

/*
NAMING CONVENTIONS which allow to see the type of a variable immediately without having to jump to the variable declaration:
 
     cName  for class    definitions
     tName  for type     definitions
     eName  for enum     definitions
     kName  for "konstruct" (struct) definitions (letter 's' already used for string)
   delName  for delegate definitions

    b_Name  for bool
    c_Name  for Char, also Color
    d_Name  for double
    e_Name  for enum variables
    f_Name  for function delegates, also float
    i_Name  for instances of classes
    k_Name  for "konstructs" (struct) (letter 's' already used for string)
	r_Name  for Rectangle
    s_Name  for strings
    o_Name  for objects
 
   s8_Name  for   signed  8 Bit (sbyte)
  s16_Name  for   signed 16 Bit (short)
  s32_Name  for   signed 32 Bit (int)
  s64_Name  for   signed 64 Bit (long)
   u8_Name  for unsigned  8 Bit (byte)
  u16_Name  for unsigned 16 bit (ushort)
  u32_Name  for unsigned 32 Bit (uint)
  u64_Name  for unsigned 64 Bit (ulong)

An additional "m" is prefixed for all member variables (e.g. ms_String)
*/

#include "stdafx.h"
#include "CANableDemo.h"
#include "Candlelight.h"
#include <setupapi.h>
#pragma comment(lib, "SetupApi.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// forward declarations
void  CandlelightDemo();
void  DfuDemo();
DWORD OpenDevice(BYTE u8_Interface);

// global instance
Candlelight gi_Candle;

// ---------------------------------------------------------------------------------------------------------------------

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

// Print coloured console output
void PrintConsole(WORD u16_Color, const WCHAR* u16_Format, ...)
{
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), u16_Color); 
    va_list  args;
    va_start(args, u16_Format);
    vwprintf(u16_Format, args);
}

// The user closes the Console window --> close the CANable
BOOL WINAPI ConsoleHandler(DWORD signal) 
{
    if (signal == CTRL_CLOSE_EVENT) 
    {
        gi_Candle.Close();
        return TRUE; // Indicate that the signal was handled
    }
    return FALSE;
}

// ---------------------------------------------------------------------------------------------------------------------

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
	if (!AfxWinInit(GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
	{
		wprintf(L"Fatal Error: MFC initialization failed\n");
        _getch();
        return 0;
	}

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    // Increase console buffer for 3000 lines output with 200 chars per line
    COORD k_Size = {120, 3000}; 
    SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), k_Size);

    COORD k_Max = GetLargestConsoleWindowSize(GetStdHandle(STD_OUTPUT_HANDLE));

    SMALL_RECT k_Wnd = {0}; 
    k_Wnd.Right  = min(k_Max.X, 120) - 4;
    k_Wnd.Bottom = min(k_Max.Y,  60) - 4;
    SetConsoleWindowInfo(GetStdHandle(STD_OUTPUT_HANDLE), TRUE, &k_Wnd);

    // This is required for wprintf() to show umlauts correctly
    setlocale(LC_ALL, "English_United States.1252");

    if (true) 
    {
        // Test interface 0 = Candlelight
        CandlelightDemo();
    }
    else
    {
        // Test interface 1 = Firmware Update
        DfuDemo();
    }

    PrintConsole(GREY, L"\nPress a key to exit ...");
    _getch();
    ExitProcess(0);
}

// ---------------------------------------------------------------------------------------------------------------------

void CandlelightDemo()
{
    PrintConsole(YELLOW, L"=============================================================================\n");
    PrintConsole(YELLOW, L"               CANable 2.5 Candlelight C++ Demo by ElmüSoft                  \n");
    PrintConsole(YELLOW, L"=============================================================================\n");

    DWORD u32_Error = OpenDevice(0);

    // if error is ERROR_INVALID_FIRMWARE, a legacy device is connected --> display data of unsupported device
    if (u32_Error == 0 || u32_Error == ERROR_INVALID_FIRMWARE)
    {
        PrintConsole(GREY, L"USB Endpoint IN:      %02X,  max packet size: %u\n", gi_Candle.mu8_EndpointIN, gi_Candle.mu16_MaxPackSizeIN);
        PrintConsole(GREY, L"USB Endpoint OUT:     %02X,  max packet size: %u\n", gi_Candle.mu8_EndpointOUT,gi_Candle.mu16_MaxPackSizeOUT);

        // If the processor clock is available -> display it
        if (u32_Error && gi_Candle.mk_Capability.fclk_can > 0)
            PrintConsole(GREY, L"CAN Clock:            %u MHz\n", gi_Candle.mk_Capability.fclk_can / 1000000);

        PrintConsole(GREY, L"Hardware Version:     %s\n", gi_Candle.FormatBcdVersion(gi_Candle.mk_DeviceVersion.hw_version_bcd));          
        PrintConsole(GREY, L"Firmware Version:     %s\n", gi_Candle.FormatBcdVersion(gi_Candle.mk_DeviceVersion.sw_version_bcd));
        PrintConsole(GREY, L"Firmware Type:        %s\n", gi_Candle.mb_LegacyFirmware ? L"Legacy" : L"CANable 2.5");
        PrintConsole(GREY, L"Supports CAN FD:      %s\n", gi_Candle.mb_SupportsFD     ? L"Yes"    : L"No");
    }

    if (u32_Error)
    {
        PrintConsole(RED, L"\n%s\n", gi_Candle.FormatLastError(u32_Error));
        return;
    }

    // The board info is only available with the new CANable 2.5 firmware
    PrintConsole(GREY, L"Target Board:         %hs\n", gi_Candle.mk_BoardInfo.BoardName);
    PrintConsole(GREY, L"Processor:            %hs, CAN Clock: %u MHz, DeviceID: 0x%X\n",
                                                       gi_Candle.mk_BoardInfo.McuName,
                                                       gi_Candle.mk_Capability.fclk_can / 1000000,
                                                       gi_Candle.mk_BoardInfo.McuDeviceID);

    // -----------------------------------------

    CString s_Display;
    // Supposed the clock is 160 MHz this will set 500 kBaud and samplepoint 87.5%
    u32_Error = gi_Candle.SetBitrate(false, 2, 139, 20, &s_Display);
    if (u32_Error)
    {
        PrintConsole(RED, L"Error setting nominal bitrate. %s\n", gi_Candle.FormatLastError(u32_Error));
        return;
    }
    PrintConsole(BROWN, L"\nSet %s\n", s_Display);

    // -----------------------------------------

    // Report bus load every 5 seconds if it is not zero.
    u32_Error = gi_Candle.EnableBusLoadReport(5);
    if (u32_Error)
        PrintConsole(RED, L"Error enabling busload report: %s\n", gi_Candle.FormatLastError(u32_Error));

    // -----------------------------------------

    // Optionally you can set a CAN FD data bitrate here.
    // This will automatically enable CAN FD mode. GS_DevFlagCAN_FD is not required.
    if (true)
    {
        // Supposed the clock is 160 MHz this will set 2 MBaud and samplepoint 75.0%
        u32_Error = gi_Candle.SetBitrate(true, 2, 29, 10, &s_Display);
        if (u32_Error)
        {
            PrintConsole(RED, L"Error setting data bitrate. %s\n", gi_Candle.FormatLastError(u32_Error));
            return;
        }
        PrintConsole(BROWN, L"Set %s\n", s_Display);
    }

    // -----------------------------------------

    // optionally you can set filters here.
    if (false)
    {
        // Only the 11 bit CAN ID 0x7E8 will pass through the filter.
        u32_Error = gi_Candle.AddMaskFilter(false, 0x7E8, 0x7FF);
        if (u32_Error)
            PrintConsole(RED, L"Error setting mask filter: %s\n", gi_Candle.FormatLastError(u32_Error));
    }

    // -----------------------------------------

    DWORD u32_DevFlags = GS_DevFlagNone;
    // u32_DevFlags |= GS_DevFlagOneShot;        // turn off automatic re-transmission
    // u32_DevFlags |= GS_DevFlagListenOnly;     // silent mode
    // u32_DevFlags |= GS_DevFlagLoopback;       // loopback mode
    // u32_DevFlags |= ELM_DevFlagDisableTxEcho; // turn off the 'echo' of sent packets

    // If you turn off GS_DevFlagTimestamp, Windows timestamps will be used.
    // Firmware timestamps produce more USB traffic and are not available for sent packets.
    // Read the comment of GetWinTimestamp()
    // u32_DevFlags |= GS_DevFlagTimestamp;

    u32_Error = gi_Candle.Start((eDeviceFlags)u32_DevFlags);
    if (u32_Error)
    {
        PrintConsole(RED, L"%s\n", gi_Candle.FormatLastError(u32_Error));
        return;
    }

    PrintConsole(YELLOW, L"\nThe device has been opened. Please send CAN packets now.\n");
    PrintConsole(YELLOW, L"When a packet is received it is displayed in the console.\n");
    PrintConsole(YELLOW, L"Additionally classic packets with 8 data bytes are sent every 2 seconds.\n");
    PrintConsole(RED,    L"ATTENTION:\n");
    PrintConsole(YELLOW, L"The Windows console is very slow. It cannot display fast CAN bus traffic.\n");
    PrintConsole(YELLOW, L"If you want to test your CANable on a real CAN bus, use HUD ECU Hacker.\n");
    PrintConsole(YELLOW, L"HUD ECU Hacker has an ultra fast speed-optimized trace pane.\n\n");

    PrintConsole(LIME,   L"Lime  = Sent packets\n");
    PrintConsole(GREEN,  L"Green = Echo of sent packets that have been ACKnowledged\n");
    PrintConsole(CYAN,   L"Cyan  = Received packets\n\n");

    PrintConsole(YELLOW, L"Press ENTER to abort and close the device.\n\n");

    DWORD u32_TxID = 0x7E0;
    BYTE u8_TxData[] = { 'E', 'l', 'm', 'u', 'S', 'o', 'f', 't' };

    bool b_BootPin;
    u32_Error = gi_Candle.IsBootPinEnabled(&b_BootPin);
    if (u32_Error)
        PrintConsole(RED,  L"Error %u getting status of pin BOOT0\n", u32_Error);
    else
    {
        PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, gi_Candle.GetWinTimestamp()));
        PrintConsole(WHITE, L" Info");
        PrintConsole(GREY,  L" Pin BOOT0 is %s\n", b_BootPin ? L"enabled" : L"disabled");
    }

    __int64 s64_LastStamp = 0;
    BYTE u8_RxData[RX_FIFO_BUF_SIZE]; // 128 byte

    while (true)
    {
        // Read the comment of GetWinTimestamp()
        __int64 s64_Now = gi_Candle.GetWinTimestamp();

        // Send the Tx frame every 2 seconds (= 2000000 µs)
        if (s64_Now - s64_LastStamp >= 2000000)
        {
            s64_LastStamp = s64_Now;

            __int64 s64_TxStamp; // only valid if no error returned
            CString   s_TxPacket;
            u32_Error = gi_Candle.SendPacket(u32_TxID, false, false, false, false, u8_TxData, sizeof(u8_TxData), &s_TxPacket, &s64_TxStamp);
            if (u32_Error)
            {
                PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, gi_Candle.GetWinTimestamp()));
                PrintConsole(WHITE, L" Send");
                PrintConsole(RED,   L" %s\n", gi_Candle.FormatLastError(u32_Error));
            }
            else
            {
                // Timestamps for sending are only available if Windows timestamps are used
                PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, s64_TxStamp));
                PrintConsole(WHITE, L" Send");
                PrintConsole(LIME,  L" %s\n", s_TxPacket);
            }

            // pseudo random data
            u8_TxData[0] ++;
            u8_TxData[1] = u8_TxData[0] * 2;
            u8_TxData[2] = u8_TxData[1] * 2;
            u8_TxData[3] = u8_TxData[2] * 50;
            u8_TxData[4] = u8_TxData[3] * 13;
            u8_TxData[5] = u8_TxData[4] * 7;
            u8_TxData[6] = u8_TxData[5] * 23;
            u8_TxData[7] = u8_TxData[6] * 19;
        }

        // Check for Rx data
        __int64 s64_WinTimestamp;
        kHeader* pk_Header = (kHeader*)u8_RxData;
        u32_Error = gi_Candle.ReceiveData(100, pk_Header, sizeof(u8_RxData), &s64_WinTimestamp);
        if (u32_Error)
        {
            // Timeout means that no data was received during 100 ms. This is not an error.
            if (u32_Error != ERROR_TIMEOUT)
            {
                // Error from WinUsb_ReadPipe() (e.g. USB device has been disconnected)
                PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, s64_WinTimestamp));
                PrintConsole(WHITE, L" Recv");
                PrintConsole(RED,   L" %s\n", gi_Candle.FormatLastError(u32_Error));
            }
        }
        else // pk_Header is valid
        {
            PrintConsole(GREY, gi_Candle.FormatTimestamp(pk_Header, s64_WinTimestamp));
            switch (pk_Header->msg_type)
            {
                case MSG_RxFrame:
                {
                    CString s_RxPacket = gi_Candle.FormatRxPacket((kRxFrameElmue*)pk_Header);
                    PrintConsole(WHITE, L" Recv");
                    PrintConsole(CYAN,  L" %s\n", s_RxPacket);
                    break;
                }
                case MSG_TxEcho:
                {
                    CString s_EchoPacket = gi_Candle.FormatTxEcho((kTxEchoElmue*)pk_Header);
                    PrintConsole(WHITE, L" Echo");
                    PrintConsole(GREEN, L" %s\n", s_EchoPacket);
                    break;
                }
                case MSG_Error:
                {
                    eErrorBusStatus e_BusStatus;
                    eErrorLevel     e_ErrLevel;
                    CString s_Error = gi_Candle.FormatCanErrors((kErrorElmue*)pk_Header, &e_BusStatus, &e_ErrLevel);
                    WORD u16_Color = GREY;
                    if (e_ErrLevel == LEVEL_Medium) u16_Color = YELLOW;
                    if (e_ErrLevel == LEVEL_High)   u16_Color = RED;
                    PrintConsole(WHITE,     L" Err ");
                    PrintConsole(u16_Color, L" %s\n", s_Error);
                    break;
                }
                case MSG_String:
                {
                    kStringElmue* pk_String = (kStringElmue*)pk_Header;
                    u8_RxData[pk_Header->size] = 0;         // Add zero termination
                    CString s_RxData(pk_String->ascii_msg); // ASCII --> Unicode
                    PrintConsole(WHITE, L" Debg");
                    PrintConsole(GREY,  L" %s\n", s_RxData);
                    break;
                }
                case MSG_Busload:
                {
                    kBusloadElmue* pk_Busload = (kBusloadElmue*)pk_Header;
                    PrintConsole(WHITE, L" Load");
                    PrintConsole(GREY,  L" Busload: %u%%\n", pk_Busload->bus_load);
                    break;
                }
                default:
                {
                    PrintConsole(WHITE, L" Err ");
                    PrintConsole(RED,   L" Unknown USB message received: %s\n", gi_Candle.FormatHexBytes(u8_RxData, pk_Header->size));
                    break;
                }
            }
        }

        // Check if ENTER key has been pressed
        INPUT_RECORD k_Buffer;
        DWORD        u32_Events;
        if (PeekConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &k_Buffer, 1, &u32_Events))
        {
            if (u32_Events > 0)
            {
                // The event must be removed from the input buffer, otherwise it is reported eternally.
                ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &k_Buffer, 1, &u32_Events);  

                if (k_Buffer.EventType == KEY_EVENT  && 
                    k_Buffer.Event.KeyEvent.bKeyDown &&
                    k_Buffer.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)
                {
                    gi_Candle.Close();
                    return;
                }
            }
        }

    } // while (true)
}

// ---------------------------------------------------------------------------------------------------------------------

// ATTENTION:
// This works only if the device is in Candlelight mode.
// If the device is already in DFU mode it will fail.
void DfuDemo()
{
    PrintConsole(YELLOW, L"=============================================================================\n");
    PrintConsole(YELLOW, L"                  CANable 2.5 Enter DFU Demo by ElmüSoft                     \n");
    PrintConsole(YELLOW, L"=============================================================================\n");

    DWORD u32_Error = OpenDevice(1);
    if (u32_Error)
    {
        PrintConsole(RED, L"%s\n", gi_Candle.FormatLastError(u32_Error));
        return;
    }

    u32_Error = gi_Candle.EnterDfuMode();
    if (u32_Error)
    {
        PrintConsole(RED, L"%s\n", gi_Candle.FormatLastError(u32_Error));
        return;
    }

    PrintConsole(LIME, L"Device has been switched successfully into DFU mode.\n");
}

// ---------------------------------------------------------------------------------------------------------------------

DWORD OpenDevice(BYTE u8_Interface)
{
    CStringArray i_DispNames, i_DevicePaths;
    DWORD u32_Error = gi_Candle.EnumDevices(u8_Interface, &i_DispNames, &i_DevicePaths);
    if (u32_Error)
    {
        PrintConsole(RED, L"%s\n", gi_Candle.FormatLastError(u32_Error));
        return u32_Error;
    }

    if (i_DispNames.GetCount() == 0)
    {
        PrintConsole(RED, L"No Candlelight device connected or in DFU mode or WinUSB driver not installed correctly.\n"
                          L"Legacy Candlelight firmware has bugs that prevent the correct driver installation.\n"
                          L"Make sure you have the new CANable 2.5 firmware from ElmüSoft.\n");
        return ERROR_NO_MORE_ITEMS;
    }

    int s32_DeviceIndex = 0;
    if (i_DispNames.GetCount() > 1)
    {
        while (true)
        {
            PrintConsole(LIME, L"\nPlease select one of the devices:\n");

            for (int i=0; i<i_DispNames.GetCount(); i++)
            {
                PrintConsole(GREY, L"%u.) %s\n", i+1, i_DispNames[i]);
            }
            s32_DeviceIndex = _getch() - '1';

            if (s32_DeviceIndex >= 0 && s32_DeviceIndex < i_DispNames.GetCount()) 
                break;
            
            PrintConsole(RED, L"Invalid key!\n");
        }
    }
   
    PrintConsole(GREY, L"\nDevice Path: \"%s\"\n", i_DevicePaths[s32_DeviceIndex]);

    // -----------------------------------------

    u32_Error = gi_Candle.Open(i_DevicePaths[s32_DeviceIndex]);

    // if error is ERROR_INVALID_DEVICE, the device descriptor is valid --> display data of unsupported device
    if (u32_Error == 0 || u32_Error == ERROR_INVALID_DEVICE || u32_Error == ERROR_INVALID_FIRMWARE)
    {
        PrintConsole(GREY, L"USB Vendor:           %s\n",   gi_Candle.ms_Vendor);
        PrintConsole(GREY, L"USB Product:          %s\n",   gi_Candle.ms_Product);
        PrintConsole(GREY, L"USB Serial  Nº:       %s\n",   gi_Candle.ms_Serial);
        PrintConsole(GREY, L"USB Vendor  ID:       %04X\n", gi_Candle.mk_DeviceDescr.idVendor);
        PrintConsole(GREY, L"USB Product ID:       %04X\n", gi_Candle.mk_DeviceDescr.idProduct);
        PrintConsole(GREY, L"USB Device Version:   %s\n",   gi_Candle.FormatBcdVersion(gi_Candle.mk_DeviceDescr.bcdDevice));
    }
    return u32_Error;
}

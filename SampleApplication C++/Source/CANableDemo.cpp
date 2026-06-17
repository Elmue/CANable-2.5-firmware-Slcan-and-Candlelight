
// https://netcult.ch/elmue/CANable%20Firmware%20Update

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

#include "CANableDemo.h"
#include "Candlelight/Candlelight.h"

using namespace CANable;

// true  --> run Candlelight demo (send and receive CAN packets)
// false --> run DFU demo (switch a device in Candlelight mode into DFU mode, fails if already in DFU mode)
bool CANDLELIGHT_DEMO = true; 

// true  --> only packets with 11 bit CAN ID 0x7E8 are sent to the host.
// false --> all packets are sent to the host
bool SET_HOST_FILTERS = false;

// true  --> Received packets with CAN ID 0x7E5 will be forwarded from channel 0 to channel 1 (only multi-channel adapters)
// false --> Do not use brdige mode
bool SET_BRIDGE_FILTERS = false;

// true  --> enable transfer of timestamps from the firmware (deprecated!)
// false --> create performance counter timestamps 
bool HW_TIMESTAMP = false;

// true --> test writing/reading user data to/from flash memory
bool FLASH_MEMORY_TEST = false;

// true --> send 3 Tx packets in one blob
bool SEND_TX_BLOB = false;


// forward declarations
void CandlelightDemo();
void DfuDemo();
bool OpenDevice();
void FlashMemoryTest();
void PrintDeviceMenu(vector<kUsbDevice>& i_Devices);

// global instances
Candlelight gi_Candle;
kDevInfo    gk_Info;
int         gs32_DeviceIndex; // user selection if multiple devices connected

// ---------------------------------------------------------------------------------------------------------------------

int _tmain(int argc, TCHAR* argv[], TCHAR* envp[])
{
    // Increase console buffer for 3000 lines output with 300 chars per line
    // Set console window to 120 chars in 60 lines
    OsLibrary::SetUpConsole(300, 3000, 120, 60, "ElmueSoft Candlelight C++ Demo");

    if (CANDLELIGHT_DEMO) 
    {
        // Test interface 0 = Candlelight
        CandlelightDemo();
    }
    else
    {
        // Test interface 1 = Firmware Update
        DfuDemo();
    }

    gi_Candle.Close(); // Close CAN bus, stop pipe thread

    OsLibrary::PrintConsole(GREY, "\nPress a key to exit ...");
    OsLibrary::WaitConsoleChar();
    ExitProcess(0);
}

// ---------------------------------------------------------------------------------------------------------------------

void CandlelightDemo()
{
    OsLibrary::PrintConsole(YELLOW, "=============================================================================\n");
    OsLibrary::PrintConsole(YELLOW, "               CANable 2.5 Candlelight C++ Demo by ElmueSoft                 \n");
    OsLibrary::PrintConsole(YELLOW, "=============================================================================\n");

    // open Candlelight interface
    if (!OpenDevice())
        return;

    // -----------------------------------------

    // Test flash writing / reading
    if (FLASH_MEMORY_TEST)
        FlashMemoryTest();

    // -----------------------------------------
    
    uint32_t u32_Error = 0;
    string s_Display;

    // Set 500 kBaud and samplepoint 60%
    switch (gk_Info.mk_Capability.fclk_can / 1000000)
    {
        case  60: u32_Error = gi_Candle.SetBitrate(false, 1, 71, 48, &s_Display); break; // STM32G0B1
        case 160: u32_Error = gi_Candle.SetBitrate(false, 2, 95, 64, &s_Display); break; // STM32G431
        default:  OsLibrary::PrintConsole(RED, "CAN Clock not implemented.\n"); return;
    }

    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "Error setting nominal bitrate. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return;
    }
    OsLibrary::PrintConsole(BROWN, "\nSet %s\n", s_Display.c_str());

    // -----------------------------------------

    // Report bus load every 5 seconds if it is not zero.
    u32_Error = gi_Candle.EnableBusLoadReport(5);
    if (u32_Error)
        OsLibrary::PrintConsole(RED, "Error enabling busload report: %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
    
    // -----------------------------------------
    
    gi_Candle.EnableTxEcho(true);

    // -----------------------------------------

    // Optionally you can set a CAN FD data bitrate here.
    // This will automatically enable CAN FD mode. GS_DevFlagCAN_FD is not required.
    if (true)
    {
        // Set 2 MBaud and samplepoint 60%
        switch (gk_Info.mk_Capability.fclk_can / 1000000)
        {
            case  60: u32_Error = gi_Candle.SetBitrate(true, 1, 17, 12, &s_Display); break; // STM32G0B1
            case 160: u32_Error = gi_Candle.SetBitrate(true, 2, 23, 16, &s_Display); break; // STM32G431
            default:  OsLibrary::PrintConsole(RED, "CAN Clock not implemented.\n"); return;
        }

        if (u32_Error)
        {
            OsLibrary::PrintConsole(RED, "Error setting data bitrate. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
            return;
        }
        OsLibrary::PrintConsole(BROWN, "Set %s\n", s_Display.c_str());
    }

    // -----------------------------------------

    // optionally you can set host filters here.
    if (SET_HOST_FILTERS)
    {
        // Only the 11 bit CAN ID 0x7E8 will pass through the filter.
        u32_Error = gi_Candle.AddHostFilter(false, 0x7E8, 0x7FF);
        if (u32_Error)
            OsLibrary::PrintConsole(RED, "Error setting host filter: %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        else
            OsLibrary::PrintConsole(BROWN, "Set host filter 7E8\n");
    }

    // -----------------------------------------

    // The adapter must have at least 2 channels
    if (SET_BRIDGE_FILTERS && 
        gk_Info.mk_DeviceVersion.icount + 1 >= 2 && 
        gk_Info.mu8_Channel == 0)
    {
        // Set filter Nº 08 to forward packets with CAN ID 0x7E5 from channel 0 to channel 1.
        u32_Error = gi_Candle.SetBridgeFilter(8, 1, true, false, false, 0x7E5, 0x7FF);
        if (u32_Error)
            OsLibrary::PrintConsole(RED, "Error setting bridge filter: %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        else
            OsLibrary::PrintConsole(BROWN, "Set bridge filter 7E5\n");
    }

    // -----------------------------------------

    uint32_t u32_DevFlags = GS_DevFlagNone;
    // u32_DevFlags |= GS_DevFlagOneShot;        // turn off automatic re-transmission
    // u32_DevFlags |= GS_DevFlagListenOnly;     // silent mode
    // u32_DevFlags |= GS_DevFlagLoopback;       // loopback mode

    // If you turn off GS_DevFlagTimestamp, Windows timestamps will be used.
    // Firmware timestamps produce more USB traffic and are not available for sent packets.
    // Read the comment of GetOsTimestamp()
    if (HW_TIMESTAMP)
        u32_DevFlags |= GS_DevFlagTimestamp;

    u32_Error = gi_Candle.Start((eDeviceFlags)u32_DevFlags);
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "%s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return;
    }

    OsLibrary::PrintConsole(YELLOW, "\nThe device has been opened. Please send CAN packets now.\n");
    OsLibrary::PrintConsole(YELLOW, "When a packet is received it is displayed in the console.\n");
    OsLibrary::PrintConsole(YELLOW, "Additionally classic packets with 8 data bytes are sent every 2 seconds.\n\n");

#if defined(_MSC_VER)
    OsLibrary::PrintConsole(RED,    "ATTENTION:\n");
    OsLibrary::PrintConsole(YELLOW, "The Windows console is very slow. It cannot display fast CAN bus traffic.\n");
    OsLibrary::PrintConsole(YELLOW, "If you want to test your CANable on a real CAN bus, use HUD ECU Hacker.\n");
    OsLibrary::PrintConsole(YELLOW, "HUD ECU Hacker has an ultra fast speed-optimized CAN Raw Terminal.\n\n");
    OsLibrary::PrintConsole(YELLOW, "A left click into the console stops output, right click continues.\n\n");
#endif

    OsLibrary::PrintConsole(LIME,   "Lime  = Sent packets\n");
    OsLibrary::PrintConsole(GREEN,  "Green = Echo of sent packets that have been ACKnowledged\n");
    OsLibrary::PrintConsole(CYAN,   "Cyan  = Received packets\n\n");

    OsLibrary::PrintConsole(MAGENTA, "Press ENTER to abort. If you only close the console window the adapter stays open.\n\n");

    // -----------------------------------------

    kCanPacket k_TxPackets[3] = {0};

    k_TxPackets[0].mu32_ID     = 0x7E0 + gs32_DeviceIndex;
    k_TxPackets[0].mu8_DataLen = 8;
    memcpy(k_TxPackets[0].mu8_Data, "ElmuSoft", 8);

    k_TxPackets[1].mu32_ID     = k_TxPackets[0].mu32_ID + 1;
    k_TxPackets[1].mu8_DataLen = 8;
    memcpy(k_TxPackets[1].mu8_Data, "TxBlob 2", 8);

    k_TxPackets[2].mu32_ID     = k_TxPackets[0].mu32_ID + 2;
    k_TxPackets[2].mu8_DataLen = 8;
    memcpy(k_TxPackets[2].mu8_Data, "TxBlob 3", 8);

    // -----------------------------------------

    int64_t s64_LastStamp = 0;
    while (true)
    {
        // Read the comment of OsLibrary::GetTimestamp()
        int64_t s64_Now = gi_Candle.GetOsTimestamp();

        // Send the Tx frame every 2 seconds (= 2000000 µs)
        if (s64_Now - s64_LastStamp >= 2000000)
        {
            s64_LastStamp = s64_Now;

            int64_t s64_TxStamp; // only valid if no error returned
            int     s32_PackCount;
            if (SEND_TX_BLOB)    // send blob with 3 packets at once over USB
            {
                u32_Error = gi_Candle.SendPacketBlob(k_TxPackets, 3, &s64_TxStamp);
                s32_PackCount = 3;
            }
            else
            {
                u32_Error = gi_Candle.SendPacket(&k_TxPackets[0], &s64_TxStamp);
                s32_PackCount = 1;
            }

            if (u32_Error)
            {
                OsLibrary::PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, gi_Candle.GetOsTimestamp()));
                OsLibrary::PrintConsole(WHITE, " Send");
                OsLibrary::PrintConsole(RED,   " %s\n", gi_Candle.FormatLastError(u32_Error).c_str());

                if (u32_Error == ERR_TOO_MANY_ERRORS)
                    return; // The CANable has been disconnected
            }
            else
            {
                for (int P=0; P<s32_PackCount; P++)
                {
                    // Timestamps for sending are only available if Windows timestamps are used
                    OsLibrary::PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, s64_TxStamp));
                    OsLibrary::PrintConsole(WHITE, " Send");
                    OsLibrary::PrintConsole(LIME,  " %s", gi_Candle.FormatCanPacket(&k_TxPackets[P]).c_str());

                    if (SEND_TX_BLOB) OsLibrary::PrintConsole(GREY, " (Tx Blob)\n");
                    else              OsLibrary::PrintConsole(GREY, "\n");
                }
            }

            // pseudo random data
            k_TxPackets[0].mu8_Data[0] ++;
            k_TxPackets[0].mu8_Data[1] = k_TxPackets[0].mu8_Data[0] * 3;
            k_TxPackets[0].mu8_Data[2] = k_TxPackets[0].mu8_Data[1] * 2;
            k_TxPackets[0].mu8_Data[3] = k_TxPackets[0].mu8_Data[2] * 51;
            k_TxPackets[0].mu8_Data[4] = k_TxPackets[0].mu8_Data[3] * 11;
            k_TxPackets[0].mu8_Data[5] = k_TxPackets[0].mu8_Data[4] * 7;
            k_TxPackets[0].mu8_Data[6] = k_TxPackets[0].mu8_Data[5] * 25;
            k_TxPackets[0].mu8_Data[7] = k_TxPackets[0].mu8_Data[6] * 17;
        }

        // Check for Rx data
        int64_t  s64_RxTimestamp;
        bool     b_RxBlob;
        kHeader* pk_Header;
        u32_Error = gi_Candle.ReceiveData(100, &pk_Header, &s64_RxTimestamp, &b_RxBlob);
        if (u32_Error)
        {
            // Timeout means that no data was received during 100 ms. This is not an error.
            if (u32_Error != ERR_TIMEOUT)
            {
                // Error is not timeout (e.g. USB device has been disconnected)
                OsLibrary::PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, s64_RxTimestamp));
                OsLibrary::PrintConsole(WHITE, " Recv");
                OsLibrary::PrintConsole(RED,   " %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
            }

            if (u32_Error == ERR_TOO_MANY_ERRORS)
                return; // The CANable has been disconnected
        }
        else // pk_Header is valid
        {
            OsLibrary::PrintConsole(GREY, gi_Candle.FormatTimestamp(pk_Header, s64_RxTimestamp));
            switch (pk_Header->msg_type)
            {
                case MSG_RxFrame:
                {
                    kCanPacket k_RxPacket = gi_Candle.RxFrameToCanPacket((kRxFrameElmue*)pk_Header);
                    OsLibrary::PrintConsole(WHITE, " Recv");
                    OsLibrary::PrintConsole(CYAN,  " %s", gi_Candle.FormatCanPacket(&k_RxPacket).c_str());
                    break;
                }
                case MSG_TxEcho:
                {
                    kCanPacket k_EchoPacket = gi_Candle.GetTxEchoPacket((kTxEchoElmue*)pk_Header);
                    OsLibrary::PrintConsole(WHITE, " Echo");
                    OsLibrary::PrintConsole(GREEN, " %s", gi_Candle.FormatCanPacket(&k_EchoPacket).c_str());
                    break;
                }
                case MSG_Error:
                {
                    eErrorBusStatus e_BusStatus;
                    eErrorLevel     e_ErrLevel;
                    string s_Error = gi_Candle.FormatCanErrors((kErrorElmue*)pk_Header, &e_BusStatus, &e_ErrLevel);
                    uint16_t u16_Color = GREY;
                    if (e_ErrLevel == LEVEL_Medium) u16_Color = YELLOW;
                    if (e_ErrLevel == LEVEL_High)   u16_Color = RED;
                    OsLibrary::PrintConsole(WHITE,     " Err ");
                    OsLibrary::PrintConsole(u16_Color, " %s", s_Error.c_str());
                    break;
                }
                case MSG_String:
                {
                    kStringElmue* pk_String = (kStringElmue*)pk_Header;
                    OsLibrary::PrintConsole(WHITE, " Debg");
                    OsLibrary::PrintConsole(GREY,  " %s", gi_Candle.ConvertStringFrame(pk_String).c_str());
                    break;
                }
                case MSG_Busload:
                {
                    kBusloadElmue* pk_Busload = (kBusloadElmue*)pk_Header;
                    OsLibrary::PrintConsole(WHITE, " Load");
                    OsLibrary::PrintConsole(GREY,  " Busload: %u%%", pk_Busload->bus_load);
                    break;
                }
                default:
                {
                    OsLibrary::PrintConsole(WHITE, " Err ");
                    OsLibrary::PrintConsole(RED,   " Unknown USB message received: %s", cUtils::FormatHexBytes((uint8_t*)pk_Header, pk_Header->size).c_str());
                    break;
                }
            }

            if (b_RxBlob) OsLibrary::PrintConsole(GREY, "   Rx Blob\n");
            else          OsLibrary::PrintConsole(GREY, "\n");
        }

        // exit if the user hits ENTER
        if (OsLibrary::CheckConsoleEnterPressed())
            return;

    } // while (true)
}

// ---------------------------------------------------------------------------------------------------------------------

// ATTENTION:
// This works only if the device is in Candlelight mode.
// If the device is already in DFU mode it will fail.
void DfuDemo()
{
    OsLibrary::PrintConsole(YELLOW, "=============================================================================\n");
    OsLibrary::PrintConsole(YELLOW, "                 CANable 2.5 Enter DFU C++ Demo by ElmüSoft                  \n");
    OsLibrary::PrintConsole(YELLOW, "=============================================================================\n");

    // Open Firmware Update interface
    if (!OpenDevice())
        return;

    uint32_t u32_Error = gi_Candle.EnterDfuMode();
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "\n%s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return;
    }

    OsLibrary::PrintConsole(LIME, "\nDevice has been switched successfully into DFU mode.\n");
}

// ---------------------------------------------------------------------------------------------------------------------

// CANDLELIGHT_DEMO = true  --> Candlelight
// CANDLELIGHT_DEMO = false --> DFU
bool OpenDevice()
{
    vector<kUsbDevice> i_Devices;
    uint32_t u32_Error = OsLibrary::EnumDevices(CANDLELIGHT_DEMO, &i_Devices);
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "Error enumerating USB devices. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return false;
    }

    if (i_Devices.size() == 0)
    {
        OsLibrary::PrintConsole(RED, "\nNo Candlelight device connected or in wrong operatiom mode or WinUSB driver not installed correctly.\n"
                          "Legacy Candlelight firmware has bugs that prevent the correct driver installation.\n"
                          "Make sure you have the new CANable 2.5 firmware from ElmüSoft.\n");
        return false;
    }

    // -----------------------------------------

    gs32_DeviceIndex = 0;
    kUsbDevice* pk_SelectedDevice = NULL;
    if (i_Devices.size() > 1) // 2 or more devices connected
    {
        while (true)
        {
            OsLibrary::PrintConsole(LIME, "\nPlease select one of the devices:");
            OsLibrary::PrintConsole(GREY, "  (Exit with ESCAPE)\n\n");

            PrintDeviceMenu(i_Devices);

            int s32_Char = OsLibrary::WaitConsoleChar();
            if (s32_Char == 27) // ESCAPE key pressed
                return false;

            gs32_DeviceIndex = s32_Char - '1';

            if (gs32_DeviceIndex >= 0 && gs32_DeviceIndex < (int)i_Devices.size()) 
            {
                pk_SelectedDevice = &i_Devices[gs32_DeviceIndex];
                break;
            }
            
            OsLibrary::PrintConsole(RED, "\nInvalid key!\n");
        }
    }

    OsLibrary::PrintConsole(GREY, "\n");

    // -----------------------------------------

    u32_Error = gi_Candle.Open(pk_SelectedDevice);
    gk_Info   = gi_Candle.GetDeviceInfo();

    vector<kDetail> i_Details = gi_Candle.GetDetails();
    for (size_t i=0; i<i_Details.size(); i++)
    {
        OsLibrary::PrintConsole(GREY, "%s\n", i_Details[i].Format(22).c_str());
    }

    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "\n%s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return false;
    }
    return true;
}

// Formatted output for each device: Product - Interface (Serial Number) CAN Channel
void PrintDeviceMenu(vector<kUsbDevice>& i_Devices)
{
    uint32_t u32_ProductLen = 0;
    uint32_t u32_SerialLen  = 0;
    uint32_t u32_InterfLen  = 0;

    for (size_t i=0; i<i_Devices.size(); i++)
    {
        kUsbDevice k_Device = i_Devices[i];
        u32_ProductLen = max(u32_ProductLen, k_Device.ms_Product  .length());
        u32_SerialLen  = max(u32_SerialLen,  k_Device.ms_SerialNo .length());
        u32_InterfLen  = max(u32_InterfLen,  k_Device.ms_Interface.length());
    }

    for (size_t i=0; i<i_Devices.size(); i++)
    {
        kUsbDevice k_Device = i_Devices[i];
        string s_SpaceProduct = string(u32_ProductLen - k_Device.ms_Product  .length(), ' ');
        string s_SpaceSerial  = string(u32_SerialLen  - k_Device.ms_SerialNo .length(), ' ');
        string s_SpaceInterf  = string(u32_InterfLen  - k_Device.ms_Interface.length(), ' ');

        OsLibrary::PrintConsole(WHITE, "%u.) %s%s - %s%s (%s)%s", i+1, 
                                k_Device.ms_Product  .c_str(), s_SpaceProduct.c_str(), 
                                k_Device.ms_Interface.c_str(), s_SpaceInterf .c_str(),
                                k_Device.ms_SerialNo .c_str(), s_SpaceSerial .c_str());

        int s32_Channel = k_Device.GetCanChannel();
        if (s32_Channel > 0) // Firmware Update interfaces have no channels
            OsLibrary::PrintConsole(WHITE, " CAN Channel: %d", s32_Channel);

        OsLibrary::PrintConsole(WHITE, "\n");
    }
}

// ---------------------------------------------------------------------------------------------------------------------

// Write a string and a random into 2 flash segments, then read the data and verify that it is correct.
void FlashMemoryTest()
{
    uint32_t u32_Error;
    uint32_t u32_Read;
    uint8_t  u8_Hello[50];
    uint8_t  u8_FlashData[5000];
    uint8_t  u8_SegmentA = 2;
    uint8_t  u8_SegmentB = 5;

    const char*   s8_Hello = "Hello World of flash data!";
    uint16_t  u16_LenHello = strlen(s8_Hello);
    // ATTENTION: String must be copied to RAM, otherwise ERROR_NOACCESS from WinUSB.
    strcpy_s((char*)u8_Hello, sizeof(u8_Hello), s8_Hello);

    uint64_t u64_Random    = cUtils::GetTickMilli() * 0x815A78F3D;
    uint16_t u16_LenRandom = sizeof(u64_Random);

    // --------------------

    if (u32_Error = gi_Candle.WriteFlash(u8_SegmentA, u8_Hello, u16_LenHello))
        goto _Error;

    if (u32_Error = gi_Candle.WriteFlash(u8_SegmentB, (uint8_t*)&u64_Random, u16_LenRandom))
        goto _Error;

    // --------------------

    if (u32_Error = gi_Candle.ReadFlash(u8_SegmentA, u8_FlashData, sizeof(u8_FlashData), &u32_Read))
        goto _Error;

    if (u32_Read != u16_LenHello || memcmp(u8_Hello, u8_FlashData, u32_Read) != 0)
    {
        OsLibrary::PrintConsole(RED, "\nFlash memory test 1 failed!\n");
        return;
    }

    // --------------------

    if (u32_Error = gi_Candle.ReadFlash(u8_SegmentB, u8_FlashData, sizeof(u8_FlashData), &u32_Read))
        goto _Error;

    if (u32_Read != u16_LenRandom || memcmp(&u64_Random, u8_FlashData, u32_Read) != 0)
    {
        OsLibrary::PrintConsole(RED, "\nFlash memory test 2 failed!\n");
        return;
    }

    // --------------------

    OsLibrary::PrintConsole(LIME, "\nFlash memory test: Success\n");
    return;

_Error:
    OsLibrary::PrintConsole(RED,  "\nFlash memory test Error: %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
}


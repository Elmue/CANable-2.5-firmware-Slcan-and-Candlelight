
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

#include "Candlelight/Candlelight.h"
using namespace CANable;

// ============================================================================================================

// true  --> set data baudrate        -> CAN FD ackets can be sent and received
// false --> do not set data baudrate -> CAN FD ackets cannot be sent and received
const bool ENABLE_CAN_FD      = true;

// true  --> only packets with 11 bit CAN ID 0x7E8 will be sent to the host.
// false --> all packets are sent to the host
const bool SET_HOST_FILTERS   = false;

// true  --> Received packets with CAN ID 0x7E5 will be forwarded from channel 0 to channel 1 (only multi-channel adapters)
// false --> Do not use brdige mode
const bool SET_BRIDGE_FILTERS = false;

// true  --> enable transfer of timestamps from the firmware (deprecated!)
// false --> create performance counter timestamps 
const bool HW_TIMESTAMP       = false;

// ============================================================================================================

// forward declarations
void CandlelightDemo();
void DfuDemo();
bool OpenDevice();
bool ReceiveAndDisplayPackets();
void FlashMemoryTest();
void LoadSlowTxPackets(bool b_Init);
void LoadFastTxPackets(bool b_Init);
void SendSlowTxPackets(int64_t* ps64_LastStamp);
void SendFastTxPackets();
bool TestSelection();
void PrintDeviceMenu(vector<kUsbDevice>& i_Devices);

// enum
enum eDemo
{
    DEMO_Receive    = 0,
    DEMO_SlowSingle,
    DEMO_SlowBlob,
    DEMO_EnterDFU,
    DEMO_FlashRW,
    DEMO_FastBlob,
};

// constants
const int FAST_PACKETS = 25;  // Tx packtes per blob (used for DEMO_FastBlob)
const int FAST_BYTES   = 64;  // Tx bytes per packet (used for DEMO_FastBlob)

// global instances
eDemo       ge_RunDemo;
Candlelight gi_Candle;
kDevInfo    gk_Info;
int         gs32_DeviceIndex; // user selection if multiple devices connected
kCanPacket  gk_TxPackets[FAST_PACKETS] = {};
uint8_t     gu8_TxPacketID = 0;

// ============================================================================================================

int main(int argc, char* argv[])
{
    UNUSED(argc);
    UNUSED(argv);

    // Increase console buffer for 3000 lines output with 300 chars per line
    // Set console window to 120 chars in 60 lines
    OsLibrary::SetUpConsole(300, 3000, 120, 60, "Elm\xC3\xBCSoft Candlelight C++ Demo"); // UTF8 'ü'
    
    // only needed for Linux
    OsLibrary::SwitchTerminalToNonCanonical();

    // loads variable ge_RunDemo
    if (!TestSelection())
        goto _Exit;

    // Print Header
    OsLibrary::ClearConsole();
    OsLibrary::PrintConsole(YELLOW, "=============================================================================\n");
    OsLibrary::PrintConsole(YELLOW, "               CANable 2.5 Candlelight C++ Demo by Elm\xC3\xBCSoft \n"); // UTF8 'ü'
    OsLibrary::PrintConsole(YELLOW, "                            ");
    switch (ge_RunDemo)
    {
        case DEMO_Receive:    OsLibrary::PrintConsole(YELLOW, "Receive Only Demo\n");          break;
        case DEMO_SlowSingle: OsLibrary::PrintConsole(YELLOW, "Slow Tx Single Packet Demo\n"); break;
        case DEMO_SlowBlob:   OsLibrary::PrintConsole(YELLOW, "Slow Tx Blob Packet Demo\n");   break;
        case DEMO_FastBlob:   OsLibrary::PrintConsole(YELLOW, "Fast Tx Blob Packet Demo\n");   break;
        case DEMO_EnterDFU:   OsLibrary::PrintConsole(YELLOW, "Enter DFU Mode Demo\n");        break;
        case DEMO_FlashRW:    OsLibrary::PrintConsole(YELLOW, "Read / Write Flash Demo\n");    break;
    }
    OsLibrary::PrintConsole(YELLOW, "=============================================================================\n");

    // open Candlelight or DFU interface
    if (OpenDevice())
    {
        switch (ge_RunDemo)
        {
            case DEMO_FlashRW:
            {
                // Test flash writing / reading
                FlashMemoryTest();
                break;
            }
            case DEMO_EnterDFU:
            {
                // Test interface 1 = Device Firmware Update
                // This works only if the device is in Candlelight mode.
                // If the device is already in DFU mode it will fail.
                uint32_t u32_Error = gi_Candle.EnterDfuMode();
                if (u32_Error)
                    OsLibrary::PrintConsole(RED, "\nError switching to DFU mode. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
                else
                    OsLibrary::PrintConsole(LIME, "\nDevice has been switched successfully into DFU mode.\n");
                break;
            }
            default:
            {
                // Test interfaces 0, 2, 3 = Candlelight
                CandlelightDemo();

                // This delay is important to give the CANable time to send pending data in the Tx FIFO to CAN bus.
                // If the sending would be aborted by closing the adapter, the other side would report CAN bus Rx errors.
                // If you use a slower baudrate this delay must be increased.
                OsLibrary::Sleep(300);
                break;
            }
        }
    }

    gi_Candle.Close(); // Close CAN bus, stop pipe thread

    _Exit:
    OsLibrary::PrintConsole(GREY, "\nPress a key to exit ...\n");
    OsLibrary::WaitConsoleChar();

    // only needed for Linux
    OsLibrary::RestoreTerminal();   
    return 0;
}

// ============================================================================================================

void CandlelightDemo()
{   
    uint32_t u32_Error = 0;
    string s_Display;

    // Set 500 kBaud and samplepoint 60%
    // Use the smallest possible prescaler!
    // Urgently read: https://netcult.ch/elmue/CANable%20Firmware%20Update
    switch (gk_Info.mk_Capability.fclk_can / 1000000)
    {
        case  60: u32_Error = gi_Candle.SetBitrate(false, 1, 71, 48, &s_Display); break; // STM32G0B1: CAN clock =  60 MHz
        case 160: u32_Error = gi_Candle.SetBitrate(false, 2, 95, 64, &s_Display); break; // STM32G431: CAN cLock = 160 MHz
        default:  OsLibrary::PrintConsole(RED, "CAN Clock not implemented.\n"); return;
    }

    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "Error setting nominal bitrate. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return;
    }
    OsLibrary::PrintConsole(BROWN, "\nSet %s\n", s_Display.c_str());

    // -----------------------------------------

    // Optionally you can set a CAN FD data bitrate here.
    // This will automatically enable CAN FD mode. GS_DevFlagCAN_FD is not required.
    if (ENABLE_CAN_FD)
    {
        // Set 2 MBaud and samplepoint 60%
        // Use the same prescaler as for nominal baudrate!
        // Urgently read: https://netcult.ch/elmue/CANable%20Firmware%20Update
        switch (gk_Info.mk_Capability.fclk_can / 1000000)
        {
            case  60: u32_Error = gi_Candle.SetBitrate(true, 1, 17, 12, &s_Display); break; // STM32G0B1: CAN clock  60 MHZ
            case 160: u32_Error = gi_Candle.SetBitrate(true, 2, 23, 16, &s_Display); break; // STM32G431: CAN clock 160 MHZ
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

    // Report bus load every 5 seconds if it is not zero.
    u32_Error = gi_Candle.EnableBusLoadReport(5);
    if (u32_Error)
        OsLibrary::PrintConsole(RED, "Error enabling busload report: %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
    
    // -----------------------------------------
    
    // Never returns an error
    gi_Candle.EnableTxEcho(true);

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

    // The adapter must have at least 2 channels. Set filter on channel 0
    if (SET_BRIDGE_FILTERS)
    {
        if (gk_Info.mk_DeviceVersion.icount + 1 >= 2 && gk_Info.mu8_Channel == 0)
        {
            // Set filter Nº 08 to forward packets with CAN ID 0x7E5 from channel 0 to channel 1.
            u32_Error = gi_Candle.SetBridgeFilter(8, 1, true, false, false, 0x7E5, 0x7FF);
            if (u32_Error)
                OsLibrary::PrintConsole(RED, "Error setting bridge filter: %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
            else
                OsLibrary::PrintConsole(BROWN, "Set bridge filter 7E5\n");
        }
        else
            OsLibrary::PrintConsole(RED, "The condition to set a bridge filter is not given\n");
    }

    // -----------------------------------------

    uint32_t u32_DevFlags = GS_DevFlagNone;
    // u32_DevFlags |= GS_DevFlagOneShot;    // turn off automatic re-transmission
    // u32_DevFlags |= GS_DevFlagListenOnly; // silent mode
    // u32_DevFlags |= GS_DevFlagLoopback;   // loopback mode

    // If you turn off GS_DevFlagTimestamp, operating system timestamps will be used.
    // Firmware timestamps produce more USB traffic and are not available for sent packets.
    // Read the comment of GetOsTimestamp()
    if (HW_TIMESTAMP)
        u32_DevFlags |= GS_DevFlagTimestamp;

    // Open the adapter, start FDCAN module in the processor
    u32_Error = gi_Candle.Start((eDeviceFlags)u32_DevFlags);
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "%s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return;
    }

#if defined(_MSC_VER)
    OsLibrary::PrintConsole(RED,    "ATTENTION:\n");
    OsLibrary::PrintConsole(YELLOW, "The Windows console is very slow. It cannot display fast CAN bus traffic.\n");
    OsLibrary::PrintConsole(YELLOW, "If you want to test your CANable on a real CAN bus, use HUD ECU Hacker.\n");
    OsLibrary::PrintConsole(YELLOW, "HUD ECU Hacker has an ultra fast speed-optimized CAN Raw Terminal.\n\n");
    OsLibrary::PrintConsole(YELLOW, "A left click into the Windows console stops output, right click continues.\n\n");
#endif

    OsLibrary::PrintConsole(LIME,   "Lime  = Sent packets\n");
    OsLibrary::PrintConsole(GREEN,  "Green = Echo of sent packets that have been ACKnowledged\n");
    OsLibrary::PrintConsole(CYAN,   "Cyan  = Received packets\n\n");

    OsLibrary::PrintConsole(MAGENTA, "Press ENTER to abort. If you only close the console window the adapter stays open.\n\n");

    // -----------------------------------------

    if (ge_RunDemo == DEMO_FastBlob) LoadFastTxPackets(true);
    else                             LoadSlowTxPackets(true);

    int64_t s64_LastStamp = OsLibrary::GetOsTimestamp();
    while (true)
    {
        if (!ReceiveAndDisplayPackets())
            break;

        if (ge_RunDemo == DEMO_FastBlob) 
            SendFastTxPackets();
        else if (ge_RunDemo != DEMO_Receive)
            SendSlowTxPackets(&s64_LastStamp);

        // exit if the user hits ENTER
        if (OsLibrary::CheckConsoleEnterPressed())
            break;
    }
}

// ============================================================================================================

// Load gk_TxPackets with 3 packets with 8 data bytes
void LoadSlowTxPackets(bool b_Init)
{
    if (b_Init)
    {
        // IMPORTANT: Each connected USB adapter must use it's own ID, otherwise CAN errors when sending!
        gk_TxPackets[0].mu32_ID     = 0x7E0 + gs32_DeviceIndex; 
        gk_TxPackets[0].mu8_DataLen = 8;
        memcpy(gk_TxPackets[0].mu8_Data, "ElmuSoft", 8);

        gk_TxPackets[1].mu32_ID     = gk_TxPackets[0].mu32_ID;
        gk_TxPackets[1].mu8_DataLen = 8;
        memcpy(gk_TxPackets[1].mu8_Data, "TxBlob 2", 8);

        gk_TxPackets[2].mu32_ID     = gk_TxPackets[0].mu32_ID;
        gk_TxPackets[2].mu8_DataLen = 8;
        memcpy(gk_TxPackets[2].mu8_Data, "TxBlob 3", 8);
    }
    else
    {
        // increment the first byte in each packet which is a counter
        gk_TxPackets[0].mu8_Data[0] = ++ gu8_TxPacketID;
        gk_TxPackets[1].mu8_Data[0] = gu8_TxPacketID + 0x10;
        gk_TxPackets[2].mu8_Data[0] = gu8_TxPacketID + 0x20;
    }
}

// Load gk_TxPackets with FAST_PACKETS packets with FAST_BYTES data bytes
void LoadFastTxPackets(bool b_Init)
{
    for (int P=0; P<FAST_PACKETS; P++)
    {
        if (b_Init)
        {
            // IMPORTANT: Each connected USB adapter must use it's own ID, otherwise CAN errors when sending!
            gk_TxPackets[P].mu32_ID     = 0x500 + gs32_DeviceIndex; 
            gk_TxPackets[P].mu8_DataLen = FAST_BYTES;

            for (int B=0; B<FAST_BYTES; B++)
            {
                gk_TxPackets[P].mu8_Data[B] = (uint8_t)(P + B);
            }
        }

        // increment the first byte in each packet which is a counter
        gk_TxPackets[P].mu8_Data[0] = gu8_TxPacketID ++;
    }
}

// ============================================================================================================

void SendSlowTxPackets(int64_t* ps64_LastStamp)
{
    // Read the comment of OsLibrary::GetOsTimestamp()
    int64_t s64_Now = OsLibrary::GetOsTimestamp();

    // Send the Tx frame every 2 seconds (= 2000000 µs)
    if (s64_Now - *ps64_LastStamp < 2000000)
        return;
    
    *ps64_LastStamp = s64_Now;

    uint32_t u32_Error;
    int64_t  s64_TxStamp; // only valid if no error returned
    int      s32_PackCount;
    if (ge_RunDemo == DEMO_SlowBlob) // send blob with 3 packets at once over USB
    {
        u32_Error = gi_Candle.SendPacketBlob(gk_TxPackets, 3, &s64_TxStamp);
        s32_PackCount = 3;
    }
    else // send a single packet
    {
        u32_Error = gi_Candle.SendPacket(&gk_TxPackets[0], &s64_TxStamp);
        s32_PackCount = 1;
    }

    if (u32_Error)
    {
        OsLibrary::PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, OsLibrary::GetOsTimestamp()));
        OsLibrary::PrintConsole(WHITE, " Send");
        OsLibrary::PrintConsole(RED,   " %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
    }
    else
    {
        for (int P=0; P<s32_PackCount; P++)
        {
            // Timestamps for sending are only available if Windows timestamps are used
            OsLibrary::PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, s64_TxStamp));
            OsLibrary::PrintConsole(WHITE, " Send");
            OsLibrary::PrintConsole(LIME,  " %s", gi_Candle.FormatCanPacket(&gk_TxPackets[P]).c_str());

            if (ge_RunDemo == DEMO_SlowBlob) OsLibrary::PrintConsole(GREY, "  Tx Blob\n");
            else                             OsLibrary::PrintConsole(GREY, "\n");
        }
    }
    LoadSlowTxPackets(false);
}

// Send a blob with FAST_PACKETS packets with FAST_BYTES data bytes at maximum CAN bus load
void SendFastTxPackets()
{
    // Read the comment of CalculateTxFifoFreeSlots()
    int s32_Available;
    uint32_t u32_Error = gi_Candle.CalculateTxFifoFreeSlots(&s32_Available);
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "Tx Echo must be enabled\n");
        return;
    }

    // not enough free slots in the firmware
    if (s32_Available <= FAST_PACKETS)
        return;

    int64_t s64_TxStamp;
    u32_Error = gi_Candle.SendPacketBlob(gk_TxPackets, FAST_PACKETS, &s64_TxStamp);

    OsLibrary::PrintConsole(GREY, gi_Candle.FormatTimestamp(NULL, s64_TxStamp));
    OsLibrary::PrintConsole(WHITE, " Send");
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, " %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
    }
    else
    {
        OsLibrary::PrintConsole(LIME, " %X: %d packets with %d bytes from 0x%02X to 0x%02X", 
                                        gk_TxPackets[0].mu32_ID, FAST_PACKETS, FAST_BYTES,
                                        (uint8_t)(gu8_TxPacketID - FAST_PACKETS), 
                                        (uint8_t)(gu8_TxPacketID - 1));
        OsLibrary::PrintConsole(GREY, "  Tx Blob\n");
    }
    LoadFastTxPackets(false);
}

// ============================================================================================================

// returns false to abort the demo when the adapter has been disconnected
bool ReceiveAndDisplayPackets()
{
    int64_t  s64_RxTimestamp;
    bool     b_RxBlob;
    kHeader* pk_Header;

    for (int L=0; L<FAST_PACKETS; L++)
    {
        // Wait once for a packet, then only get packets that are in the buffer in OsLibrary.
        int s32_Timeout = (L == 0) ? 100 : 0;

        // Check for Rx data
        uint32_t u32_Error = gi_Candle.ReceiveData(s32_Timeout, &pk_Header, &s64_RxTimestamp, &b_RxBlob);
        if (u32_Error)
        {
            // Timeout means that no data was received. This is not an error.
            if (u32_Error == ERR_TIMEOUT)
                return true;
            
            // Error is not timeout (e.g. USB device has been disconnected)
            OsLibrary::PrintConsole(GREY,  gi_Candle.FormatTimestamp(NULL, s64_RxTimestamp));
            OsLibrary::PrintConsole(WHITE, " Recv");
            OsLibrary::PrintConsole(RED,   " %s\n", gi_Candle.FormatLastError(u32_Error).c_str());

            // The CANable has been disconnected --> return false
            return (u32_Error != ERR_TOO_MANY_ERRORS);
        }

        #if defined(_MSC_VER)
            // The Windows console is too slow to print all the Tx echo packets at maximum busload
            if (ge_RunDemo == DEMO_FastBlob && pk_Header->msg_type == MSG_TxEcho)
                continue;
        #endif

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
                OsLibrary::PrintConsole(WHITE, " Echo");
                kCanPacket k_EchoPacket;
                if (!gi_Candle.GetTxEchoPacket((kTxEchoElmue*)pk_Header, &k_EchoPacket))
                    OsLibrary::PrintConsole(RED,   " Invalid echo marker received");
                else
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
        } // switch

        if (b_RxBlob) OsLibrary::PrintConsole(GREY, "  Rx Blob\n");
        else          OsLibrary::PrintConsole(GREY, "\n");
    } // for
    return true;
}

// ============================================================================================================

// Write a string and a 64 bit random into 2 flash segments, then read the data and verify that it is correct.
void FlashMemoryTest()
{
    OsLibrary::PrintConsole(YELLOW, "\nTest 1: Write string \"Hello C++ World of flash data!\" to flash segment 2\n");
    OsLibrary::PrintConsole(YELLOW, "Test 2: Write 8 random bytes to flash segment 5\n");

    uint32_t u32_Error;
    uint32_t u32_Read;
    uint8_t  u8_FlashData[4096];
    uint8_t  u8_SegmentA = 2;
    uint8_t  u8_SegmentB = 5;

    const char* s8_Hello   = "Hello C++ World of flash data!";
    uint16_t  u16_LenHello = strlen(s8_Hello);

    uint64_t u64_Random    = cUtils::GetTickMilli() * 0x815A78F3D;
    uint16_t u16_LenRandom = sizeof(u64_Random);

    // --------------------

    if ((u32_Error = gi_Candle.WriteFlash(u8_SegmentA, (uint8_t*)s8_Hello, u16_LenHello)))
        goto _Error;

    if ((u32_Error = gi_Candle.WriteFlash(u8_SegmentB, (uint8_t*)&u64_Random, u16_LenRandom)))
        goto _Error;

    // --------------------

    if ((u32_Error = gi_Candle.ReadFlash(u8_SegmentA, u8_FlashData, sizeof(u8_FlashData), &u32_Read)))
        goto _Error;

    if (u32_Read != u16_LenHello || memcmp(s8_Hello, u8_FlashData, u32_Read) != 0)
    {
        OsLibrary::PrintConsole(RED, "\nFlash memory test 1 failed!\n");
        return;
    }

    // --------------------

    if ((u32_Error = gi_Candle.ReadFlash(u8_SegmentB, u8_FlashData, sizeof(u8_FlashData), &u32_Read)))
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

// ============================================================================================================

// Candlelight interface or DFU interface
bool OpenDevice()
{
    vector<kUsbDevice> i_Devices;
    uint32_t u32_Error = gi_Candle.EnumDevices(ge_RunDemo != DEMO_EnterDFU, &i_Devices);
    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "Error enumerating USB devices. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return false;
    }

    if (i_Devices.size() == 0)
    {
        OsLibrary::PrintConsole(RED, "\nNo Candlelight device connected or in wrong operatiom mode or driver not installed correctly.\n"
                                     "Legacy Candlelight firmware has bugs that prevent the correct driver installation.\n"
                                     "Make sure you have the new CANable 2.5 firmware from Elm\xC3\xBCSoft.\n"); // UTF8 'ü'
        return false;
    }

    // -----------------------------------------

    gs32_DeviceIndex = 0;
    if (i_Devices.size() == 1)
    {
        PrintDeviceMenu(i_Devices);
    }
    else // Two or more devices connected
    {
        while (true)
        {
            PrintDeviceMenu(i_Devices);

            OsLibrary::PrintConsole(LIME, "\nPlease select the adapter.");
            OsLibrary::PrintConsole(GREY, "  (Exit with ESCAPE)\n\n");

            int s32_Char = OsLibrary::WaitConsoleChar();
            if (s32_Char == 27) // ESCAPE key pressed
                return false;

            gs32_DeviceIndex = s32_Char - '1';

            if (gs32_DeviceIndex >= 0 && gs32_DeviceIndex < (int)i_Devices.size()) 
                break;
            
            OsLibrary::PrintConsole(RED, "\nInvalid key!\n");
        }
    }

    OsLibrary::PrintConsole(GREY, "\n");

    // -----------------------------------------

    u32_Error = gi_Candle.Open(&i_Devices[gs32_DeviceIndex]);

    // Even after an error some of the device details may be valid --> always print
    vector<kDetail> i_Details = gi_Candle.GetDetails();
    for (size_t i=0; i<i_Details.size(); i++)
    {
        OsLibrary::PrintConsole(GREY, "%s\n", i_Details[i].Format(22).c_str());
    }

    if (u32_Error)
    {
        OsLibrary::PrintConsole(RED, "\nError opening device. %s\n", gi_Candle.FormatLastError(u32_Error).c_str());
        return false;
    }
    
    gk_Info = gi_Candle.GetDeviceInfo();
    return true;
}

// ============================================================================================================

bool TestSelection()
{
    OsLibrary::ClearConsole();
    while (true)
    {
        OsLibrary::PrintConsole(WHITE,  "\nA.) Receive Only Demo\n");
        OsLibrary::PrintConsole(YELLOW, "    Display all received CAN traffic and send nothing.\n");
        OsLibrary::PrintConsole(WHITE,  "B.) Slow Tx Single Demo\n");
        OsLibrary::PrintConsole(YELLOW, "    Send one CAN packet with 8 data bytes every 2 seconds and display all received CAN traffic.\n");
        OsLibrary::PrintConsole(WHITE,  "C.) Slow Tx Blob Demo\n");
        OsLibrary::PrintConsole(YELLOW, "    Send a Tx blob with 3 CAN packets with 8 data bytes every 2 seconds and display all received CAN traffic.\n");
        OsLibrary::PrintConsole(WHITE,  "D.) Enter DFU Demo\n");
        OsLibrary::PrintConsole(YELLOW, "    Switch the adapter into DFU mode. This fails if already in DFU mode.\n");
        OsLibrary::PrintConsole(WHITE,  "E.) Flash Write / Read Demo\n");
        OsLibrary::PrintConsole(YELLOW, "    Write user data to the flash memory of the CANable, read it back and verify correct operation.\n");
        OsLibrary::PrintConsole(WHITE,  "F.) 100%% Bus Load Demo\n");
        OsLibrary::PrintConsole(YELLOW, "    Send %d packets with %d bytes in a blob to the adapter to generate maximum CAN traffic.\n", FAST_PACKETS, FAST_BYTES);
        OsLibrary::PrintConsole(RED,    "    IMPORTANT:\n");
        OsLibrary::PrintConsole(YELLOW, "    Do NOT run this demo against another side which also sends CAN packets.\n");
        OsLibrary::PrintConsole(YELLOW, "    When one adapter occupies CAN bus with 100%% busload the other side has no chance to send a packet.\n");
        OsLibrary::PrintConsole(YELLOW, "    The side with the higher CAN ID will always lose arbitration and you see Tx Timeout errors.\n");
        OsLibrary::PrintConsole(YELLOW, "    This demo has been designed to send only unidirectional high speed traffic.\n");
        OsLibrary::PrintConsole(YELLOW, "    On Linux run this demo against the \"Receive Only Demo\" on the other side.\n");
        OsLibrary::PrintConsole(YELLOW, "    On Windows the console is too slow to display the CAN traffic generated by this demo.\n");
        OsLibrary::PrintConsole(YELLOW, "    If you use the Windows console don't be surprised to see errors \"Polling is too slow\".\n");
        OsLibrary::PrintConsole(YELLOW, "    I recommend to use the HUD ECU Hacker CAN Raw Terminal as packet receiver on the other side.\n");
        OsLibrary::PrintConsole(YELLOW, "    HUD ECU Hacker has an ultra fast Trace pane which is able of displaying CAN FD traffic at maximum speed.\n");
        OsLibrary::PrintConsole(LIME,   "\nPlease select the test to execute.");
        OsLibrary::PrintConsole(GREY,   "  (Exit with ESCAPE)\n\n");
    
        int s32_Char = OsLibrary::WaitConsoleChar();
        if (s32_Char == 27) // ESCAPE key pressed
            return false;

        if (s32_Char >= 'a')   
            s32_Char -= 32; // make upper case

        if (s32_Char >= 'A' && s32_Char <= 'F') 
        {
            ge_RunDemo = (eDemo)(s32_Char - 'A');
            return true;
        }
            
        OsLibrary::PrintConsole(RED, "\nInvalid key!\n");
    }
}

// ============================================================================================================

// Formatted output for each device: Product - Interface (Serial Number) CAN Channel
void PrintDeviceMenu(vector<kUsbDevice>& i_Devices)
{
    OsLibrary::PrintConsole(WHITE, "\n");

    uint32_t u32_ProductLen = 0;
    uint32_t u32_SerialLen  = 0;
    uint32_t u32_InterfLen  = 0;

    for (size_t i=0; i<i_Devices.size(); i++)
    {
        kUsbDevice k_Device = i_Devices[i];
        u32_ProductLen = max(u32_ProductLen, (uint32_t)k_Device.ms_Product  .length());
        u32_SerialLen  = max(u32_SerialLen,  (uint32_t)k_Device.ms_SerialNo .length());
        u32_InterfLen  = max(u32_InterfLen,  (uint32_t)k_Device.ms_Interface.length());
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


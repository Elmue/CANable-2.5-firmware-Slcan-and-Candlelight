
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

using System;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Text;
using System.Threading;

using WinUSB              = CANable.WinUSB;
using cUsbDevice          = CANable.SetupApi.cUsbDevice;
using SetupApi            = CANable.SetupApi;
using Candlelight         = CANable.Candlelight;
using CanPacket           = CANable.Candlelight.CanPacket;
using kDevInfo            = CANable.Candlelight.kDevInfo;
using cHeader             = CANable.Candlelight.cHeader;
using eDeviceFlags        = CANable.Candlelight.eDeviceFlags;
using eMessageType        = CANable.Candlelight.eMessageType;
using eBusStatus          = CANable.Candlelight.eBusStatus;
using eErrorLevel         = CANable.Candlelight.eErrorLevel;
using cTxEchoElmue        = CANable.Candlelight.cTxEchoElmue;
using cRxFrameElmue       = CANable.Candlelight.cRxFrameElmue;
using cErrorElmue         = CANable.Candlelight.cErrorElmue;
using cStringElmue        = CANable.Candlelight.cStringElmue;
using cBusloadElmue       = CANable.Candlelight.cBusloadElmue;
using cDetail             = CANable.Candlelight.cDetail;
using AbortException      = CANable.Candlelight.AbortException;
using Utils               = CANable.Utils;
using INPUT_KEY_RECORD    = CANable.Utils.INPUT_KEY_RECORD;

namespace CandlelightDemo
{
class Program
{
    // true  --> set data baudrate        -> CAN FD ackets can be sent and received
    // false --> do not set data baudrate -> CAN FD ackets cannot be sent and received
    static bool ENABLE_CAN_FD      = true;

    // true  --> only packets with the 11 bit CAN ID 0x7E8 will be sent to the host.
    // false --> all packets are sent to the host
    static bool SET_HOST_FILTERS   = false;

    // true  --> Received packets with CAN ID 0x7E5 will be forwarded from channel 0 to channel 1 (only multi-channel adapters)
    // false --> Do not use brdige mode
    static bool SET_BRIDGE_FILTERS = false;

    // true  --> enable transfer of timestamps from the firmware (deprecated!)
    // false --> create performance counter timestamps 
    static bool HW_TIMESTAMP       = false;

    // ============================================================================================================

    // enums
    enum eDemo
    {
        Receive = 0,
        SlowSingle,
        SlowBlob,
        EnterDFU,
        FlashRW,
        FastBlob,
    }

    // constants
    const int FAST_PACKETS = 25;  // Tx packtes per blob (used for eDemo.FastBlob)
    const int FAST_BYTES   = 64;  // Tx bytes per packet (used for eDemo.FastBlob)

    // class members
    static eDemo       ge_RunDemo;
    static Candlelight mi_Candle = new Candlelight();
    static kDevInfo    mk_Info;
    static int         ms32_DeviceIndex; // user selection if multiple devices connected
    static CanPacket[] mk_TxPackets;
    static Byte        mu8_TxPacketID = 0;

    static void Main(string[] args)
    {
        Console.SetBufferSize(300, 3000);
            
        int s32_Width  = Math.Min(Console.LargestWindowWidth, 120) - 4;
        int s32_Height = Math.Min(Console.LargestWindowHeight, 60) - 4;
        Console.SetWindowSize(s32_Width, s32_Height);

        Console.Title = "ElmüSoft Candlelight C# Demo";

        // loads variable ge_RunDemo
        if (!TestSelection())
            goto _Exit;

        // Print Header
        Console.Clear();
        Print(ConsoleColor.Yellow, "=============================================================================\n");
        Print(ConsoleColor.Yellow, "                CANable 2.5 Candlelight C# Demo by ElmüSoft                  \n");
        Print(ConsoleColor.Yellow, "                            ");
        switch (ge_RunDemo)
        {
            case eDemo.Receive:    Print(ConsoleColor.Yellow, "Receive Only Demo\n");       break;
            case eDemo.SlowSingle: Print(ConsoleColor.Yellow, "Slow Tx Single Packet Demo\n"); break;
            case eDemo.SlowBlob:   Print(ConsoleColor.Yellow, "Slow Tx Blob Packet Demo\n");   break;
            case eDemo.FastBlob:   Print(ConsoleColor.Yellow, "Fast Tx Blob Packet Demo\n");   break;
            case eDemo.EnterDFU:   Print(ConsoleColor.Yellow, "Enter DFU Mode Demo\n");     break;
            case eDemo.FlashRW:    Print(ConsoleColor.Yellow, "Read / Write Flash Demo\n"); break;
        }
        Print(ConsoleColor.Yellow, "=============================================================================\n");

        // open Candlelight or DFU interface
        if (OpenDevice())
        {
            switch (ge_RunDemo)
            {
                case eDemo.FlashRW:
                    // Test flash writing / reading
                    FlashMemoryTest();
                    break;

                case eDemo.EnterDFU:
                    try
                    {
                        // Test interface 1 = Device Firmware Update
                        // This works only if the device is in Candlelight mode.
                        // If the device is already in DFU mode it will fail.
                        mi_Candle.EnterDfuMode();
                        Print(ConsoleColor.Green, "\nDevice has been switched successfully into DFU mode.\n");
                    }
                    catch (Exception Ex)
                    {
                        Print(ConsoleColor.Red, "\nError switching to DFU mode. {0}\n", Ex.Message);
                        #if DEBUG
                            Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
                        #endif
                    }
                    break;
            
                default:
                    // Test interfaces 0, 2, 3 = Candlelight
                    CandlelightDemo();

                    // This delay is important to give the CANable time to send pending data in the Tx FIFO to CAN bus.
                    // If the sending would be aborted by closing the adapter, the other side would report CAN bus Rx errors.
                    // If you use a slower baudrate this delay must be increased.
                    Thread.Sleep(300);
                    break;
            }
        }
        
        mi_Candle.Dispose(); // Always disconnect from CAN bus, stop pipe thread

        _Exit:
        Print(ConsoleColor.Gray, "\nPress a key to exit ...\n");
        Console.ReadKey();

        Process.GetCurrentProcess().Kill();
    }

    static void CandlelightDemo()
    {
        String s_Action = "";
        try
        {
            s_Action = "Error setting nominal bitrate.";
            String s_Display;

            // Set 500 kBaud and samplepoint 60%
            // Use the smallest possible prescaler!
            // Urgently read: https://netcult.ch/elmue/CANable%20Firmware%20Update
            switch (mk_Info.mk_Capability.ms32_CanClock / 1000000)
            {
                case  60: mi_Candle.SetBitrate(false, 1, 71, 48, out s_Display); break; // STM32G0B1: CAN clock  60 MHZ
                case 160: mi_Candle.SetBitrate(false, 2, 95, 64, out s_Display); break; // STM32G431: CAN clock 160 MHZ
                default: throw new Exception("CAN Clock not implemented!");
            }

            Print(ConsoleColor.DarkYellow, "\nSet {0}\n", s_Display);

            // -----------------------------------------

            // Optionally you can set a CAN FD data bitrate here.
            // This will automatically enable CAN FD mode. GS_DevFlagCAN_FD is not required.
            if (ENABLE_CAN_FD)
            {
                s_Action = "Error setting data bitrate.";

                // Set 2 MBaud and samplepoint 60%
                // Use the same prescaler as for nominal baudrate!
                // Urgently read: https://netcult.ch/elmue/CANable%20Firmware%20Update
                switch (mk_Info.mk_Capability.ms32_CanClock / 1000000)
                {
                    case  60: mi_Candle.SetBitrate(true, 1, 17, 12, out s_Display); break; // STM32G0B1: CAN clock  60 MHZ
                    case 160: mi_Candle.SetBitrate(true, 2, 23, 16, out s_Display); break; // STM32G431: CAN clock 160 MHZ
                    default: throw new Exception("CAN Clock not implemented!");
                }

                Print(ConsoleColor.DarkYellow, "Set {0}\n", s_Display);
            }

            // -----------------------------------------

            s_Action = "Error enabling busload report.";

            // Report bus load every 5 seconds if it is not zero.
            mi_Candle.EnableBusLoadReport(5);
            
            // -----------------------------------------
            
            // Never throws an exception
            mi_Candle.EnableTxEcho(true);           

            // -----------------------------------------

            // optionally you can set host filters here.
            if (SET_HOST_FILTERS)
            {
                s_Action = "Error setting host filter.";

                // Only the 11 bit CAN ID 0x7E8 will pass through the filter.
                mi_Candle.AddHostFilter(false, 0x7E8, 0x7FF);

                Print(ConsoleColor.DarkYellow, "Set host filter 7E8\n");
            }

            // -----------------------------------------

            // The adapter must have at least 2 channels. Set filter on channel 0
            if (SET_BRIDGE_FILTERS)
            {
                if (mk_Info.mk_DeviceVersion.ChannelCount >= 2 && mk_Info.mu8_Channel == 0)
                {
                    s_Action = "Error setting bridge filter.";

                    // Set filter Nº 09 to forward packets with CAN ID 0x7E5 from channel 0 to channel 1.
                    mi_Candle.SetBridgeFilter(9, 1, true, false, false, 0x7E5, 0x7FF);

                    Print(ConsoleColor.DarkYellow, "Set bridge filter 7E5\n");
                }
                else
                    Print(ConsoleColor.Red, "The condition to set a bridge filter is not given\n");
            }

            // -----------------------------------------

            s_Action = "Error starting CAN bus.";

            eDeviceFlags e_DevFlags = eDeviceFlags.None;
            // e_DevFlags |= eDeviceFlags.OneShot;    // turn off automatic re-transmission
            // e_DevFlags |= eDeviceFlags.ListenOnly; // silent mode
            // e_DevFlags |= eDeviceFlags.Loopback;   // loopback mode

            // If you turn off eDeviceFlags.Timestamp, operating system timestamps will be used.
            // Firmware timestamps produce more USB traffic and are not available for sent packets.
            // Read the comment of GetOsTimestamp()
            if (HW_TIMESTAMP)
                e_DevFlags |= eDeviceFlags.HwTimestamp;

            // Open the adapter, start FDCAN module in the processor
            mi_Candle.Start(e_DevFlags);

            Print(ConsoleColor.Red,    "ATTENTION:\n");
            Print(ConsoleColor.Yellow, "The Windows console is very slow. It cannot display fast CAN bus traffic.\n");
            Print(ConsoleColor.Yellow, "If you want to test your CANable on a real CAN bus, use HUD ECU Hacker.\n");
            Print(ConsoleColor.Yellow, "HUD ECU Hacker has an ultra fast speed-optimized CAN Raw Terminal.\n\n");

            Print(ConsoleColor.Yellow, "A left click into the Windows console stops output, right click continues.\n\n");

            Print(ConsoleColor.Green,     "Lime  = Sent packets\n");
            Print(ConsoleColor.DarkGreen, "Green = Echo of sent packets that have been ACKnowledged\n");
            Print(ConsoleColor.Cyan,      "Cyan  = Received packets\n\n");

            Print(ConsoleColor.Magenta, "Press ENTER to abort. If you only close the console window the adapter stays open.\n\n");
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Red, "{0} {1}\n", s_Action, Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
            #endif
            return;
        }

        // -----------------------------------------

        if (ge_RunDemo == eDemo.FastBlob) LoadFastTxPackets(true);
        else                              LoadSlowTxPackets(true);

        Int64 s64_LastStamp = Utils.GetOsTimestamp();
        while (true)
        {
            if (!ReceiveAndDisplayPackets())
                break;

            if (ge_RunDemo == eDemo.FastBlob) 
                SendFastTxPackets();
            else if (ge_RunDemo != eDemo.Receive)
                SendSlowTxPackets(ref s64_LastStamp);

            // exit if the user hits ENTER
            if (Utils.CheckConsoleEnterPressed())
                break;
        }
    }

    // ============================================================================================================

    // Load mk_TxPackets with 3 packets with 8 data bytes
    static void LoadSlowTxPackets(bool b_Init)
    {
        if (b_Init)
        {
            mk_TxPackets = new CanPacket[3];

            // IMPORTANT: Each connected USB adapter must use it's own ID, otherwise CAN errors when sending!
            mk_TxPackets[0] = new CanPacket();
            mk_TxPackets[0].ms32_ID = 0x7E0 + ms32_DeviceIndex; 
            mk_TxPackets[0].mi_Data.AddRange(Encoding.ASCII.GetBytes("ElmuSoft"));

            mk_TxPackets[1] = new CanPacket();
            mk_TxPackets[1].ms32_ID = mk_TxPackets[0].ms32_ID;
            mk_TxPackets[1].mi_Data.AddRange(Encoding.ASCII.GetBytes("TxBlob 2"));

            mk_TxPackets[2] = new CanPacket();
            mk_TxPackets[2].ms32_ID = mk_TxPackets[0].ms32_ID;
            mk_TxPackets[2].mi_Data.AddRange(Encoding.ASCII.GetBytes("TxBlob 3"));
        }
        else
        {
            // increment the first byte in each packet which is a counter
            mk_TxPackets[0].mi_Data[0] = ++ mu8_TxPacketID;
            mk_TxPackets[1].mi_Data[0] = (Byte)(mu8_TxPacketID + 0x10);
            mk_TxPackets[2].mi_Data[0] = (Byte)(mu8_TxPacketID + 0x20);
        }
    }

    // Load mk_TxPackets with FAST_PACKETS packets with FAST_BYTES data bytes
    static void LoadFastTxPackets(bool b_Init)
    {
        if (b_Init)
            mk_TxPackets = new CanPacket[FAST_PACKETS];

        for (int P=0; P<FAST_PACKETS; P++)
        {
            if (b_Init)
            {
                // IMPORTANT: Each connected USB adapter must use it's own ID, otherwise CAN errors when sending!
                mk_TxPackets[P] = new CanPacket();
                mk_TxPackets[P].ms32_ID = 0x500 + ms32_DeviceIndex; 
                mk_TxPackets[P].mi_Data.AddRange(new Byte[FAST_BYTES]);

                for (int B=0; B<FAST_BYTES; B++)
                {
                    mk_TxPackets[P].mi_Data[B] = (Byte)(P + B);
                }
            }

            // increment the first byte in each packet which is a counter
            mk_TxPackets[P].mi_Data[0] = mu8_TxPacketID ++;
        }
    }

    // ============================================================================================================

    static void SendSlowTxPackets(ref Int64 s64_LastStamp)
    {
        // Read the comment of OsLibrary::GetOsTimestamp()
        Int64 s64_Now = Utils.GetOsTimestamp();

        // Send the Tx frame every 2 seconds (= 2000000 µs)
        if (s64_Now - s64_LastStamp < 2000000)
            return;
    
        s64_LastStamp = s64_Now;

        Int64 s64_TxStamp = 0; 
        try
        {
            int s32_PackCount;
                    
            if (ge_RunDemo == eDemo.SlowBlob) // send blob with 3 packets at once over USB
            {
                mi_Candle.SendPacketBlob(mk_TxPackets, out s64_TxStamp);
                s32_PackCount = mk_TxPackets.Length;
            }
            else // send single packet
            {
                mi_Candle.SendPacket(mk_TxPackets[0], out s64_TxStamp);
                s32_PackCount = 1;
            }

            for (int P=0; P<s32_PackCount; P++)
            {
                // Timestamps for sending are only available if Windows timestamps are used
                Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_TxStamp));
                Print(ConsoleColor.White, " Send");
                Print(ConsoleColor.Green, " {0}", mk_TxPackets[P]);

                if (ge_RunDemo == eDemo.SlowBlob) Print(ConsoleColor.Gray, "  Tx Blob\n");
                else                              Print(ConsoleColor.Gray, "\n");
            }
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_TxStamp));
            Print(ConsoleColor.White, " Send");
            Print(ConsoleColor.Red,   " {0}\n", Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
            #endif
        }
        LoadSlowTxPackets(false);
    }

    // Send a blob with FAST_PACKETS packets with FAST_BYTES data bytes at maximum CAN bus load
    static void SendFastTxPackets()
    {
        Int64 s64_TxStamp = 0;
        try
        {
            // Read the comment of CalculateTxFifoFreeSlots()
            int s32_Available = mi_Candle.CalculateTxFifoFreeSlots();

            // not enough free slots in the firmware
            if (s32_Available <= FAST_PACKETS)
                return;
            
            mi_Candle.SendPacketBlob(mk_TxPackets, out s64_TxStamp);
        
            Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_TxStamp));
            Print(ConsoleColor.White, " Send");

            Print(ConsoleColor.Green, " {0:X}: {1} packets with {2} bytes from {3:X2} to {4:X2}", 
                                            mk_TxPackets[0].ms32_ID, FAST_PACKETS, FAST_BYTES,
                                            (Byte)(mu8_TxPacketID - FAST_PACKETS), 
                                            (Byte)(mu8_TxPacketID - 1));
            Print(ConsoleColor.Gray, "  Tx Blob\n");
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_TxStamp));
            Print(ConsoleColor.White, " Send");
            Print(ConsoleColor.Red,   " {0}\n", Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
            #endif
        }
        LoadFastTxPackets(false);
    }

    // ============================================================================================================

    // returns false to abort the demo when the adapter has been disconnected
    static bool ReceiveAndDisplayPackets()
    {
        Int64 s64_RxTimestamp = 0;
        bool    b_RxBlob = false;
        cHeader i_Header = null;

        for (int L=0; L<FAST_PACKETS; L++)
        {
            try
            {
                // Wait once for a packet, then only get packets that are in the buffer in OsLibrary.
                int s32_Timeout = (L == 0) ? 100 : 0;

                i_Header = mi_Candle.ReceiveData(s32_Timeout, out s64_RxTimestamp, out b_RxBlob);
                if (i_Header == null)
                    return true; // timeout
            }
            catch (Exception Ex)
            {
                // Error from WinUsb_ReadPipe() 
                Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_RxTimestamp));
                Print(ConsoleColor.White, " Recv");
                Print(ConsoleColor.Red,   " {0}\n", Ex.Message);
                #if DEBUG
                    Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
                #endif

                // The CANable has been disconnected --> return false
                return !(Ex is AbortException);
            }

            // The Windows console is too slow to print all the Tx echo packets at maximum busload
            if (ge_RunDemo == eDemo.FastBlob && i_Header.me_MesgType == eMessageType.TxEcho)
                continue;

            Print(ConsoleColor.Gray, mi_Candle.FormatTimestamp(i_Header, s64_RxTimestamp));
            switch (i_Header.me_MesgType)
            {
                case eMessageType.RxFrame:
                {
                    CanPacket i_Packet = mi_Candle.RxFrameToCanPacket((cRxFrameElmue)i_Header);
                    Print(ConsoleColor.White, " Recv");
                    Print(ConsoleColor.Cyan,  " {0}", i_Packet);
                    break;
                }
                case eMessageType.TxEcho:
                {
                    CanPacket i_EchoPacket = mi_Candle.GetTxEchoPacket((cTxEchoElmue)i_Header); 
                    Print(ConsoleColor.White, " Echo");
                    if (i_EchoPacket == null) Print(ConsoleColor.Red, " Invalid echo marker received");
                    else                      Print(ConsoleColor.DarkGreen, " {0}", i_EchoPacket);
                    break;
                }
                case eMessageType.Error:
                {
                    eBusStatus  e_BusStatus;
                    eErrorLevel e_ErrLevel;
                    String s_Error = mi_Candle.FormatCanErrors((cErrorElmue)i_Header, out e_BusStatus, out e_ErrLevel);
                    ConsoleColor e_Color = ConsoleColor.Gray;
                    if (e_ErrLevel == eErrorLevel.Medium) e_Color = ConsoleColor.Yellow;
                    if (e_ErrLevel == eErrorLevel.High)   e_Color = ConsoleColor.Red;
                    Print(ConsoleColor.White, " Err ");
                    Print(e_Color, " {0}", s_Error);
                    break;
                }
                case eMessageType.String:
                {
                    cStringElmue i_String = (cStringElmue)i_Header;
                    Print(ConsoleColor.White, " Debg");
                    Print(ConsoleColor.Gray,  " {0}", i_String.Message);
                    break;
                }
                case eMessageType.Busload:
                {
                    cBusloadElmue i_Busload = (cBusloadElmue)i_Header;
                    Print(ConsoleColor.White, " Load");
                    Print(ConsoleColor.Gray,  " Busload: {0}%", i_Busload.mu8_BusLoad);
                    break;
                }
                default:
                {
                    Print(ConsoleColor.White, " Err ");
                    Print(ConsoleColor.Red,   " Unknown USB message received: {0}", i_Header.me_MesgType);
                    break;
                }
            } // switch

            if (b_RxBlob) Print(ConsoleColor.Gray, "   Rx Blob\n");
            else          Print(ConsoleColor.Gray, "\n");
        } // for
        return true;
    }

    // ============================================================================================================

    // Write a string and a 64 bit random into 2 flash segments, then read the data and verify that it is correct.
    static void FlashMemoryTest()
    {
        Print(ConsoleColor.Yellow, "\nTest 1: Write string \"Hello C# World of flash data.\" to flash segment 3\n");
        Print(ConsoleColor.Yellow, "Test 2: Write 8 random bytes to flash segment 7\n");

        try
        {
            Byte u8_SegmentA = 3;
            Byte u8_SegmentB = 7;

            Byte[]  u8_String = Encoding.ASCII.GetBytes("Hello C# World of flash data.");

            UInt64 u64_Random = (UInt64)Environment.TickCount * 0x915B76F32;
            Byte[]  u8_Random = Utils.StructureToBytesFix(u64_Random);

            mi_Candle.WriteFlash(u8_SegmentA, u8_String);
            mi_Candle.WriteFlash(u8_SegmentB, u8_Random);

            // --------------------

            Byte[] u8_Flash1 = mi_Candle.ReadFlash(u8_SegmentA);
            if (!Utils.ByteArraysEqual(u8_String, u8_Flash1))
            {
                Print(ConsoleColor.Red, "\nFlash memory test 1 failed!\n");
                return;
            }

            Byte[] u8_Flash2 = mi_Candle.ReadFlash(u8_SegmentB);
            if (!Utils.ByteArraysEqual(u8_Random, u8_Flash2))
            {
                Print(ConsoleColor.Red, "\nFlash memory test 2 failed!\n");
                return;
            }

            Print(ConsoleColor.Green, "\nFlash memory test: Success\n");
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Red, "\nFlash memory test error: {0}\n", Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
            #endif
        }
    }
    
    // ============================================================================================================

    /// <summary>
    /// Does not throw
    /// CANDLELIGHT_DEMO = true  --> open Candlelight interface
    /// CANDLELIGHT_DEMO = false --> open DFU interface
    /// </summary>
    static bool OpenDevice()
    {
        List<cUsbDevice> i_Devices;
        try
        {
            i_Devices = SetupApi.EnumerateUsbDevices(ge_RunDemo != eDemo.EnterDFU);
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Red, "\nError enumerating USB devices. {0}\n", Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace + "\n");
            #endif
            return false;
        }

        if (i_Devices.Count == 0)
        {
            Print(ConsoleColor.Red, "\nNo Candlelight device connected or in wrong operatiom mode or driver not installed correctly.\n"
                                  + "Legacy Candlelight firmware has bugs that prevent the correct driver installation.\n"
                                  + "Make sure you have the new CANable 2.5 firmware from ElmüSoft.\n");
            return false;
        }

        // -----------------------------------------

        ms32_DeviceIndex = 0;
        if (i_Devices.Count == 1)
        {
            PrintDeviceMenu(i_Devices);
        }
        else // Two or more devices connected
        {
            while (true)
            {
                PrintDeviceMenu(i_Devices);

                Print(ConsoleColor.Green, "\nPlease select the adapter.");
                Print(ConsoleColor.Gray,  "  (Exit with ESCAPE)\n\n");

                ConsoleKeyInfo k_Key = Console.ReadKey(true);
                if (k_Key.Key == ConsoleKey.Escape)
                    return false;

                ms32_DeviceIndex = k_Key.KeyChar - '1';

                if (ms32_DeviceIndex >= 0 && ms32_DeviceIndex < i_Devices.Count) 
                    break;
            
                Print(ConsoleColor.Red, "\nInvalid key!\n");
            }
        }
   
        Print(ConsoleColor.Gray, "\n");

        // -----------------------------------------

        Exception i_Exception = null;
        try
        {
            mi_Candle.Open(i_Devices[ms32_DeviceIndex].ms_DevPath);
        }
        catch (Exception Ex)
        {
            i_Exception = Ex;
        }

        // Even after an exception some of the device details may be valid --> always print
        foreach (cDetail i_Detail in mi_Candle.DeviceDetails)
        {
            Print(ConsoleColor.Gray, "{0}\n", i_Detail.Format(21));
        }

        if (i_Exception != null)
        {
            Print(ConsoleColor.Red, "\nError opening device. {0}\n", i_Exception.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, i_Exception.StackTrace);
            #endif
            return false;
        }
        
        mk_Info = mi_Candle.DeviceInfo;        
        return true;
    }

// ============================================================================================================

    static bool TestSelection()  
    {
        Console.Clear();
        while (true)
        {
            Print(ConsoleColor.White,  "\nA.) Receive Only Demo\n");
            Print(ConsoleColor.Yellow, "    Display all received CAN traffic and send nothing.\n");
            Print(ConsoleColor.White,  "B.) Slow Tx Single Demo\n");
            Print(ConsoleColor.Yellow, "    Send one CAN packet with 8 data bytes every 2 seconds and display all received CAN traffic.\n");
            Print(ConsoleColor.White,  "C.) Slow Tx Blob Demo\n");
            Print(ConsoleColor.Yellow, "    Send a Tx blob with 3 CAN packets with 8 data bytes every 2 seconds and display all received CAN traffic.\n");
            Print(ConsoleColor.White,  "D.) Enter DFU Demo\n");
            Print(ConsoleColor.Yellow, "    Switch the adapter into DFU mode. This fails if already in DFU mode.\n");
            Print(ConsoleColor.White,  "E.) Flash Write / Read Demo\n");
            Print(ConsoleColor.Yellow, "    Write user data to the flash memory of the CANable, read it back and verify correct operation.\n");
            Print(ConsoleColor.White,  "F.) 100% Bus Load Demo\n");
            Print(ConsoleColor.Yellow, "    Send {0} packets with {1} bytes in a blob to the adapter to generate maximum CAN traffic.\n", FAST_PACKETS, FAST_BYTES);
            Print(ConsoleColor.Red,    "    IMPORTANT:\n");
            Print(ConsoleColor.Yellow, "    Do NOT run this demo against another side which also sends CAN packets.\n");
            Print(ConsoleColor.Yellow, "    When one adapter occupies CAN bus with 100% busload the other side has no chance to send a packet.\n");
            Print(ConsoleColor.Yellow, "    The side with the higher CAN ID will always lose arbitration and you see Tx Timeout errors.\n");
            Print(ConsoleColor.Yellow, "    This demo has been designed to send only unidirectional high speed traffic.\n");
            Print(ConsoleColor.Yellow, "    On Linux run this demo against the \"Receive Only Demo\" on the other side.\n");
            Print(ConsoleColor.Yellow, "    On Windows the console is too slow to display the CAN traffic generated by this demo.\n");
            Print(ConsoleColor.Yellow, "    If you use the Windows console don't be surprised to see errors \"Polling is too slow\".\n");
            Print(ConsoleColor.Yellow, "    I recommend to use the HUD ECU Hacker CAN Raw Terminal as packet receiver on the other side.\n");
            Print(ConsoleColor.Yellow, "    HUD ECU Hacker has an ultra fast Trace pane which is able of displaying CAN FD traffic at maximum speed.\n");
            Print(ConsoleColor.Green,  "\nPlease select the test to execute.");
            Print(ConsoleColor.Gray,   "  (Exit with ESCAPE)\n\n");

            ConsoleKeyInfo k_Key = Console.ReadKey(true);
            if (k_Key.Key == ConsoleKey.Escape)
                return false;

            int s32_Char = k_Key.KeyChar;
            if (s32_Char >= 'a')   
                s32_Char -= 32; // make upper case

            if (s32_Char >= 'A' && s32_Char <= 'F') 
            {
                ge_RunDemo = (eDemo)(s32_Char - 'A');
                return true;
            }
            
            Print(ConsoleColor.Red, "\nInvalid key!\n");
        }
    }

    // ============================================================================================================

    // Formatted output for each device: Product - Interface (Serial Number) CAN Channel
    static void PrintDeviceMenu(List<cUsbDevice> i_Devices)
    {
        Print(ConsoleColor.White, "\n");

        int s32_ProductLen = 0;
        int s32_SerialLen  = 0;
        int s32_InterfLen  = 0;

        for (int i=0; i<i_Devices.Count; i++)
        {
            cUsbDevice i_Device = i_Devices[i];
            s32_ProductLen = Math.Max(s32_ProductLen, i_Device.ms_Product  .Length);
            s32_SerialLen  = Math.Max(s32_SerialLen,  i_Device.ms_SerialNo .Length);
            s32_InterfLen  = Math.Max(s32_InterfLen,  i_Device.ms_Interface.Length);
        }

        for (int i=0; i<i_Devices.Count; i++)
        {
            cUsbDevice i_Device = i_Devices[i];
            Print(ConsoleColor.White, "{0}.) {1}{2} - {3}{4} ({5}){6}", i+1, 
                  i_Device.ms_Product,   new String(' ', s32_ProductLen - i_Device.ms_Product  .Length), 
                  i_Device.ms_Interface, new String(' ', s32_InterfLen  - i_Device.ms_Interface.Length),
                  i_Device.ms_SerialNo,  new String(' ', s32_SerialLen  - i_Device.ms_SerialNo .Length));

            int s32_Channel = i_Device.GetCanChannel();
            if (s32_Channel > 0) // Firmware Update interfaces have no channels
                Print(ConsoleColor.White, " CAN Channel: {0}", s32_Channel);

            Print(ConsoleColor.White, "\n");
        }
    }


    static void Print(ConsoleColor e_Color, String s_Format, params Object[] o_Param)
    {
        Console.ForegroundColor = e_Color;
        Console.Write(String.Format(s_Format, o_Param));
    }

} // class
} // namespace

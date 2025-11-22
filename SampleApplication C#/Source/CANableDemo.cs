
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
using System.Diagnostics;
using System.Collections.Generic;
using System.Text;

using WinUSB              = CANable.WinUSB;
using kUsbDevice          = CANable.SetupApi.kUsbDevice;
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
using Utils               = CANable.Utils;
using INPUT_KEY_RECORD    = CANable.Utils.INPUT_KEY_RECORD;

namespace CandlelightDemo
{
class Program
{
    // true  --> run Candlelight demo
    // false --> run DFU demo
    static bool CANDLELIGHT_DEMO = true; 

    // true  --> Only packets with 11 bit CAN ID 0x7E8 will be received.
    static bool SET_FILTERS = false;

    // Enable transfer of timestamps from the firmware (deprecated!)
    static bool HW_TIMESTAMP = false;

    static Candlelight mi_Candle = new Candlelight();
    static kDevInfo    mk_Info;
    static int         ms32_DeviceIndex; // user selection if multiple devices connected

    static void Main(string[] args)
    {
        Console.SetBufferSize(120, 3000);
            
        int s32_Width  = Math.Min(Console.LargestWindowWidth, 120) - 4;
        int s32_Height = Math.Min(Console.LargestWindowHeight, 60) - 4;
        Console.SetWindowSize(s32_Width, s32_Height);

        Console.Title = "ElmüSoft Candlelight C# Demo";
        Utils.SetConsoleCtrlHandler(CtrlHandler, true);

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

        Print(ConsoleColor.Gray, "\nPress a key to exit ...");
        Console.ReadKey();

        mi_Candle.Dispose(); // Always disconnect from CAN bus!
        Process.GetCurrentProcess().Kill();
    }

    /// <summary>
    /// The user closes the Console window with the mouse --> close the CANable
    /// </summary>
    static bool CtrlHandler(int signal)
    {
        const int CTRL_CLOSE_EVENT = 2;

        if (signal == CTRL_CLOSE_EVENT) 
        {
            mi_Candle.Dispose(); // Always disconnect from CAN bus!
            return true; // Indicate that the signal was handled
        }
        return false;
    }

    static void CandlelightDemo()
    {
        Print(ConsoleColor.Yellow, "=============================================================================\n");
        Print(ConsoleColor.Yellow, "                CANable 2.5 Candlelight C# Demo by ElmüSoft                  \n");
        Print(ConsoleColor.Yellow, "=============================================================================\n");

        // open interface 0
        if (!OpenDevice(0))
            return;

        // -----------------------------------------

        String s_Action = "";
        try
        {
            s_Action = "Error setting nominal bitrate.";

            String s_Display;
            // Supposed the clock is 160 MHz this will set 500 kBaud and samplepoint 87.5%
            mi_Candle.SetBitrate(false, 2, 139, 20, out s_Display);

            Print(ConsoleColor.DarkYellow, "\nSet {0}\n", s_Display);

            // -----------------------------------------

            s_Action = "Error enabling busload report.";

            // Report bus load every 5 seconds if it is not zero.
            mi_Candle.EnableBusLoadReport(5);

            // -----------------------------------------

            // Optionally you can set a CAN FD data bitrate here.
            // This will automatically enable CAN FD mode. GS_DevFlagCAN_FD is not required.
            if (true)
            {
                s_Action = "Error setting data bitrate.";

                // Supposed the clock is 160 MHz this will set 2 MBaud and samplepoint 75.0%
                mi_Candle.SetBitrate(true, 2, 29, 10, out s_Display);
                Print(ConsoleColor.DarkYellow, "Set {0}\n", s_Display);
            }

            // -----------------------------------------

            // optionally you can set filters here.
            if (SET_FILTERS)
            {
                s_Action = "Error setting mask filter.";

                // Only the 11 bit CAN ID 0x7E8 will pass through the filter.
                mi_Candle.AddMaskFilter(false, 0x7E8, 0x7FF);
            }

            // -----------------------------------------

            s_Action = "Error starting CAN bus.";

            eDeviceFlags e_DevFlags = eDeviceFlags.None;
            // e_DevFlags |= eDeviceFlags.OneShot;       // turn off automatic re-transmission
            // e_DevFlags |= eDeviceFlags.ListenOnly;    // silent mode
            // e_DevFlags |= eDeviceFlags.Loopback;      // loopback mode
            // e_DevFlags |= eDeviceFlags.DisableTxEcho; // turn off the 'echo' of sent packets

            // If you turn off eDeviceFlags.Timestamp, Windows timestamps will be used.
            // Firmware timestamps produce more USB traffic and are not available for sent packets.
            // Read the comment of GetWinTimestamp()
            if (HW_TIMESTAMP)
                e_DevFlags |= eDeviceFlags.HwTimestamp;

            mi_Candle.Start(e_DevFlags);

            Print(ConsoleColor.Yellow, "\nThe device has been opened. Please send CAN packets now.\n");
            Print(ConsoleColor.Yellow, "When a packet is received it is displayed in the console.\n");
            Print(ConsoleColor.Yellow, "Additionally classic packets with 8 data bytes are sent every 2 seconds.\n");
            Print(ConsoleColor.Red,    "ATTENTION:\n");
            Print(ConsoleColor.Yellow, "The Windows console is very slow. It cannot display fast CAN bus traffic.\n");
            Print(ConsoleColor.Yellow, "If you want to test your CANable on a real CAN bus, use HUD ECU Hacker.\n");
            Print(ConsoleColor.Yellow, "HUD ECU Hacker has an ultra fast speed-optimized trace pane.\n\n");

            Print(ConsoleColor.Yellow, "A left click into the console stops output, right click continues.\n\n");

            Print(ConsoleColor.Green,     "Lime  = Sent packets\n");
            Print(ConsoleColor.DarkGreen, "Green = Echo of sent packets that have been ACKnowledged\n");
            Print(ConsoleColor.Cyan,      "Cyan  = Received packets\n\n");

            Print(ConsoleColor.Yellow, "Press ENTER to abort and close the device.\n\n");
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Red, "{0} {1}\n", s_Action, Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace);
            #endif
            return;
        }

        CanPacket i_TxPacket = new CanPacket();
        i_TxPacket.ms32_ID = 0x7E0 + ms32_DeviceIndex;
        i_TxPacket.mi_Data.AddRange(Encoding.ASCII.GetBytes("ElmuSoft"));

        Int64 s64_LastStamp = 0;
        Byte  u8_LastEchoMarker;

        while (true)
        {
            // Read the comment of GetWinTimestamp()
            Int64 s64_Now = Utils.GetWinTimestamp();

            // Send the Tx frame every 2 seconds (= 2000000 µs)
            if (s64_Now - s64_LastStamp >= 2000000)
            {
                s64_LastStamp = s64_Now;

                Int64 s64_TxStamp; // only valid if no error returned
                try
                {
                    mi_Candle.SendPacket(i_TxPacket, out s64_TxStamp, out u8_LastEchoMarker);

                    // Timestamps for sending are only available if Windows timestamps are used
                    Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_TxStamp));
                    Print(ConsoleColor.White, " Send");
                    Print(ConsoleColor.Green, " {0}\n", i_TxPacket);
                }
                catch (Exception Ex)
                {
                    Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, Utils.GetWinTimestamp()));
                    Print(ConsoleColor.White, " Send");
                    Print(ConsoleColor.Red,   " {0}\n", Ex.Message);
                }

                // pseudo random data
                i_TxPacket.mi_Data[0] ++;
                i_TxPacket.mi_Data[1] = (Byte)(i_TxPacket.mi_Data[0] * 2);
                i_TxPacket.mi_Data[2] = (Byte)(i_TxPacket.mi_Data[1] * 2);
                i_TxPacket.mi_Data[3] = (Byte)(i_TxPacket.mi_Data[2] * 50);
                i_TxPacket.mi_Data[4] = (Byte)(i_TxPacket.mi_Data[3] * 13);
                i_TxPacket.mi_Data[5] = (Byte)(i_TxPacket.mi_Data[4] * 7);
                i_TxPacket.mi_Data[6] = (Byte)(i_TxPacket.mi_Data[5] * 23);
                i_TxPacket.mi_Data[7] = (Byte)(i_TxPacket.mi_Data[6] * 19);
            }

            // Check for Rx data
            Int64 s64_RxTimestamp = 0;
            cHeader i_Header = null;
            try
            {
                i_Header = mi_Candle.ReceiveData(100, out s64_RxTimestamp);
            }
            catch (Exception Ex)
            {
                // Error from WinUsb_ReadPipe() (e.g. USB device has been disconnected)
                Print(ConsoleColor.Gray,  mi_Candle.FormatTimestamp(null, s64_RxTimestamp));
                Print(ConsoleColor.White, " Recv");
                Print(ConsoleColor.Red,   " {0}\n", Ex.Message);
            }

            if (i_Header != null)
            {
                Print(ConsoleColor.Gray, mi_Candle.FormatTimestamp(i_Header, s64_RxTimestamp));
                switch (i_Header.me_MesgType)
                {
                    case eMessageType.RxFrame:
                    {
                        CanPacket i_Packet = mi_Candle.RxFrameToCanPacket((cRxFrameElmue)i_Header);
                        Print(ConsoleColor.White, " Recv");
                        Print(ConsoleColor.Cyan,  " {0}\n", i_Packet);
                        break;
                    }
                    case eMessageType.TxEcho:
                    {
                        CanPacket i_EchoPacket = mi_Candle.GetTxEchoPacket((cTxEchoElmue)i_Header);
                        Print(ConsoleColor.White, " Echo");
                        if (i_EchoPacket == null) Print(ConsoleColor.Red, "Internal Error");
                        else                      Print(ConsoleColor.DarkGreen, " {0}\n", i_EchoPacket);
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
                        Print(e_Color, " {0}\n", s_Error);
                        break;
                    }
                    case eMessageType.String:
                    {
                        cStringElmue i_String = (cStringElmue)i_Header;
                        Print(ConsoleColor.White, " Debg");
                        Print(ConsoleColor.Gray,  " {0}\n", i_String.Message);
                        break;
                    }
                    case eMessageType.Busload:
                    {
                        cBusloadElmue i_Busload = (cBusloadElmue)i_Header;
                        Print(ConsoleColor.White, " Load");
                        Print(ConsoleColor.Gray,  " Busload: {0}%\n", i_Busload.mu8_BusLoad);
                        break;
                    }
                    default:
                    {
                        Print(ConsoleColor.White, " Err ");
                        Print(ConsoleColor.Red,   " Unknown USB message received: {0}\n", i_Header.me_MesgType);
                        break;
                    }
                }
            }

            // ==================================================

            // Check if ENTER key has been pressed
            const int STD_INPUT_HANDLE = -10;
            const int KEY_EVENT = 0x01;
            const int VK_RETURN = 0x0D;
            IntPtr h_Console = Utils.GetStdHandle(STD_INPUT_HANDLE);

            // Check if ENTER key has been pressed
            INPUT_KEY_RECORD k_Buffer;
            int s32_Events;
            if (Utils.PeekConsoleInput(h_Console, out k_Buffer, 1, out s32_Events))
            {
                if (s32_Events > 0)
                {
                    // The event must be removed from the input buffer, otherwise it is reported eternally.
                    Utils.ReadConsoleInputW(h_Console, out k_Buffer, 1, out s32_Events);  

                    if (k_Buffer.EventType == KEY_EVENT  && 
                        k_Buffer.bKeyDown &&
                        k_Buffer.wVirtualKeyCode == VK_RETURN)
                    {
                        mi_Candle.Dispose();
                        return;
                    }
                }
            }

        } // while (true)
    }

    static void DfuDemo()
    {
        Print(ConsoleColor.Yellow, "=============================================================================\n");
        Print(ConsoleColor.Yellow, "                 CANable 2.5 Enter DFU C# Demo by ElmüSoft                   \n");
        Print(ConsoleColor.Yellow, "=============================================================================\n");

        // open interface 1
        if (!OpenDevice(1))
            return;

        try
        {
            bool b_ReconnectRequired;
            mi_Candle.EnterDfuMode(out b_ReconnectRequired);

            if (b_ReconnectRequired)
                Print(ConsoleColor.Red, "\nPlease reconnect the USB cable\n");
            else
                Print(ConsoleColor.Green, "\nDevice has been switched successfully into DFU mode.\n");
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Red, "\n{0}\n", Ex.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, Ex.StackTrace);
            #endif
        }
    }

    // ---------------------------------------------------------------------------------------------------------------------

    /// <summary>
    /// Does not throw
    /// </summary>
    static bool OpenDevice(Byte u8_Interface)
    {
        List<kUsbDevice> i_Devices;
        try
        {
            i_Devices = SetupApi.EnumerateUsbDevices(u8_Interface);
        }
        catch (Exception Ex)
        {
            Print(ConsoleColor.Red, "\n{0}\n", Ex.Message);
            return false;
        }

        if (i_Devices.Count == 0)
        {
            Print(ConsoleColor.Red, "\nNo Candlelight device connected or in wrong operatiom mode or WinUSB driver not installed correctly.\n"
                                  + "Legacy Candlelight firmware has bugs that prevent the correct driver installation.\n"
                                  + "Make sure you have the new CANable 2.5 firmware from ElmüSoft.\n");
            return false;
        }

        // -----------------------------------------

        ms32_DeviceIndex = 0;
        if (i_Devices.Count > 1) // 2 ore more devices connected
        {
            while (true)
            {
                Print(ConsoleColor.Green, "\nPlease select one of the devices:\n");

                for (int i=0; i<i_Devices.Count; i++)
                {
                    Print(ConsoleColor.Gray, "{0}.) {1}\n", i+1, i_Devices[i]);
                }

                ConsoleKeyInfo k_Key = Console.ReadKey();
                ms32_DeviceIndex = k_Key.KeyChar - '1';

                if (ms32_DeviceIndex >= 0 && ms32_DeviceIndex < i_Devices.Count) 
                    break;
            
                Print(ConsoleColor.Red, "Invalid key!\n");
            }
        }
   
        Print(ConsoleColor.Gray, "\nDevice Path: \"{0}\"\n", i_Devices[ms32_DeviceIndex].NtPath);

        // -----------------------------------------

        Exception i_Exception = null;
        try
        {
            mi_Candle.Open(i_Devices[ms32_DeviceIndex].NtPath);
        }
        catch (Exception Ex)
        {
            i_Exception = Ex;
        }

        // Even after an exception some of the device details may be valid --> always print
        Print(ConsoleColor.Gray, mi_Candle.DeviceDetails);
        mk_Info = mi_Candle.DeviceInfo;

        if (i_Exception != null)
        {
            Print(ConsoleColor.Red, "\n{0}\n", i_Exception.Message);
            #if DEBUG
                Print(ConsoleColor.Gray, i_Exception.StackTrace);
            #endif
            return false;
        }
        return true;
    }

    static void Print(ConsoleColor e_Color, String s_Format, params Object[] o_Param)
    {
        Console.ForegroundColor = e_Color;
        Console.Write(String.Format(s_Format, o_Param));
    }
} // class Program
} // namespace CANable_Demo

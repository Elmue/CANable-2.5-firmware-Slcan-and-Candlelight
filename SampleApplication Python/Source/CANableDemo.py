
# https://netcult.ch/elmue/CANable%20Firmware%20Update

# NAMING CONVENTIONS which allow to see the type of a variable immediately without having to jump to the variable declaration:

#      cName  for class    definitions
#      tName  for type     definitions
#      eName  for enum     definitions
#      kName  for "konstruct" (struct) definitions (letter 's' already used for string)
#    delName  for delegate definitions

#     b_Name  for bool
#     c_Name  for Char, also Color
#     d_Name  for double
#     e_Name  for enum variables
#     f_Name  for function delegates, also float
#     i_Name  for instances of classes
#     k_Name  for "konstructs" (struct) (letter 's' already used for string)
#     r_Name  for Rectangle
#     s_Name  for strings
#     o_Name  for objects

#    s8_Name  for   signed  8 Bit (sbyte)
#   s16_Name  for   signed 16 Bit (short)
#   s32_Name  for   signed 32 Bit (int)
#   s64_Name  for   signed 64 Bit (long)
#    u8_Name  for unsigned  8 Bit (byte)
#   u16_Name  for unsigned 16 bit (ushort)
#   u32_Name  for unsigned 32 Bit (uint)
#   u64_Name  for unsigned 64 Bit (ulong)

# An additional "m" is prefixed for all member variables (e.g. ms_String)

# ===================================================================================

# This class demonstrates the new CANable 2.5 ElmüSoft protocol.

import sys
import traceback
import faulthandler

# avoid contamination with ugly subfolders __pycache__ in each subdirectory
sys.dont_write_bytecode = True

# Print Segment Fault and other crashes to the output
faulthandler.enable()

from Utils       import kDevInfo, kUsbDevice
from Candlelight import *
if sys.platform == "win32": from OsLibrary_Windows import OsLibrary, eConsole
else:                       from OsLibrary_Linux   import OsLibrary, eConsole

# True  --> run Candlelight demo (send and receive CAN packets)
# False --> run DFU demo (switch a device in Candlelight mode into DFU mode, fails if already in DFU mode)
CANDLELIGHT_DEMO   = True

# true  --> set data baudrate        -> CAN FD ackets can be sent and received
# false --> do not set data baudrate -> CAN FD ackets cannot be sent and received
ENABLE_CAN_FD      = True;

# True  --> only packets with 11 bit CAN ID 0x7E8 are sent to the host.
# False --> all packets are sent to the host
SET_HOST_FILTERS   = False

# True  --> Received packets with CAN ID 0x7E5 will be forwarded from channel 0 to channel 1 (only multi-channel adapters)
# False --> Do not use brdige mode
SET_BRIDGE_FILTERS = False

# True  --> enable transfer of timestamps from the firmware (deprecated!)
# False --> create performance counter timestamps 
HW_TIMESTAMP       = False

# True --> test writing/reading user data to/from flash memory
FLASH_MEMORY_TEST  = False

# True --> send 3 Tx packets in one blob
SEND_TX_BLOB       = False

# True  --> print an additional grey stacktrace for all exceptions
# False --> print only the red error message
STACK_TRACE        = False

# ----- static variables -----

gi_Candle = Candlelight()
gk_Info   = kDevInfo()
gs32_DeviceIndex = 0   # user selection if multiple devices connected

# ---------------------------------------------------------------------------------------------------------------------

# entry point for demo application
def CanableDemo() -> None:
    # Increase console buffer for 3000 lines output with 300 chars per line
    # Set console window to 120 chars in 60 lines
    OsLibrary.SetUpConsole(300, 3000, 120, 60, "ElmüSoft Candlelight Python Demo")
    
    # Only required for Linux
    OsLibrary.SwitchTerminalToNonCanonical()   

    if CANDLELIGHT_DEMO:
        # Test interface 0 = Candlelight
        CandlelightDemo()
    else:
        # Test interface 1 = Firmware Update
        DfuDemo()

    gi_Candle.Close() # Close CAN bus, stop pipe thread

    OsLibrary.PrintConsole(eConsole.Grey, "\nPress a key to exit ...\n")
    OsLibrary.WaitConsoleChar()
    
    # Only required for Linux
    OsLibrary.RestoreTerminal()      
   
# ---------------------------------------------------------------------------------------------------------------------

def CandlelightDemo() -> None:
    OsLibrary.PrintConsole(eConsole.Yellow, "=============================================================================\n")
    OsLibrary.PrintConsole(eConsole.Yellow, "              CANable 2.5 Candlelight Python Demo by ElmüSoft                \n")
    OsLibrary.PrintConsole(eConsole.Yellow, "=============================================================================\n")
    
    # open Candlelight interface
    if not OpenDevice():
        return

    try:
        s_Action = "Error from flash memory test."

        # Test flash writing / reading
        if FLASH_MEMORY_TEST:
            FlashMemoryTest()

        s_Action = "Error setting nominal bitrate."

        # Set 500 kBaud and samplepoint 60%
        # Use the smallest possible prescaler!
        # Urgently read: https://netcult.ch/elmue/CANable%20Firmware%20Update
        s32_Clk_Mhz = gk_Info.mk_Capability.fclk_can // 1000000
        if   s32_Clk_Mhz ==  60: s_Display = gi_Candle.SetBitrate(False, 1, 71, 48) # STM32G0B1: CAN clock  60 MHZ
        elif s32_Clk_Mhz == 160: s_Display = gi_Candle.SetBitrate(False, 2, 95, 64) # STM32G431: CAN clock 160 MHZ
        else:
            OsLibrary.PrintConsole(eConsole.Red, "CAN Clock not implemented.\n")
            return
       
        OsLibrary.PrintConsole(eConsole.Brown, "\nSet %s\n", s_Display)

        # -----------------------------------------

        # Optionally you can set a CAN FD data bitrate here.
        # This will automatically enable CAN FD mode. GS_DevFlagCAN_FD is not required.
        if ENABLE_CAN_FD:
            s_Action = "Error setting data bitrate.";
            
            # Set 2 MBaud and samplepoint 60%
            # Use the same prescaler as for nominal baudrate!
            # Urgently read: https://netcult.ch/elmue/CANable%20Firmware%20Update
            if   s32_Clk_Mhz ==  60: s_Display = gi_Candle.SetBitrate(True, 1, 17, 12) # STM32G0B1: CAN clock  60 MHZ
            elif s32_Clk_Mhz == 160: s_Display = gi_Candle.SetBitrate(True, 2, 23, 16) # STM32G431: CAN clock 160 MHZ
            else:
                OsLibrary.PrintConsole(eConsole.Red, "CAN Clock not implemented.\n")
                return
            
            OsLibrary.PrintConsole(eConsole.Brown, "Set %s\n", s_Display)

        # -----------------------------------------

        s_Action = "Error enabling busload report.";

        # Report bus load every 5 seconds if it is not zero.
        gi_Candle.EnableBusLoadReport(5)
        
        # -----------------------------------------
        
        # Never raises an exception
        gi_Candle.EnableTxEcho(True)

        # -----------------------------------------

        # optionally you can set host filters here.
        if SET_HOST_FILTERS:
            s_Action = "Error setting host filter.";
            
            # Only the 11 bit CAN ID 0x7E8 will pass through the filter.
            gi_Candle.AddHostFilter(False, 0x7E8, 0x7FF)

            OsLibrary.PrintConsole(eConsole.Brown, "Set host filter 7E8\n")

        # -----------------------------------------

        # The adapter must have at least 2 channels. Set filter on channel 0
        if SET_BRIDGE_FILTERS:
            if gk_Info.mk_DeviceVersion.icount + 1 >= 2 and gk_Info.mu8_Channel == 0:
                s_Action = "Error setting bridge filter.";
                # Set filter Nº 08 to forward packets with CAN ID 0x7E5 from channel 0 to channel 1.
                gi_Candle.SetBridgeFilter(8, 1, True, False, False, 0x7E5, 0x7FF)

                OsLibrary.PrintConsole(eConsole.Brown, "Set bridge filter 7E5\n")
            else:
                OsLibrary.PrintConsole(eConsole.Red, "The condition to set a bridge filter is not given\n")

        # -----------------------------------------
        
        s_Action = "Error starting CAN bus.";

        e_DevFlags = eDeviceFlags.GS_DevFlagNone
        # e_DevFlags |= eDeviceFlags.GS_DevFlagOneShot     # turn off automatic re-transmission
        # e_DevFlags |= eDeviceFlags.GS_DevFlagListenOnly  # silent mode
        # e_DevFlags |= eDeviceFlags.GS_DevFlagLoopback    # loopback mode

        # If you turn off GS_DevFlagTimestamp, operating system timestamps will be used.
        # Firmware timestamps produce more USB traffic and are not available for sent packets.
        # Read the comment of GetOsTimestamp()
        if HW_TIMESTAMP:
            e_DevFlags |= eDeviceFlags.GS_DevFlagTimestamp

        # Open the adapter, start FDCAN module in the processor
        gi_Candle.Start(e_DevFlags)

        OsLibrary.PrintConsole(eConsole.Yellow, "\nThe device has been opened. Please send CAN packets now.\n")
        OsLibrary.PrintConsole(eConsole.Yellow, "When a packet is received it is displayed in the console.\n")
        OsLibrary.PrintConsole(eConsole.Yellow, "Additionally classic packets with 8 data bytes are sent every 2 seconds.\n\n")

        if sys.platform == "win32":
            OsLibrary.PrintConsole(eConsole.Red,    "ATTENTION:\n")
            OsLibrary.PrintConsole(eConsole.Yellow, "The Windows console is very slow. It cannot display fast CAN bus traffic.\n")
            OsLibrary.PrintConsole(eConsole.Yellow, "If you want to test your CANable on a real CAN bus, use HUD ECU Hacker.\n")
            OsLibrary.PrintConsole(eConsole.Yellow, "HUD ECU Hacker has an ultra fast speed-optimized CAN Raw Terminal.\n\n")
            OsLibrary.PrintConsole(eConsole.Yellow, "A left click into the console stops output, right click continues.\n\n")

        OsLibrary.PrintConsole(eConsole.Lime,   "Lime  = Sent packets\n")
        OsLibrary.PrintConsole(eConsole.Green,  "Green = Echo of sent packets that have been ACKnowledged\n")
        OsLibrary.PrintConsole(eConsole.Cyan,   "Cyan  = Received packets\n\n")

        OsLibrary.PrintConsole(eConsole.Magenta, "Press ENTER to abort. If you only close the console window the adapter stays open.\n\n")
        
    except Exception as Ex:
        OsLibrary.PrintConsole(eConsole.Red,  "%s %s\n", s_Action, str(Ex))
        if STACK_TRACE:
            OsLibrary.PrintConsole(eConsole.Grey, "%s\n", traceback.format_exc())
        return

    # -----------------------------------------

    k_TxPackets = [kCanPacket() for _ in range(3)]

    k_TxPackets[0].mu32_ID  = 0x7E0 + gs32_DeviceIndex # Each USB adapter has it's own ID
    k_TxPackets[0].mu8_Data = bytearray(b"ElmuSoft") # 8 bytes

    k_TxPackets[1].mu32_ID  = k_TxPackets[0].mu32_ID + 1
    k_TxPackets[1].mu8_Data = bytearray(b"TxBlob 2") # 8 bytes

    k_TxPackets[2].mu32_ID  = k_TxPackets[0].mu32_ID + 2
    k_TxPackets[2].mu8_Data = bytearray(b"TxBlob 3") # 8 bytes

    # -----------------------------------------

    s64_LastStamp = Utils.GetOsTimestamp()
    while True:
        # Read the comment of OsLibrary::GetTimestamp()
        s64_Now = Utils.GetOsTimestamp()

        # Send the Tx frame every 2 seconds (= 2000000 µs)
        if s64_Now - s64_LastStamp >= 2000000:
            s64_LastStamp = s64_Now

            try:
                if SEND_TX_BLOB:  # send blob with 3 packets at once over USB
                    s64_TxStamp   = gi_Candle.SendPacketBlob(k_TxPackets)
                    s32_PackCount = len(k_TxPackets)                 
                else:
                    s64_TxStamp   = gi_Candle.SendPacket(k_TxPackets[0])
                    s32_PackCount = 1                                

                for P in range(s32_PackCount):
                    # Timestamps for sending are only available if operating system timestamps are used
                    OsLibrary.PrintConsole(eConsole.Grey,  gi_Candle.FormatTimestamp(None, s64_TxStamp))
                    OsLibrary.PrintConsole(eConsole.White, " Send")
                    OsLibrary.PrintConsole(eConsole.Lime,  " %s", gi_Candle.FormatCanPacket(k_TxPackets[P]))

                    if SEND_TX_BLOB: OsLibrary.PrintConsole(eConsole.Grey, " (Tx Blob)\n")
                    else:            OsLibrary.PrintConsole(eConsole.Grey, "\n")
                        
            except Exception as Ex:
                OsLibrary.PrintConsole(eConsole.Grey,  gi_Candle.FormatTimestamp(None, Utils.GetOsTimestamp()))
                OsLibrary.PrintConsole(eConsole.White, " Send")
                OsLibrary.PrintConsole(eConsole.Red,   " %s\n", str(Ex))
                if STACK_TRACE:
                    OsLibrary.PrintConsole(eConsole.Grey, "%s\n", traceback.format_exc())

                if isinstance(Ex, AbortError):
                    return # The CANable has been disconnected

            # pseudo random data
            k_TxPackets[0].mu8_Data[0] = (k_TxPackets[0].mu8_Data[0] +  1) & 0xFF
            k_TxPackets[0].mu8_Data[1] = (k_TxPackets[0].mu8_Data[0] *  3) & 0xFF
            k_TxPackets[0].mu8_Data[2] = (k_TxPackets[0].mu8_Data[1] *  2) & 0xFF
            k_TxPackets[0].mu8_Data[3] = (k_TxPackets[0].mu8_Data[2] * 51) & 0xFF
            k_TxPackets[0].mu8_Data[4] = (k_TxPackets[0].mu8_Data[3] * 11) & 0xFF
            k_TxPackets[0].mu8_Data[5] = (k_TxPackets[0].mu8_Data[4] *  7) & 0xFF
            k_TxPackets[0].mu8_Data[6] = (k_TxPackets[0].mu8_Data[5] * 25) & 0xFF
            k_TxPackets[0].mu8_Data[7] = (k_TxPackets[0].mu8_Data[6] * 17) & 0xFF
            
            k_TxPackets[1].mu8_Data[0] = (k_TxPackets[0].mu8_Data[0] + 0x10) & 0xFF
            k_TxPackets[2].mu8_Data[0] = (k_TxPackets[0].mu8_Data[0] + 0x20) & 0xFF

        # Check for Rx data
        k_RecvData = None
        try:
            k_RecvData = gi_Candle.ReceiveData(100)
            
        except Exception as Ex:
            OsLibrary.PrintConsole(eConsole.Grey,  gi_Candle.FormatTimestamp(None, Utils.GetOsTimestamp()))
            OsLibrary.PrintConsole(eConsole.White, " Recv")
            OsLibrary.PrintConsole(eConsole.Red,   " %s\n", str(Ex))
            if STACK_TRACE:
                OsLibrary.PrintConsole(eConsole.Grey, "%s\n", traceback.format_exc())

            if isinstance(Ex, AbortError):
                return # The CANable has been disconnected

        if k_RecvData is not None:
            OsLibrary.PrintConsole(eConsole.Grey, gi_Candle.FormatTimestamp(k_RecvData, k_RecvData.ms64_OsTimestamp))
            
            if   k_RecvData.mk_RxFrame is not None:
                k_RxPacket = gi_Candle.RxFrameToCanPacket(k_RecvData)
                OsLibrary.PrintConsole(eConsole.White, " Recv")
                OsLibrary.PrintConsole(eConsole.Cyan,  " %s", gi_Candle.FormatCanPacket(k_RxPacket))

            elif k_RecvData.mk_TxEcho is not None:
                k_EchoPacket = gi_Candle.GetTxEchoPacket(k_RecvData)
                OsLibrary.PrintConsole(eConsole.White, " Echo")
                OsLibrary.PrintConsole(eConsole.Green, " %s", gi_Candle.FormatCanPacket(k_EchoPacket))

            elif k_RecvData.mk_Error is not None:
                s_ErrMsg, e_BusStatus, e_ErrLevel = gi_Candle.FormatCanErrors(k_RecvData)
                e_Color = eConsole.Grey
                if e_ErrLevel == eErrorLevel.Medium: e_Color = eConsole.Yellow
                if e_ErrLevel == eErrorLevel.High:   e_Color = eConsole.Red
                OsLibrary.PrintConsole(eConsole.White, " Err ")
                OsLibrary.PrintConsole(e_Color,        " %s", s_ErrMsg)

            elif k_RecvData.mk_String is not None:
                OsLibrary.PrintConsole(eConsole.White, " Debg")
                OsLibrary.PrintConsole(eConsole.Grey,  " %s", gi_Candle.ConvertStringFrame(k_RecvData))

            elif k_RecvData.mk_Busload is not None:
                OsLibrary.PrintConsole(eConsole.White, " Load")
                OsLibrary.PrintConsole(eConsole.Grey,  " Busload: %u%%", k_RecvData.mk_Busload.bus_load)

            else:
                OsLibrary.PrintConsole(eConsole.White, " Err ")
                OsLibrary.PrintConsole(eConsole.Red,   " Unknown USB message received: %s", 
                                       Utils.FormatHexBytes(k_RecvData.mu8_RawBytes))

            if k_RecvData.mb_RxBlob: OsLibrary.PrintConsole(eConsole.Grey, "   Rx Blob\n")
            else:                    OsLibrary.PrintConsole(eConsole.Grey, "\n")

        # exit if the user hits ENTER
        if OsLibrary.CheckConsoleEnterPressed():
            return

    # while (true)

# ---------------------------------------------------------------------------------------------------------------------

# Write a string and a 64 bit random into 2 flash segments, then read the data and verify that it is correct.
def FlashMemoryTest() -> None:
    u8_SegmentA = 4
    u8_SegmentB = 6
    
    u8_Hello   = bytearray(b"Hello World of flash data!")
    u64_Random = ctypes.c_longlong(Utils.GetTickMilli() * 0x815A78F3D)
    u8_Random  = bytearray(bytes(u64_Random))
    
    gi_Candle.WriteFlash(u8_SegmentA, u8_Hello)
    gi_Candle.WriteFlash(u8_SegmentB, u8_Random)

    # --------------------
    
    u8_FlashData = gi_Candle.ReadFlash(u8_SegmentA)

    if u8_Hello != u8_FlashData:
        OsLibrary.PrintConsole(eConsole.Red, "\nFlash memory test 1 failed!\n")
        return

    u8_FlashData = gi_Candle.ReadFlash(u8_SegmentB)

    if u8_Random != u8_FlashData:
        OsLibrary.PrintConsole(eConsole.Red, "\nFlash memory test 2 failed!\n")
        return

    OsLibrary.PrintConsole(eConsole.Lime, "\nFlash memory test: Success\n")
    
# ---------------------------------------------------------------------------------------------------------------------

# ATTENTION:
# This works only if the device is in Candlelight mode.
# If the device is already in DFU mode it will fail.
def DfuDemo() -> None:
    OsLibrary.PrintConsole(eConsole.Yellow, "=============================================================================\n")
    OsLibrary.PrintConsole(eConsole.Yellow, "                   CANable 2.5 DFU Python Demo by ElmüSoft                   \n")
    OsLibrary.PrintConsole(eConsole.Yellow, "=============================================================================\n")

    # Open Firmware Update interface
    if not OpenDevice():
        return

    try:
        gi_Candle.EnterDfuMode()
        OsLibrary.PrintConsole(eConsole.Lime, "\nDevice has been switched successfully into DFU mode.\n")
        
    except Exception as Ex:
        OsLibrary.PrintConsole(eConsole.Red, "\nError switching to DFU mode. %s\n", str(Ex))
        if STACK_TRACE:
            OsLibrary.PrintConsole(eConsole.Grey, "%s\n", traceback.format_exc())

# ---------------------------------------------------------------------------------------------------------------------

# CANDLELIGHT_DEMO = true  --> open Candlelight interface
# CANDLELIGHT_DEMO = false --> open DFU interface
def OpenDevice() -> bool:
    global gs32_DeviceIndex, gk_Info
    
    try:
        i_Devices = gi_Candle.EnumDevices(CANDLELIGHT_DEMO)
        
    except Exception as Ex:
        OsLibrary.PrintConsole(eConsole.Red, "\nError enumerating USB devices. %s\n", str(Ex))
        if STACK_TRACE:
            OsLibrary.PrintConsole(eConsole.Grey, "%s\n", traceback.format_exc())
        return False

    if len(i_Devices) == 0:
        OsLibrary.PrintConsole(eConsole.Red, "\nNo Candlelight device connected or in wrong operatiom mode or WinUSB driver not installed correctly.\n"
                                             "Legacy Candlelight firmware has bugs that prevent the correct driver installation.\n"
                                             "Make sure you have the new CANable 2.5 firmware from ElmüSoft.\n")
        return False

    # -----------------------------------------
    
    gs32_DeviceIndex = 0
    if len(i_Devices) > 1: # Two or more devices connected
        while True:
            OsLibrary.PrintConsole(eConsole.Lime, "\nPlease select one of the devices:")
            OsLibrary.PrintConsole(eConsole.Grey, "  (Exit with ESCAPE)\n\n")

            PrintDeviceMenu(i_Devices)

            s32_Char = OsLibrary.WaitConsoleChar()
            if s32_Char == 27: # ESCAPE key pressed
                return False

            gs32_DeviceIndex = s32_Char - ord('1')

            if 0 <= gs32_DeviceIndex < len(i_Devices): 
                break
            
            OsLibrary.PrintConsole(eConsole.Red, "\nInvalid key!\n")

    OsLibrary.PrintConsole(eConsole.Grey, "\n")

    # -----------------------------------------
    
    s_Error = ""
    s_Stack = ""
    try:
        gi_Candle.Open(i_Devices[gs32_DeviceIndex])

    except Exception as Ex:
        s_Error = str(Ex)
        s_Stack = traceback.format_exc()
        
    # Even after an exception some of the device details may be valid --> always print
    for k_Detail in gi_Candle.GetDetails():
        OsLibrary.PrintConsole(eConsole.Grey, "%s\n", k_Detail.Format(22))

    if len(s_Error):
        OsLibrary.PrintConsole(eConsole.Red, "\nError opening device. %s\n", s_Error)
        if STACK_TRACE:
            OsLibrary.PrintConsole(eConsole.Grey, "%s\n", s_Stack)
        return False
    
    gk_Info = gi_Candle.GetDeviceInfo()    
    return True
    
# ---------------------------------------------------------------------------------------------------------------------

# Formatted output for each device: Product - Interface (Serial Number) CAN Channel
def PrintDeviceMenu(i_Devices: List[kUsbDevice]) -> None:
    u32_ProductLen = 0
    u32_SerialLen  = 0
    u32_InterfLen  = 0

    for k_Device in i_Devices:
        u32_ProductLen = max(u32_ProductLen, len(k_Device.ms_Product))
        u32_SerialLen  = max(u32_SerialLen,  len(k_Device.ms_SerialNo))
        u32_InterfLen  = max(u32_InterfLen,  len(k_Device.ms_Interface))

    Idx = 1
    for k_Device in i_Devices:
        s_SpaceProduct = ' ' * (u32_ProductLen - len(k_Device.ms_Product))
        s_SpaceSerial  = ' ' * (u32_SerialLen  - len(k_Device.ms_SerialNo))
        s_SpaceInterf  = ' ' * (u32_InterfLen  - len(k_Device.ms_Interface))

        OsLibrary.PrintConsole(eConsole.White, "%u.) %s%s - %s%s (%s)%s", Idx, 
                               k_Device.ms_Product,   s_SpaceProduct, 
                               k_Device.ms_Interface, s_SpaceInterf,
                               k_Device.ms_SerialNo,  s_SpaceSerial)

        s32_Channel = k_Device.GetCanChannel()
        if s32_Channel > 0: # Firmware Update interfaces have no channels
            OsLibrary.PrintConsole(eConsole.White, " CAN Channel: %d", s32_Channel)

        OsLibrary.PrintConsole(eConsole.White, "\n")
        Idx += 1

# ---------------------------------------------------------------------------------------------------------------------

# run the script
CanableDemo()



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

# This class implements the new CANable 2.5 ElmüSoft protocol.

import sys
import time
import copy
import ctypes
from dataclasses     import dataclass, field
from enum            import IntEnum
from typing          import List, Optional, Tuple
from Candlelight_def import *
from Utils           import *

if sys.platform == "win32": from OsLibrary_Windows import OsLibrary, NO_ERROR, eConsole
else:                       from OsLibrary_Linux   import OsLibrary, NO_ERROR, eConsole
    
# Adapt this to the latest available CANable 2.5 firmware version.
# It shows an error to upload the latest firmware to the adapter.
# The version number is BCD encoded (0x251218 = 18.dec.2025)
MIN_FIRMWARE = 0x260914

# must be equal to CAN_QUEUE_SIZE in buffer.h in the firmware
CAN_QUEUE_SIZE = 64

class eErrorLevel(IntEnum):
    Low    = 0  # print error in grey
    Medium = 1  # print error in yellow / orange
    High   = 2  # print error in red

# Remote frames store the DLC value in the first data byte
class kCanPacket:
    def __init__(self):
        self.mu32_ID     = 0
        self.mu8_Data    = bytearray(0)
        self.mb_29bit    = False  # Extended ID.
        self.mb_RTR      = False  # Remote Frame                     Only used if mb_FDF = False
        self.mb_FDF      = False  # CAN FD Frame
        self.mb_BRS      = False  # CAN FD Bit Rate Switching        Only used if mb_FDF = True
        self.mb_ESI      = False  # CAN FD Error State Passive flag  Only used if mb_FDF = True

class kDetail:
    def __init__(self, s_Name: str, s_Value: str):
        self.ms_Name  = s_Name
        self.ms_Value = s_Value

    # returns "USB Product:         Candlelight 2.5 - Multiboard"
    def Format(self, s32_ColumnWidth: int) -> str:
        s_Out   = self.ms_Name + ":"
        s32_Len = max(1, s32_ColumnWidth - len(s_Out))
        s_Out  += " " * s32_Len
        s_Out  += self.ms_Value
        return s_Out

class kRecvData:
    def __init__(self) -> None:
        self.me_MesgType      = 0 # eMessageType
        self.mb_RxBlob        = False
        self.ms64_OsTimestamp = 0
        self.mu8_RawBytes     = bytearray(0)
        self.mk_RxFrame : Optional[kRxFrameElmue] = None
        self.mk_TxEcho  : Optional[kTxEchoElmue]  = None
        self.mk_Error   : Optional[kErrorElmue]   = None
        self.mk_String  : Optional[kStringElmue]  = None
        self.mk_Busload : Optional[kBusloadElmue] = None
    
# Raised when the USB device has too many errors --> The caller must abort.
class AbortError(Exception):
    def __init__(self, s_Message):
        super().__init__(s_Message)        

# =========================================================================================

class Candlelight:

    def __init__(self) -> None:
        self.mb_InitDone    = False
        self.mi_OsLibrary   = OsLibrary()
        self.mk_EchoPackets = [kCanPacket() for _ in range(256)]  # Tx packets 1...255
        self.mi_Details: List[kDetail] = []

    def __del__(self) -> None:
        self.Close()

    # --------------------------------------------------------------------

    def Close(self) -> None:
        if self.mi_OsLibrary.IsOpen():
            try:
                self.Reset() # stop the CAN interface and reset all variables in the firmware
            except Exception as e:
                pass         # ignore error. The device may have been disconnected

        self.mi_OsLibrary.Close()
        self.mb_InitDone = False

    # STEP 1)
    # Initialize WinUSB and get the Candlelight structures with board info, capabilities, etc from the firmware
    # k_Device comes from OsLibrary.EnumDevices()
    def Open(self, k_Device) -> None:
        if self.mi_OsLibrary.IsOpen():
           raise RuntimeError("The adapter is already open")

        self.mu8_EchoMarker      = 1  # counter 1...255
        self.ms64_McuRollOver    = 0
        self.ms64_LastMcuStamp   = 0
        self.ms64_TimestampStart = 0
        self.mu64_TxOverflow     = 0
        self.ms32_BlobOffset     = 0
        self.ms32_BlobFrames     = 0
        self.mb_BaudFDSet        = False
        self.mb_InitDone         = False
        self.mb_Started          = False
        self.mb_EnableTxEcho     = True

        self.mi_Details.clear()
        self.mk_EchoPackets = [kCanPacket() for _ in range(256)]

        self.mi_OsLibrary.Open(k_Device)

        self.mk_Info       = self.mi_OsLibrary.GetDevInfo()
        self.mu8_Interface = self.mk_Info.mk_InterfDescr.bInterfaceNumber

        self.mi_Details.append(kDetail("Device Path",        k_Device.ms_DevicePath))
        self.mi_Details.append(kDetail("USB Vendor",         "\"%s\"" % self.mk_Info.ms_Vendor))
        self.mi_Details.append(kDetail("USB Product",        "\"%s\"" % self.mk_Info.ms_Product))
        self.mi_Details.append(kDetail("USB Serial  Nº",     "\"%s\"" % self.mk_Info.ms_Serial))
        self.mi_Details.append(kDetail("USB Interface Name", "\"%s\"" % self.mk_Info.ms_Interface))
        self.mi_Details.append(kDetail("USB Vendor  ID",     "%04X"   % self.mk_Info.mk_DeviceDescr.idVendor))
        self.mi_Details.append(kDetail("USB Product ID",     "%04X"   % self.mk_Info.mk_DeviceDescr.idProduct))
        self.mi_Details.append(kDetail("USB Device Version", Utils.FormatBcdVersion(self.mk_Info.mk_DeviceDescr.bcdDevice)))

        # -------------------------- DFU --------------------------------

        if self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            # The Firmware Update interface has no IN / OUT endpoints. It supports only SETUP requests.
            if self.mk_Info.mk_InterfDescr.bNumEndpoints != 0:
                raise RuntimeError("The device is not a valid Candlelight adapter")

            self.mb_InitDone = True
            return

        # ------------------------ Candlelight --------------------------

        # There must be exactly 2 endpoints: IN (81) and OUT (02)
        if self.mk_Info.mk_InterfDescr.bNumEndpoints != 2:
            raise RuntimeError("The device is not a valid Candlelight adapter")

        # Interface 0 -> Channel 0
        # Interface 1 -> Firmware Update (never comes here)
        # Interface 2 -> Channel 1
        # Interface 3 -> Channel 2
        self.mu8_Channel = self.mu8_Interface
        if self.mu8_Channel > 0:
           self.mu8_Channel -= 1

        self.mk_Info.mu8_Channel = self.mu8_Channel

        self.mi_Details.append(kDetail("USB Endpoint CTRL",   "00,  max packet size: %u byte" % (self.mk_Info.mk_DeviceDescr.bMaxPacketSize0)))
        self.mi_Details.append(kDetail("USB Endpoint IN",   "%02X,  max packet size: %u byte" % (self.mk_Info.mu8_EndpointIN,  self.mk_Info.mu16_MaxPackSizeIN)))
        self.mi_Details.append(kDetail("USB Endpoint OUT",  "%02X,  max packet size: %u byte" % (self.mk_Info.mu8_EndpointOUT, self.mk_Info.mu16_MaxPackSizeOUT)))

        # --------------------------------------------------------------------

        # Reset() should always be the first command.
        # The device may still be open --> close it, which resets all variables in the firmware.
        # And the CANable 2.5 firmware allows to set eDeviceFlags.ELM_DevFlagProtocolElmue which enables debug messages at the very beginnning.
        self.Reset()

        # GS_ReqGetCapabilities is a legacy command supported by all Candlelight's
        self._CtrlTransfer(eDirection.In, eUsbRequest.GS_ReqGetCapabilities, self.mu8_Channel, 
                           ctypes.byref(self.mk_Info.mk_Capability), ctypes.sizeof(kCapabilityClassic))

        self.mk_Info.mb_IsElmueSoft =  (self.mk_Info.mk_Capability.feature & eDeviceFlags.ELM_DevFlagProtocolElmue) > 0
        self.mk_Info.mb_SupportsFD  = ((self.mk_Info.mk_Capability.feature & eDeviceFlags.GS_DevFlagCAN_FD)      > 0 and
                                       (self.mk_Info.mk_Capability.feature & eDeviceFlags.GS_DevFlagBitTimingFD) > 0)

        if self.mk_Info.mb_SupportsFD:
            # GS_ReqGetCapabilitiesFD is a legacy command supported by all Candlelight's that support CAN FD
            self._CtrlTransfer(eDirection.In, eUsbRequest.GS_ReqGetCapabilitiesFD, self.mu8_Channel, 
                               ctypes.byref(self.mk_Info.mk_CapabilityFD), ctypes.sizeof(kCapabilityFD))

        # GS_ReqGetDeviceVersion is a legacy command supported by all Candlelight's
        self._CtrlTransfer(eDirection.In, eUsbRequest.GS_ReqGetDeviceVersion, self.mu8_Channel, 
                           ctypes.byref(self.mk_Info.mk_DeviceVersion), ctypes.sizeof(kDeviceVersion))

        self.mi_Details.append(kDetail("Hardware Version", Utils.FormatBcdVersion(self.mk_Info.mk_DeviceVersion.hw_version_bcd)))
        self.mi_Details.append(kDetail("Firmware Version", Utils.FormatBcdVersion(self.mk_Info.mk_DeviceVersion.sw_version_bcd)))

        if self.mk_Info.mb_IsElmueSoft:  # not BCD encoded
           self.mi_Details.append(kDetail("HAL Version", "%u.%u.%u" % (self.mk_Info.mk_DeviceVersion.hal_ver_high,
                                                                       self.mk_Info.mk_DeviceVersion.hal_ver_mid,
                                                                       self.mk_Info.mk_DeviceVersion.hal_ver_low)))

        self.mi_Details.append(kDetail("Firmware Type",   "CANable 2.5" if self.mk_Info.mb_IsElmueSoft else "Legacy"))
        self.mi_Details.append(kDetail("Supports CAN FD", "Yes"         if self.mk_Info.mb_SupportsFD  else "No"))

        if not self.mk_Info.mb_IsElmueSoft:
            self.mi_Details.append(kDetail("CAN Clock", "%u MHz" % (self.mk_Info.mk_Capability.fclk_can // 1000000)))
            raise RuntimeError("This class supports only devices that have the CANable 2.5 firmware from ElmüSoft.")

        # --------------- Here comes only ElmüSoft firmware ---------------

        # ELM_ReqGetBoardInfo requires ElmüSoft firmware
        self._CtrlTransfer(eDirection.In, eUsbRequest.ELM_ReqGetBoardInfo, self.mu8_Channel, 
                           ctypes.byref(self.mk_Info.mk_BoardInfo), ctypes.sizeof(kBoardInfo))

        # IsBootPinEnabled() cannot be called here because mb_InitDone must be set at the end of this function.
        u16_PinStatus = ctypes.c_uint16(0)
        self._CtrlTransfer(eDirection.In, eUsbRequest.ELM_ReqGetPinStatus, ePinID.BOOT0, 
                           ctypes.byref(u16_PinStatus), ctypes.sizeof(u16_PinStatus))

        self.mi_Details.append(kDetail("Target Board", self.mk_Info.mk_BoardInfo.BoardName.decode("ascii")))
        self.mi_Details.append(kDetail("Processor", "%s, CAN Clock: %u MHz, MCU DeviceID: 0x%X"
                                                    % (self.mk_Info.mk_BoardInfo.McuName.decode("ascii"),
                                                       self.mk_Info.mk_Capability.fclk_can // 1000000,
                                                       self.mk_Info.mk_BoardInfo.McuDeviceID)))

        b_UseQuartz = (self.mk_Info.mk_BoardInfo.BoardFlags & eBoardFlags.Quartz_In_Use) > 0
        self.mi_Details.append(kDetail("Quartz in use", "Yes" if b_UseQuartz else "No"))
        self.mi_Details.append(kDetail("CAN Channel", "%d of %d" % (self.mu8_Channel + 1, self.mk_Info.mk_DeviceVersion.icount + 1)))
        self.mi_Details.append(kDetail("Pin BOOT0", "Enabled" if (u16_PinStatus.value & ePinStatus.Enabled) else "Disabled"))

        if self.mk_Info.mk_DeviceVersion.sw_version_bcd < MIN_FIRMWARE:
            raise RuntimeError("Please upload the latest firmware to the device.")

        assert self.mk_Info.mk_DeviceVersion.sw_version_bcd == MIN_FIRMWARE, "Update MIN_FIRMWARE in the code to the latest firmware version!"

        self.mi_OsLibrary.StartPipes()
        self.mb_InitDone = True

    # --------------------------------------------------------------------

    # STEP 2)  (optional)
    # Define if you want to receive Tx Echo Markers
    def EnableTxEcho(self, b_Enable: bool) -> None:
        self.mb_EnableTxEcho = b_Enable

    # --------------------------------------------------------------------

    # STEP 3)
    # Please read "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder Documentation
    # returns display string with formatted baudrate and samplepoint
    def SetBitrate(self, b_FD: bool, s32_BRP: int, s32_Seg1: int, s32_Seg2: int) -> str:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        if b_FD and not self.mk_Info.mb_SupportsFD:
            raise ValueError("The adapter does not support CAN FD.")

        # NOTE:
        # It is not necessary to check if BRP, Seg1, Seg2 are in the allowed range defined in kTimeMinMax in the Capabilities.
        # If an invalid value is sent the firmware will return an error.
        # The values in kTimeMinMax are only required if you write an algorithm that calculates BRP, Seg1, Seg2
        # automatically from a given baudrate and samplepoint.

        k_Timing = kBitTiming()
        k_Timing.brp  = s32_BRP  # bitrate prescaler
        k_Timing.prop = 0        # Propagation segment, not used, this is already included in Segment 1
        k_Timing.seg1 = s32_Seg1 # Time Segment 1 (Time quantums before samplepoint)
        k_Timing.seg2 = s32_Seg2 # Time Segment 2 (Time quantums after samplepoint)
        k_Timing.sjw  = min(s32_Seg1, s32_Seg2) # Synchronization Jump Width

        e_Requ = eUsbRequest.GS_ReqSetBitTimingFD if b_FD else eUsbRequest.GS_ReqSetBitTiming
        self._CtrlTransfer(eDirection.Out, e_Requ, self.mu8_Channel, 
                           ctypes.byref(k_Timing), ctypes.sizeof(k_Timing))

        # --- Format display string ----

        s32_TotTQ  = 1 + s32_Seg1 + s32_Seg2
        s32_Baud   = self.mk_Info.mk_Capability.fclk_can // s32_BRP // s32_TotTQ
        s32_Sample = 1000 * (1 + s32_Seg1) // s32_TotTQ

        # Do not display 83333 baud as "83k"
        s_Unit = ""
        if s32_Baud  >= 1000000 and (s32_Baud % 1000000) == 0:
           s32_Baud //= 1000000
           s_Unit = "M"
        elif s32_Baud >= 1000 and (s32_Baud % 1000) == 0:
           s32_Baud  //= 1000
           s_Unit = "k"

        if b_FD: self.mb_BaudFDSet = True

        s_Type = "Data   " if b_FD else "Nominal"
        return "%s Baudrate: %u%s, Samplepoint: %u.%u%%" % (s_Type, s32_Baud, s_Unit, s32_Sample // 10, s32_Sample % 10)

    # STEP 4)  (optional)
    # Add one to eight host filters
    # ATTENTION: If you set only an 11 bit filter, no 29 bit ID's will pass and vice versa.
    def AddHostFilter(self, b_29bit: bool, s32_Filter: int, s32_Mask: int) -> None:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        k_Filter = kFilter()
        k_Filter.Operation = eFilterOperation.HostPass_29 if b_29bit else eFilterOperation.HostPass_11
        k_Filter.Filter    = s32_Filter
        k_Filter.Mask      = s32_Mask

        self._CtrlTransfer(eDirection.Out, eUsbRequest.ELM_ReqSetFilter, self.mu8_Channel, 
                           ctypes.byref(k_Filter), ctypes.sizeof(k_Filter))

    # STEP 5)  (optional)
    # set / clear one of 20 bridge filters
    # b_Enable = False and Index == 0x13 --> clear only bridge filter Nº 0x13
    # b_Enable = False and Index == 0xFF --> clear all bridge filters
    # b_Enable = True and b_Block = True  --> set block filter
    # b_Enable = True and b_Block = False --> set pass filter
    def SetBridgeFilter(self, u8_FilterIndex: int, u8_DestChannel: int, b_Enable: bool,
                        b_Block: bool, b_29bit: bool, s32_Filter: int, s32_Mask: int) -> None:

        k_Filter = kFilter()
        k_Filter.Operation   = eFilterOperation.BridgeClear
        k_Filter.Filter      = s32_Filter
        k_Filter.Mask        = s32_Mask
        k_Filter.DestChannel = u8_DestChannel
        k_Filter.Index       = u8_FilterIndex

        if b_Enable:
            if b_Block: k_Filter.Operation = eFilterOperation.BridgeBlock_29 if b_29bit else eFilterOperation.BridgeBlock_11
            else:       k_Filter.Operation = eFilterOperation.BridgePass_29  if b_29bit else eFilterOperation.BridgePass_11

        self._CtrlTransfer(eDirection.Out, eUsbRequest.ELM_ReqSetFilter, self.mu8_Channel, 
                           ctypes.byref(k_Filter), ctypes.sizeof(k_Filter))

    # --------------------------------------------------------------------

    # STEP 6)
    # Connect to CAN bus, turn off the Tx LED
    def Start(self, e_Flags: eDeviceFlags) -> None:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        k_Mode = kDeviceMode()
        k_Mode.flags = e_Flags
        k_Mode.mode  = eDeviceMode.ModeStart

        k_Mode.flags |= eDeviceFlags.ELM_DevFlagProtocolElmue  # required for this demo!
        if self.mk_Info.mk_Capability.feature & eDeviceFlags.ELM_DevFlagSendUsbBlobs:
            k_Mode.flags |= eDeviceFlags.ELM_DevFlagSendUsbBlobs

        self._CtrlTransfer(eDirection.Out, eUsbRequest.GS_ReqSetDeviceMode, self.mu8_Channel, 
                           ctypes.byref(k_Mode), ctypes.sizeof(k_Mode))  # turn off Tx LED

        self.mb_McuTimestamp = (e_Flags & eDeviceFlags.GS_DevFlagTimestamp) > 0
        self.mb_Started = True

    # Stop CAN bus and reset all variables and user settings in the adapter, turn on Tx LED
    def Reset(self) -> None:
        self.mb_Started = False

        # IMPORTANT: Set flag eDeviceFlags.ELM_DevFlagProtocolElmue always to make sure that the device can send debug messages.
        # Should there be a legacy device connected, it will ignore all flags sent with GS_ModeReset
        k_Mode = kDeviceMode()
        k_Mode.flags = eDeviceFlags.ELM_DevFlagProtocolElmue
        k_Mode.mode  = eDeviceMode.ModeReset
        self._CtrlTransfer(eDirection.Out, eUsbRequest.GS_ReqSetDeviceMode, self.mu8_Channel, 
                           ctypes.byref(k_Mode), ctypes.sizeof(k_Mode))

    # ======================================= Send ========================================

    # Send multiple CAN packets in one blob over USB to the firmware.
    # This optimizes the USB speed to the maximum.
    # returns OsTimestamp
    # raises AbortError if the CANable has been disconnected
    def SendPacketBlob(self, k_Packets: List[kCanPacket]) -> int:
        if not self.mb_InitDone or not self.mb_Started:
            raise RuntimeError("The device must be open and started.")

        if (self.mk_Info.mk_Capability.feature & eDeviceFlags.ELM_DevFlagSendUsbBlobs) == 0:
            raise RuntimeError("Blobs are not supported by the firmware")

        # the firmware has a FIFO for max 64 packets
        if len(k_Packets) > CAN_QUEUE_SIZE:
            raise ValueError("Too many Tx packets.")

        k_Blob = kBlob()
        k_Blob.frame_count = len(k_Packets)
        k_Blob.msg_type    = eMessageType.TxBlob

        # WritePipe() does not work with a bytearray!
        u8_Transmit = (ctypes.c_ubyte * MAX_BLOB_SIZE)()
        ctypes.memmove(u8_Transmit, ctypes.addressof(k_Blob), ctypes.sizeof(kBlob))

        s32_Offset = ctypes.sizeof(kBlob)
        for k_Packet in k_Packets:
            s32_Offset = self._TxPacketToTxBytes(k_Packet, u8_Transmit, s32_Offset)

        # Get timestamp immediately before sending the packet
        s64_Timestamp = Utils.GetOsTimestamp()
        self.mi_OsLibrary.WritePipeOut(u8_Transmit, s32_Offset)
        return s64_Timestamp

    # CAN FD packets (b_FDF) can only be sent if a data baudrate has been set before.
    # Remote frames (b_RTR = True): s32_DataLen = 0 --> DLC = 0 will be sent, or s32_DataLen = 1 and u8_Data[0] contains the DLC to send.
    # returns OsTimestamp
    # raises AbortError if the CANable has been disconnected
    def SendPacket(self, k_Packet: kCanPacket) -> int:
        if not self.mb_InitDone or not self.mb_Started:
            raise RuntimeError("The device must be open and started.")

        # WritePipe() does not work with a bytearray!
        u8_Transmit = (ctypes.c_ubyte * 256)()
        s32_Offset = 0
        s32_Offset = self._TxPacketToTxBytes(k_Packet, u8_Transmit, s32_Offset)

        # Get timestamp immediately before sending the packet
        s64_Timestamp = Utils.GetOsTimestamp()
        self.mi_OsLibrary.WritePipeOut(u8_Transmit, s32_Offset)
        return s64_Timestamp

    # If the packet has insufficient bytes to match one of the CAN FD DLC values, it will be padded with PAD_BYTE.
    # returns modified s32_Offset
    def _TxPacketToTxBytes(self, k_Packet: kCanPacket, u8_Transmit: Any, s32_Offset: int) -> int:
        # Pad missing bytes with zero's
        PAD_BYTE = b"\x00"

        if self.mi_OsLibrary.HasPipeErrors():
            raise AbortError("Too many errors. The CANable has a problem or has been disconnected.")

        s32_MaxData = 64 if self.mb_BaudFDSet else 8
        if len(k_Packet.mu8_Data) > s32_MaxData:
            raise ValueError("The CAN data must not be longer than %d bytes" % s32_MaxData)

        # Remote frames do not exist in CAN FD
        if self.mb_BaudFDSet and k_Packet.mb_RTR:
            raise ValueError("A remote frame cannot be sent in CAN FD mode.")

        # FDF and BRS flags require CAN FD
        if not self.mb_BaudFDSet and (k_Packet.mb_FDF or k_Packet.mb_BRS):
            raise ValueError("CAN FD frames can only be sent when a data baudrate has been set.")

        # 3 + 64 messages have been sent to the firmware which were not acknowledged.
        # The adapter is blocked --> report error once only.
        # If no errors were reported in the last 3 seconds the buffer is not full anymore
        if self.mu64_TxOverflow > 0 and (Utils.GetTickMilli() - self.mu64_TxOverflow) < 4000:
           self.mu64_TxOverflow = 0
           raise RuntimeError("Sending is not possible because the Tx buffer is full.")

        u32_ID    = k_Packet.mu32_ID
        u32_MaxID = eCanIdFlags.MASK_29 if k_Packet.mb_29bit else eCanIdFlags.MASK_11
        if u32_ID > u32_MaxID:
            raise ValueError("The CAN ID is invalid.")

        if k_Packet.mb_29bit: u32_ID |= eCanIdFlags.Extended # 29 bit CAN ID
        if k_Packet.mb_RTR:   u32_ID |= eCanIdFlags.RTR      # Remote Transmission Request

        if k_Packet.mb_RTR and len(k_Packet.mu8_Data) > 1:
            raise ValueError("Remote frames contain no data or only one byte that defines the DLC value.")

        # Pad to match one of the CAN FD DLC values
        s32_PadLen = len(k_Packet.mu8_Data)
        if   s32_PadLen > 48: s32_PadLen = 64
        elif s32_PadLen > 32: s32_PadLen = 48
        elif s32_PadLen > 24: s32_PadLen = 32
        elif s32_PadLen > 20: s32_PadLen = 24
        elif s32_PadLen > 16: s32_PadLen = 20
        elif s32_PadLen > 12: s32_PadLen = 16
        elif s32_PadLen >  8: s32_PadLen = 12
        
        # append zero padding bytes
        k_Packet.mu8_Data = bytearray(k_Packet.mu8_Data.ljust(s32_PadLen, PAD_BYTE))

        k_TxFrame = kTxFrameElmue()
        k_TxFrame.header.size     = ctypes.sizeof(k_TxFrame) + len(k_Packet.mu8_Data)
        k_TxFrame.header.msg_type = eMessageType.TxFrame;
        k_TxFrame.can_id          = u32_ID;
        k_TxFrame.flags           = 0;
        if k_Packet.mb_FDF: k_TxFrame.flags |= eFrameFlags.FDF
        if k_Packet.mb_BRS: k_TxFrame.flags |= eFrameFlags.BRS

        if s32_Offset + k_TxFrame.header.size >= len(u8_Transmit):
            raise ValueError("The Tx data is too long.")

        # The STM32G431 supports to store a unique 8 bit marker for each sent frame which is returned when the frame has been acknowledged.
        # The firmware sends the marker back in kTxEchoElmue and we get the sent frame from mk_EchoFrames to display it to the user.
        # 255 markers are far more than enough because the processor has a Tx FIFO for 3 CAN packets and the firmware can store
        # additionally 64 waiting frames in the queue. When a Tx buffer overflow is reported any further SendPacket() is blocked.
        if self.mb_EnableTxEcho:
           self.mu8_EchoMarker = max(1, (self.mu8_EchoMarker + 1) & 0xFF) # If k_TxFrame.marker == 0 --> firmware does not send an echo
           k_TxFrame.marker = self.mu8_EchoMarker;

        # IMPORTANT: The packet must be cloned. The caller may modify it. 
        # Avoid that mk_EchoPackets stores a reference to the orignal packet.
        self.mk_EchoPackets[self.mu8_EchoMarker] = copy.deepcopy(k_Packet)

        p_Dest = ctypes.addressof(u8_Transmit) + s32_Offset
        ctypes.memmove(p_Dest, ctypes.addressof(k_TxFrame), ctypes.sizeof(k_TxFrame))
        s32_Offset += ctypes.sizeof(k_TxFrame)

        # Copying a few bytes is the most complicated nightmare in clumsy Python!
        p_Dest = ctypes.addressof(u8_Transmit) + s32_Offset
        p_Src  = ctypes.addressof(ctypes.c_ubyte.from_buffer(k_Packet.mu8_Data))
        ctypes.memmove(p_Dest, p_Src, len(k_Packet.mu8_Data))
        s32_Offset += len(k_Packet.mu8_Data)

        return s32_Offset

    # Receive any struct derived from kHeader (Rx packet, Tx echo packet, error frame, debug message, busload)
    # returns None if nothing was received during the timeout
    # raises AbortError if the CANable has been disconnected
    def ReceiveData(self, s32_Timeout: int) -> Optional[kRecvData]:
        if not self.mb_InitDone or not self.mb_Started:
            raise RuntimeError("The device must be open and started.")

        if self.mi_OsLibrary.HasPipeErrors():
            raise AbortError("Too many errors. The CANable has a problem or has been disconnected.")

        # Get frames form the IN pipe if there is no pending data in mk_UsbInPacket
        if self.ms32_BlobFrames <= 0:
            self.ms32_BlobFrames = 0
            self.ms32_BlobOffset = 0

            self.mk_UsbInPacket = self.mi_OsLibrary.ReadPipeIn(s32_Timeout)
            if self.mk_UsbInPacket == None: # Timeout
                return None

            k_Blob = kBlob.from_buffer(self.mk_UsbInPacket.mu8_Buffer)
            
            if k_Blob.msg_type == eMessageType.RxBlob:
                self.ms32_BlobFrames = k_Blob.frame_count
                self.ms32_BlobOffset = ctypes.sizeof(kBlob)
                
        # This is totally superfluous. Only required for Python type checker
        assert self.mk_UsbInPacket is not None
                
        k_Header = kHeader.from_address(ctypes.addressof(self.mk_UsbInPacket.mu8_Buffer) + self.ms32_BlobOffset)
                
        if self.ms32_BlobOffset + k_Header.size > self.mk_UsbInPacket.ms32_BytesRead:
            self.ms32_BlobFrames = 0
            raise RuntimeError("Corrupt USB IN data received")

        k_Data = kRecvData()
        k_Data.ms64_OsTimestamp = self.mk_UsbInPacket.ms64_OsTimestamp
        k_Data.mu8_RawBytes     = bytearray(bytes(self.mk_UsbInPacket.mu8_Buffer)[self.ms32_BlobOffset : self.ms32_BlobOffset + k_Header.size])
        k_Data.me_MesgType      = k_Header.msg_type
        k_Data.mb_RxBlob        = self.ms32_BlobFrames > 0  # FIRST

        self.ms32_BlobFrames -= 1  # AFTER
        self.ms32_BlobOffset += k_Header.size

        # Make sure that there are always enough bytes to be copied.
        # If the struct defines a timestamp that is not sent by the firmware --> fill it with zero bytes
        u8_ExtBytes = k_Data.mu8_RawBytes + bytearray(10)
             
        if   k_Header.msg_type == eMessageType.RxFrame: k_Data.mk_RxFrame = kRxFrameElmue.from_buffer(u8_ExtBytes)
        elif k_Header.msg_type == eMessageType.TxEcho:  k_Data.mk_TxEcho  = kTxEchoElmue .from_buffer(u8_ExtBytes)
        elif k_Header.msg_type == eMessageType.Error:   k_Data.mk_Error   = kErrorElmue  .from_buffer(u8_ExtBytes)
        elif k_Header.msg_type == eMessageType.String:  k_Data.mk_String  = kStringElmue .from_buffer(u8_ExtBytes)
        elif k_Header.msg_type == eMessageType.Busload: k_Data.mk_Busload = kBusloadElmue.from_buffer(u8_ExtBytes)
        return k_Data

    def RxFrameToCanPacket(self, k_RecvData: kRecvData) -> kCanPacket:
        k_Frame     = k_RecvData.mk_RxFrame
        u8_RawBytes = k_RecvData.mu8_RawBytes
        
        if k_Frame is None:
            raise ValueError("Invalid parameter")
        
        k_Packet = kCanPacket()
        k_Packet.mu32_ID  =  k_Frame.can_id & eCanIdFlags.MASK_29
        k_Packet.mb_29bit = (k_Frame.can_id & eCanIdFlags.Extended) != 0
        k_Packet.mb_RTR   = (k_Frame.can_id & eCanIdFlags.RTR)      != 0
        k_Packet.mb_FDF   = (k_Frame.flags  & eFrameFlags.FDF)      != 0
        k_Packet.mb_BRS   =  k_Packet.mb_FDF and ((k_Frame.flags & eFrameFlags.BRS) != 0)
        k_Packet.mb_ESI   =  k_Packet.mb_FDF and ((k_Frame.flags & eFrameFlags.ESI) != 0)
        
        s32_DataStart = ctypes.sizeof(kRxFrameElmue)
        if not self.mb_McuTimestamp:
            s32_DataStart -= 4 # data starts behind CAN ID
       
        k_Packet.mu8_Data = u8_RawBytes[s32_DataStart : k_Frame.header.size]
        return k_Packet

    def GetTxEchoPacket(self, k_RecvData: kRecvData) -> kCanPacket:
        if k_RecvData.mk_TxEcho is None:
            raise ValueError("Invalid parameter")
        
        return self.mk_EchoPackets[k_RecvData.mk_TxEcho.marker]

    # Get the content of a debug message from the adapter
    def ConvertStringFrame(self, k_RecvData: kRecvData) -> str:
        if k_RecvData.mk_String is None:
            raise ValueError("Invalid parameter")
        
        k_String    = k_RecvData.mk_String
        u8_RawBytes = k_RecvData.mu8_RawBytes
        s32_AsciiStart = ctypes.sizeof(kStringElmue)
        return u8_RawBytes[s32_AsciiStart : k_String.header.size].decode("ascii", errors="replace")

    # ==========================================================================================

    # Flashes the Rx + Tx LEDs on the board
    # ATTENTION: When you open the adapter with Start() the firmware stops LED flashing
    def Identify(self, b_Blink: bool) -> None:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        if b_Blink: u32_Mode = ctypes.c_uint32(1)
        else:       u32_Mode = ctypes.c_uint32(0)
        self._CtrlTransfer(eDirection.Out, eUsbRequest.GS_ReqIdentify, self.mu8_Channel, 
                           ctypes.byref(u32_Mode), ctypes.sizeof(u32_Mode))

    # Interval = 7 --> report busload in percent every 700 ms.
    # NOTE: The firmware does not report the busload if bus load is permanently 0%.
    def EnableBusLoadReport(self, s32_Interval: int) -> None:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        u8_Interval = ctypes.c_uint8(s32_Interval)
        self._CtrlTransfer(eDirection.Out, eUsbRequest.ELM_ReqSetBusLoadReport, self.mu8_Channel, 
                           ctypes.byref(u8_Interval), ctypes.sizeof(u8_Interval))

    # Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
    # Enabling the pin needs not to be implemented here.
    # The pin is automatically enabled when entering DFU mode with EnterDfuMode()
    def DisableBootPin(self) -> None:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        k_PinStatus = kPinStatus()
        k_PinStatus.Operation = ePinOperation.Disable
        k_PinStatus.PinID     = ePinID.BOOT0
        self._CtrlTransfer(eDirection.Out, eUsbRequest.ELM_ReqSetPinStatus, self.mu8_Channel, 
                           ctypes.byref(k_PinStatus), ctypes.sizeof(k_PinStatus))

    # Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
    def IsBootPinEnabled(self) -> bool:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        # The requested pin ID must be transmitted in SETUP.wValue because a USB IN request cannot otherwise transmit parameters to the firmware.
        u16_PinStatus = ctypes.c_uint16(0)
        self._CtrlTransfer(eDirection.In, eUsbRequest.ELM_ReqGetPinStatus, ePinID.BOOT0, 
                           ctypes.byref(u16_PinStatus), ctypes.sizeof(u16_PinStatus))

        return (u16_PinStatus.value & ePinStatus.Enabled) > 0

    # Write user data to flash memory. The firmware also stores the length of the data and returns the same data in ReadFlash()
    # A segment of the STM32G431 has 2 kB. Segment 0 is the last segment in the flash memory.
    def WriteFlash(self, u8_Segment: int, u8_Data: bytearray) -> None:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        # If the user passes an inmutable byte array it must be copied to a new writable buffer for WinUSB.
        u8_Buffer = (ctypes.c_ubyte * len(u8_Data)).from_buffer(u8_Data)
        self._CtrlTransfer(eDirection.Out, eUsbRequest.ELM_ReqWriteFlash, u8_Segment, 
                           u8_Buffer, len(u8_Data))

    # Read user data from the flash memory that was written before with WriteFlash()
    # A segment of the STM32G431 has 2 kB. Segment 0 is the last segment in the flash memory.
    def ReadFlash(self, u8_Segment: int) -> bytearray:
        if not self.mb_InitDone or self.mu8_Interface == FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the Candlelight interface.")

        u8_Buffer = (ctypes.c_ubyte * 4096)()
        s32_Read  = self._CtrlTransfer(eDirection.In, eUsbRequest.ELM_ReqReadFlash, u8_Segment, 
                                       u8_Buffer, len(u8_Buffer))
        return bytearray(u8_Buffer[:s32_Read])

    # --------------------------------------------------------------------

    # Send a SETUP request to the firmware
    # u32_DataSize must be the expected byte count to be received from the firmware or to be sent to the firmware.
    # u8_Request must be eUsbRequest for interface 0 and eDfuRequest for interface 1.
    # This function can obtain the feedback from the ElmüSoft firmware, but works also with legacy firmware.
    # returns the byte count that the USB SETUP request has returned
    # ATTENTION: p_Data must point to writable memory, otherwise ERROR_NOACCESS.
    # For IN transfers the received bytes from USB are written into the buffer that is passed as pointer in p_Data
    def _CtrlTransfer(self, e_Dir: eDirection, u8_Request: int, u16_Value: int,
                      p_Data: Any, u16_DataSize: int) -> int:

        # A USB Control Transfer must not exceed 4 kB.
        if u16_DataSize > 4096:
            raise ValueError("The Tx data is too long.")

        # The Candlelight interface implements Vendor requests while the Firmware Update interface implements Class requests.
        e_Type = eSetupType.Class if (self.mu8_Interface == FIRMW_UPDATE_INTERFACE) else eSetupType.Vendor

        k_Setup = kSetup()
        k_Setup.bRequestType = eSetupRecip.Interface | e_Type | e_Dir
        k_Setup.bRequest     = u8_Request
        k_Setup.wValue       = u16_Value          # Channel / PinID for ELM_ReqGetPinStatus
        k_Setup.wIndex       = self.mu8_Interface # Destination interface (0,2,3 = Candlelight, 1 = Firmware Update)
        k_Setup.wLength      = u16_DataSize

        # -------- Execute Request ------------

        # ATTENTION: returns ERROR_NOACCESS if p_Data is not writable !
        s32_CmdErr, s32_CmdBytes = self.mi_OsLibrary.ControlTransfer(k_Setup, p_Data)

        # The Firmware Update interface sends no feedback
        if self.mu8_Interface != FIRMW_UPDATE_INTERFACE:

            # ---------- Get Feedback -------------

            # ALWAYS get the feedback, even if the previous command execution did NOT return an error!
            # In second stage of the SETUP request the firmware can NOT stall the endpoint which is the only way to alert an USB error.
            u8_Feedback = ctypes.c_uint8(0) # The feedback is a one byte response
            
            k_Setup.bRequestType = eSetupRecip.Interface | eSetupType.Vendor | eDirection.In
            k_Setup.bRequest     = eUsbRequest.ELM_ReqGetLastError
            k_Setup.wLength      = ctypes.sizeof(u8_Feedback)

            s32_FbkErr, s32_FbkBytes = self.mi_OsLibrary.ControlTransfer(k_Setup, ctypes.byref(u8_Feedback))

            # --------- Process Errors ------------

            # u8_Feedback is only valid if s32_FbkErr == NO_ERROR
            # if a legacy board is connected it will not understand request ELM_ReqGetLastError 
            # --> Endpoint stalled --> s32_FbkErr = ERROR_GEN_FAILURE
            if s32_FbkErr == NO_ERROR and u8_Feedback.value != eFeedback.Success:
                self._RaiseFeedbackError(eFeedback(u8_Feedback.value))

        if s32_CmdErr:
            OsLibrary.RaiseLastError(self.mi_OsLibrary, s32_CmdErr)

        # When reading flash memory the firmware will return less bytes than requested, this is not an error.
        if e_Dir == eDirection.In and s32_CmdBytes < u16_DataSize and u8_Request != eUsbRequest.ELM_ReqReadFlash:
            raise RuntimeError("Invalid USB Rx data was received from the device")

        return s32_CmdBytes

    # =======================================================================================================================

    # Formats a timestamp with 1 µs precision
    # returns "HH:MM:SS.mmm.µµµ"
    # k_RecvData may contain a timestamp if eDeviceFlags.GS_DevFlagTimestamp is set --> mb_McuTimestamp = true
    # otherwise use ms64_OsTimestamp which comes from GetOsTimestamp() at packet reception
    def FormatTimestamp(self, k_RecvData: Optional[kRecvData], s64_OsTimestamp: int) -> str:
        if not self.mb_Started:
            return "Not Initialized " # the variable mb_McuTimestamp is not yet valid
            
        s64_Stamp = -1
        if self.mb_McuTimestamp:
            if k_RecvData is not None:
                # These 3 messages send firmware timestamps
                if   k_RecvData.mk_TxEcho  is not None: s64_Stamp = k_RecvData.mk_TxEcho .timestamp
                elif k_RecvData.mk_RxFrame is not None: s64_Stamp = k_RecvData.mk_RxFrame.timestamp
                elif k_RecvData.mk_Error   is not None: s64_Stamp = k_RecvData.mk_Error  .timestamp

            if s64_Stamp >= 0:
                # The bug has been fixed in firmware 14.09.2026 that timestamps were jumping 133µs backwards
                if self.ms64_LastMcuStamp > s64_Stamp:
                   OsLibrary.PrintConsole(eConsole.Yellow, "Timestamp jumps %d µs backwards. Update the firmware.\n" % (self.ms64_LastMcuStamp - s64_Stamp))
                
                # The 32 bit firmware timestamp will roll over after 1 hour, this must be detected here.
                if s64_Stamp              < 0x050000000 and \
                   self.ms64_LastMcuStamp > 0x0A0000000:   # ignore small jumps
                   self.ms64_McuRollOver += 0x100000000
                    
                self.ms64_LastMcuStamp = s64_Stamp
                
                # roll-over compensated 64 bit timestamp
                s64_Stamp += self.ms64_McuRollOver
                
        else: # Operating System performance counter timestamps are used
            s64_Stamp = s64_OsTimestamp

        if s64_Stamp < 0:
            return "No Timestamp    "

        if self.ms64_TimestampStart == 0:
           self.ms64_TimestampStart = s64_Stamp;

        s64_Stamp -= self.ms64_TimestampStart;

        s32_Micro = s64_Stamp % 1000
        s64_Stamp //= 1000
        s32_Milli = s64_Stamp % 1000
        s64_Stamp //= 1000
        s32_Sec   = s64_Stamp % 60
        s64_Stamp //= 60
        s32_Min   = s64_Stamp % 60
        s64_Stamp //= 60
        s32_Hour  = s64_Stamp % 24

        return "%02u:%02u:%02u.%03u.%03u" % (s32_Hour, s32_Min, s32_Sec, s32_Milli, s32_Micro)

    def FormatCanPacket(self, k_Packet: kCanPacket) -> str:
        s_Frame = "";
        if k_Packet.mb_29bit: s_Frame = "%08X: " % (k_Packet.mu32_ID & eCanIdFlags.MASK_29)
        else:                 s_Frame = "%03X: " % (k_Packet.mu32_ID & eCanIdFlags.MASK_11)

        # For remote frames the DLC (0...8) may be transmitted in the first data byte.
        # The display of "7E8: RTR [5]" means that a remote request with DLC = 5 has been sent/received
        if k_Packet.mb_RTR:
            s_Frame += "RTR [" # Remote Transmission Request
            if len(k_Packet.mu8_Data) > 0:
                s_Frame += chr(k_Packet.mu8_Data[0] + ord('0'))
            else:
                s_Frame += "0"
            s_Frame += "]"
        else:
            s_Frame += Utils.FormatHexBytes(k_Packet.mu8_Data)

            if k_Packet.mb_FDF or k_Packet.mb_BRS or k_Packet.mb_ESI:
                s_Frame += "-"

            if k_Packet.mb_FDF: s_Frame += " FDF" # Flexible Datarate Frame
            if k_Packet.mb_BRS: s_Frame += " BRS" # Bitrate Switch
            if k_Packet.mb_ESI: s_Frame += " ESI" # Error Indicator

        return s_Frame

    # From the multiple flags that have been defined by previous programmers we check only those which the CANable 2.5 firmware sets.
    # return value 1 = formatted error message
    # return value 2 = eErrorBusStatus --> the current bus status (active, warning, passive, off)
    # return value 3 = eErrorLevel     --> the error level (low, ledium, high)
    def FormatCanErrors(self, k_RecvData: kRecvData) -> Tuple[str, eErrorBusStatus, eErrorLevel]:
        k_Error = k_RecvData.mk_Error
        if k_Error is None:
            raise ValueError("Invalid parameter")
        
        e_ID    : eErrFlagsCanID = k_Error.err_id
        e_Byte1 : eErrFlagsByte1 = k_Error.err_data[1]
        e_Byte2 : eErrFlagsByte2 = k_Error.err_data[2]
        e_App   : eErrorAppFlags = k_Error.err_data[5]

        if e_App & eErrorAppFlags.CanTxOverflow:
            self.mu64_TxOverflow = Utils.GetTickMilli() # block sending further packets
        else:
            self.mu64_TxOverflow = 0

        e_BusStatus : eErrorBusStatus = eErrorBusStatus.StatusActive
        e_Level     : eErrorLevel     = eErrorLevel.Low

        s_Mesg = ""
        if e_ID & eErrFlagsCanID.Bus_is_off:
            e_BusStatus = eErrorBusStatus.StatusOff
            e_Level     = eErrorLevel.High
            s_Mesg += "Bus Off, "
        elif e_Byte1 & (eErrFlagsByte1.Rx_Passive_status_reached | eErrFlagsByte1.Tx_Passive_status_reached):
            e_BusStatus = eErrorBusStatus.StatusPassive
            e_Level     = eErrorLevel.High
            s_Mesg += "Bus Passive, "
        elif e_Byte1 & (eErrFlagsByte1.Rx_Errors_at_warning_level | eErrFlagsByte1.Tx_Errors_at_warning_level):
            e_BusStatus = eErrorBusStatus.StatusWarning
            e_Level     = eErrorLevel.Medium
            s_Mesg += "Bus Warning, "
        else: # Active
            if e_Byte1 & eErrFlagsByte1.Bus_is_back_active:
                s_Mesg += "Back to Active, "
            else:
                s_Mesg += "Bus Active, "

        # All errors generated by the firmware are bigger problems (Level High)
        if e_App > 0: e_Level = eErrorLevel.High
        if e_App & eErrorAppFlags.CanRxFail:     s_Mesg += "Rx Failed, "
        if e_App & eErrorAppFlags.CanTxFail:     s_Mesg += "Tx Failed, "
        if e_App & eErrorAppFlags.CanTxTimeout:  s_Mesg += "Tx Timeout, "
        if e_App & eErrorAppFlags.CanTxOverflow: s_Mesg += "CAN Tx Overflow, "
        if e_App & eErrorAppFlags.UsbInOverflow: s_Mesg += "USB IN Overflow, "

        # Error cause
        if e_ID    & eErrFlagsCanID.No_ACK_received:              s_Mesg += "No ACK received, "
        if e_ID    & eErrFlagsCanID.CRC_Error:                    s_Mesg += "CRC Error, "
        if e_Byte2 & eErrFlagsByte2.Bit_stuffing_error:           s_Mesg += "Bit Stuffing Error, "
        if e_Byte2 & eErrFlagsByte2.Frame_format_error:           s_Mesg += "Frame Format Error, " # e.g. CAN FD frame received in classic mode
        if e_Byte2 & eErrFlagsByte2.Unable_to_send_dominant_bit:  s_Mesg += "Dominant Bit Error, "
        if e_Byte2 & eErrFlagsByte2.Unable_to_send_recessive_bit: s_Mesg += "Recessive Bit Error, "

        if k_Error.err_data[6] > 0: s_Mesg += "Tx Errors: %u, " % k_Error.err_data[6]
        if k_Error.err_data[7] > 0: s_Mesg += "Rx Errors: %u, " % k_Error.err_data[7]

        return s_Mesg.rstrip(", "), e_BusStatus, e_Level
        
    @staticmethod
    def _RaiseFeedbackError(e_Feedback : eFeedback) -> None:
        if   e_Feedback == eFeedback.InvalidCommand:      raise ValueError  ("The command is invalid.")
        elif e_Feedback == eFeedback.InvalidParameter:    raise ValueError  ("One of the parameters is invalid.")
        elif e_Feedback == eFeedback.AdapterMustBeOpen:   raise RuntimeError("This command cannot be executed before opening the adapter.")
        elif e_Feedback == eFeedback.AdapterMustBeClosed: raise RuntimeError("This command cannot be executed after  opening the adapter.")
        elif e_Feedback == eFeedback.ErrorFromHAL:        raise RuntimeError("The HAL from ST Microelectronics has reported an error.")
        elif e_Feedback == eFeedback.UnsupportedFeature:  raise RuntimeError("The feature is not implemented or not supported by the adapter.")
        elif e_Feedback == eFeedback.TxBufferFull:        raise RuntimeError("Sending is not possible because the Tx buffer is full.")
        elif e_Feedback == eFeedback.BusIsOff:            raise RuntimeError("Sending is not possible because the processor is blocked in BusOff state.")
        elif e_Feedback == eFeedback.NoTxInSilentMode:    raise RuntimeError("Sending is not possible because the adapter is in bus monitoring mode.")
        elif e_Feedback == eFeedback.BaudrateNotSet:      raise RuntimeError("The baudrate has not been set.")
        elif e_Feedback == eFeedback.OptBytesProgrFailed: raise RuntimeError("Programming the Option Bytes failed.")
        elif e_Feedback == eFeedback.ResetRequired:       raise RuntimeError("Please reconnect the USB cable or press the Reset button.")
        elif e_Feedback == eFeedback.ParamOutOfRange:     raise ValueError  ("A paramter is outside the valid range.")
        else: raise RuntimeError("Unknown feedback code %d received from the device." % e_Feedback)

    # ================================== DFU ========================================

    # Switch the Candlelight into firmware update mode.
    # This function requires that you have called EnumDevices(Interface = 1) before to get access to interface 1.
    # IMPORTANT:
    # This will ONLY work if the Candlelight has the new CANable 2.5 firmware from ElmüSoft.
    # ALL legacy Candlelights have a sloppy firmware that does not respond to the Microsoft OS descriptor request for interface 1.
    # The consequence is that Windows cannot install the WinUSB driver for the Firmware Update interface and EnumDevices() will not find the device.
    # ATTENTION:
    # This works only if the device is in Candlelight mode. If the device is already in DFU mode it will fail.
    # If the device is already in DFU mode you cannot use the WinUSB driver, you need the STtube30 driver from ST Microelectronics.
    # If you want to update the firmware use HUD ECU Hacker which comes with a very comfortable STM32 Firmware Programmer.
    def EnterDfuMode(self) -> None:
        if not self.mb_InitDone or self.mu8_Interface != FIRMW_UPDATE_INTERFACE:
            raise RuntimeError("The adapter must be opened for the DFU interface.")

        # The legacy firmware would have entered immediately in DFU mode and _CtrlTransfer() would have returned ERROR_GEN_FAILURE.
        # But the CANable 2.5 firmware responds correctly to all SETUP requests because it makes a delay of 300 ms before entering DFU mode.
        self._CtrlTransfer(eDirection.Out, eDfuRequest.RequDetach, 0, None, 0)

        k_Status = kDfuStatus()
        try:
            self._CtrlTransfer(eDirection.In, eDfuRequest.RequGetStatus, 0, 
                               ctypes.byref(k_Status), ctypes.sizeof(k_Status))
        except Exception as e:
            # A legacy device enters boot mode immediately and _CtrlTransfer() raises ERROR_GEN_FAILURE.
            self.Close()
            return
            
        # Here k_Status.State is either AppIdle or AppDetach or Error.

        # returning AppDetach has been added by ElmüSoft to the firmware and means that the user must reconnect the USB cable.
        # This happens only if the pin BOOT0 was disabled before calling EnterDfuMode()
        if k_Status.State == eDfuState.AppDetach:
            self._RaiseFeedbackError(eFeedback.ResetRequired) # The user must reconnect the USB cable now.

        # Since firmware 17.May.2026 the feedback code is transferred in StringIdx.
        # Feedback = UnsupportedFeature, AdapterMustBeClosed, OptBytesProgrFailed
        if k_Status.State == eDfuState.Error and 0 < k_Status.StringIdx < 255:
            self._RaiseFeedbackError(k_Status.StringIdx)

        # The device will enter DFU mode in 300 ms --> the WinUSB handle is not valid anymore.
        self.Close()

# ===================================== Helpers =====================================

    def EnumDevices(self, b_GetCandlelight: bool) -> List[kUsbDevice]:
        return self.mi_OsLibrary.EnumDevices(b_GetCandlelight)

    def GetDetails(self) -> List[kDetail]:
        return self.mi_Details

    def GetDeviceInfo(self):
        return copy.deepcopy(self.mi_OsLibrary.GetDevInfo())


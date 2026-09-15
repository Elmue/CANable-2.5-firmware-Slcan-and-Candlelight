
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

# Candlelight_def.py must be identical with Candlelight_def.h in the firmware.
import ctypes
import time
from dataclasses     import dataclass, field
from typing          import Dict, Any
from functools       import total_ordering
from enum            import IntEnum
from Candlelight_def import *

# must be equal to FIRMW_UPDATE_INTERFACE in usb_class.h in the firmware
FIRMW_UPDATE_INTERFACE = 1

# Timeout for writing the OUT pipe and for Control Transfer (500 ms is far more than required)
PIPE_TIMEOUT = 500

# ---------------

# standard USB device descriptor
class kDeviceDescriptor(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("bLength",            ctypes.c_uint8),
        ("bDescriptorType",    ctypes.c_uint8),
        ("bcdUSB",             ctypes.c_uint16),
        ("bDeviceClass",       ctypes.c_uint8),
        ("bDeviceSubClass",    ctypes.c_uint8),
        ("bDeviceProtocol",    ctypes.c_uint8),
        ("bMaxPacketSize0",    ctypes.c_uint8),
        ("idVendor",           ctypes.c_uint16),
        ("idProduct",          ctypes.c_uint16),
        ("bcdDevice",          ctypes.c_uint16),
        ("iManufacturer",      ctypes.c_uint8),
        ("iProduct",           ctypes.c_uint8),
        ("iSerialNumber",      ctypes.c_uint8),
        ("bNumConfigurations", ctypes.c_uint8),
    ]

# ---------------

# standard USB interface descriptor
class kInterfaceDescriptor(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("bLength",            ctypes.c_uint8),
        ("bDescriptorType",    ctypes.c_uint8),
        ("bInterfaceNumber",   ctypes.c_uint8),
        ("bAlternateSetting",  ctypes.c_uint8),
        ("bNumEndpoints",      ctypes.c_uint8),
        ("bInterfaceClass",    ctypes.c_uint8),
        ("bInterfaceSubClass", ctypes.c_uint8),
        ("bInterfaceProtocol", ctypes.c_uint8),
        ("iInterface",         ctypes.c_uint8),
    ]

# =============== USB SETUP Request ================

# Bits 0,1,2,3,4 of kSetup.bRequestType
class eSetupRecip(IntEnum):
    Device    = 0x00
    Interface = 0x01
    Endpoint  = 0x02
    Other     = 0x03
    #            .... 0x1F,

# ---------------

# Bits 5,6 of kSetup.bRequestType
class eSetupType(IntEnum):
    Standard = 0x00  # 0 << 5
    Class    = 0x20  # 1 << 5
    Vendor   = 0x40  # 2 << 5

# ---------------

# Bit 7 of kSetup.bRequestType, also used for endpoints
class eDirection(IntEnum):
    Out = 0x00
    In  = 0x80

# ---------------

# standard USB Setup request
class kSetup(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("bRequestType", ctypes.c_uint8),  # eSetupRecip | eSetupType | eDirection
        ("bRequest",     ctypes.c_uint8),  # GS_ReqGetCapabilities,... / DFU_RequDetach, DFU_RequGetStatus,...
        ("wValue",       ctypes.c_uint16), # CAN Channel / ePinID for ELM_ReqGetPinStatus
        ("wIndex",       ctypes.c_uint16), # Interface number (0 = Candlelight, 1 = DFU)
        ("wLength",      ctypes.c_uint16), # Byte count
    ]


# ================= DFU =================

# See "DFU Functional Descriptor 1.1.pdf" in subfolder "Documentation".

# These requests can be sent to the firmware update interface.
# In DFU mode they are all functional, but require the STtube30 driver from ST Microelectronics.
# In APP mode the Candlelight exposes a reduced Firmware Update interface which implements only DFU_RequDetach and DFU_RequGetStatus.
class eDfuRequest(IntEnum):
    RequDetach      = 0  # RequType = 0x21, Tells device to detach and re-enter DFU mode (wValue = Timeout)
    RequDownload    = 1  # RequType = 0x21, Download firmware data to device (wValue = BlockNumber)
    RequUpload      = 2  # RequType = 0xA1, Upload firmware data from device
    RequGetStatus   = 3  # RequType = 0xA1, Get device status and poll timeout (6 byte)
    RequClearStatus = 4  # RequType = 0x21, Clear current device status
    RequGetState    = 5  # RequType = 0xA1, Get current device state (1 byte)
    RequAbort       = 6  # RequType = 0x21, Abort current operation

# This is sent in byte 0 (Status) of kDfuStatus from a DFU_RequGetStatus request
class eDfuStatus(IntEnum):
    OK             = 0  # No error condition is present.
    ErrTarget      = 1  # File is not targeted for use by this device.
    ErrFile        = 2  # File is for this device but fails some vendor-specific verification test.
    ErrWrite       = 3  # Device is unable to write memory.
    ErrErase       = 4  # Memory erase function failed.
    ErrCheckErased = 5  # Memory erase check failed.
    ErrProg        = 6  # Program memory function failed.
    ErrVerify      = 7  # Programmed memory failed verification.
    ErrAddress     = 8  # Cannot program memory due to received address that is out of range.
    ErrNotDone     = 9  # Received DFU_DNLOAD with wLength = 0, but device does not think it has all of the data yet.
    ErrFirmware    = 10 # Device’s firmware is corrupt.  It cannot return to run-time (non-DFU) operations.
    ErrVendor      = 11 # StringIdx indicates a vendor-specific error.
    ErrUSBR        = 12 # Device detected unexpected USB reset signaling.
    ErrPOR         = 13 # Device detected unexpected power on reset.
    ErrUnknown     = 14 # Something went wrong, but the device does not know what it was.
    ErrStallEP     = 15 # Device stalled an unexpected request.

# This is sent in byte 4 (State) of kDfuStatus from a DFU_RequGetStatus request
class eDfuState(IntEnum):
    AppIdle           = 0  # Device is running its normal application mode.
    AppDetach         = 1  # Device is running its normal application, has received the DFU_DETACH request, and is waiting for a USB reset.
    DfuIdle           = 2  # Device is operating in the DFU mode and is waiting for requests.
    DownloadSync      = 3  # Device has received a block and is waiting for the host to solicit the status via DFU_GETSTATUS.
    DownloadBusy      = 4  # Device is programming a control-write block into its nonvolatile memories.
    DownloadIdle      = 5  # Device is processing a download operation, expecting DFU_DNLOAD requests.
    ManifestSync      = 6  # Device has received the final block of firmware and waits for DFU_GETSTATUS to begin Manifestation phase
    Manifest          = 7  # Device is in the Manifestation phase.
    ManifestWaitReset = 8  # Device has programmed its memories and is waiting for a USB reset or a power-on reset
    UploadIdle        = 9  # Device is processing an upload operation.
    Error             = 10 # An error has occurred. Awaiting the DFU_CLRSTATUS request.
    # -------------
    UploadSync        = 0x91
    UploadBusy        = 0x92

# response to DFU_RequGetStatus request (size = 6 byte)
class kDfuStatus(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("Status",      ctypes.c_uint8),  # eDfuStatus
        ("PollTimeout", ctypes.c_uint8 * 3),
        ("State",       ctypes.c_uint8),  # eDfuState
        ("StringIdx",   ctypes.c_uint8),  # string index for proprietary vendor error messages (see DfuStatus_ErrVendor)
    ]

# -------------------------------------------------------------

# This class contains all the details of the connected Candlelight device.
# This class can be obtained with GetDeviceInfo() in Candlelight.py
class kDevInfo:
    def __init__(self):
        kDevInfo.Clear(self)

    def Clear(self):
        # the following members are set in Open() in OsLibrary.py
        self.ms_Vendor           = "" # from device descriptor
        self.ms_Product          = "" # from device descriptor
        self.ms_Serial           = "" # from device descriptor
        self.ms_Interface        = "" # from interface descriptor
        self.mu8_EndpointIN      = 0  # e.g. 0x81
        self.mu8_EndpointOUT     = 0  # e.g. 0x02
        self.mu16_MaxPackSizeIN  = 0  # max packet size for IN  endpoint (64 bytes for Full Speed USB)
        self.mu16_MaxPackSizeOUT = 0  # max packet size for OUT endpoint (64 bytes for Full Speed USB)
        self.mk_DeviceDescr      = kDeviceDescriptor()    # entire device descriptor
        self.mk_InterfDescr      = kInterfaceDescriptor() # entire interface descriptor
    
        # the following members are set in Open() in Candlelight.py
        self.mu8_Channel      = 0                    # CAN channel 0,1,2
        self.mb_IsElmueSoft   = False                # The adapter supports the ElmüSoft protocol
        self.mb_SupportsFD    = False                # The adapter supports CAN FD
        self.mk_Capability    = kCapabilityClassic() # see Candlelight_def.py
        self.mk_CapabilityFD  = kCapabilityFD()      # see Candlelight_def.py
        self.mk_DeviceVersion = kDeviceVersion()     # see Candlelight_def.py
        self.mk_BoardInfo     = kBoardInfo()         # see Candlelight_def.py


# A USB packet that was received on the IN pipe
class kUsbInPacket:
    def __init__(self) -> None:
        self.mu8_Buffer       = (ctypes.c_ubyte * MAX_BLOB_SIZE)()
        self.ms32_BytesRead   = 0
        self.ms32_Error       = 0
        self.ms64_OsTimestamp = 0  # Timestamp with 1µs precision from operating system

# This struct is filled by EnumDevices() in OsLibrary.py
# The information is displayed to the user, so he can select one of the connected USB devices.
@total_ordering
class kUsbDevice:
    def __init__(self) -> None:
        self.ms_Product:      str = "" # from Device Descriptor
        self.ms_SerialNo:     str = "" # from Device Descriptor
        self.ms_Interface:    str = "" # from Interface Descriptor
        self.ms32_Interface:  int = 0  # zero-based interface index
        self.mpi_LinuxDevice: Any = 0  # on Linux: libusb_device*
        # Windows = "\\?\USB#VID_1D50&PID_606F&MI_00#7&1B930F3C&0&0000#{C15B4308-04D3-11E6-B3EA-6057189E6443}"
        # Linux   = "Bus number: 2, Device address: 3"
        self.ms_DevicePath:   str = ""

    # The Firmware Update Interface (1) has no CAN channels --> return -1
    # The Candlelight interfaces are 0,2,3,... --> display as CAN Channel 1,2,3,...
    def GetCanChannel(self) -> int:
        if self.ms32_Interface == 0:
            return 1   # display one-based channel number
        elif self.ms32_Interface == FIRMW_UPDATE_INTERFACE:
            return -1  # invalid
        else:
            return self.ms32_Interface

    # Sort by by Serial Number and then by Channel number
    def __lt__(self, i_Dev2: "kUsbDevice") -> bool:
        if not isinstance(i_Dev2, kUsbDevice):
            return NotImplemented

        if self.ms_SerialNo != i_Dev2.ms_SerialNo:
            return self.ms_SerialNo < i_Dev2.ms_SerialNo

        return self.ms32_Interface < i_Dev2.ms32_Interface

# =========================================================================================

class Utils:

    # return time counter in milliseconds (64 bit)
    @staticmethod
    def GetTickMilli() -> int:
        return int(time.perf_counter() * 1000)
        
    # Create a timestamp with 1 µs precision.
    # It is recommended to turn off transmission of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
    # Then this function is used as a replacement to generate a timestamp on reception of a USB packet and when sending a packet.
    @staticmethod
    def GetOsTimestamp() -> int:
        # Convert nanoseconds to microseconds
        return time.perf_counter_ns() // 1000        

    # returns "02 67 5E C7 FF "
    @staticmethod
    def FormatHexBytes(u8_Data: bytearray) -> str:
        return "".join(f"{b:02X} " for b in u8_Data)

    # 0x00000000 --> "0"
    # 0x00000011 --> "11"
    # 0x00000105 --> "1.5"
    # 0x11223344 --> "11.22.33.44"
    # 0x00YYMMDD --> "Day.Month.Year"
    @staticmethod
    def FormatBcdVersion(u32_Version: int) -> str:
        if u32_Version == 0:
            return "0"

        # BCD encoded 0x00YYMMDD
        if 0x250101 < u32_Version < 0x991231:
            u8_Day   =  u32_Version        & 0xFF
            u8_Month = (u32_Version >>  8) & 0xFF
            u8_Year  = (u32_Version >> 16) & 0xFF

            i_Months = {
                0x01: "Jan",
                0x02: "Feb",
                0x03: "Mar",
                0x04: "Apr",
                0x05: "May",
                0x06: "Jun",
                0x07: "Jul",
                0x08: "Aug",
                0x09: "Sep",
                0x10: "Oct",
                0x11: "Nov",
                0x12: "Dec",
            }

            s_MonthName = i_Months.get(u8_Month)
            if s_MonthName and 1 <= u8_Day <= 0x31:
                return "%X.%s.%02X" % (u8_Day, s_MonthName, u8_Year)

        # regular BCD version number "3.14.5" (skip leading zeroes)
        s_Version = ""
        for s32_Shift in range(24, -1, -8):
            u8_Part = (u32_Version >> s32_Shift) & 0xFF
            if s_Version:
               s_Version += ".%X" % u8_Part
            elif u8_Part > 0:
               s_Version += "%X"  % u8_Part

        return s_Version

    # Get a debug dump of a struct or class
    @staticmethod
    def ObjectToString(obj) -> str:
        lines = ["[%s]" % type(obj).__name__]

        # Check if the object is a ctypes.Structure instance
        if isinstance(obj, ctypes.Structure):
            for field in type(obj)._fields_:
                field_name = field[0]
                field_type = field[1]
                val = getattr(obj, field_name)

                val_repr: Any = None

                # 1. Handle ctypes Arrays
                if issubclass(type(val), ctypes.Array):
                    val_repr = list(val)
                    type_str = "Array[%s]" % type(val)._type_.__name__

                # 2. Handle ctypes Simple Data Types / Custom Enum Subclasses
                elif isinstance(val, ctypes._SimpleCData):
                    raw_val = val.value
                    val_repr = raw_val

                    # Try to map numeric enum value to its class variable name (e.g., 2 -> 'Bulk')
                    val_type = type(val)
                    for attr_name in dir(val_type):
                        if not attr_name.startswith("__"):
                            attr_val = getattr(val_type, attr_name)
                            if (
                                isinstance(attr_val, (int, val_type))
                                and attr_val == raw_val
                            ):
                                val_repr = "%s (%d)" % (attr_name, raw_val)
                                break

                    type_str = getattr(field_type, "__name__", str(field_type))

                # 3. Standard types
                else:
                    val_repr = val
                    type_str = getattr(field_type, "__name__", str(field_type))

                lines.append("  %s (%s): %s" % (field_name, type_str, val_repr))

        # Fallback to standard Python objects (vars / __dict__)
        elif hasattr(obj, "__dict__"):
            for field_name, val in vars(obj).items():
                type_str = type(val).__name__
                lines.append("  %s (%s): %s" % (field_name, type_str, val))

        # Generic fallback for C++ / Pybind11 / SWIG objects without __dict__
        else:
            for field_name in dir(obj):
                if field_name.startswith("__"):
                    continue
                val = getattr(obj, field_name)
                if callable(val):
                    continue
                lines.append(
                    "  %s (%s): %s" % (field_name, type(val).__name__, val)
                )

        return "\n".join(lines) + "\n\n"
        
        
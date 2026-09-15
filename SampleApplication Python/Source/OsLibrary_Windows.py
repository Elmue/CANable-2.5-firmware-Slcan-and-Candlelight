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

# =======================================================================================================
#
#  This class contains code for Windows. It has been tested on Python 3.8
#  NOTE: This class uses WinUSB by purpose:
#  The WinUSB driver is part of the operating system and installed 100% automatically
#  when connecting the device for the first time.
#  On the other hand when using libusb on Windows the user would be forced to download
#  and install a driver manually that has no advantage over WinUSB.
#
# =======================================================================================================

import time
import threading
import platform
import ctypes
from ctypes import wintypes
from typing import Tuple, List, Dict, Optional
from enum   import IntEnum
from Utils  import *

# Fix Python's buggy definition: 
# On Windows BYTE is unsigned. Python defines it as c_byte which is WRONG!
wintypes.BYTE = ctypes.c_ubyte # type: ignore[assignment]

kernel32 = ctypes.windll.kernel32
winusb   = ctypes.windll.winusb
setupapi = ctypes.windll.setupapi
advapi32 = ctypes.windll.advapi32
msvcrt   = ctypes.cdll.msvcrt

# ============= Console Colors ===============

# Windows Console Foreground Colors
FOREGROUND_BLUE      = 0x1
FOREGROUND_GREEN     = 0x2
FOREGROUND_RED       = 0x4
FOREGROUND_INTENSITY = 0x8

# Demo Application Console colors
class eConsole(IntEnum):
    White   = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
    Grey    = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_BLUE
    Cyan    = FOREGROUND_GREEN | FOREGROUND_BLUE  | FOREGROUND_INTENSITY
    Magenta = FOREGROUND_RED   | FOREGROUND_BLUE  | FOREGROUND_INTENSITY
    Yellow  = FOREGROUND_RED   | FOREGROUND_GREEN | FOREGROUND_INTENSITY
    Lime    = FOREGROUND_GREEN | FOREGROUND_INTENSITY
    Red     = FOREGROUND_RED   | FOREGROUND_INTENSITY
    Blue    = FOREGROUND_BLUE  | FOREGROUND_INTENSITY
    Brown   = FOREGROUND_RED   | FOREGROUND_GREEN
    Green   = FOREGROUND_GREEN

# ========== Win32 API Definitions ===============

# Windwos API Errors
NO_ERROR                      = 0
ERROR_ACCESS_DENIED           = 5
ERROR_NOT_ENOUGH_MEMORY       = 8
ERROR_GEN_FAILURE             = 31
ERROR_NO_MORE_ITEMS           = 259
ERROR_IO_PENDING              = 997
ERROR_TIMEOUT                 = 1460

GENERIC_READ                  = 0x80000000
GENERIC_WRITE                 = 0x40000000
OPEN_EXISTING                 = 3
FILE_ATTRIBUTE_NORMAL         = 0x00000080
FILE_FLAG_OVERLAPPED          = 0x40000000

PIPE_TRANSFER_TIMEOUT         = 0x03

USB_DEVICE_DESCRIPTOR_TYPE    = 0x01
USB_STRING_DESCRIPTOR_TYPE    = 0x03
RAW_IO                        = 0x07

INVALID_HANDLE_VALUE          = -1
DIGCF_PRESENT                 = 0x02
DIGCF_DEVICEINTERFACE         = 0x10
SPDRP_BASE_CONTAINERID        = 0x24

HKEY_LOCAL_MACHINE            = wintypes.HKEY(0x80000002)
KEY_QUERY_VALUE               = 0x01
KEY_ENUMERATE_SUB_KEYS        = 0x08

WAIT_OBJECT_0                 = 0
WAIT_TIMEOUT                  = 0x102
INFINITE                      = 0xFFFFFFFF

THREAD_PRIORITY_TIME_CRITICAL = 15

STD_INPUT_HANDLE              = -10
STD_OUTPUT_HANDLE             = -11

KEY_EVENT                     = 0x01
VK_RETURN                     = 0x0D

FORMAT_MESSAGE_FROM_SYSTEM    = 0x1000
FORMAT_MESSAGE_IGNORE_INSERTS = 0x0200

LANGUAGE_ENGLISH_USA          = 0x409

class USBD_PIPE_TYPE(IntEnum):
    Control     = 0
    Isochronous = 1
    Bulk        = 2
    Interrupt   = 3

class WINUSB_PIPE_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("PipeType",          wintypes.DWORD), # USBD_PIPE_TYPE
        ("PipeId",            wintypes.BYTE),
        ("MaximumPacketSize", wintypes.USHORT),
        ("Interval",          wintypes.BYTE),
    ]

class OVERLAPPED(ctypes.Structure):
    _fields_ = [
        ("Internal",     ctypes.c_ulonglong),
        ("InternalHigh", ctypes.c_ulonglong),
        ("Offset",       wintypes.DWORD),
        ("OffsetHigh",   wintypes.DWORD),
        ("hEvent",       wintypes.HANDLE),
    ]

class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", wintypes.DWORD),
        ("Data2", wintypes.WORD),
        ("Data3", wintypes.WORD),
        ("Data4", wintypes.BYTE * 8),
    ]

class COORD(ctypes.Structure):
    _fields_ = [
        ("X", wintypes.SHORT),
        ("Y", wintypes.SHORT),
    ]

# This function returns a COORD struct by value
kernel32.GetLargestConsoleWindowSize.restype = COORD

class SMALL_RECT(ctypes.Structure):
    _fields_ = [
        ("Left",   wintypes.SHORT),
        ("Top",    wintypes.SHORT),
        ("Right",  wintypes.SHORT),
        ("Bottom", wintypes.SHORT),
    ]

# size = 16 bytes
class KEY_EVENT_RECORD(ctypes.Structure):
    class _Chars(ctypes.Union):
        _fields_ = [
            ("UnicodeChar", wintypes.WCHAR),
            ("AsciiChar",   wintypes.CHAR),
        ]

    _fields_ = [
        ("bKeyDown",          wintypes.BOOL),
        ("wRepeatCount",      wintypes.WORD),
        ("wVirtualKeyCode",   wintypes.WORD),
        ("wVirtualScanCode",  wintypes.WORD),
        ("uChar",             _Chars),
        ("dwControlKeyState", wintypes.DWORD),
    ]

class INPUT_RECORD(ctypes.Structure):
    class _Event(ctypes.Union): # size = 16 bytes
        _fields_ = [
            ("KeyEvent",              KEY_EVENT_RECORD),
          # ("MouseEvent",            MOUSE_EVENT_RECORD),
          # ("WindowBufferSizeEvent", WINDOW_BUFFER_SIZE_RECORD),
          # ("MenuEvent",             MENU_EVENT_RECORD),
          # ("FocusEvent",            FOCUS_EVENT_RECORD),
        ]

    _fields_ = [
        ("EventType", wintypes.WORD),
        ("Event",     _Event),
    ]

class SP_DEVICE_INTERFACE_DATA(ctypes.Structure):
    _fields_ = [
        ("cbSize",             wintypes.DWORD),
        ("InterfaceClassGuid", GUID),
        ("Flags",              wintypes.DWORD),
        ("Reserved",           ctypes.POINTER(wintypes.ULONG)),
    ]

class SP_DEVINFO_DATA(ctypes.Structure):
    _fields_ = [
        ("cbSize",    wintypes.DWORD),
        ("ClassGuid", GUID),
        ("DevInst",   wintypes.DWORD),
        ("Reserved",  ctypes.POINTER(wintypes.ULONG)),
    ]

# for easier coding the definition of this struct has been modified to have 1000 characters instead of one
class SP_DEVICE_INTERFACE_DETAIL_DATA_W(ctypes.Structure):
    _fields_ = [
        ("cbSize",     wintypes.DWORD),
        ("DevicePath", wintypes.WCHAR * 1000),
    ]

class DEVPROPKEY(ctypes.Structure):
    _fields_ = [
        ("fmtid", GUID),
        ("pid",   wintypes.DWORD),
    ]

DEVPKEY_Device_BusReportedDeviceDesc = DEVPROPKEY(
    GUID(0x540b947e, 0x8b40, 0x45bc, (wintypes.BYTE * 8)(0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2)), 4,
)

DEVPKEY_Device_Parent = DEVPROPKEY(
    GUID(0x4340a6c5, 0x93fa, 0x4706, (wintypes.BYTE * 8)(0x97, 0x2c, 0x7b, 0x64, 0x80, 0x08, 0xa5, 0xa7)), 8,
)

IS_64BIT = platform.architecture()[0] == '64bit'

# ============= Candlelight Constants ============

# up to 30 USB IN packets can be stored in the Rx FIFO
RX_FIFO_MAX_COUNT = 30

# Interface 0 "{c15b4308-04d3-11e6-b3ea-6057189e6443}"
GUID_CANDLELIGHT  = GUID(0xc15b4308, 0x04d3, 0x11e6, (wintypes.BYTE * 8)(0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43))

# Interface 1 "{c25b4308-04d3-11e6-b3ea-6057189e6443}"
# This GUID can be used to switch the device into DFU mode. Requires the CANable 2.5 firmware from ElmüSoft.
GUID_FIRMW_UPDATE = GUID(0xc25b4308, 0x04d3, 0x11e6, (wintypes.BYTE * 8)(0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43))

# static instances
gh_ConsoleOut = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
gh_ConsoleIn  = kernel32.GetStdHandle(STD_INPUT_HANDLE)

# =========================================================================================

class OsLibrary:

    def __init__(self) -> None:
        self.mh_ReceiveEvent : wintypes.HANDLE = kernel32.CreateEventW(None, False, False, None)
        self.mh_ThreadEvent  : wintypes.HANDLE = kernel32.CreateEventW(None, False, False, None)
        self.mh_Device       = wintypes.HANDLE()
        self.mh_WinUsb       = wintypes.HANDLE()
        self.mb_ThreadRuns   = False
        self.mk_Info         = kDevInfo()
        self.mk_RxFifo       = [kUsbInPacket() for _ in range(RX_FIFO_MAX_COUNT)]

        # This lock must be used for each access to:
        # mk_RxFifo, mh_ReceiveEvent, ms32_FifoCount, ms32_FifoReadIdx, mb_FifoOverflow
        self.mk_Lock = threading.RLock()

    def __del__(self) -> None:
        if self.mh_ReceiveEvent:
            kernel32.CloseHandle(self.mh_ReceiveEvent)
            self.mh_ReceiveEvent = wintypes.HANDLE()

        if self.mh_ThreadEvent:
            kernel32.CloseHandle(self.mh_ThreadEvent)
            self.mh_ThreadEvent = wintypes.HANDLE()

    # --------------------------------------------------------------------

    # Called from Open() in Candlelight.py only if the device is not already open
    # k_Device comes from EnumDevices()
    # k_Device->ms_WinNtPath = "\\?\USB#VID_1D50&PID_606F&MI_00#7&20E43BBC&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
    def Open(self, k_Device) -> None:
        self.mu32_RxPipeErrors   = 0
        self.mu32_TxPipeErrors   = 0
        self.ms32_FifoCount      = 0
        self.ms32_FifoReadIdx    = 0
        self.mb_FifoOverflow     = False
        self.mb_AbortThread      = False
        self.mk_Info.Clear()

        # IMPORTANT:
        # Do NOT set FILE_SHARE_READ or FILE_SHARE_WRITE here!
        # This assures that any other application that tries to open the device at the same time will get ERROR_ACCESS_DENIED.
        # NOTE:
        # Here we enable Overlapped mode although we do not use a OVERLAPPED structure. This is unusual.
        # But it works here because we set a timeout with WinUsb_SetPipePolicy(PIPE_TRANSFER_TIMEOUT)
        self.mh_Device = kernel32.CreateFileW(k_Device.ms_DevicePath, GENERIC_READ | GENERIC_WRITE, 0, None,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, None)

        if int(self.mh_Device) == INVALID_HANDLE_VALUE:
            s32_Error = kernel32.GetLastError()
            if s32_Error == ERROR_ACCESS_DENIED:
               raise RuntimeError("Access denied. Probably the device is already open elsewhere.")
            else:
                OsLibrary.RaiseLastError(self, s32_Error)

        if not winusb.WinUsb_Initialize(self.mh_Device, ctypes.byref(self.mh_WinUsb)):
            s32_Error = kernel32.GetLastError()
            if s32_Error == ERROR_NOT_ENOUGH_MEMORY:
               raise RuntimeError("The WinUSB driver is not installed correctly")
            else:
                OsLibrary.RaiseLastError(self, s32_Error)

        # Set timeout for control pipe (500 ms is far more than enough)
        u32_Timeout = wintypes.DWORD(PIPE_TIMEOUT)
        if not winusb.WinUsb_SetPipePolicy(self.mh_WinUsb, 0, PIPE_TRANSFER_TIMEOUT,
                                           ctypes.sizeof(u32_Timeout), ctypes.byref (u32_Timeout)):
            OsLibrary.RaiseLastError(self)

        # Get Device Descriptor
        u32_Read = wintypes.DWORD(0)
        if not winusb.WinUsb_GetDescriptor(self.mh_WinUsb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                                           ctypes.byref (self.mk_Info.mk_DeviceDescr),
                                           ctypes.sizeof(kDeviceDescriptor),
                                           ctypes.byref (u32_Read)):
            OsLibrary.RaiseLastError(self)

        # Get Interface Descriptor
        # Windows uses a unique s_DevicePath for each interface. There is no need to specify an interface number here.
        # The device path defines which interface is opened with CreateFile().
        # "{c15b4308-04d3-11e6-b3ea-6057189e6443}" opens interface 0, 2, 3
        # "{c25b4308-04d3-11e6-b3ea-6057189e6443}" opens interface 1
        if not winusb.WinUsb_QueryInterfaceSettings(self.mh_WinUsb, 0, ctypes.byref(self.mk_Info.mk_InterfDescr)):
            OsLibrary.RaiseLastError(self)

        # Microsoft manipulates iProduct in the device descriptor to point to the string for the interface name.
        # In the vast majority of USB devices we find: iManufacturer = 1, iProduct = 2, iSerialNumber = 3.
        # WinUSB sets iProduct = iInterface which is in case of the Candlelight the string for interface 0.
        # We try to fix this here to get the string of the device descriptor instead of the interface descriptor.
        if (self.mk_Info.mk_DeviceDescr.iManufacturer == 1 and
            self.mk_Info.mk_DeviceDescr.iSerialNumber == 3):
            self.mk_Info.mk_DeviceDescr.iProduct = 2

        self.mk_Info.ms_Vendor    = self._ReadStringDescriptor(self.mk_Info.mk_DeviceDescr.iManufacturer, LANGUAGE_ENGLISH_USA)
        self.mk_Info.ms_Product   = self._ReadStringDescriptor(self.mk_Info.mk_DeviceDescr.iProduct,      LANGUAGE_ENGLISH_USA)
        self.mk_Info.ms_Serial    = self._ReadStringDescriptor(self.mk_Info.mk_DeviceDescr.iSerialNumber, LANGUAGE_ENGLISH_USA)
        self.mk_Info.ms_Interface = self._ReadStringDescriptor(self.mk_Info.mk_InterfDescr.iInterface,    LANGUAGE_ENGLISH_USA)

        # Get the 2 pipes of the Candlelight interface (the Firmware Update interface has bNumEndpoints == 0)
        for P in range(self.mk_Info.mk_InterfDescr.bNumEndpoints):
            k_PipeInfo = WINUSB_PIPE_INFORMATION()
            if not winusb.WinUsb_QueryPipe(self.mh_WinUsb, 0, P, ctypes.byref(k_PipeInfo)):
                OsLibrary.RaiseLastError(self)

            if k_PipeInfo.PipeType != USBD_PIPE_TYPE.Bulk:
                raise RuntimeError("The device is not a Candlelight adapter.")

            if k_PipeInfo.PipeId & eDirection.In:
                self.mk_Info.mu8_EndpointIN      = k_PipeInfo.PipeId
                self.mk_Info.mu16_MaxPackSizeIN  = k_PipeInfo.MaximumPacketSize
            else:  # OUT
                self.mk_Info.mu8_EndpointOUT     = k_PipeInfo.PipeId
                self.mk_Info.mu16_MaxPackSizeOUT = k_PipeInfo.MaximumPacketSize

    # This is not called for the Firmware Update interface which has no endpoints
    def StartPipes(self) -> None:
        u8_True = wintypes.BYTE(1)
        if not winusb.WinUsb_SetPipePolicy(self.mh_WinUsb, self.mk_Info.mu8_EndpointIN, RAW_IO,
                                           ctypes.sizeof(u8_True), ctypes.byref(u8_True)):
            OsLibrary.RaiseLastError(self)

        # Set timeout for OUT pipe (500 ms is far more than enough)
        # This timeout assures that pipe operations are not blocking eternally as an OVERLAPPED structure is not used.
        u32_Timeout = wintypes.DWORD(PIPE_TIMEOUT)
        if not winusb.WinUsb_SetPipePolicy(self.mh_WinUsb, self.mk_Info.mu8_EndpointOUT, PIPE_TRANSFER_TIMEOUT,
                                           ctypes.sizeof(u32_Timeout), ctypes.byref(u32_Timeout)):
            OsLibrary.RaiseLastError(self)

        i_Thread = threading.Thread(target=self._PipeThread, daemon=True, name="PipeThread")
        i_Thread.start()

    # Called from Candlelight.Close()
    def Close(self) -> None:
        # abort PipeThread and wait until it has exited. Timeout is 1 second.
        i = 0
        while self.mb_ThreadRuns and i < 100:
            self.mb_AbortThread = True
            kernel32.SetEvent(self.mh_ThreadEvent)
            kernel32.Sleep(10)
            i += 1

        if self.mh_WinUsb:
            winusb.WinUsb_Free(self.mh_WinUsb)
            self.mh_WinUsb = wintypes.HANDLE()

        if self.mh_Device:
            kernel32.CloseHandle(self.mh_Device)
            self.mh_Device = wintypes.HANDLE()

    # --------------------------------------------------------------------

    # Read a string descriptor
    def _ReadStringDescriptor(self, u8_Index: int, u16_LanguageID: int) -> str:
        # If the descriptor does not define a string, the index is zero. This is not an error.
        if u8_Index == 0:
            return ""

        # 256 bytes = 2 byte header + 127 Unicode chars
        u8_Buffer = (wintypes.BYTE * 256)()
        u32_Read  =  wintypes.DWORD(0)
        if not winusb.WinUsb_GetDescriptor(self.mh_WinUsb, USB_STRING_DESCRIPTOR_TYPE, u8_Index, u16_LanguageID,
                                           ctypes.byref(u8_Buffer), ctypes.sizeof(u8_Buffer), ctypes.byref(u32_Read)):
            return "Error %d reading USB string" % kernel32.GetLastError() # Do not raise an exception if a string cannot be obtained

        u8_Length = u8_Buffer[0]
        u8_Type   = u8_Buffer[1]

        if (u8_Type != USB_STRING_DESCRIPTOR_TYPE or u8_Length != u32_Read.value or
            u32_Read.value < 2 or (u32_Read.value & 1) > 0):
            return "Error Crippled String"

        # The unicode string data starts after the first 2 bytes
        s32_StrLen = (u32_Read.value - 2) // 2
        return ctypes.wstring_at(ctypes.addressof(u8_Buffer) + 2, s32_StrLen)

    # ===================================== CTRL Pipe =====================================

    # Send SETUP packet and optionally additional data bytes as IN or OUT transfer.
    # Timeout has been set to 500 ms in Open()
    # returns ErrorCode and the count of bytes that were transferred, does not raise exceptions
    # ATTENTION: WinUsb_ControlTransfer() returns ERROR_NOACCESS if p_Data is not writable !
    # For IN transfers the received bytes from USB are written into the buffer that is passed as pointer in p_Data
    def ControlTransfer(self, k_Setup: kSetup, p_Data: Any) -> Tuple[int, int]:
        u32_Transferred = wintypes.DWORD(0)
        if winusb.WinUsb_ControlTransfer(self.mh_WinUsb, k_Setup, p_Data,
                                         k_Setup.wLength, ctypes.byref(u32_Transferred), None):
            return 0, u32_Transferred.value
        else:
            return kernel32.GetLastError(), 0

    # ===================================== OUT Pipe ======================================

    # Timeout has been set to 500 ms in StartPipes()
    # ATTENTION In p_Data you must pass a writable buffer, otherwise ERROR_NOACCESS from WinUSB!
    def WritePipeOut(self, p_Data: Any, u32_TxLen: int) -> None:
        u32_Transferred = wintypes.DWORD(0)
        if not winusb.WinUsb_WritePipe(self.mh_WinUsb, self.mk_Info.mu8_EndpointOUT,
                                       p_Data, u32_TxLen, ctypes.byref(u32_Transferred), None):
            self.mu32_TxPipeErrors += 1
            OsLibrary.RaiseLastError(self)

        self.mu32_TxPipeErrors = 0

    # ====================================== IN Pipe =======================================

    # ------------------------------------------------------------------------------------------------------------------------------------
    # IMPORTANT:
    # WinUSB is different from other Windows API's.
    # An overlapped read operation with WinUsb_ReadPipe() is totally different from the usual overlapped read operation on Windows.
    # This extremely important detail is not documented by Microsoft, nor does Microsoft give us any useful sample code.
    # Therefore you find this implemented totally wrong in Cangaroo and in Candle.NET on Github.
    # You cannot use the typical scheme ReadPipe() --> ERROR_IO_PENDING --> WaitForSingleObject(Timeout) --> GetOverlappedResult().
    # If you do this with a short timeout (50 ms) you will receive NOTHING !!!
    # If you do it with a longer timeout (500 ms) it will work mostly, but some USB IN packets will be lost.
    # To not lose USB packets the timeout for WaitForSingleObject() *MUST* be INIFINTE.
    # The reason is that WinUSB starts polling the USB IN endpoint when you call WinUsb_ReadPipe().
    # But when this operation is aborted by an elapsed timeout, any USB IN packet that was about to arrive will be dropped.
    # WinUSB does NOT have an internal buffer to store packets that arrive between calls to WinUsb_ReadPipe().
    # So the unusual is here that we use an overlapped read operation with an INFINITE timeout.
    # This requires to run in a thread and the overlapped event is required to abort the thread.
    # ------------------------------------------------------------------------------------------------------------------------------------

    def _PipeThread(self) -> None:
        self.mb_AbortThread = False
        self.mb_ThreadRuns  = True
        kernel32.ResetEvent(self.mh_ReceiveEvent)

        k_Overlapped = OVERLAPPED()
        k_Overlapped.hEvent = self.mh_ThreadEvent

        # This thread is time critical
        # If Rx Events are not polled fast enough USB packets may get lost because WinUSB does not have an internal Rx buffer.
        # WinUsb_ReadPipe() must be called as fast as possible again after a USB packet was received.
        kernel32.SetThreadPriority(kernel32.GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL)

        while not self.mb_AbortThread:
            with self.mk_Lock:
                if self.ms32_FifoCount >= RX_FIFO_MAX_COUNT:
                   self.mb_FifoOverflow = True

            # if an overflow occurred, stop reading USB packets and inform the caller that it is polling too slowly.
            if self.mb_FifoOverflow:
                kernel32.Sleep(50)
                continue

            with self.mk_Lock:
                s32_FifoWriteIdx = (self.ms32_FifoReadIdx + self.ms32_FifoCount) % RX_FIFO_MAX_COUNT
                k_FifoWrite      = self.mk_RxFifo[s32_FifoWriteIdx]

            u32_Read  = wintypes.DWORD(0)
            s32_Error = NO_ERROR

            # Initiate Overlapped Read Operations via WinUSB
            success = winusb.WinUsb_ReadPipe(self.mh_WinUsb, self.mk_Info.mu8_EndpointIN,
                                             ctypes.byref (k_FifoWrite.mu8_Buffer),
                                             ctypes.sizeof(k_FifoWrite.mu8_Buffer),
                                             None, ctypes.byref(k_Overlapped))
            if success:
                assert False, "Error WinUsb_ReadPipe terminated synchronously"
            else:
                s32_Error = kernel32.GetLastError()
                if s32_Error == ERROR_IO_PENDING:
                    s32_Error = NO_ERROR

                    # mh_ThreadEvent = k_Overlapped.hEvent is set when a USB IN packet was received and in Close() to abort the thread
                    u32_Result = kernel32.WaitForSingleObject(self.mh_ThreadEvent, INFINITE)
                    if self.mb_AbortThread:
                        break

                    if u32_Result == WAIT_TIMEOUT:
                        s32_Error = ERROR_TIMEOUT  # This should never happen with timeout = INFINITE
                    elif u32_Result == WAIT_OBJECT_0:
                        if winusb.WinUsb_GetOverlappedResult(self.mh_WinUsb, ctypes.byref(k_Overlapped),
                                                             ctypes.byref(u32_Read), False):
                            self.mu32_RxPipeErrors = 0
                        else:
                            s32_Error = kernel32.GetLastError() # Error from WinUsb_GetOverlappedResult()
                    else:
                        s32_Error = kernel32.GetLastError() # WAIT_FAILED from WaitForSingleObject() This should never happen

            k_FifoWrite.ms32_BytesRead   = u32_Read.value
            k_FifoWrite.ms32_Error       = s32_Error
            k_FifoWrite.ms64_OsTimestamp = Utils.GetOsTimestamp()

            # Increment write index for the next ReadPipe, leave read index unchanged
            with self.mk_Lock:
                self.ms32_FifoCount += 1
                kernel32.SetEvent(self.mh_ReceiveEvent)

            # If the CANable has been disconnected an error ERROR_BAD_COMMAND or ERROR_GEN_FAILURE will be reported in each loop.
            # This high priority thread must be slowed down to avoid that it consumes
            # a lot of CPU power running in an endless loop and to avoid that the FIFO overflows with errors.
            if s32_Error != NO_ERROR:
                self.mu32_RxPipeErrors += 1
                kernel32.Sleep(50)

        self.mb_ThreadRuns = False

    # return the next frame from the Rx FIFO
    # If the Rx FIFO is empty -> wait for more data from USB.
    # If no data received during timeout --> return None.
    def ReadPipeIn(self, s32_Timeout: int) -> Optional[kUsbInPacket]:
        with self.mk_Lock:
            k_FifoRead    = self.mk_RxFifo[self.ms32_FifoReadIdx]
            s32_Available = self.ms32_FifoCount
            if s32_Available > 0:
                kernel32.ResetEvent(self.mh_ReceiveEvent)

        if s32_Available == 0:  # nothing received
            # After all messages in the FIFO have been returned inform once about the FIFO overflow.
            if self.mb_FifoOverflow:
                with self.mk_Lock:
                     self.mb_FifoOverflow = False
                raise RuntimeError("USB Rx FIFO overflow. Polling is too slow.") # in the Windows demo app the reason is the slow Windows console.

            u32_Result = kernel32.WaitForSingleObject(self.mh_ReceiveEvent, s32_Timeout)
            if u32_Result == WAIT_TIMEOUT:
                return None

            with self.mk_Lock:
                s32_Available = self.ms32_FifoCount

            if s32_Available == 0:
                return None

        with self.mk_Lock:
            self.ms32_FifoReadIdx = (self.ms32_FifoReadIdx + 1) % RX_FIFO_MAX_COUNT
            self.ms32_FifoCount -= 1

        if k_FifoRead.ms32_Error > 0:
           OsLibrary.RaiseLastError(self, k_FifoRead.ms32_Error)

        return k_FifoRead

    # =================================== Enumerate USB Devices ==================================

    # Returns device name, serial number and path like "\\?\USB#VID_1D50&PID_606F&MI_00#7&20E43BBC&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
    # b_GetCandlelight = false -> this function enumerates the Firmware Update interfaces using GUID_FIRMW_UPDATE, but only if the device has the ElmüSoft firmware.
    # All legacy fimrware versions were buggy and unable to send the two Microsoft OS descriptors correctly, so the driver is not installed.
    def EnumDevices(self, b_GetCandlelight: bool) -> List[kUsbDevice]:

        i_Serials = OsLibrary._EnumSerialNumbers()

        k_Guid = GUID_CANDLELIGHT if b_GetCandlelight else GUID_FIRMW_UPDATE

        # Enumerate all USB devices with the given GUID that are currently connected
        h_DevInfo = setupapi.SetupDiGetClassDevsW(ctypes.byref(k_Guid), None, None, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
        if int(h_DevInfo) == INVALID_HANDLE_VALUE:
            OsLibrary.RaiseLastError()

        h_ParentInfo = setupapi.SetupDiCreateDeviceInfoList(None, None)
        if int(h_ParentInfo) == INVALID_HANDLE_VALUE:
            setupapi.SetupDiDestroyDeviceInfoList(h_DevInfo)
            OsLibrary.RaiseLastError()

        k_InterfaceData = SP_DEVICE_INTERFACE_DATA()
        k_InterfaceData.cbSize = ctypes.sizeof(SP_DEVICE_INTERFACE_DATA)

        k_DevicInfo = SP_DEVINFO_DATA()
        k_DevicInfo.cbSize = ctypes.sizeof(SP_DEVINFO_DATA)

        k_DetailData = SP_DEVICE_INTERFACE_DETAIL_DATA_W()

        # cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) with one single character = sizeof(DWORD) + sizeof(WCHAR) = 6
        # The 64 Bit SetupApi.dll was compiled with 8 byte padding and requires cbSize to be set correctly
        # The 32 Bit SetupApi.dll was compiled with 4 byte padding and requires cbSize to be set correctly
        if IS_64BIT: k_DetailData.cbSize = 8
        else:        k_DetailData.cbSize = 6

        u32_PropType = wintypes.DWORD()
        u32_RequSize = wintypes.DWORD()

        c_Interface = (wintypes.WCHAR * 128)()  # USB Interface string                  (max 127 unicode chars)
        c_Product   = (wintypes.WCHAR * 128)()  # USB device descriptor product string  (max 127 unicode chars)
        c_Parent    = (wintypes.WCHAR * 256)()
        c_Container = (wintypes.WCHAR *  50)()

        i_Devices : List[kUsbDevice] = []
        Idx = 0
        while True:
            if not setupapi.SetupDiEnumDeviceInterfaces(h_DevInfo, None, ctypes.byref(k_Guid),
                                                        Idx, ctypes.byref(k_InterfaceData)):
                s32_Error = kernel32.GetLastError()
                if s32_Error == ERROR_NO_MORE_ITEMS:
                   s32_Error = NO_ERROR  # All existing devices have been enumerated. This is not an error.
                break

            # Get the NT path of the device that will be passed to CreateFile()
            if not setupapi.SetupDiGetDeviceInterfaceDetailW(h_DevInfo, ctypes.byref(k_InterfaceData),
                                                             ctypes.byref(k_DetailData), ctypes.sizeof(k_DetailData),
                                                             ctypes.byref(u32_RequSize), ctypes.byref(k_DevicInfo)):
                s32_Error = kernel32.GetLastError()
                break

            # Get the 'ContainerID' GUID string (since Windows 7) which is identical for all interfaces of the same device
            if not setupapi.SetupDiGetDeviceRegistryPropertyW(h_DevInfo, ctypes.byref(k_DevicInfo), SPDRP_BASE_CONTAINERID,
                                                              None, ctypes.cast(c_Container, ctypes.POINTER(wintypes.BYTE)),
                                                              ctypes.sizeof(c_Container), None):
                s32_Error = kernel32.GetLastError()
                break

            # Get the Interface string from Interface Descriptor (max USB string descriptor length = 127 Unicode chars)
            # If a legacy interface descriptor has iInterface == 0 (no string available) this will return the product string instead.
            if not setupapi.SetupDiGetDevicePropertyW(h_DevInfo, ctypes.byref(k_DevicInfo),
                                                      ctypes.byref(DEVPKEY_Device_BusReportedDeviceDesc),
                                                      ctypes.byref(u32_PropType),
                                                      ctypes.cast(c_Interface, ctypes.POINTER(wintypes.BYTE)),
                                                      ctypes.sizeof(c_Interface), ctypes.byref(u32_RequSize), 0):
                s32_Error = kernel32.GetLastError()
                break

            # Go one level up from USB interface to USB device --> c_Parent = "USB\VID_1D50&PID_606F\208A347D4B4550142"
            if not setupapi.SetupDiGetDevicePropertyW(h_DevInfo, ctypes.byref(k_DevicInfo), ctypes.byref(DEVPKEY_Device_Parent),
                                                      ctypes.byref(u32_PropType),
                                                      ctypes.cast(c_Parent, ctypes.POINTER(wintypes.BYTE)),
                                                      ctypes.sizeof(c_Parent), ctypes.byref(u32_RequSize), 0):
                s32_Error = kernel32.GetLastError()
                break

            if not setupapi.SetupDiOpenDeviceInfoW(h_ParentInfo, c_Parent, None, 0, ctypes.byref(k_DevicInfo)):
                s32_Error = kernel32.GetLastError()
                break

            # Get the Product string from Device Descriptor (max USB string descriptor length = 127 Unicode chars)
            if not setupapi.SetupDiGetDevicePropertyW(h_ParentInfo, ctypes.byref(k_DevicInfo),
                                                      ctypes.byref(DEVPKEY_Device_BusReportedDeviceDesc), ctypes.byref(u32_PropType),
                                                      ctypes.cast(c_Product, ctypes.POINTER(wintypes.BYTE)), ctypes.sizeof(c_Product),
                                                      ctypes.byref(u32_RequSize), 0):
                s32_Error = kernel32.GetLastError()
                break

            # ---------------------

            k_UsbDev = kUsbDevice()
            k_UsbDev.ms_Product   = c_Product  .value # "Candlelight 2.5 - OleksiiDual"
            k_UsbDev.ms_Interface = c_Interface.value # "CAN FD Interface 2"
            s_Container           = c_Container.value # "{2c7d6257-7635-5dc8-ad4f-f4d3ad209925}"

            # "\\?\usb#vid_1d50&pid_606f&mi_00#7&1b930f3c&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
            k_UsbDev.ms_DevicePath = k_DetailData.DevicePath.upper()
            k_UsbDev.ms_SerialNo   = i_Serials.get(s_Container.upper(), "")

            # If a legacy Candlelight device does not expose a string in the Candlelight interface,
            # Windows returns the Product string instead --> both are identical ("canable gs_usb")
            if k_UsbDev.ms_Interface == k_UsbDev.ms_Product:
               k_UsbDev.ms_Interface = "[N/A]"

            # Append interface number for multi-interface (MI) adapters
            s32_Pos = k_UsbDev.ms_DevicePath.find("&MI_0")
            if s32_Pos > 0:
                # MI_00 --> CAN channel 1
                # MI_01 --> Firmware Update
                # MI_02 --> CAN channel 2
                # MI_03 --> CAN channel 3
                k_UsbDev.ms32_Interface = int(k_UsbDev.ms_DevicePath[s32_Pos + 5 : s32_Pos + 6])

            i_Devices.append(k_UsbDev)
            Idx += 1

        setupapi.SetupDiDestroyDeviceInfoList(h_DevInfo)    # free memory
        setupapi.SetupDiDestroyDeviceInfoList(h_ParentInfo) # free memory

        if s32_Error != NO_ERROR:
           OsLibrary.RaiseLastError(None, s32_Error)

        # Sort by serial number and then by interface number
        i_Devices.sort()
        return i_Devices

    # Get the serial numbers of all Candlelight devices
    # "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\USB\VID_1D50&PID_606F\2066349E39455006"
    # The last part is the serial number: "2066349E39455006"
    # Fills a dictionary with ContainerID --> Serial Number
    @staticmethod
    def _EnumSerialNumbers() -> Dict[str, str]:
        s_RootPath = "System\\CurrentControlSet\\Enum\\USB\\VID_1D50&PID_606F"

        h_RootKey = wintypes.HKEY()
        s32_Error = advapi32.RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_RootPath, 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                                           ctypes.byref(h_RootKey))
        if s32_Error != NO_ERROR:
           OsLibrary.RaiseLastError(None, s32_Error)

        i_Serials: Dict[str, str] = dict()
        c_Serial = (wintypes.WCHAR * 100)()
        i = -1
        while True:
            i += 1
            s32_Error = advapi32.RegEnumKeyW(h_RootKey, i, c_Serial, ctypes.sizeof(c_Serial))
            if s32_Error == ERROR_NO_MORE_ITEMS:
                break

            if s32_Error != NO_ERROR or not c_Serial[0]:
                assert False, "Error enumerating device serial number in registry"
                continue

            # All interfaces of a multi-interface device have the same ContainerID
            s32_Error, s_Container = OsLibrary._RegReadString(HKEY_LOCAL_MACHINE, "%s\\%s" % (s_RootPath, c_Serial.value), "ContainerID")
            if s32_Error != NO_ERROR:
                assert False, "Error reading device serial number from registry"
                continue

            i_Serials[s_Container.upper()] = c_Serial.value

        advapi32.RegCloseKey(h_RootKey)
        return i_Serials

    # read a string from the registry (max 1000 chars)
    # returns ErrorCode, String from registry
    @staticmethod
    def _RegReadString(h_Class: wintypes.HKEY, s8_Path: str, s8_Entry: str) -> Tuple[int, str]:
        h_Key = wintypes.HKEY()
        s32_Error = advapi32.RegOpenKeyExW(h_Class, s8_Path, 0, KEY_QUERY_VALUE, ctypes.byref(h_Key))
        if s32_Error != NO_ERROR:
            return s32_Error, ""

        c_Buffer  = (wintypes.WCHAR * 1000)()
        u32_Type  = wintypes.DWORD()
        u32_Size  = wintypes.DWORD(ctypes.sizeof(c_Buffer))  # IN = buffer size = 2000 --> OUT = count of bytes read
        s32_Error = advapi32.RegQueryValueExW(h_Key, s8_Entry, None, ctypes.byref(u32_Type),
                                              ctypes.cast(c_Buffer, ctypes.POINTER(wintypes.BYTE)), ctypes.byref(u32_Size))
        advapi32.RegCloseKey(h_Key)

        c_Buffer[u32_Size.value // 2] = '\x00'
        return s32_Error, c_Buffer.value

    # ===================================== Console OUT =====================================

    # Set console title, buffer size and window size
    @staticmethod
    def SetUpConsole(s16_BufWidth: int, s16_BufHeight: int, s16_WndWidth: int, s16_WndHeight: int, s_Title: str) -> None:
        kernel32.SetConsoleTitleW(s_Title)

        k_Size = COORD(s16_BufWidth, s16_BufHeight)
        kernel32.SetConsoleScreenBufferSize(gh_ConsoleOut, k_Size)

        k_Max = kernel32.GetLargestConsoleWindowSize(gh_ConsoleOut)

        k_Wnd = SMALL_RECT(0, 0, 0, 0)
        k_Wnd.Right  = min(k_Max.X, s16_WndWidth)  - 4
        k_Wnd.Bottom = min(k_Max.Y, s16_WndHeight) - 4
        kernel32.SetConsoleWindowInfo(gh_ConsoleOut, True, ctypes.byref(k_Wnd))

    @staticmethod
    def PrintConsole(e_Color: eConsole, s_Format: str, *args) -> None:
        kernel32.SetConsoleTextAttribute(gh_ConsoleOut, e_Color)

        if args: s_Formatted = s_Format % args
        else:    s_Formatted = s_Format

        # WriteConsole() is significantly faster than wprinf() or vwprintf(), which need 10 ms per line!
        u32_Written = wintypes.DWORD()
        kernel32.WriteConsoleW(gh_ConsoleOut, s_Formatted, len(s_Formatted), ctypes.byref(u32_Written), None)
        
    # ===================================== Console IN =====================================

    # Check if the user has pressed the ENTER key in the console (non-blocking function)
    @staticmethod
    def CheckConsoleEnterPressed() -> bool:
        k_Buffer   = INPUT_RECORD()
        u32_Events = wintypes.DWORD()
        if not kernel32.PeekConsoleInputW(gh_ConsoleIn, ctypes.byref(k_Buffer), 1, ctypes.byref(u32_Events)):
            return False

        if u32_Events.value == 0:
            return False

        # The event must be removed from the input buffer, otherwise it is reported eternally.
        kernel32.ReadConsoleInputW(gh_ConsoleIn, ctypes.byref(k_Buffer), 1, ctypes.byref(u32_Events))

        return (k_Buffer.EventType == KEY_EVENT  and
                k_Buffer.Event.KeyEvent.bKeyDown and
                k_Buffer.Event.KeyEvent.wVirtualKeyCode == VK_RETURN)

    # Wait until the user hits a key, returns the ASCII code (blocking function)
    @staticmethod
    def WaitConsoleChar() -> int:
        return msvcrt._getch()
        
    # Only needed for Linux
    @staticmethod
    def SwitchTerminalToNonCanonical() -> None:
        return

    # Only needed for Linux
    @staticmethod
    def RestoreTerminal() -> None:
        return

    # ===================================== Helpers =====================================

    def IsOpen(self) -> bool:
        return self.mh_WinUsb is not None and self.mb_ThreadRuns

    def HasPipeErrors(self) -> bool:
        return self.mu32_RxPipeErrors > 30 or self.mu32_TxPipeErrors > 30;

    def GetDevInfo(self) -> kDevInfo:
        return self.mk_Info

    # Format Windows API error and raise exception
    # This function can act as a class member function or as a static function
    @staticmethod
    def RaiseLastError(self: Optional["OsLibrary"] = None, s32_Error: int = -1) -> None:
        if s32_Error < 0:
           s32_Error = kernel32.GetLastError()
           
        # Replace stupid message "A device attached to the system is not functioning." when an endpoint has been stalled.
        if isinstance(self, OsLibrary) and self.IsOpen() and s32_Error == ERROR_GEN_FAILURE:
            s_Messg = "API Error 31: No response from the WinUSB device";
        else:
            c_Buffer = (wintypes.WCHAR * 1000)()
            kernel32.FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
                                    None, s32_Error, 0, c_Buffer, 1000, None)

            s_Messg = "API Error %u: %s" % (s32_Error, c_Buffer.value)

        raise RuntimeError(s_Messg.rstrip(" \n\r\t"))

    # Print a debug dump of a struct or class to the console
    @staticmethod
    def PrintObjectDump(o_Object) -> None:
        s_Debug = Utils.ObjectToString(o_Object)
        OsLibrary.PrintConsole(eConsole.Grey, "\nDump: %s\n", s_Debug)
        
        
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
#  This class is for Linux. It has been tested on Fedora 44.
#
# =======================================================================================================

import os
import sys
import termios
import select
import ctypes
import atexit
from typing import Tuple, List, Optional
from enum   import IntEnum
from Utils  import *

# ============= Console Colors ===============

class eConsole(IntEnum):
    White   = 0
    Grey    = 1
    Cyan    = 2
    Magenta = 3
    Yellow  = 4
    Lime    = 5
    Red     = 6
    Blue    = 7
    Brown   = 8
    Green   = 9

# ========== libusb Definitions ===============

class libusb_version(ctypes.Structure):
    _fields_ = [
        ("major",    ctypes.c_uint16),
        ("minor",    ctypes.c_uint16),
        ("micro",    ctypes.c_uint16),
        ("nano",     ctypes.c_uint16),
        ("rc",       ctypes.c_char_p),
        ("describe", ctypes.c_char_p),
    ]

class libusb_endpoint_descriptor(ctypes.Structure):
    _fields_ = [
        ("bLength",          ctypes.c_uint8),
        ("bDescriptorType",  ctypes.c_uint8),
        ("bEndpointAddress", ctypes.c_uint8),
        ("bmAttributes",     ctypes.c_uint8),
        ("wMaxPacketSize",   ctypes.c_uint16),
        ("bInterval",        ctypes.c_uint8),
        ("bRefresh",         ctypes.c_uint8),
        ("bSynchAddress",    ctypes.c_uint8),
        ("extra",            ctypes.POINTER(ctypes.c_ubyte)),
        ("extra_length",     ctypes.c_int),
    ]

class libusb_interface_descriptor(ctypes.Structure):
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
        ("endpoint",           ctypes.POINTER(libusb_endpoint_descriptor)),
        ("extra",              ctypes.POINTER(ctypes.c_ubyte)),
        ("extra_length",       ctypes.c_int),
    ]

class libusb_interface(ctypes.Structure):
    _fields_ = [
        ("altsetting",     ctypes.POINTER(libusb_interface_descriptor)),
        ("num_altsetting", ctypes.c_int),
    ]

class libusb_config_descriptor(ctypes.Structure):
    _fields_ = [
        ("bLength",             ctypes.c_uint8),
        ("bDescriptorType",     ctypes.c_uint8),
        ("wTotalLength",        ctypes.c_uint16),
        ("bNumInterfaces",      ctypes.c_uint8),
        ("bConfigurationValue", ctypes.c_uint8),
        ("iConfiguration",      ctypes.c_uint8),
        ("bmAttributes",        ctypes.c_uint8),
        ("MaxPower",            ctypes.c_uint8),
        ("interface",           ctypes.POINTER(libusb_interface)),
        ("extra",               ctypes.POINTER(ctypes.c_ubyte)),
        ("extra_length",        ctypes.c_int),
    ]

# Load libusb C library on Linux
_libusb = ctypes.CDLL("libusb-1.0.so.0")

t_libusb_device           = ctypes.c_void_p
t_libusb_device_ptr       = ctypes.POINTER(t_libusb_device)
t_libusb_device_list      = ctypes.POINTER(t_libusb_device_ptr) # in C++:  libusb_device*** list
t_libusb_context          = ctypes.c_void_p
t_libusb_device_handle    = ctypes.c_void_p
t_libusb_config_descr_ptr = ctypes.POINTER(libusb_config_descriptor)

_libusb.libusb_get_version.argtypes = []
_libusb.libusb_get_version.restype  = ctypes.POINTER(libusb_version)

_libusb.libusb_strerror.argtypes = [ctypes.c_int]
_libusb.libusb_strerror.restype  = ctypes.c_char_p

_libusb.libusb_set_auto_detach_kernel_driver.argtypes = [t_libusb_device_handle, ctypes.c_int]
_libusb.libusb_set_auto_detach_kernel_driver.restype  = ctypes.c_int

_libusb.libusb_claim_interface.argtypes = [t_libusb_device_handle, ctypes.c_int]
_libusb.libusb_claim_interface.restype  = ctypes.c_int

_libusb.libusb_release_interface.argtypes = [t_libusb_device_handle, ctypes.c_int]
_libusb.libusb_release_interface.restype  = ctypes.c_int

_libusb.libusb_get_device_list.argtypes = [t_libusb_context, t_libusb_device_list]
_libusb.libusb_get_device_list.restype  = ctypes.c_ssize_t 

# kDeviceDescriptor is defined in Utils.py (also used for Windows)
_libusb.libusb_get_device_descriptor.argtypes = [t_libusb_device, ctypes.POINTER(kDeviceDescriptor)]
_libusb.libusb_get_device_descriptor.restype  = ctypes.c_int

_libusb.libusb_get_device_string.argtypes = [t_libusb_device, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
_libusb.libusb_get_device_string.restype  = ctypes.c_int

_libusb.libusb_get_active_config_descriptor.argtypes = [t_libusb_device, ctypes.POINTER(t_libusb_config_descr_ptr)]
_libusb.libusb_get_active_config_descriptor.restype  = ctypes.c_int

_libusb.libusb_free_config_descriptor.argtypes = [t_libusb_config_descr_ptr]
_libusb.libusb_free_config_descriptor.restype  = None

_libusb.libusb_free_device_list.argtypes = [t_libusb_device_ptr, ctypes.c_int]
_libusb.libusb_free_device_list.restype  = None

_libusb.libusb_open.argtypes = [t_libusb_device, ctypes.POINTER(t_libusb_device_handle)]
_libusb.libusb_open.restype  = ctypes.c_int

_libusb.libusb_close.argtypes = [t_libusb_device_handle]
_libusb.libusb_close.restype  = None

_libusb.libusb_exit.argtypes = [t_libusb_context]
_libusb.libusb_exit.restype  = None

_libusb.libusb_get_string_descriptor_ascii.argtypes = [t_libusb_device_handle, ctypes.c_uint8, ctypes.c_void_p, ctypes.c_int]
_libusb.libusb_get_string_descriptor_ascii.restype  = ctypes.c_int

_libusb.libusb_control_transfer.argtypes = [t_libusb_device_handle, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint]
_libusb.libusb_control_transfer.restype  = ctypes.c_int

_libusb.libusb_bulk_transfer.argtypes = [t_libusb_device_handle, ctypes.c_uint8, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_uint]
_libusb.libusb_bulk_transfer.restype  = ctypes.c_int

_libusb.libusb_get_bus_number.argtypes = [t_libusb_device]
_libusb.libusb_get_bus_number.restype  = ctypes.c_uint8

_libusb.libusb_get_device_address.argtypes = [t_libusb_device]
_libusb.libusb_get_device_address.restype  = ctypes.c_uint8

LIBUSB_TRANSFER_TYPE_MASK           = 0x03
LIBUSB_TRANSFER_TYPE_BULK           = 0x02

LIBUSB_DEVICE_STRING_MANUFACTURER   = 0
LIBUSB_DEVICE_STRING_PRODUCT        = 1
LIBUSB_DEVICE_STRING_SERIAL_NUMBER  = 2

NO_ERROR                            = 0
LIBUSB_ERROR_TIMEOUT                = -7

USB_STRING_DESCRIPTOR_TYPE          = 0x03

# ----- static variables -----

gs32_OldTermSettg: Optional[List[Any]] = None   # int[7]

# =======================================================================================================

class OsLibrary:

    def __init__(self) -> None:
        self.mpi_UsbContext      = t_libusb_context()       # libusb_init() --> libusb_context*
        self.mppi_UsbDeviceList  = t_libusb_device_ptr()    # EnumDevices() --> libusb_device**       
        self.mpi_DevHandle       = t_libusb_device_handle() # Open()        --> libusb_device_handle* 
        self.mk_Info             = kDevInfo()               # shared with Candlelight class
        self.mb_IsOpen           = False
        self.mb_LibUsbVersionOld = False

        # This class needs libusb_get_interface_string() which has been added in pull request 1860.
        # https://github.com/libusb/libusb/pull/1860
        k_Version = _libusb.libusb_get_version().contents
        if (k_Version.major, k_Version.minor, k_Version.micro) < (1, 0, 30):
            raise RuntimeError("Please install libusb version 1.0.31 or if not availble at least 1.0.30")
            
        if (k_Version.major, k_Version.minor, k_Version.micro) < (1, 0, 31):
            OsLibrary.PrintConsole(eConsole.Yellow, "Update to libusb 1.0.31\n")
            self.mb_LibUsbVersionOld = True

    def __del__(self) -> None:
        self.Close()

        if self.mppi_UsbDeviceList:
            _libusb.libusb_free_device_list(self.mppi_UsbDeviceList, 1)

        if self.mpi_UsbContext:
            _libusb.libusb_exit(self.mpi_UsbContext)

    # Called from Candlelight.Open() only if the device is not already open
    # k_Device comes from OsLibrary.EnumDevices()
    def Open(self, k_Device) -> None:
        self.ms32_RxPipeErrors = 0
        self.ms32_TxPipeErrors = 0
        self.mk_Info.Clear()

        # in case the last call to Open() failed with an exception and mpi_DevHandle is still open
        self.Close()

        pi_UsbDevice = k_Device.mpi_LinuxDevice # libusb_device

        s32_Error = _libusb.libusb_get_device_descriptor(pi_UsbDevice, ctypes.byref(self.mk_Info.mk_DeviceDescr))
        if s32_Error < 0:
            OsLibrary.RaiseLastError(None, s32_Error)

        s32_Error = _libusb.libusb_open(pi_UsbDevice, ctypes.byref(self.mpi_DevHandle))
        if s32_Error < 0:
            OsLibrary.RaiseLastError(None, s32_Error)

        # ------------------------

        # throws exception
        self.mk_Info.ms_Vendor  = self._ReadStringDescriptor(self.mk_Info.mk_DeviceDescr.iManufacturer)
        self.mk_Info.ms_Product = self._ReadStringDescriptor(self.mk_Info.mk_DeviceDescr.iProduct)
        self.mk_Info.ms_Serial  = self._ReadStringDescriptor(self.mk_Info.mk_DeviceDescr.iSerialNumber)

        # ------------------------

        pk_ConfigDesc = t_libusb_config_descr_ptr()
        s32_Error = _libusb.libusb_get_active_config_descriptor(pi_UsbDevice, ctypes.byref(pk_ConfigDesc))
        if s32_Error < 0:
            OsLibrary.RaiseLastError(None, s32_Error)

        k_Interface  = pk_ConfigDesc.contents.interface[k_Device.ms32_Interface] # libusb_interface
        k_InterfDesc = k_Interface.altsetting[0]                                 # libusb_interface_descriptor

        # copy the first 9 bytes of libusb_interface_descriptor
        p_Src = ctypes.addressof(k_InterfDesc)
        p_Dst = ctypes.addressof(self.mk_Info.mk_InterfDescr)
        ctypes.memmove(p_Dst, p_Src, ctypes.sizeof(kInterfaceDescriptor))

        # throws exception
        self.mk_Info.ms_Interface = self._ReadStringDescriptor(k_InterfDesc.iInterface)

        # ------------------------

        # Get the 2 endpoints of the Candlelight interface (the Firmware Update interface has bNumEndpoints == 0)
        for P in range(k_InterfDesc.bNumEndpoints):
            k_Endpoint = k_InterfDesc.endpoint[P] # libusb_endpoint_descriptor
            if (k_Endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK:
                _libusb.libusb_free_config_descriptor(pk_ConfigDesc)
                raise RuntimeError("The device is not a Candlelight adapter.")

            if k_Endpoint.bEndpointAddress & eDirection.In:
                self.mk_Info.mu8_EndpointIN      = k_Endpoint.bEndpointAddress
                self.mk_Info.mu16_MaxPackSizeIN  = k_Endpoint.wMaxPacketSize
            else:  # OUT
                self.mk_Info.mu8_EndpointOUT     = k_Endpoint.bEndpointAddress
                self.mk_Info.mu16_MaxPackSizeOUT = k_Endpoint.wMaxPacketSize
       
        # ------------------------
                
        # If the gs_usb kernel driver is attached --> detach it and claim the interface for libusb.
        _libusb.libusb_set_auto_detach_kernel_driver(self.mpi_DevHandle, 1)

        s32_Error = _libusb.libusb_claim_interface(self.mpi_DevHandle, self.mk_Info.mk_InterfDescr.bInterfaceNumber)
        if s32_Error < 0:
            OsLibrary.RaiseLastError(None, s32_Error)
            
        # ------------------------

        _libusb.libusb_free_config_descriptor(pk_ConfigDesc)
        self.mb_IsOpen = True

    # this function is only needed for WinUSB
    def StartPipes(self) -> None:
        return

    # Called from Candlelight.Close()
    def Close(self) -> None:
        if self.mpi_DevHandle:
            _libusb.libusb_release_interface(self.mpi_DevHandle, self.mk_Info.mk_InterfDescr.bInterfaceNumber)
            _libusb.libusb_close(self.mpi_DevHandle)
            self.mpi_DevHandle = ctypes.c_void_p()
            
        self.mb_IsOpen = False

    # ===================================== CTRL Pipe =====================================

    # Send SETUP packet and optionally additional data bytes as IN or OUT transfer.
    # returns ErrorCode and the count of bytes that were transferred, does not raise exceptions
    # For IN transfers the received bytes from USB are written into the buffer that is passed as pointer in p_Data
    def ControlTransfer(self, k_Setup: kSetup, p_Data: Any) -> Tuple[int, int]:
        s32_Transferred = _libusb.libusb_control_transfer(self.mpi_DevHandle, k_Setup.bRequestType,
                                                          k_Setup.bRequest, k_Setup.wValue, k_Setup.wIndex,
                                                          p_Data, k_Setup.wLength, PIPE_TIMEOUT)
        if s32_Transferred < 0:
            return s32_Transferred, 0 # error
        else:
            return 0, s32_Transferred # success

    # ===================================== OUT Pipe ======================================

    def WritePipeOut(self, p_Data: Any, u32_TxLen: int) -> None:
        s32_Transferred = ctypes.c_int(0)
        s32_Error = _libusb.libusb_bulk_transfer(self.mpi_DevHandle, self.mk_Info.mu8_EndpointOUT,
                                                 p_Data, int(u32_TxLen), ctypes.byref(s32_Transferred), PIPE_TIMEOUT)
        if s32_Error < 0:
            self.ms32_TxPipeErrors += 1
            OsLibrary.RaiseLastError(None, s32_Error)

        self.ms32_TxPipeErrors = 0

    # ====================================== IN Pipe =======================================

    # Get the next frame from USB and return a k_UsbInPacket.
    # If no data received during timeout return None.
    def ReadPipeIn(self, u32_Timeout: int) -> Optional[kUsbInPacket]:
        k_UsbInPacket   = kUsbInPacket()
        s32_Transferred = ctypes.c_int(0)
        s32_Error = _libusb.libusb_bulk_transfer(self.mpi_DevHandle, self.mk_Info.mu8_EndpointIN,
                                                 k_UsbInPacket.mu8_Buffer, MAX_BLOB_SIZE, ctypes.byref(s32_Transferred), u32_Timeout)
        if s32_Error < 0:
            if s32_Error == LIBUSB_ERROR_TIMEOUT:
                return None

            self.ms32_RxPipeErrors += 1
            OsLibrary.RaiseLastError(None, s32_Error)

        self.ms32_RxPipeErrors = 0

        k_UsbInPacket.ms32_BytesRead   = s32_Transferred.value
        k_UsbInPacket.ms64_OsTimestamp = Utils.GetOsTimestamp()
        return k_UsbInPacket

    # =================================== Enumerate USB Devices ==================================

    # Returns device name, serial number and libusb_device of all connected Candlelight devices.
    # b_GetCandlelight = false -> this function enumerates the Firmware Update interfaces.
    # Calling EnumDevices() again will invalidate any previously returned device handles in mpi_LinuxDevice
    def EnumDevices(self, b_GetCandlelight: bool) -> List[kUsbDevice]:

        if not self.mpi_UsbContext:  # init once only
            s32_Error = _libusb.libusb_init(ctypes.byref(self.mpi_UsbContext))
            if s32_Error < 0:
                OsLibrary.RaiseLastError(None, s32_Error)

        if self.mppi_UsbDeviceList:  # free any previous list
            _libusb.libusb_free_device_list(self.mppi_UsbDeviceList, 1)
            self.mppi_UsbDeviceList = ctypes.POINTER(ctypes.c_void_p)()

        s32_DevCount = _libusb.libusb_get_device_list(self.mpi_UsbContext, ctypes.byref(self.mppi_UsbDeviceList))
        if s32_DevCount < 0:
            OsLibrary.RaiseLastError(None, s32_DevCount)

        i_Devices : List[kUsbDevice] = []

        # enumerate USB devices
        for Dev in range(s32_DevCount):
            pi_UsbDevice = self.mppi_UsbDeviceList[Dev]

            k_DevDescr = kDeviceDescriptor()
            s32_Error  = _libusb.libusb_get_device_descriptor(pi_UsbDevice, ctypes.byref(k_DevDescr))
            if s32_Error < 0:
                OsLibrary.RaiseLastError(None, s32_Error)

            if (k_DevDescr.idVendor  != 0x1D50 or
                k_DevDescr.idProduct != 0x606F):  # OpenMoko Inc.
                continue  # not a Candlelight device

            pk_ConfigDesc = t_libusb_config_descr_ptr()
            s32_Error = _libusb.libusb_get_active_config_descriptor(pi_UsbDevice, ctypes.byref(pk_ConfigDesc))
            if s32_Error < 0:
                OsLibrary.RaiseLastError(None, s32_Error)

            # If there are not at least 2 interfaces, it is not a valid Candlelight device
            if pk_ConfigDesc.contents.bNumInterfaces >= 2:

                # get string descriptor from the kernel without opening the device
                # libusb_get_device_string() requires libusb version 1.0.30
                s8_Product = (ctypes.c_char * 256)()
                s32_Error = _libusb.libusb_get_device_string(pi_UsbDevice, LIBUSB_DEVICE_STRING_PRODUCT, s8_Product, ctypes.sizeof(s8_Product))
                if s32_Error < 0:
                    _libusb.libusb_free_config_descriptor(pk_ConfigDesc)
                    OsLibrary.RaiseLastError(None, s32_Error)

                # get string descriptor from the kernel without opening the device
                # libusb_get_device_string() requires libusb version 1.0.30
                s8_Serial = (ctypes.c_char * 256)()
                s32_Error = _libusb.libusb_get_device_string(pi_UsbDevice, LIBUSB_DEVICE_STRING_SERIAL_NUMBER, s8_Serial, ctypes.sizeof(s8_Serial))
                if s32_Error < 0:
                    _libusb.libusb_free_config_descriptor(pk_ConfigDesc)
                    OsLibrary.RaiseLastError(None, s32_Error)

                # Add each interface as a separate device to i_Devices
                for Idx in range(pk_ConfigDesc.contents.bNumInterfaces):
                    # The Firmware Update interface is always the second interface (Idx == 1)
                    # The others are Candlelight interfaces: (Idx == 0, 2, 3,...)
                    b_IsCandle = (Idx != FIRMW_UPDATE_INTERFACE)
                    if b_IsCandle != b_GetCandlelight:
                        continue  # not the requested interface type

                    k_Interface = pk_ConfigDesc.contents.interface[Idx] # libusb_interface
                    if k_Interface.num_altsetting != 1:
                        break  # not a valid Candlelight device

                    k_InterfDesc = k_Interface.altsetting[0] # libusb_interface_descriptor

                    k_UsbDev = kUsbDevice()
                    k_UsbDev.mpi_LinuxDevice = pi_UsbDevice
                    k_UsbDev.ms32_Interface  = Idx
                    k_UsbDev.ms_Product      = s8_Product.value.decode("ascii")
                    k_UsbDev.ms_SerialNo     = s8_Serial .value.decode("ascii")
                    
                    # On Windows ms_DevicePath is the real Windows NT device path used by the kernel.
                    # But libusb does not offer an API that returns the Linux device path although it is stored internally in priv->sysfs_dir.
                    # We build a string here that gives a little information about the USB device location on the USB bus.
                    k_UsbDev.ms_DevicePath = "Bus number: %u, Device address: %u" % (_libusb.libusb_get_bus_number    (pi_UsbDevice),
                                                                                     _libusb.libusb_get_device_address(pi_UsbDevice))

                    # Get the interface name from the kernel without opening the device.
                    # libusb_get_interface_string() requires libusb version 1.0.31
                    if self.mb_LibUsbVersionOld:
                        k_UsbDev.ms_Interface = "[libusb is too old]"                        
                    else:                        
                        s8_Interface = (ctypes.c_char * 256)()
                        s32_Error = _libusb.libusb_get_interface_string(pi_UsbDevice, pk_ConfigDesc.contents.bConfigurationValue,
                                                                        k_InterfDesc.bInterfaceNumber, k_InterfDesc.bAlternateSetting,
                                                                        s8_Interface, ctypes.sizeof(s8_Interface))

                        if s32_Error < 0: k_UsbDev.ms_Interface = "[libusb error %s]" % _libusb.libusb_strerror(s32_Error)
                        else:             k_UsbDev.ms_Interface = s8_Interface.value.decode("ascii")

                    i_Devices.append(k_UsbDev)

            _libusb.libusb_free_config_descriptor(pk_ConfigDesc)
        
        return i_Devices

    # ===================================== Console OUT =====================================

    # Set console title, buffer size and window size
    @staticmethod
    def SetUpConsole(s16_BufWidth, s16_BufHeight, s16_WndWidth, s16_WndHeight, s_Title) -> None:
        # Linux uses a cryptic Escape sequence to set the window title
        print("\033]2;%s\007" % s_Title, end="", flush=True)

        # TODO: Set console window size and buffer size

    # Print coloured console output
    @staticmethod
    def PrintConsole(e_Color: eConsole, s_Format, *args) -> None:
        # Linux uses cryptic Escape sequences to set the text color
        s_ColorMap = {
            eConsole.Green:   "\033[1;38;2;0;180;0m",     # dark Lime
            eConsole.Brown:   "\033[1;38;2;180;100;0m",   # dark Yellow
            eConsole.Grey:    "\033[1;38;2;160;160;160m", # dark White
            eConsole.Red:     "\033[1;38;2;255;30;30m",   # bright
            eConsole.Lime:    "\033[1;38;2;0;255;0m",     # bright
            eConsole.Yellow:  "\033[1;38;2;255;235;0m",   # bright
            eConsole.Blue:    "\033[1;38;2;30;130;255m",  # bright
            eConsole.Magenta: "\033[1;38;2;255;0;255m",   # bright
            eConsole.Cyan:    "\033[1;38;2;0;235;255m",   # bright
            eConsole.White:   "\033[1;38;2;255;255;255m", # bright
        }

        if args: s_Formatted = s_Format % args
        else:    s_Formatted = s_Format

        print(s_ColorMap[e_Color], end="")
        print(s_Formatted,         end="", flush=True)
        
    # ===================================== Console IN =====================================
    
    # Check if the user has pressed the ENTER key in the console (non-blocking function)
    @staticmethod
    def CheckConsoleEnterPressed() -> bool:
        s32_Ascii = OsLibrary._GetKeyboardInput()
        return s32_Ascii == 10 or s32_Ascii == 13

    # Wait until the user hits a key, returns the ASCII code (blocking function)
    @staticmethod
    def WaitConsoleChar() -> int:
        while True:
            s32_Ascii = OsLibrary._GetKeyboardInput()
            if s32_Ascii > -1:
                return s32_Ascii

            time.sleep(0.05)  # 50 ms

    # returns the ASCII code of the key pressed, or -1 if no key was pressed
    @staticmethod
    def _GetKeyboardInput() -> int:
        h_StdIn = sys.stdin.fileno()

        # Instant poll check via select
        rlist, _, _ = select.select([h_StdIn], [], [], 0)
        if not rlist:
            return -1

        try:
            u8_Buf = os.read(h_StdIn, 1)
            if u8_Buf:
                return ord(u8_Buf)
        except OSError:
            pass

        return -1
        
    # -------------------
        
    @staticmethod
    def SwitchTerminalToNonCanonical() -> None:
        global gs32_OldTermSettg
        
        if gs32_OldTermSettg or not sys.stdin.isatty():
            return

        h_StdIn = sys.stdin.fileno()
        
        # Save original settings (7 integers)
        gs32_OldTermSettg = termios.tcgetattr(h_StdIn)

        # Disable line buffering and echo and set non-blocking read parameters
        s32_NewTermSettg = termios.tcgetattr(h_StdIn)
        s32_NewTermSettg[3] &= ~(termios.ICANON | termios.ECHO)
        s32_NewTermSettg[6][termios.VMIN]  = 0
        s32_NewTermSettg[6][termios.VTIME] = 0

        # Apply settings without flushing pending input
        termios.tcsetattr(h_StdIn, termios.TCSADRAIN, s32_NewTermSettg)

        # Register emergency restoration on script termination
        atexit.register(OsLibrary.RestoreTerminal)   

    @staticmethod
    def RestoreTerminal():
        global gs32_OldTermSettg
        
        if gs32_OldTermSettg and sys.stdin.isatty():
            h_StdIn = sys.stdin.fileno()
            termios.tcsetattr(h_StdIn, termios.TCSADRAIN, gs32_OldTermSettg)
            gs32_OldTermSettg = None            
        
    # ===================================== Helpers =====================================

    def IsOpen(self) -> bool:
        return self.mb_IsOpen

    def HasPipeErrors(self) -> bool:
        return self.ms32_RxPipeErrors > 30 or self.ms32_TxPipeErrors > 30

    def GetDevInfo(self) -> kDevInfo:
        return self.mk_Info

    # convert libusb error code into string and raise exception
    # parameter self is only needed for Windows
    @staticmethod
    def RaiseLastError(self: Optional["OsLibrary"] = None, s32_Error: int = 0) -> None:
        s_Messg = _libusb.libusb_strerror(s32_Error).decode("utf-8")
        raise RuntimeError("LibUsb error %d: %s" % (s32_Error, s_Messg))

    def _ReadStringDescriptor(self, u8_StrIndex: int) -> str:
        # If the descriptor does not define a string, the index is zero. This is not an error.
        if u8_StrIndex == 0:
            return ""

        s8_String = (ctypes.c_char * 256)()
        s32_Read  = _libusb.libusb_get_string_descriptor_ascii(self.mpi_DevHandle, u8_StrIndex, s8_String, ctypes.sizeof(s8_String))
        if s32_Read < 0:
            OsLibrary.RaiseLastError(None, s32_Read)

        return s8_String.value.decode('ascii', errors='ignore')


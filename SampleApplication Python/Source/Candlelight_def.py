
#  These definititions are part of the CANable 2.5 firmware, translated to Python
#  https://netcult.ch/elmue/CANable Firmware Update

#  The enums and structs of the Linux Kernel GS driver can be found here:
#  https://elixir.bootlin.com/linux/v6.18.6/source/drivers/net/can/usb/gs_usb.c

# ==============================================================================

import ctypes
from enum import IntEnum, IntFlag

# Command sent from the host application in a SETUP request
# transferred as 8 bit
class eUsbRequest(IntEnum):
    GS_ReqSetHostFormat     = 0   # uint32_t: define little/big endian data transfer (only little is supported)
    GS_ReqSetBitTiming      = 1   # kBitTiming: set CAN classic/nominal bit timing (baudrate + samplepoint)
    GS_ReqSetDeviceMode     = 2   # kDeviceMode: start / stop CAN device and set device flags
    GS_ReqBerrReport        = 3   # -- not implemented, undocumented
    GS_ReqGetCapabilities   = 4   # kCapabilityClassic: get supported features and processor limits of timing for classic frames
    GS_ReqGetDeviceVersion  = 5   # kDeviceVersion: get version numbers
    GS_ReqGetTimestamp      = 6   # uint32_t: get firmware 1 µs timestamp (Needs roll over detection! Roll over after one hour!)
    GS_ReqIdentify          = 7   # uint32_t (ignored): blink LEDs for device identification
    GS_ReqGetUserID         = 8   # -- not implemented, undocumented  (WTF is a user ID ??)
    GS_ReqSetUserID         = 9   # -- not implemented, undocumented  (WTF is a user ID ??)
    GS_ReqSetBitTimingFD    = 10  # kBitTiming: set data bit timing (CAN FD data baudrate + samplepoint)
    GS_ReqGetCapabilitiesFD = 11  # kCapabilityFD: get supported features and processor limits of timing for CAN FD
    GS_ReqSetTermination    = 12  # eTermination: enable the 120 Ohm termination resistor (if supported by the board)
    GS_ReqGetTermination    = 13  # eTermination: get status of 120 Ohm termination resistor (if supported by the board)
    GS_ReqGetErrorState     = 14  # kErrorState: the host can poll for bus errors (deprecated)

    # ----------- ELM commands added by ElmüSoft -----------
    ELM_ReqFIRST            = 20  # First request that requires ElmüSoft protocol to be enabled
    ELM_ReqGetBoardInfo     = 20  # kBoardInfo: get name about target board and processor
    ELM_ReqSetFilter        = 21  # kFilter: set up to 8 acceptance mask filters
    ELM_ReqGetLastError     = 22  # uint8_t: get the eFeedback error of the last SETUP request. This works also in legacy mode!
    ELM_ReqSetBusLoadReport = 23  # uint8_t: enable busload report in percent to be sent in a user defined interval
    ELM_ReqSetPinStatus     = 24  # kPinStatus: set, reset, enable, disable,... processor pins
    ELM_ReqGetPinStatus     = 25  # Receive: SETUP.wValue = ePinID, Send: ePinStatus in 2 data bytes
    ELM_ReqReadFlash        = 26  # Read  user data from a segment in flash memory
    ELM_ReqWriteFlash       = 27  # Write user data to   a segment in flash memory

# ==============================================================================

# These flags are used to enable/disable a mode with GS_ReqSetDeviceMode
# and the same flags are returned as capability with commands GS_ReqGetCapabilities and GS_ReqGetCapabilitiesFD
# Prefix GS_xxx  = legacy flags from Geschwister Schneider
# Prefix ELM_xxx = new CANable 2.5 flags added by ElmüSoft
# Prefix MKB_xxx = flags added by Marc Kleine Budde to legacy firmware
# transferred as 32 bit
class eDeviceFlags(IntFlag):
    GS_DevFlagNone                  = 0

    # silent mode (do not send ACK)
    GS_DevFlagListenOnly            = 0x00001 # bit 0

    # support of loopback mode (sent packets are received directly inside the processor)
    # If this flag is combined with ListenOnly, the internal loopback mode is enabled, otherwise the external loopback mode.
    GS_DevFlagLoopback              = 0x00002 # bit 1

    # take 3 samples per 1 bit on CAN bus, not implemented.
    # None of the modern CAN FD processors implements triple sampling.
    # This was a feature for some old processors and only available for CAN classic.
    # GS_DevFlagTripleSample          = 0x00004 # bit 2

    # if set, send a packet only once, otherwise retransmit until an ACK was revcived
    GS_DevFlagOneShot               = 0x00008 # bit 3

    # Send a hardware timestamp with each Rx packet and Tx echo.
    # Deprecated: creates more USB traffic overhead on a slow Full speed USB device.
    # Timestamps should be created in the host application at packet reception.
    # See subfolder "SampleApplication C++" for a sample code how to generate precise timestamps in Windows.
    GS_DevFlagTimestamp             = 0x00010 # bit 4

    # blink the LEDs to distinguish between multiple connected devices
    GS_DevFlagIdentify              = 0x00020 # bit 5

    # Old processors did not have a unique serial number that is programmed in the factory.
    # This feature allowed the user to store an individual serial number in the processor.
    # Modern processors do not need this anymore, not implemented.
    # GS_DevFlagUserID              = 0x00040 # bit 6

    # This is total nonsense: Send always 128 byte USB packets to the host, not implemented
    # GS_DevFlagPadPacketsToMaxSize = 0x00080 # bit 7

    # In the feature flags this means that CAN FD is supported.
    # In kDeviceMode it is useless because CAN FD is enabled as soon as a data bitrate has been set.
    GS_DevFlagCAN_FD                = 0x00100 # bit 8

    # request workaround for LPC546XX erratum USB.15:
    # Let host driver add a padding byte to each USB frame, not implemented
    # GS_DevFlagQuirk_LPC546XX      = 0x00200 # bit 9

    # Setting a data bitrate for CAN FD is supported (commands GS_ReqGetCapabilitiesFD and GS_ReqSetBitTimingFD can be used)
    GS_DevFlagBitTimingFD           = 0x00400 # bit 10

    # The 120 ohm termination resistor can be turned on/off by command, only few boards support this.
    GS_DevFlagTermination           = 0x00800 # bit 11

    # Enable automatic error reports sending error frames with flag CAN_ID_Error.
    # This flag has never been implemented by any legacy firmware.
    # Therfore all the CAN software expects error frames to be sent by the firmware without setting this flag.
    # GS_DevFlagErrorReporting      = 0x01000 # bit 12

    # Request GS_ReqGetErrorState can poll for kErrorState that contains the current error status (deprecated).
    # This is only implemented for compatibility with legacy software.
    # This was inefficient, because the host had to poll for errors producing useless USB traffic.
    # This firmware sends error reports as soon as a CAN error appears. Polling the error state is not required.
    GS_DevFlagGetErrorState         = 0x02000 # bit 13

    # Switch to the new extended ElmüSoft CANable 2.5 protocol (use kHostFrameElmue instead of kHostFrameLegacy)
    # ATTENTION: This flag enables the ElmüSoft protocol for ALL channels and it stays enabled until all channels have been closed!
    # This flag also enables debug reports (USR_DebugReport).
    # In the Capabilities this flag means that all the ELM_ReqXXX commands are supported.
    ELM_DevFlagProtocolElmue        = 0x04000 # bit 14

    # This flag has been replaced with ELM_DevFlagSendUsbBlobs in firmware version 29.may.2026
    # Now you can decide on a per packet basis if you want to receive an echo marker back or not.
    # Now you can send kTxFrameElmue.marker = 0 if you don't want to receive a Tx echo.
    # Now Tx markers are only valid from 0x01 to 0xFF.
    # ELM_DevFlagDisableTxEcho      = 0x08000 # bit 15

    # IN:  If there are multiple CAN Rx packets waiting in the FIFO buffer -> optimize USB transfer by sendig them together in a blob.
    # OUT: The host can always send multiple CAN Tx frames with kBlob and MSG_TxBlob even without setting this flag.
    ELM_DevFlagSendUsbBlobs         = 0x08000 # bit 15

    # Legacy hardware filters supported. Not implemented here.
    # ELM_DevFlagProtocolElmue includes support for HW filters since the very first version.
    # No additional flag is required to indicate this feature.
    # Marc Kleine Budde uses a completely incompatible struct to transmit the filter settings.
    # MKB_DevFlagFilter             = 0x10000 # bit 16

# ==============================================================================

# sent as 8 bit
class eFeedback(IntEnum):
    Success             = 2            # Command successfully executed
    # ----------------------
    InvalidCommand      = ord('1')     # The command is invalid
    InvalidParameter    = ord('2')     # One of the parameters is invalid
    AdapterMustBeOpen   = ord('3')     # The command cannot be executed before opening the adapter
    AdapterMustBeClosed = ord('4')     # The command cannot be executed after  opening the adapter
    ErrorFromHAL        = ord('5')     # The HAL from ST Microelectronics has reported an error
    UnsupportedFeature  = ord('6')     # The feature is not implemented or not supported by the adapter
    TxBufferFull        = ord('7')     # Sending is not possible because the buffer is full (only Slcan)
    BusIsOff            = ord('8')     # Sending is not possible because the processor is blocked in the BusOff state
    NoTxInSilentMode    = ord('9')     # Sending is not possible because the adapter is in Bus Monitoring mode
    BaudrateNotSet      = ord('9') + 1 # Opening the adapter is not possible if no baudrate has been set
    OptBytesProgrFailed = ord('9') + 2 # Programming the Option Bytes failed
    ResetRequired       = ord('9') + 3 # The user must disconnect and reconnect the USB cable to enter boot mode
    ParamOutOfRange     = ord('9') + 4 # A paramter is outside the valid range

# ---------------

# If bus status is BUS_OFF both LED's (Rx + Tx) are permanently ON
# This status is controlled only by hardware
# Slcan sends this in the error report "EXXXXXXXX\r"
# sent as 4 bit
class eErrorBusStatus(IntEnum):
    StatusActive     = 0x00 # operational  (must be zero because this is not an error)
    StatusWarning    = 0x10 # set in can.c (>  96 errors)
    StatusPassive    = 0x20 # set in can.c (> 128 errors)
    StatusOff        = 0x30 # set in can.c (> 248 errors)

# ---------------

# If any of these flags is set, both LED's (Rx + Tx) are permanently ON
# These flags are reset after sending them once to the host
# They are set again if the error is still present
# Slcan sends this in the error report "EXXXXXXXX\r"
# Candlelight sends this in a special error packet with a flag (legacy: CAN_ID_Error, ElmüSoft: MSG_Error)
# sent as 8 bit
class eErrorAppFlags(IntFlag):
    NoError         = 0x00 # no error
    CanRxFail       = 0x01 # CAN packets arrive faster than the firmware can process them
    CanTxFail       = 0x02 # trying to send while in silent mode, while bus off or adaper not open or invalid Tx packet or HAL error
    CanTxOverflow   = 0x04 # a CAN packet could not be sent because the Tx FIFO + buffer are full (mostly because bus is passive).
    UsbInOverflow   = 0x08 # a USB IN packet could not be sent because CAN traffic is faster than USB transfer.
    CanTxTimeout    = 0x10 # a packet in the transmit FIFO was not acknowledged during 500 ms --> abort Tx and clear Tx buffer.

# ==============================================================================

# GS_ReqGetDeviceVersion
class kDeviceVersion(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("hal_ver_high",   ctypes.c_uint8),  # The HAL version (not BCD encoded)
        ("hal_ver_mid",    ctypes.c_uint8),
        ("hal_ver_low",    ctypes.c_uint8),
        ("icount",         ctypes.c_uint8),  # Candlelight interface count - 1
        ("sw_version_bcd", ctypes.c_uint32), # software (firmware) version in BCD format
        ("hw_version_bcd", ctypes.c_uint32), # hardware version in BCD format
    ]

# ---------------

# GS_ReqSetDeviceMode
# transferred as 32 bit
class eDeviceMode(IntEnum):
    ModeReset = 0 # turn off CAN interface
    ModeStart = 1 # turn on  CAN interface

# ---------------

# GS_ReqSetDeviceMode
class kDeviceMode(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("mode",  ctypes.c_uint32),  # eDeviceMode
        ("flags", ctypes.c_uint32),  # eDeviceFlags
    ]

# ---------------

# GS_ReqGetTermination + GS_ReqSetTermination
# transferred as 32 bit
class eTermination(IntEnum):
    TerminationOFF = 0
    TerminationON  = 1

# ---------------

# GS_ReqSetBitTiming + GS_ReqSetBitTimingFD
class kBitTiming(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("prop", ctypes.c_uint32), # Propagation Segment (Time quantums before samplepoint, added to Segemnt 1, legacy, useless, can be always zero)
        ("seg1", ctypes.c_uint32), # Time Segment 1 (Time quantums before samplepoint)
        ("seg2", ctypes.c_uint32), # Time Segment 2 (Time quantums after samplepoint)
        ("sjw",  ctypes.c_uint32), # Synchronization Jump Width, should be min(seg1, seg2)
        ("brp",  ctypes.c_uint32), # Bitrate Prescaler
    ]

# ---------------

# GS_ReqGetCapabilities + GS_ReqGetCapabilitiesFD
class kTimeMinMax(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("seg1_min", ctypes.c_uint32), # minimum allowed by the processor for Time Segment 1 (always 1)
        ("seg1_max", ctypes.c_uint32), # maximum allowed by the processor for Time Segment 1 (including Propagation Segment)
        ("seg2_min", ctypes.c_uint32), # minimum allowed by the processor for Time Segment 2 (always 1)
        ("seg2_max", ctypes.c_uint32), # maximum allowed by the processor for Time Segment 2
        ("sjw_max",  ctypes.c_uint32), # maximum allowed by the processor for Synchronization Jump Width
        ("brp_min",  ctypes.c_uint32), # minimum allowed by the processor for Bitrate Prescaler
        ("brp_max",  ctypes.c_uint32), # maximum allowed by the processor for Bitrate Prescaler
        ("brp_inc",  ctypes.c_uint32), # Undocumented. What is this ???
    ]

# ---------------

# GS_ReqGetCapabilities
# all devices must return this structure
class kCapabilityClassic(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("feature",  ctypes.c_uint32), # eDeviceFlags
        ("fclk_can", ctypes.c_uint32), # CAN Clock which is divided by Bitrate Prescaler
        ("time",     kTimeMinMax),     # Min/Max values for CAN Classic bitrate
    ]

# ---------------

# GS_ReqGetCapabilitiesFD
# this structure is only supported if the device supports CAN FD
class kCapabilityFD(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("feature",   ctypes.c_uint32), # eDeviceFlags
        ("fclk_can",  ctypes.c_uint32), # CAN Clock which is divided by Bitrate Prescaler
        ("time_nom",  kTimeMinMax),     # Min/Max values for CAN FD nominal bitrate
        ("time_data", kTimeMinMax),     # Min/Max values for CAN FD data bitrate
    ]

# ---------------

# transferred as 32 bit
class eBusState(IntEnum):
    BusActive    = 0 # no CAN bus errors
    ErrorWarning = 1 # >=  96 errors
    ErrorPassive = 2 # >= 128 errors
    BusOff       = 3 # >= 248 errors
    Stopped      = 4 # CAN channel is closed
    Sleeping     = 5 # Not used. Complete nonsense: If USB is suspended -> no USB traffic is possible

# ---------------

# GS_ReqGetErrorState
# Read the comment for GS_DevFlagGetErrorState
class kErrorState(ctypes.Structure):
    _pack_ = 4
    _fields_ = [
        ("state",  ctypes.c_uint32), # eBusState
        ("rx_err", ctypes.c_uint32), # count of RX errors (0 ... 248)
        ("tx_err", ctypes.c_uint32), # count of TX errors (0 ... 248)
    ]


# =========================== ERROR REPORT =============================

# The majority of the following errors are not supported by the STM32 processors.
# ElmüSoft error status has been inserted in data byte 5 which was always zero before.

# The errors are sent in the CAN ID and in the data bytes of a special error frame.
# CanID   = eErrFlagsCanID
# data[0] = always zero
# data[1] = eErrFlagsByte1
# data[2] = eErrFlagsByte2
# data[3] = eErrFlagsByte3
# data[4] = eErrFlagsByte4_Hi + eErrFlagsByte4_Lo
# data[5] = ElmüSoft has added missing error flags here: eErrorAppFlags (see settings.h)
# data[6] = Tx Error count
# data[7] = Rx Error count

# transferred as 32 bit
class eErrFlagsCanID(IntFlag):
    Tx_Timeout           = 0x0001   # TX timeout
    Arbitration_lost     = 0x0002   # lost arbitration
    # ---------- useless ------------
    Controller_problem   = 0x0004   # bus status has changed     in data[1]   (useless flag, information already in byte 1)
    Protocol_violation   = 0x0008   # protocol violations stored in data[2+3] (useless flag, information already in bytes 2,3)
    Transceiver_error    = 0x0010   # transceiver status  stored in data[4]   (useless flag, information already in byte 4)
    # -------------------------------
    No_ACK_received      = 0x0020   # received no ACK on transmission
    Bus_is_off           = 0x0040   # bus off
    Bus_error            = 0x0080   # bus error
    Controller_restarted = 0x0100   # controller restarted
    CRC_Error            = 0x0200   # added by ElmüSoft

# ---------------

# Bus Status
# transferred as 8 bit
class eErrFlagsByte1(IntFlag):
    Rx_Buffer_Overflow         = 0x01 # RX buffer overflow (only for legacy, ElmüSoft sends eErrorAppFlags)
    Tx_Buffer_Overflow         = 0x02 # TX buffer overflow (only for legacy, ElmüSoft sends eErrorAppFlags)
    Rx_Errors_at_warning_level = 0x04 # reached warning level at > 96 RX errors
    Tx_Errors_at_warning_level = 0x08 # reached warning level at > 96 TX errors
    Rx_Passive_status_reached  = 0x10 # reached error passive status RX at > 128 errors
    Tx_Passive_status_reached  = 0x20 # reached error passive status TX at > 128 errors
    Bus_is_back_active         = 0x40 # recovered to error active state (this is not an error!)

# ---------------

# Protocol violation
# transferred as 8 bit
class eErrFlagsByte2(IntFlag):
    Single_bit_error             = 0x01 # single bit error
    Frame_format_error           = 0x02 # frame format error
    Bit_stuffing_error           = 0x04 # bit stuffing error
    Unable_to_send_dominant_bit  = 0x08 # unable to send dominant bit
    Unable_to_send_recessive_bit = 0x10 # unable to send recessive bit
    Bus_overload                 = 0x20 # bus overload
    Active_error_announcement    = 0x40 # active error announcement
    Transmission_error           = 0x80 # error occurred on transmission

# ---------------

# Error location of Protocol violation
# This enum is not used, the processor does not give these details.
# And the information is irrelevant at which bit an error occurred.
# If you have spikes that disturb the CAN bus they interfere at any monment.
# transferred as 8 bit
class eErrFlagsByte3(IntEnum):
    at_ID_bits_28__21    = 0x02 # ID bits 28 - 21 (SFF: 10 - 3)
    at_SOF               = 0x03 # start of frame
    at_RTR_substitute    = 0x04 # substitute RTR (SFF: RTR)
    at_IDE_bit           = 0x05 # identifier extension
    at_ID_bits_20__18    = 0x06 # ID bits 20 - 18 (SFF: 2 - 0 )
    at_ID_bits_17__13    = 0x07 # ID bits 17-13
    at_CRC_Sequence      = 0x08 # CRC sequence
    at_Reserved_bit_0    = 0x09 # reserved bit 0
    in_data_section      = 0x0A # data section
    at_DLC_bit           = 0x0B # data length code
    at_RTR_bit           = 0x0C # RTR
    at_Reserved_bit_1    = 0x0D # reserved bit 1
    at_ID_bits_4__0      = 0x0E # ID bits 4-0
    at_ID_bits_12__5     = 0x0F # ID bits 12-5
    Intermission         = 0x12 # intermission
    at_CRC_delimiter     = 0x18 # CRC delimiter
    at_ACK_slot          = 0x19 # ACK slot
    at_EOF               = 0x1A # end of frame
    at_ACK_delimiter     = 0x1B # ACK delimiter

# ---------------

# Transceiver Error at wire CAN High
# This enum is not used, the processor does not give these details.
# transferred as 4 bit
class eErrFlagsByte4_Hi(IntEnum):
    CAN_H_No_wire         = 0x04
    CAN_H_Shortcut_to_Bat = 0x05
    CAN_H_Shortcut_to_VCC = 0x06
    CAN_H_Shortcut_to_GND = 0x07
    # ------------------------------
    MASK_H                = 0x0F

# ---------------

# Transceiver Error at wire CAN Low
# This enum is not used, the processor does not give these details.
# transferred as 4 bit
class eErrFlagsByte4_Lo(IntEnum):
    CAN_L_No_wire         = 0x40
    CAN_L_Shortcut_to_Bat = 0x50
    CAN_L_Shortcut_to_VCC = 0x60
    CAN_L_Shortcut_to_GND = 0x70
    CAN_L_Shortcut_CAN__H = 0x80
    # ------------------------------
    MASK_L                = 0xF0

# flags detected in firmware (e.g. buffer overflow)
# are transferred in byte 5 see settings.h --> eErrorAppFlags


# ###############################################################################
#         Legacy GS Transfer Protocol (Geschwister Schneider compatible)
# ###############################################################################

# These flags are OR'ed with the CAN ID
# 3 bit
class eCanIdFlags(IntFlag):
    Error    = 0x20000000 # the frame is an error frame which does not contain CAN bus data (only used in kHostFrameLegacy).
    RTR      = 0x40000000 # the frame is a Remote Transmission Request
    Extended = 0x80000000 # the frame has an extended CAN ID with 29 bit
    # -------------------
    MASK_11  = 0x000007FF # Mask for standard 11 bit ID
    MASK_29  = 0x1FFFFFFF # Mask for extended 29 bit ID

# ---------------

# 8 bit
class eFrameFlags(IntFlag):
    Overflow = 0x01 # not used
    FDF      = 0x02 # The CAN frame has the FDF (Flexible Datarate Frame) flag set. It is a CAN FD frame.
    BRS      = 0x04 # The CAN frame has the BRS (Bit Rate Switch) flag set. The data is transmitted with a higher baudrate
    ESI      = 0x08 # The CAN frame has the ESI (Error State Indicator) flag set. The sender reports errors.

# ---------------

# 32 bit
class eEchoID(IntEnum):
    RxData = 0xFFFFFFFF  # the frame is a Rx packet received from the bus
    # any other value is 'echoed' back to the host at reception by the legacy protocol.
    # Read the detailed comment below about the wrong design of this feature.

# ---------------------------

class kPacketClassic(ctypes.Structure): # Legacy
    _pack_ = 4
    _fields_ = [
        ("data",         ctypes.c_uint8 * 8),
        ("timestamp_us", ctypes.c_uint32), # precision 1 µs (Needs roll over detection! Roll over after one hour!)
    ]

# This is an incredibly stupid design.
# The timestamp is sent behind the data bytes!
# If a CAN FD packet with 8 data bytes is received, 64 data bytes are transmitted over USB !!
class kPacketFD(ctypes.Structure): # Legacy
    _pack_ = 4
    _fields_ = [
        ("data",         ctypes.c_uint8 * 64),
        ("timestamp_us", ctypes.c_uint32), # precision 1 µs (Needs roll over detection! Roll over after one hour!)
    ]

# ---------------------------

class _kHostFrameLegacyUnion(ctypes.Union):
    _fields_ = [
        ("pack_classic", kPacketClassic), # used if flags does not contain FDF
        ("pack_FD",      kPacketFD),      # used if flags contains FDF
        ("raw_data",     ctypes.c_uint8 * ctypes.sizeof(kPacketFD)),
    ]

# this packet is exchanged over USB with the host (Rx / Tx)
class kHostFrameLegacy(ctypes.Structure): # Legacy (size = 80 byte)
    _pack_ = 4
    _anonymous_ = ("_u",)
    _fields_ = [
        ("echo_id",  ctypes.c_uint32),       # eEchoID
        ("can_id",   ctypes.c_uint32),       # CAN ID + eCanIdFlags or error flags
        ("can_dlc",  ctypes.c_uint8),        # 0 ... 15
        ("channel",  ctypes.c_uint8),        # channel number (zero based)
        ("flags",    ctypes.c_uint8),        # eFrameFlags
        ("reserved", ctypes.c_uint8),        # unused
        ("_u",      _kHostFrameLegacyUnion), # size = 68 byte
    ]


# ###############################################################################
#     New ElmüSoft CANable 2.5 Protocol (optimnized for max USB throughput)
# ###############################################################################

# Geschwister Schneider have designed the above structs which have later been adapted on Github to support CAN FD.
# There are several design errors in the legacy Candlelight protocol that have been fixed in the new ElmüSoft protocol.
# These errors reduce the possible USB data throughput unneccessarily.
# We have only a Full Speed USB interface (12 MBit) and want to transfer as much as possible CAN data which may come with 10 Mbaud.
# In case of a multi-channel adapter USB must transfer data of multiple CAN channels.
#
# Issues with the legacy firmware:
# --------------------------------
# 1) When a CAN packet with 8 data bytes was received in CAN FD mode, always 64 data bytes were transmitted in an 80 byte struct over USB.
# 2) kHostFrameLegacy generated unneccessary traffic by sending 6 bytes that are not required in each frame.
# 3) All Tx frames were always echoed back entirely to the host and this additional USB traffic could not be turned off.
# 4) Bus errors were sent in a stupid way (flooding the host with the same error again and again, hundreds per second).
# 5) The legacy structures did not allow to send other data than CAN packets or error frames.
# 6) The legacy firmware had fatal bugs, one of them even resulted in a firmware crash.
# 7) The legacy code was very difficult to understand because the authors were too lazy to write comments.
#
# However, the legacy GS protocol with all it's design errors is still implemented here for backward compatibility with Linux.
#
# The new ElmüSoft protocol:
# --------------------------
# You have to set ELM_DevFlagProtocolElmue to enable the new CANable 2.5 protocol which significantly optimizes USB transfer.
# If you additionally set ELM_DevFlagSendUsbBlobs the USB transfer speed will be optimized to the maximum that is possible.
# The new firmware enables double buffering for USB OUT endpoints for the highest transfer that the hardware allows.
# The new ElmüSoft protocol can also send string messages and calculates the bus load and has a lots of bugfixes.
# See subfolder "SampleApplication C++" for a sample code how to generate precise timestamps using the performance counter in the CPU.
# A new error reporting has been implemented that sends bus errors (passive, bus off, error counters) in an efficient way to the host.
# For more details see https://netcult.ch/elmue/CANable Firmware Update
# ---------------------------------------------------------------------------------

# The buffer size for USB In and OUT transfer
MAX_BLOB_SIZE = 2048

# Detail information about the board / firmware
# added in february 2026 update
# 32 bit
class eBoardFlags(IntFlag):
    Quartz_In_Use  = 0x00000001 # the board has a quartz and the firmware is using it
    USB_HighSpeed  = 0x00000002 # the board supports ultra fast USB transfer (480 MBit/s)

# ---------------

# ELM_ReqGetBoardInfo
# McuDeviceId comes from HAL_GetDEVID() which returns a unique identifier (DBG_IDCODE) for each processor family.
# The STM32G0xx serie uses 0x460, 0x465, 0x476, 0x477 and STM32G4xx uses 0x468, 0x469, 0x479.
class kBoardInfo(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("McuDeviceID", ctypes.c_uint16),    # 0x468
        ("McuName",     ctypes.c_char * 25), # "STM32G431xx" from makefile
        ("BoardName",   ctypes.c_char * 25), # "Multiboard", "OpenlightLabs", "Jhoinrch" from makefile
        # added in february 2026 update
        ("BoardFlags",  ctypes.c_uint32),    # eBoardFlags
    ]

# -----------------------------------------

# ELM_ReqSetFilter
# 8 bit
class eFilterOperation(IntEnum):
    HostClear      =  0 # remove all host filters (adapter must be closed)
    HostPass_11    =  1 # add a new host pass mask filter for 11 bit CAN IDs to be sent to the host over USB
    HostPass_29    =  2 # add a new host pass mask filter for 29 bit CAN IDs to be sent to the host over USB
    # ------------------
    # Bridge Mode (only for multi-channel adapters):
    BridgeClear    = 10 # remove one of the bridge filters. If kFilter.Index = 0xFF --> clear all bridge filters.
    BridgePass_11  = 11 # set a bridge pass  mask filter for 11 bit CAN IDs to be forwarded to kFilter.DestChannel
    BridgePass_29  = 12 # set a bridge pass  mask filter for 29 bit CAN IDs to be forwarded to kFilter.DestChannel
    BridgeBlock_11 = 13 # set a bridge block mask filter for 11 bit CAN IDs to be blocked (not forwarded to kFilter.DestChannel)
    BridgeBlock_29 = 14 # set a bridge block mask filter for 11 bit CAN IDs to be blocked (not forwarded to kFilter.DestChannel)
    # ------------------
    # xxxx             # future expansions are easily possible

# ---------------

# ELM_ReqSetFilter
class kFilter(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        # all filters:
        ("Operation",   ctypes.c_uint8),  # eFilterOperation
        ("Filter",      ctypes.c_uint32), # the filter (e.g. 0x7E0)
        ("Mask",        ctypes.c_uint32), # the mask   (e.g. 0x7FF)
        # only for bridge filters:
        ("Index",       ctypes.c_uint8),  # filter index
        ("DestChannel", ctypes.c_uint8),  # destination channel
        ("Reserved",    ctypes.c_uint8 * 6),
    ]


# -----------------------------------------

# 16 bit = 65536 possible operations
# 16 bit
class ePinOperation(IntEnum):
    Reset    = 0 # Set pin to Low
    Set      = 1 # Set pin to High
    Tristate = 2 # Set pin into tri-state mode.
    PullDown = 3 # Enable a pull down resistor.
    PullUp   = 4 # Enable a pull up resistor.
    Disable  = 5 # Disable pin (used for pin BOOT0 in the Option Bytes)
    Enable   = 6 # Enable  pin
    # xxxx       # future expansions are easily possible

# ---------------

# This enum is limited to 16 bit because it must be transmitted in SETUP.wValue with ELM_ReqGetPinStatus (65535 possible pins).
# In the future pins can be added here that the user can control. Some boards have jumpers where processor pins are connected.
# But it would be completely wrong to allow the user to set *ANY* pin here like Pin 15 of GPIO port B.
# Many pins have special functions and changing them may result in a crash.
# If you add pins to be controlled here, make sure that only valid values are accepted.
# For example if you have a board with more LEDs than usual a new Pin ID could be PINID_LED_ERROR.
# As the pins depend on the board, the final pins will have to be defined in settings.h under #if defined(BoardName) ...
# and here only an ID is defined that is forwarded to the destination pin and port defined in settings.h
# Currently only disabling pin BOOT0 is implemented.
# 16 bit
class ePinID(IntEnum):
    BOOT0 = 1  # the pin BOOT0 can be disabled in the Option Bytes
    # xxxx     # future expansions are easily possible

# ELM_ReqSetPinStatus
class kPinStatus(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("Operation", ctypes.c_uint16), # ePinOperation
        ("PinID",     ctypes.c_uint16), # ePinID
        ("Reserved1", ctypes.c_uint32),
        ("Reserved2", ctypes.c_uint32),
    ]

# -------------------

# ELM_ReqGetPinStatus (bit flags)
# The USB protocol does not allow to receive OUT data bytes from the host and return in the same SETUP request IN data bytes to the host.
# So we cannot receive the desired pin ID from the host and return the pin status in the data bytes.
# Therefore this command must receive the requested Pin ID in the SETUP packet in wValue (16 bit).
# Receive: SETUP.wValue = ePinID, Send: ePinStatus in 2 data bytes
# 16 bit
class ePinStatus(IntFlag):
    High    = 0x0001  # the pin is currently High.    If this bit is not set it is Low.
    Enabled = 0x0002  # the pin is currently Enabled. If this bit is not set it is Disabled.
    # xxxx            # future expansions are easily possible

# -----------------------------------------------------------------------------------------------

# 8 bit
class eMessageType(IntEnum):
    TxFrame = 10  # 0x0A: the message contains a CAN frame to be sent to CAN bus (kTxFrameElmue)
    TxEcho  = 11  # 0x0B: the message contains the echo marker of a Tx CAN frame (only sent if Tx marker > 0)
    RxFrame = 12  # 0x0C: the message contains a received CAN frame from CAN bus (kRxFrameElmue)
    Error   = 13  # 0x0D: the message contains multiple error flags (kErrorElmue, same format as legacy protocol)
    String  = 14  # 0x0E: the message contains an ASCII string to be displayed to the user (kStringElmue)
    Busload = 15  # 0x0F: the message contains one byte which is the bus load in percent (kBusloadElmue)
    TxBlob  = 16  # 0x10: the message contains a blob (kBlob) with multiple kTxFrameElmue
    RxBlob  = 17  # 0x11: the message contains a blob (kBlob) with multiple kRxFrameElmue

# ---------------

# Multiple CAN frames can be transferred in one blob (binary large object) to reduce the overhead of USB tokens and USB handshake.
# This requires the flag ELM_DevFlagSendUsbBlobs to be set when opening the channel.
# The maximum size of the entire blob are MAX_BLOB_SIZE bytes.
class kBlob(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("frame_count", ctypes.c_uint8),  # the count of kTxFrameElmue or kRxFrameElmue that follow after this header
        ("msg_type",    ctypes.c_uint8),  # eMessageType = MSG_TxBlob or MSG_RxBlob
    ]

# ---------------

# Common header for all structs. Allows easily adding new features in the future.
class kHeader(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("size",     ctypes.c_uint8),  # the total length of this message (struct + the appended data bytes)
        ("msg_type", ctypes.c_uint8),  # eMessageType
    ]

# ---------------

# This struct is received on the OUT endpoint from the host.
# A DLC byte is not required. The count of transferred data bytes is calculated as: header.size - sizeof(kTxFrameElmue)
# For remote frames the host can write the DLC value into the first data byte, otherwise DLC = 0 is sent.
class kTxFrameElmue(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("header", kHeader),          # msg_type = MSG_TxFrame
        ("flags",  ctypes.c_uint8),   # eFrameFlags
        ("can_id", ctypes.c_uint32),  # CAN ID + eCanIdFlags
        ("marker", ctypes.c_uint8),   # one-byte marker that is sent back to the host with MSG_TxEcho when ACKnowledged
    ]

# ---------------

# This struct is transmitted on the IN endpoint to the host.
# A DLC byte is not required. The count of transferred data bytes is calculated as: header.size - sizeof(kRxFrameElmue)
# For remote frames the DLC from the Rx packet is transmitted in the first data byte to the host.
# If timestamps are not used, subtract 4 additional bytes.
class kRxFrameElmue(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("header",    kHeader),            # msg_type = MSG_RxFrame
        ("flags",     ctypes.c_uint8),     # eFrameFlags
        ("can_id",    ctypes.c_uint32),    # CAN ID + eCanIdFlags
        ("timestamp", ctypes.c_uint32),    # timestamp with 1 µs precision (only sent if GS_DevFlagTimestamp is set)
      # ("data",      ctypes.c_uint8 * X), -> followed by variable-length data bytes
    ]

# ---------------

# Echo packet sent to host upon CAN bus acknowledgement.
class kTxEchoElmue(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("header",    kHeader),         # msg_type = MSG_TxEcho
        ("marker",    ctypes.c_uint8),  # the same marker that was sent in kTxFrameElmue
        ("timestamp", ctypes.c_uint32), # timestamp with 1 µs precision (only sent if GS_DevFlagTimestamp is set)
    ]

# ---------------

# Error report packet.
class kErrorElmue(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("header",    kHeader),            # msg_type = MSG_Error
        ("err_id",    ctypes.c_uint32),    # eErrFlagsCanID
        ("err_data",  ctypes.c_uint8 * 8), # several error flags and error counters
        ("timestamp", ctypes.c_uint32),    # timestamp with 1 µs precision (only sent if GS_DevFlagTimestamp is set)
    ]

# ---------------

# Debug ASCII string message packet.
class kStringElmue(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("header",    kHeader),           # msg_type = MSG_String
      # ("ascii_msg", ctypes.c_char * X), -> followed by variable-length string data
    ]

# ---------------

# Bus load percentage report.
class kBusloadElmue(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("header",   kHeader),          # msg_type = MSG_Busload
        ("bus_load", ctypes.c_uint8),   # current bus load in percent
    ]
    

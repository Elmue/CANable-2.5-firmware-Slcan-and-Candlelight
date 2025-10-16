/*
    This class is part of the CANable 2.5 firmware, adapted to Visual Studio
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

// define Linux stuff
#define  uint8_t    BYTE
#define  uint16_t   WORD
#define  uint32_t   DWORD
#define  __aligned(x)  
#define  __packed 
#pragma warning(disable: 4200) // warning: nonstandard extension used : zero-sized array in struct/union

// ==============================================================================

// Command sent from the host application in a SETUP request
typedef enum // transferred as 8 bit 
{
    // ---------- GS commands from Geschwister Schneider -----------
    GS_ReqSetHostFormat  = 0,  // uint32_t: define little/big endian data transfer (only little is supported)
    GS_ReqSetBitTiming,        // kBitTiming: set CAN classic/nominal bit timing (baudrate + samplepoint)
    GS_ReqSetDeviceMode,       // kDeviceMode: start / stop CAN device and set device flags
    GS_ReqBerrReport,          // -- not implemented, undocumented 
    GS_ReqGetCapabilities,     // kCapabilityClassic: get supported features and processor limits of timing for classic frames
    GS_ReqGetDeviceVersion,    // kDeviceVersion: get version numbers
    GS_ReqGetTimestamp,        // uint32_t: get firmware 1 µs timestamp (Needs roll over detection! Roll over after one hour!)
    GS_ReqIdentify,            // uint32_t (ignored): blink LEDs for device identification
    GS_ReqGetUserID,           // -- not implemented, undocumented  (WTF is a user ID ??)
    GS_ReqSetUserID,           // -- not implemented, undocumented  (WTF is a user ID ??)
    GS_ReqSetBitTimingFD,      // kBitTiming: set data bit timing (CAN FD data baudrate + samplepoint)
    GS_ReqGetCapabilitiesFD,   // kCapabilityFD: get supported features and processor limits of timing for CAN FD
    GS_ReqSetTermination,      // eTermination: enable the 120 Ohm termination resistor (if supported by the board)
    GS_ReqGetTermination,      // eTermination: get status of 120 Ohm termination resistor (if supported by the board)
    GS_ReqGetState,            // kDeviceState: not implemented, undocumented

    // ----------- ELM commands added by ElmüSoft -----------
    ELM_ReqGetBoardInfo = 20,  // kBoardInfo: get name about target board and processor
    ELM_ReqSetFilter,          // kFilter: set up to 8 acceptance mask filters
    ELM_ReqGetLastError,       // uint8_t: get the eFeedback error that has stalled the SETUP request of the last command
    ELM_ReqSetBusLoadReport,   // uint8_t: enable busload report in percent to be sent in a user defined interval
    ELM_ReqSetPinStatus,       // kPinStatus: set, reset, enable, disable,... processor pins
    ELM_ReqGetPinStatus,       // Receive: SETUP.wValue = ePinID, Send: ePinStatus in 2 data bytes
} eUsbRequest;

// These flags are used to enable/disable a mode with GS_ReqSetDeviceMode 
// and the same flags are returned as capability with commands GS_ReqGetCapabilities and GS_ReqGetCapabilitiesFD
typedef enum // transferred as 32 bit 
{
    GS_DevFlagNone                    = 0,
    // ----------- GS flags from Geschwister Schneider -----------
    // silent mode (do not send ACK)
    GS_DevFlagListenOnly              = 0x00001,
    // support of loopback mode (sent packets are received directly inside the processor)
    // If this flag is combined with ListenOnly, the internal loopback mode is enabled, otherwise the external loopback mode.
    GS_DevFlagLoopback                = 0x00002,
    // take 3 samples per 1 bit on CAN bus, not implemenetd
    GS_DevFlagTripleSample            = 0x00004,
    // if set, send a packet only once, otherwise retransmit until an ACK was revcived
    GS_DevFlagOneShot                 = 0x00008,
    // Send a hardware timestamp with each Rx packet and Tx echo.
    // Deprecated: creates more USB traffic overhead on a slow Full speed USB device.
    // Timestamps should be created in the host application at packet reception.
    // See subfolder "SampleApplication C++" for a sample code how to generate precise timestamps in Windows.
    GS_DevFlagTimestamp               = 0x00010,
    // blink the LEDs to distinguish between multiple connected devices
    GS_DevFlagIdentify                = 0x00020,
    // undocumented, not implemented, WTF is a user id ?
    GS_DevFlagUserID                  = 0x00040,
    // This is total nonsense: Send always 128 byte USB packets to the host, not implemented
    GS_DevFlagPadPacketsToMaxSize     = 0x00080,
    // In the feature flags this means that CAN FD is supported.
    // In kDeviceMode it is useless because CAN FD is enabled as soon as a data bitrate has been set.
    GS_DevFlagCAN_FD                  = 0x00100,
    // request workaround for LPC546XX erratum USB.15: let host driver add a padding byte to each USB frame, not implemented
    GS_DevFlagQuirk_LPC546XX          = 0x00200,
    // Setting a data bitrate for CAN FD is supported (commands GS_ReqGetCapabilitiesFD and GS_ReqSetBitTimingFD can be used)
    GS_DevFlagBitTimingFD             = 0x00400,
    // The 120 ohm termination resistor can be turned on/off by command, only few boards support this.
    GS_DevFlagTermination             = 0x00800,
    // undocumented, not implemented
    GS_DevFlagBerrReporting           = 0x01000,
    // Not implemented (send struct kDeviceState) because errors are reported in special error frames.
    // It is not required that the host application must poll errors. They are reported automatically when the error status changes.
    GS_DevFlagGetState                = 0x02000,

    // ----------- ELM flags added by ElmüSoft -----------
    // Switch to the new extended ElmüSoft CANable 2.5 protocol (use kHostFrameElmue instead of kHostFrameLegacy)  
    // In the Capabilities this flag means that all ELM_ReqXXX commands are supported
    ELM_DevFlagProtocolElmue          = 0x04000, 
    // Do not send an echo for the successfully sent CAN packets (by default this is enabled in the Candlelight firmware)
    // The Tx event packet is sent in the moment when the ACK was recived. You can turn this off to reduce USB traffic.
    ELM_DevFlagDisableTxEcho          = 0x08000, 
} eDeviceFlags;

// ==============================================================================

typedef enum // sent as 8 bit
{
    FBK_Success           = 2,    // Command successfully executed
    // --------------------------    
    FBK_InvalidCommand    = '1',  // The command is invalid
    FBK_InvalidParameter,         // One of the parameters is invalid
    FBK_AdapterMustBeOpen,        // The command cannot be executed before opening the adapter
    FBK_AdapterMustBeClosed,      // The command cannot be executed after  opening the adapter
    FBK_ErrorFromHAL,             // The HAL from ST Microelectronics has reported an error
    FBK_UnsupportedFeature,       // The feature is not implemented or not supported by the board
    FBK_TxBufferFull,             // Sending is not possible because the buffer is full (only Slcan)
    FBK_BusIsOff,                 // Sending is not possible because the processor is blocked in the BusOff state
    FBK_NoTxInSilentMode,         // Sending is not possible because the adapter is in Bus Monitoring mode
    FBK_BaudrateNotSet,           // Opening the adapter is not possible if no baudrate has been set
    FBK_OptBytesProgrFailed,      // Programming the Option Bytes failed
    FBK_ResetRequired,            // The user must disconnect and reconnect the USB cable to enter boot mode
} eFeedback;

// If bus status is BUS_OFF both LED's (green + blue) are permanently ON
// This status is controlled only by hardware
// Slcan sends this in the error report "EXXXXXXXX\r"
typedef enum // sent as 4 bit
{
    BUS_StatusActive     = 0x00, // operational  (must be zero because this is not an error)
    BUS_StatusWarning    = 0x10, // set in can.c (>  96 errors)
    BUS_StatusPassive    = 0x20, // set in can.c (> 128 errors)
    BUS_StatusOff        = 0x30, // set in can.c (> 248 errors)
} eErrorBusStatus;

// If any of these flags is set, both LED's (green + blue) are permanently ON
// These flags are reset after sending them once to the host
// They are set again if the error is still present
// Slcan sends this in the error report "EXXXXXXXX\r"
// Candlelight sends this in a special error packet with a flag (legacy: CAN_ID_Error, ElmüSoft: MSG_Error)
typedef enum // sent as 8 bit 
{
    APP_CanRxFail       = 0x01, // the HAL reports an error receiving a CAN packet.
    APP_CanTxFail       = 0x02, // trying to send while in silent mode, while bus off or adaper not open or HAL error
    APP_CanTxOverflow   = 0x04, // a CAN packet could not be sent because the Tx FIFO + buffer are full (mostly because bus is passive).
    APP_UsbInOverflow   = 0x08, // a USB IN packet could not be sent because CAN traffic is faster than USB transfer.
    APP_CanTxTimeout    = 0x10, // A packet in the transmit FIFO was not acknowledged during 500 ms --> abort Tx and clear Tx buffer.
} eErrorAppFlags;

// ==============================================================================

// 4 byte alignment
#pragma pack(push,4)

// GS_ReqGetDeviceVersion
typedef struct  
{
    uint8_t  reserved1;
    uint8_t  reserved2;
    uint8_t  reserved3;
    uint8_t  icount;         // Always zero. Undocumented. What is this ?
    uint32_t sw_version_bcd; // software (firmware) version in BCD format
    uint32_t hw_version_bcd; // hardware version in BCD format
} __packed __aligned(4) kDeviceVersion;

// ---------------

// GS_ReqSetDeviceMode
typedef enum // transferred as 32 bit 
{
    GS_ModeReset = 0, // turn off CAN interface
    GS_ModeStart,     // turn on  CAN interface
} eDeviceMode;

// GS_ReqSetDeviceMode
typedef struct  
{
    uint32_t  mode;   // eDeviceMode
    uint32_t  flags;  // eDeviceFlags
} __packed __aligned(4) kDeviceMode;

// ---------------

// GS_ReqGetTermination + GS_ReqSetTermination
typedef enum // transferred as 32 bit
{
    GS_TerminationOFF = 0,
    GS_TerminationON,
} eTermination;

// ---------------

// GS_ReqSetBitTiming + GS_ReqSetBitTimingFD
typedef struct  
{
    uint32_t prop;     // Propagation Segment (Time quantums before samplepoint, added to Segemnt 1, legacy, useless, can be always zero)
    uint32_t seg1;     // Time Segment 1 (Time quantums before samplepoint)
    uint32_t seg2;     // Time Segment 2 (Time quantums after samplepoint)
    uint32_t sjw;      // Synchronization Jump Width, should be min(seg1, seg2)
    uint32_t brp;      // Bitrate Prescaler
} __packed __aligned(4) kBitTiming;

// ---------------

// GS_ReqGetCapabilities + GS_ReqGetCapabilitiesFD
typedef struct
{
    uint32_t seg1_min;  // minimum allowed by the processor for Time Segment 1 (always 1)
    uint32_t seg1_max;  // maximum allowed by the processor for Time Segment 1 (including Propagation Segment)
    uint32_t seg2_min;  // minimum allowed by the processor for Time Segment 2 (always 1)
    uint32_t seg2_max;  // maximum allowed by the processor for Time Segment 2
    uint32_t sjw_max;   // maximum allowed by the processor for Synchronization Jump Width
    uint32_t brp_min;   // minimum allowed by the processor for Bitrate Prescaler
    uint32_t brp_max;   // maximum allowed by the processor for Bitrate Prescaler
    uint32_t brp_inc;   // Undocumented. What is this ???
} __packed kTimeMinMax;

// GS_ReqGetCapabilities
// all devices must return this structure
typedef struct
{
    uint32_t    feature;  // eDeviceFlags
    uint32_t    fclk_can; // CAN Clock which is divided by Bitrate Prescaler
    kTimeMinMax time;     // Min/Max values for CAN Classic bitrate
} __packed __aligned(4) kCapabilityClassic;

// GS_ReqGetCapabilitiesFD
// this structure is only supported if the device supports CAN FD
typedef struct  
{
    uint32_t    feature;   // eDeviceFlags
    uint32_t    fclk_can;  // CAN Clock which is divided by Bitrate Prescaler
    kTimeMinMax time_nom;  // Min/Max values for CAN FD nominal bitrate
    kTimeMinMax time_data; // Min/Max values for CAN FD data bitrate
} __packed __aligned(4) kCapabilityFD;

// ---------------

/*
// The following has never been implemented in firmware on Github.
// This struct has been replaced by error packets with the flag CAN_ID_Error.
// The advantage is that errors are sent automatically to the host only when they are present.
// GS_ReqGetState / kDeviceState was clumsy, because the host had to poll for errors.

// not used (previously the intention was to use this struct with GS_ReqGetState)
typedef enum // transferred as 32 bit
{
    GS_BusActive = 0, // no CAN bus errors
    GS_ErrorWarning,  // >=  96 errors
    GS_ErrorPassive,  // >= 128 errors
    GS_BusOff,        // >= 248 errors
    GS_Stopped,
    GS_Sleeping,
} eBusState;

// not used (previously the intention was to use this struct with GS_ReqGetState)
typedef struct  
{
    uint32_t state;  // eBusState
    uint32_t rx_err; // count of RX errors (0 ... 248)
    uint32_t tx_err; // count of TX errors (0 ... 248)
} __packed __aligned(4) kDeviceState;

*/


// =========================== ERROR REPORT =============================

// The majority of the following errors are not supported by the STM32 processors.
// ElmüSoft error status has been inserted in data byte 5 which was always zero before.

// The errors are sent in the CAN ID and in the data bytes of a special error frame.
// CanID   = eErrFlagsCanID
// data[0] = always zero
// data[1] = eErrFlagsByte1
// data[2] = eErrFlagsByte2
// data[3] = eErrFlagsByte3
// data[4] = eErrFlagsByte4_Hi + eErrFlagsByte4_Lo
// data[5] = ElmüSoft has added missing error flags here: eErrorAppFlags (see settings.h)
// data[6] = Tx Error count
// data[7] = Rx Error count

typedef enum // transferred as 32 bit 
{
    ERID_Tx_Timeout           = 0x0001,   // TX timeout
    ERID_Arbitration_lost     = 0x0002,   // lost arbitration
    // ---------- useless ------------
    ERID_Controller_problem   = 0x0004,   // bus status has changed     in data[1]   (useless flag, information already in byte 1)
    ERID_Protocol_violation   = 0x0008,   // protocol violations stored in data[2+3] (useless flag, information already in bytes 2,3)
    ERID_Transceiver_error    = 0x0010,   // transceiver status  stored in data[4]   (useless flag, information already in byte 4)   
    // -------------------------------
    ERID_No_ACK_received      = 0x0020,   // received no ACK on transmission
    ERID_Bus_is_off           = 0x0040,   // bus off 
    ERID_Bus_error            = 0x0080,   // bus error
    ERID_Controller_restarted = 0x0100,   // controller restarted
    ERID_CRC_Error            = 0x0200,   // added by ElmüSoft
} eErrFlagsCanID;

// Bus Status
typedef enum // transferred as 8 bit 
{
    ER1_Rx_Buffer_Overflow         = 0x01, // RX buffer overflow (only for legacy, ElmüSoft sends eErrorAppFlags)
    ER1_Tx_Buffer_Overflow         = 0x02, // TX buffer overflow (only for legacy, ElmüSoft sends eErrorAppFlags)
    ER1_Rx_Errors_at_warning_level = 0x04, // reached warning level at > 96 RX errors
    ER1_Tx_Errors_at_warning_level = 0x08, // reached warning level at > 96 TX errors
    ER1_Rx_Passive_status_reached  = 0x10, // reached error passive status RX at > 128 errors
    ER1_Tx_Passive_status_reached  = 0x20, // reached error passive status TX at > 128 errors
    ER1_Bus_is_back_active         = 0x40, // recovered to error active state (this is not an error!)
} eErrFlagsByte1;

// Protocol violation
typedef enum eErrFlagsByte2 // transferred as 8 bit 
{
    ER2_Single_bit_error             = 0x01, // single bit error 
    ER2_Frame_format_error           = 0x02, // frame format error 
    ER2_Bit_stuffing_error           = 0x04, // bit stuffing error 
    ER2_Unable_to_send_dominant_bit  = 0x08, // unable to send dominant bit 
    ER2_Unable_to_send_recessive_bit = 0x10, // unable to send recessive bit
    ER2_Bus_overload                 = 0x20, // bus overload 
    ER2_Active_error_announcement    = 0x40, // active error announcement
    ER2_Transmission_error           = 0x80, // error occurred on transmission 
} eErrFlagsByte2;

// Error location of Protocol violation
// This enum is not used, the processor does not give these details.
// And the information is irrelevant at which bit an error occurred. 
// If you have spikes that disturb the CAN bus they interfere at any monment.
typedef enum // transferred as 8 bit  
{
    ER3_at_ID_bits_28__21    = 0x02, // ID bits 28 - 21 (SFF: 10 - 3)
    ER3_at_SOF               = 0x03, // start of frame 
    ER3_at_RTR_substitute    = 0x04, // substitute RTR (SFF: RTR) 
    ER3_at_IDE_bit           = 0x05, // identifier extension 
    ER3_at_ID_bits_20__18    = 0x06, // ID bits 20 - 18 (SFF: 2 - 0 )
    ER3_at_ID_bits_17__13    = 0x07, // ID bits 17-13 
    ER3_at_CRC_Sequence      = 0x08, // CRC sequence 
    ER3_at_Reserved_bit_0    = 0x09, // reserved bit 0 
    ER3_in_data_section      = 0x0A, // data section 
    ER3_at_DLC_bit           = 0x0B, // data length code 
    ER3_at_RTR_bit           = 0x0C, // RTR 
    ER3_at_Reserved_bit_1    = 0x0D, // reserved bit 1 
    ER3_at_ID_bits_4__0      = 0x0E, // ID bits 4-0 
    ER3_at_ID_bits_12__5     = 0x0F, // ID bits 12-5 
    ER3_Intermission         = 0x12, // intermission 
    ER3_at_CRC_delimiter     = 0x18, // CRC delimiter 
    ER3_at_ACK_slot          = 0x19, // ACK slot 
    ER3_at_EOF               = 0x1A, // end of frame 
    ER3_at_ACK_delimiter     = 0x1B, // ACK delimiter 
} eErrFlagsByte3;

// Transceiver Error at wire CAN High
// This enum is not used, the processor does not give these details.
typedef enum // transferred as 4 bit 
{
    ER4_CAN_H_No_wire         = 0x04,
    ER4_CAN_H_Shortcut_to_Bat = 0x05,
    ER4_CAN_H_Shortcut_to_VCC = 0x06,
    ER4_CAN_H_Shortcut_to_GND = 0x07,
    // --------------------------------
    ER4_MASK_H                = 0x0F,
} eErrFlagsByte4_Hi;

// Transceiver Error at wire CAN Low
// This enum is not used, the processor does not give these details.
typedef enum // transferred as 4 bit 
{
    ER4_CAN_L_No_wire         = 0x40,
    ER4_CAN_L_Shortcut_to_Bat = 0x50,
    ER4_CAN_L_Shortcut_to_VCC = 0x60,
    ER4_CAN_L_Shortcut_to_GND = 0x70,
    ER4_CAN_L_Shortcut_CAN__H = 0x80,
    // --------------------------------
    ER4_MASK_L                = 0xF0,
} eErrFlagsByte4_Lo;

// flags detected in firmware (e.g. buffer overflow)
// are transferred in byte 5 see settings.h --> eErrorAppFlags


// ###############################################################################
//           Legacy GS Transfer Protocol (Geschwister Schneider compatible)
// ###############################################################################

// These flags are OR'ed with the CAN ID
typedef enum // 3 bit
{
    CAN_ID_Error = 0x20000000, // the frame is an error frame which does not contain CAN bus data.
    CAN_ID_RTR   = 0x40000000, // the frame is a Remote Transmission Request
    CAN_ID_29Bit = 0x80000000, // the frame has an extended CAN ID with a 29 bit
    CAN_MASK_11  = 0x000007FF, // Mask for standard 11 bit ID
    CAN_MASK_29  = 0x1FFFFFFF, // Mask for extended 29 bit ID
} eCanIdFlags;

typedef enum // 8 bit
{
    FRM_Overflow = 0x01, // not used
    FRM_FDF      = 0x02, // The CAN frame has the FDF (Flexible Datarate Frame) flag set. It is a CAN FD frame.
    FRM_BRS      = 0x04, // The CAN frame has the BRS (Bit Rate Switch) flag set. The data is transmitted with a higher baudrate
    FRM_ESI      = 0x08, // The CAN frame has the ESI (Error State Indicator) flag set. The sender reports errors.
} eFrameFlags;

typedef enum // 32 bit 
{
    ECHO_RxData = 0xFFFFFFFF,  // the frame is a Rx packet received from the bus
    // any other value is 'echoed' back to the host at reception by the legacy protocol.
    // Read the detailed comment below about the wrong design of this feature.
} eEchoID;

// ---------------------------

typedef struct  // Legacy
{
    uint8_t  data[8];
    uint32_t timestamp_us; // precision 1 µs (Needs roll over detection! Roll over after one hour!)
} __packed kPacketClassic;

// This is an incredibly stupid design.
// The timestamp is sent behind the data bytes!
// If a CAN FD packet with 8 data bytes is received, 64 data bytes are transmitted over USB !!
typedef struct  // Legacy  
{
    uint8_t  data[64];
    uint32_t timestamp_us; // precision 1 µs (Needs roll over detection! Roll over after one hour!)
} __packed kPacketFD;

// ---------------------------

// this packet is exchanged over USB with the host (Rx / Tx)
typedef struct  // Legacy (size = 80 byte)
{
    uint32_t echo_id;    // eEchoID
    uint32_t can_id;     // CAN ID + eCanIdFlags or error flags
    uint8_t  can_dlc;    // 0 ... 15
    uint8_t  channel;    // unused, always zero
    uint8_t  flags;      // eFrameFlags
    uint8_t  reserved;   // unused
    union // size = 68 byte
    {
        kPacketClassic pack_classic;
        kPacketFD      pack_FD;
        uint8_t        raw_data[sizeof(kPacketFD)];
    };
} __packed __aligned(4) kHostFrameLegacy;

#pragma pack(pop)

// ###############################################################################
//     New ElmüSoft CANable 2.5 Protocol (optimnized for max USB throughput)
// ###############################################################################

// Geschwister Schneider have designed the above structs which have later been adapted on Github to support CAN FD.
// There are several design errors in the legacy Candlelight protocol that have been fixed in the new ElmüSoft protocol.
// These errors reduce the possible USB data throughput unneccessarily.
// We have only a Full Speed USB interface (12 MBit) and want to transfer as much as possible CAN data which may come with 10 Mbaud.
//
// 1) When a CAN packet with 8 data bytes is received in CAN FD mode, always 64 data bytes were transmitted in an 80 byte struct over USB.
// 2) kHostFrameLegacy generates unneccessary traffic by sending 6 bytes that are not required in each frame.
// 3) All Tx frames are always echoed back entirely to the host and this additional USB traffic cannot be turned off.
// 4) The idea to send multiple CAN channels over one Full speed USB connection is totally absurd.
// 5) Bus errors are sent in a stupid way (flooding the host with the same error again and again, hundreds per second).
// 6) The legacy structures do not allow to send other data than CAN packets or error frames.
// 7) The legacy firmware had fatal bugs, of which one resulted even in a firmware crash.
//
// However, the legacy GS protocol with all it's design errors is still implemented here for backward compatibility with legacy software.
// You have to set ELM_DevFlagProtocolElmue to enable the new CANable 2.5 protocol which optimizes USB transfer to the maximum.
// Additionally the new ElmüSoft protocol can send string messages and calculates the bus load and has a lots of bugfixes.
// See subfolder "SampleApplication C++" for a sample code how to generate precise timestamps using the performance counter in the CPU.
// The legacy code was very difficult to understand because the authors were too lazy to write comments. This has been fixed by ElmüSoft.
// A new error reporting has been implemented that sends bus errors (passive, bus off, error counters) in an efficient way to the host.
// For more details see https://netcult.ch/elmue/CANable Firmware Update
// ---------------------------------------------------------------------------------

// one byte alignment
#pragma pack(push,1)

// ELM_ReqGetBoardInfo
// McuDeviceId comes from HAL_GetDEVID() which returns a unique identifier (DBG_IDCODE) for each processor family.
// The STM32G0xx serie uses 0x460, 0x465, 0x476, 0x477 and STM32G4xx uses 0x468, 0x469, 0x479.
typedef struct 
{
    uint16_t McuDeviceID;   // 0x468
    char     McuName  [25]; // "STM32G431xx" from makefile
    char     BoardName[25]; // "MksMakerbase", "OpenlightLabs" from makefile
} __packed __aligned(1) kBoardInfo;

// -----------------------------------------

// 8 bit = 256 possible operations
typedef enum // 8 bit
{
    FIL_ClearAll = 0,    // remove all filters
    FIL_AcceptMask11bit, // add a new acceptance mask filter for 11 bit CAN IDs
    FIL_AcceptMask29bit, // add a new acceptance mask filter for 29 bit CAN IDs
//  FIL_xxxx             // future expansions are easily possible
} eFilterOperation;

// ELM_ReqSetFilter
typedef struct
{
    uint8_t  Operation; // eFilterOperation
    uint32_t Filter;    // the filter (e.g. 0x7E0), ignored for Operation FIL_ClearAll
    uint32_t Mask;      // the mask   (e.g. 0x7FF), ignored for Operation FIL_ClearAll
    uint32_t Reserved1;
    uint32_t Reserved2;
} __packed __aligned(1) kFilter;


// -----------------------------------------

// 16 bit = 65536 possible operations
typedef enum // 16 bit
{
    PINOP_Reset = 0,    // Set pin to Low  
    PINOP_Set,          // Set pin to High 
    PINOP_Tristate,     // Set pin into tri-state mode.
    PINOP_PullDown,     // Enable a pull down resistor.
    PINOP_PullUp,       // Enable a pull up resistor.
    PINOP_Disable,      // Disable pin (used for pin BOOT0 in the Option Bytes)
    PINOP_Enable,       // Enable  pin 
//  PINOP_xxxx          // future expansions are easily possible
} ePinOperation;

// This enum is limited to 16 bit because it must be transmitted in SETUP.wValue with ELM_ReqGetPinStatus (65535 possible pins).
// In the future pins can be added here that the user can control. Some boards have jumpers where processor pins are connected.
// But it would be completely wrong to allow the user to set *ANY* pin here like Pin 15 of GPIO port B.
// Many pins have special functions and changing them may result in a crash.
// If you add pins to be controlled here, make sure that only valid values are accepted.
// For example if you have a board with more LEDs than usual a new Pin ID could be PINID_LED_ERROR.
// As the pins depend on the board, the final pins will have to be defined in settings.h under #if defined(BoardName) ...
// and here only an ID is defined that is forwarded to the destination pin and port defined in settings.h
// Currently only disabling pin BOOT0 is implemented.
typedef enum // 16 bit
{
    PINID_BOOT0 = 1,    // the pin BOOT0 can be disabled in the Option Bytes
//  PINID_xxxx          // future expansions are easily possible
} ePinID;

// ELM_ReqSetPinStatus
typedef struct
{
    uint16_t  Operation; // ePinOperation
    uint16_t  PinID;     // ePinID
    uint32_t  Reserved1;
    uint32_t  Reserved2;
} __packed __aligned(1) kPinStatus;

// -------------------

// ELM_ReqGetPinStatus (bit flags)
// The USB protocol does not allow to receive OUT data bytes from the host and return in the same SETUP request IN data bytes to the host.
// So we cannot receive the desired pin ID from the host and return the pin status in the data bytes.
// Therefore this command must receive the requested Pin ID in the SETUP packet in wValue (16 bit).
// Receive: SETUP.wValue = ePinID, Send: ePinStatus in 2 data bytes
typedef enum // 16 bit
{
    PINST_High    = 0x0001,  // the pin is currently High.    If this bit is not set it is Low.
    PINST_Enabled = 0x0002,  // the pin is currently Enabled. If this bit is not set it is Disabled.
//  PINST_xxxx               // future expansions are easily possible    
} ePinStatus;

// -----------------------------------------------------------------------------------------------

typedef enum // 8 bit
{
    // received from host
    MSG_TxFrame = 10, // the message contains a CAN frame to be sent to CAN bus (kTxFrameElmue)
    // sent to host
    MSG_TxEcho,       // the message contains the echo marker of a Tx CAN frame (kTxEchoElmue, can be disabled with ELM_DevFlagDisableTxEcho)    
    MSG_RxFrame,      // the message contains a received CAN frame from CAN bus (kRxFrameElmue)
    MSG_Error,        // the message contains multiple error flags (kErrorElmue, same format as legacy protocol, see buf_store_error())
    MSG_String,       // the message contains an ASCII string to be displayed to the user (kStringElmue)
    MSG_Busload,      // the message contains one byte which is the bus load in percent (kBusloadElmue)
//  MSG_xxxx          // future expansions are easily possible
} eMessageType;

// common header for all structs. Allows easily adding new features in the future.
typedef struct 
{
    uint8_t  size;      // the total length of this message (struct + the appended data bytes)
    uint8_t  msg_type;  // eMessageType
} __packed __aligned(1) kHeader;

// this struct is received on endpoint 02 (OUT) from the host
// A DLC byte is not required: count of transferred data bytes is calculated as: header.size - sizeof(kTxFrameElmue)
// see buf_process_can_bus()
typedef struct 
{
    kHeader  header;      // MSG_TxFrame
    uint8_t  flags;       // eFrameFlags    
    uint32_t can_id;      // CAN ID + eCanIdFlags
    uint8_t  marker;      // one-byte marker that is sent back to the host with MSG_TxEcho when the packet has been ACKnowledged    
} __packed __aligned(1) kTxFrameElmue;

// this struct is transmitted on endpoint 81 (IN) to the host
// A DLC byte is not required: count of transferred data bytes is calculated as: header.size - sizeof(kRxFrameElmue)
// if timestamps are not used subtract 4 additional bytes
// see buf_store_rx_packet()
typedef struct 
{
    kHeader  header;      // MSG_RxFrame
    uint8_t  flags;       // eFrameFlags    
    uint32_t can_id;      // CAN ID + eCanIdFlags
    uint32_t timestamp;   // timestamp with 1 µs precision, only sent to host if GS_DevFlagTimestamp has been set, roll over detection required!
} __packed __aligned(1) kRxFrameElmue;

// see buf_store_tx_echo()
typedef struct 
{
    kHeader  header;      // MSG_TxEcho
    uint8_t  marker;      // the same marker that was sent in kTxFrameElmue sent back to the host when the packet was ACKnowledged on CAN bus.
    uint32_t timestamp;   // timestamp with 1 µs precision, only sent to host if GS_DevFlagTimestamp has been set, roll over detection required!
} __packed __aligned(1) kTxEchoElmue;

// see buf_store_error()
typedef struct 
{
    kHeader  header;      // MSG_Error
    uint32_t err_id;      // eErrFlagsCanID
    uint8_t  err_data[8]; // several error flags and error counters
    uint32_t timestamp;   // timestamp with 1 µs precision, only sent to host if GS_DevFlagTimestamp has been set, roll over detection required!
} __packed __aligned(1) kErrorElmue;

// see control_send_debug_mesg()
typedef struct 
{
    kHeader  header;       // MSG_String
    char     ascii_msg[0]; // string data
} __packed __aligned(1) kStringElmue;

// see control_report_busload()
typedef struct 
{
    kHeader  header;      // MSG_Busload
    uint8_t  bus_load;    // current bus load in percent
} __packed __aligned(1) kBusloadElmue;

#pragma pack(pop)


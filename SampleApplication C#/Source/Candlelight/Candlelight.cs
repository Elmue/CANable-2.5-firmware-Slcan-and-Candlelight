
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
using System.Text;
using System.ComponentModel;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

using eApiError           = CANable.Utils.eApiError;
using cInterface          = CANable.WinUSB.cInterface;
using cPipeIn             = CANable.WinUSB.cPipeIn;
using cPipeOut            = CANable.WinUSB.cPipeOut;
using ePipeType           = CANable.WinUSB.ePipeType;
using ePipePolicy         = CANable.WinUSB.ePipePolicy;
using eDirection          = CANable.WinUSB.eDirection;
using eSetupType          = CANable.WinUSB.eSetupType;
using eSetupRecip         = CANable.WinUSB.eSetupRecip;
using eDfuRequest         = CANable.WinUSB.eDfuRequest;
using kDeviceDescriptor   = CANable.WinUSB.kDeviceDescriptor;

namespace CANable
{
    public class Candlelight : IDisposable
    {
        #region enums legacy protocol

        enum eUsbRequest : byte
        {
            SetHostFormat = 0, // not implemented (send UInt32 = 0x0000beef) 
            SetBitTiming,      // kBitTiming  
            SetDeviceMode,     // kDeviceMode 
            BerrReport,        // not implemented
            GetCapability,     // kCapability    
            GetDeviceVersion,  // kDeviceVersion       
            GetTimestamp,      // UInt32         
	        Identify,          // UInt32 = 0,1 make the blue LED flash 
	        GetUserID,         // not implemented           
	        SetUserID,         // not implemented           
	        SetBitTimingFD,    // kBitTiming                
	        GetCapabilityFD,   // kCapabilityFD             
	        SetTermination,    // UInt32 (eTermination)     
	        GetTermination,    // UInt32 (eTermination)     
	        GetState,          // not implemented (eCanState ?)
            // ------- ElmüSoft -------
            GetBoardInfo = 20,
            SetFilter,
            GetLastError,
            SetBusLoadReport,  // set interval of report
            SetPinStatus,
            GetPinStatus,
        }

        enum eDevMode : int
        {
            Reset = 0,
            Start,
        }
       
        [FlagsAttribute]
        public enum eDeviceFlags : int // also used to report available features
        {
            None                = 0,
            ListenOnly          = 0x00001, // Alone: Bus Monitoring,    Loopback + ListenOnly: Internal Loopback
            Loopback            = 0x00002, // Alone: External Loopback, Loopback + ListenOnly: Internal Loopback
            TripleSample        = 0x00004, // three samples per bit (mostly used for low baudrates)
            OneShot             = 0x00008, // make only one attempt to send a message, do not repeat until an ACK is received (auto retransmission = Off)
            HwTimestamp         = 0x00010, // add a timestamp to the received packet
            Identify            = 0x00020, // make the blue LED flash
            UserID              = 0x00040, // not implemented
            PadPktsToMaxPktSize = 0x00080, // send USB packets of 32 byte (endpoint MaxPacketSize) seems to be a nonsense workaround, turned off for CAN FD.
            CanFD               = 0x00100, // supports CAN-FD
            QuirkLPC546XX       = 0x00200, // workaround for LPC546XX erratum USB.15 --> host driver add a padding byte to each USB frame
            BitTimingFD         = 0x00400, // supports separate bit timing constants for CAN-FD in kCapabilityFD
            Termination         = 0x00800, // termination resistor can be switched on/off by software
            BerrReporting       = 0x01000, // not implemented
            GetState            = 0x02000, // not implemented
            // ------- ElmüSoft -------
            ProtocolElmue       = 0x04000, // ElmüSoft protocol
            DisableTxEcho       = 0x08000, // no echo for Tx commands
        }

        enum eTermination : int
        {
	        Off = 0,
	        On  = 1,
        }

        // --------------------------------------------------------

        [FlagsAttribute]
        enum eCanIdFlags : uint
        {
            Extended = 0x80000000, // 29 bit CAN ID used
            RTR      = 0x40000000, // Remote frame
            Error    = 0x20000000, // 8 Byte packet with flags in can_id and in bytes 0...4 (see can_parse_error_status)
            MASK_11  = 0x000007FF, // Mask for standard 11 bit ID
            MASK_29  = 0x1FFFFFFF, // Mask for extended 29 bit ID
        };

        [FlagsAttribute]
        public enum eFrameFlags : byte
        {
            Overflow = 0x01, // not used
            FDF      = 0x02, // CAN-FD frame
            BRS      = 0x04, // Bit rate switch (for CAN-FD frames) stupid Tymmothy sets this always when FDF is set when sending
            ESI      = 0x08, // Error state indicator (for CAN-FD frames) Error Passive means > 127 errors have occurred
        };

        // --------------------------------------------------------

        // see can_parse_error_status()
        [FlagsAttribute]
        public enum eErrFlagsCanID : int
        {
            // data[0] always zero 
            // data[5] = eAppFlags
            // data[6] = Tx Error counter
            // data[7] = Rx Error counter
            Tx_Timeout           = 0x0001,   // TX timeout (treated separately)
            Arbitration_lost     = 0x0002,   // lost arbitration
            // -------- useless ---------
            Controller_problem   = 0x0004,   // bus status has changed in data[1]   (useless flag, information already in byte 1)
            Protocol_violation   = 0x0008,   // protocol violations    in data[2+3] (useless flag, information already in byte 2,3)
            Transceiver_error    = 0x0010,   // transceiver status     in data[4]   (useless flag, information already in byte 4)
            // ---------------------------
            No_ACK_received      = 0x0020,   // received no ACK on transmission
            Bus_Off              = 0x0040,   // bus off  (treated separately)
            Bus_Error            = 0x0080,   // bus error
            Controller_restarted = 0x0100,   // controller restarted 
            CRC_Error            = 0x0200,   // CRC error (added by ElmüSoft)
            MASK_Display         = 0xFFFF & ~(Tx_Timeout | Bus_Off | Controller_problem | Protocol_violation | Transceiver_error),
            // ---------------------------
         // ErrorFlag            = 0x20000000 (legacy only)
        }

        // see can_parse_error_status() in firmware
        // Bus Status
        [FlagsAttribute]
        enum eErrFlagsByte1 : byte
        {
            Rx_Buffer_Overflow   = 0x01, // RX buffer overflow
            Tx_Buffer_Overflow   = 0x02, // TX buffer overflow
            Rx_Warning_Level     = 0x04, // reached warning level for RX errors
            Tx_Warning_Level     = 0x08, // reached warning level for TX errors
            Rx_Bus_Passive       = 0x10, // reached error passive status RX
            Tx_Bus_Passive       = 0x20, // reached error passive status TX
            Bus_is_back_active   = 0x40, // recovered to error active state (this is not an error!)
        }

        // see can_parse_error_status() in firmware
        // Protocol violation
        [FlagsAttribute]
        enum eErrFlagsByte2 : byte
        {
            Single_bit_error          = 0x01, // single bit error 
            Frame_format_error        = 0x02, // frame format error 
            Bit_stuffing_error        = 0x04, // bit stuffing error 
            Dominant_bit_error        = 0x08, // unable to send dominant bit 
            Recessive_bit_error       = 0x10, // unable to send recessive bit
            Bus_overload              = 0x20, // bus overload 
            Active_error_announcement = 0x40, // active error announcement 
            Transmission_error        = 0x80, // error occurred on transmission 
        }

        // see can_parse_error_status() in firmware
        // Protocol violation, error location
        // replace "__" with "-"
        enum eErrFlagsByte3 : byte
        {
            at_ID_bits_28__21    = 0x02, // ID bits 28 - 21 (SFF: 10 - 3)
            at_SOF               = 0x03, // start of frame 
            at_RTR_substitute    = 0x04, // substitute RTR (SFF: RTR) 
            at_IDE_bit           = 0x05, // identifier extension 
            at_ID_bits_20__18    = 0x06, // ID bits 20 - 18 (SFF: 2 - 0 )
            at_ID_bits_17__13    = 0x07, // ID bits 17-13 
            at_CRC_Sequence      = 0x08, // CRC sequence 
            at_Reserved_bit_0    = 0x09, // reserved bit 0 
            in_data_section      = 0x0A, // data section 
            at_DLC_bit           = 0x0B, // data length code 
            at_RTR_bit           = 0x0C, // RTR 
            at_Reserved_bit_1    = 0x0D, // reserved bit 1 
            at_ID_bits_4__0      = 0x0E, // ID bits 4-0 
            at_ID_bits_12__5     = 0x0F, // ID bits 12-5 
            Intermission         = 0x12, // intermission 
            at_CRC_delimiter     = 0x18, // CRC delimiter 
            at_ACK_slot          = 0x19, // ACK slot 
            at_EOF               = 0x1A, // end of frame 
            at_ACK_delimiter     = 0x1B, // ACK delimiter 
        }

        // see can_parse_error_status() in firmware
        // Transceiver Error
        // replace "__" with "-"
        enum eErrFlagsByte4_Hi : byte
        {
            CAN__H_No_wire         = 0x04,
            CAN__H_Shortcut_to_Bat = 0x05,
            CAN__H_Shortcut_to_VCC = 0x06,
            CAN__H_Shortcut_to_GND = 0x07,
            MASK                   = 0x0F,
        }

        // see can_parse_error_status() in firmware
        // Transceiver Error
        // replace "__" with "-"
        enum eErrFlagsByte4_Lo : byte
        {
            CAN__L_No_wire         = 0x40,
            CAN__L_Shortcut_to_Bat = 0x50,
            CAN__L_Shortcut_to_VCC = 0x60,
            CAN__L_Shortcut_to_GND = 0x70,
            CAN__L_Shortcut_CAN__H = 0x80,
            MASK                   = 0xF0,
        }

        #endregion

        #region enums ElmüSoft protocol

        public enum eFeedback
        {
            None           =  0,
            Success        =  2,
            // ---------------------
            Invalid_comand = '1',   // Slcan = "#1\r"
            Invalid_parameter,      // Slcan = "#2\r"
            Adapter_must_be_open,   // this command must be executed after  opening the adapter
            Adapter_must_be_closed, // this command must be executed before opening the adapter
            Error_from_HAL,
            Unsupported_feature,    // the processor is not implemented for DFU mode / no termination resistor
            Tx_buffer_overflow,
            Bus_is_off,             // the adapter is closed or in Silent Mode or Bus is Off
            No_Tx_in_silent_mode,
            Baudrate_not_set,
            Option_bytes_programming_failed,
            Please_reconnect_the_USB_cable,
        }

        enum eFilterOperation : byte
        {
            ClearAll = 0,    // remove all filters
            AcceptMask11bit, // add a new acceptance mask filter for 11 bit CAN IDs
            AcceptMask29bit, // add a new acceptance mask filter for 29 bit CAN IDs
        } 

        enum ePinOperation : ushort
        {
            Reset = 0,    // Set pin to Low  (not implemented, option for the future)
            Set,          // Set pin to High (not implemented, option for the future)
            Tristate,     // Set pin into tri-state mode.
            PullDown,     // Set pin into pull down mode.
            PullUp,       // Set pin into pull up mode.
            Disable,      // Disable pin (used for pin BOOT0 in the option bytes)
            Enable,       // Enable  pin
        } 

        // This enum is limited to 16 bit because it must be transmitted in SETUP.wValue with ELM_ReqGetPinStatus (65535 possible pins).
        // In the future pins can be added here that the user can control. Some boards have jumpers where processor pins are connected.
        // Currently only disabling pin BOOT0 is implemented.
        enum ePinID : ushort
        {
            BOOT0 = 1,    // the pin BOOT0 can be enabled / disabled in the Option Bytes
        }

        [FlagsAttribute]
        enum ePinStatus : ushort
        {
            High     = 0x0001,  // the pin is currently High.    If this bit is not set it is Low.
            Enabled  = 0x0002,  // the pin is currently Enabled. If this bit is not set it is Disabled.
        }

        public enum eMessageType : byte
        {
            // received from host
            TxFrame = 10, // the message contains a CAN frame to be sent to CAN bus
            // sent to host
            TxEcho,       // the message contains the echo marker of a Tx CAN frame (can be disabled with ELM_DevFlagDisableTxEcho)    
            RxFrame,      // the message contains a received CAN frame from CAN bus
            Error,        // the message contains multiple error flags (same format as legacy protocol, see buf_store_error())
            String,       // the message contains an ASCII string to be displayed to the user
            Busload,      // the message contains one byte which is the bus load in percent
        } 

        // If any of these flags is set, both LED's (green + blue) are permanently ON
        // These flags are reset after sending them once to the host
        [FlagsAttribute]
        enum eErrorAppFlags : byte
        {
            None             = 0,
            Rx_Failed        = 0x01,
            Tx_Failed        = 0x02,
            CAN_Tx_overflow  = 0x04,
            USB_IN_overflow  = 0x08,
            Tx_Timeout       = 0x10,
            // max 8 bits
        }

        // If bus is off, both LED's (green + blue) are permanently ON
        // This status is only controlled by hardware
        public enum eBusStatus : byte
        {
            Active   = 0x00,
            Warning  = 0x10, // (>=  96 errors)
            Passive  = 0x20, // (>= 128 errors)
            Off      = 0x30, // (>= 248 errors)
            // max value = 15
        }

        public enum eErrorLevel
        {
            Low,    // print error in grey
            Medium, // print error in yellow / orange
            High,   // print error in red
        } 

        // ===============================================================================================

        #endregion

        #region structs legacy protcol

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct kTimingMinMax
        {
	        public int    ms32_Seg1_Min;
	        public int    ms32_Seg1_Max;
	        public int    ms32_Seg2_Min;
	        public int    ms32_Seg2_Max;
	        public int    ms32_Sjw_Max;   // synchronization jump width
	        public int    ms32_Brp_Min;
	        public int    ms32_Brp_Max;
	        public UInt32 ms32_Brp_Inc;   // ????
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct kCapabilityClassic
        {
            public eDeviceFlags  me_Feature;
            public int           ms32_CanClock;
            public kTimingMinMax mk_TimeMinMax;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct kCapabilityFD  
        {
	        public eDeviceFlags  me_Feature;
	        public int           ms32_CanClock;
	        public kTimingMinMax mk_NominalMinMax;
	        public kTimingMinMax mk_DataMinMax;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct kDeviceMode
        {
            public eDevMode     me_Mode;  // int
            public eDeviceFlags me_Flags; // int

            public kDeviceMode(eDevMode e_Mode, eDeviceFlags e_Flags)
            {
                me_Mode  = e_Mode;
                me_Flags = e_Flags;
            }
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct kDeviceVersion
        {
            public Byte   mu8_Reserved1;
            public Byte   mu8_Reserved2;
            public Byte   mu8_Reserved3;
            public Byte   mu8_Icount;     // number of CAN channels - 1  (WTF is this nonsense for?)
            public UInt32 mu32_SoftVersionBcd;
            public UInt32 mu32_HardVersionBcd;

            public String SoftVersion
            {
                get { return Utils.FormatBcdVersion(mu32_SoftVersionBcd); }
            }
            public String HardVersion
            {
                get { return Utils.FormatBcdVersion(mu32_HardVersionBcd); }
            }
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct kBitTiming 
        {
            public int ms32_Prop;   // Propagation Segment (always 0)
            public int ms32_Seg1;   // Phase1 segment             
            public int ms32_Seg2;   // Phase2 segment             
            public int ms32_Sjw;    // Synchronization jump width 
            public int ms32_Brp;    // Baud Rate Prescaler        

            public override string ToString()
            {
                return String.Format("BRP: {0}, Seg1: {1}, Seg2: {2}, SJW: {3}", ms32_Brp, ms32_Seg1, ms32_Seg2, ms32_Sjw);
            }
        }

        #endregion

        #region structs ElmüSoft protocol

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public struct kBoardInfo
        {
            public UInt16  mu16_McuDeviceID;    // 0x468
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 25)]
            public Byte[]  mu8_McuName;         // "STM32G431xx"
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 25)]
            public Byte[]  mu8_BoardName;       // "MksMakerbase", "OpenlightLabs"

            public String McuName
            {
                get { return Encoding.UTF8.GetString(mu8_McuName).TrimEnd('\0'); }
            }
            public String BoardName
            {
                get { return Encoding.UTF8.GetString(mu8_BoardName).TrimEnd('\0'); }
            }
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct kFilter
        {
            public eFilterOperation me_Operation;
            public int   ms32_Filter;
            public int   ms32_Mask;
            public int   ms32_Reserved1;
            public int   ms32_Reserved2;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct kPinStatus
        {
            public ePinOperation me_Operation; 
            public ePinID        me_PinID;     
            public int           ms32_Reserved1;
            public int           ms32_Reserved2;
        }

        // =======================================================================

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class cHeader
        {
            public Byte         mu8_Size;     // the total length of this message (this struct + the appended data bytes)
            public eMessageType me_MesgType;  // eMessageType

            /// <summary>
            /// Get the size of the fix fields in the struct before the variable fields begin
            /// If the firmware sends less bytes than this minimum size an exception is thrown
            /// </summary>
            public virtual int GetMinSize(bool b_McuTimestamp)
            {
                return 2;
            }
        }

        // this struct is received on endpoint 02 (OUT) from the host
        // A DLC byte is not required. The count of transferred data bytes is calculated as: size - sizeof(kTxFrameElmue)
        // For remote frames the host can write the DLC value into the first data byte, otherwise DLC = 0 is sent.
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        private class cTxFrameElmue : cHeader
        {
            public eFrameFlags  me_Flags;    // eFrameFlags    
            public UInt32       mu32_CanID;  // CAN ID + eCanIdFlags or error flags
            public Byte         mu8_Marker;  // one-byte marker that is sent back to the host with MSG_TxEcho when the packet has been ACKnowledged    
            // ----- variable start ------
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
            public Byte[]       mu8_Data;    // max. 64 data bytes

            /// <summary>
            /// Get the size of the fix fields in the struct before the variable fields begin
            /// </summary>
            public override int GetMinSize(bool b_McuTimestamp)
            {
                return (int)Marshal.OffsetOf(GetType(), "mu8_Data");
            }

            public cTxFrameElmue(CanPacket i_Packet, Byte u8_Marker)
            {
                mu8_Size    = (Byte)(GetMinSize(false) + i_Packet.mi_Data.Count);
                me_MesgType = eMessageType.TxFrame;
                mu32_CanID  = (UInt32)i_Packet.ms32_ID;
                mu8_Marker  = u8_Marker;
                if (i_Packet.mb_29bit) mu32_CanID |= (UInt32)eCanIdFlags.Extended;
                if (i_Packet.mb_RTR)   mu32_CanID |= (UInt32)eCanIdFlags.RTR;
                if (i_Packet.mb_FDF)   me_Flags   |= eFrameFlags.FDF;
                if (i_Packet.mb_BRS)   me_Flags   |= eFrameFlags.BRS;

                mu8_Data = new Byte[64]; // required for Utils.StructureToBytesVar()
                Array.Copy(i_Packet.mi_Data.ToArray(), mu8_Data, i_Packet.mi_Data.Count);
            }
        }

        // this struct is transmitted on endpoint 81 (IN) to the host
        // A DLC byte is not required. The count of transferred data bytes is calculated as: size - sizeof(kRxFrameElmue)
        // For remote frames the DLC from the Rx packet is transmitted in the first data byte to the host.
        // if timestamps are not used subtract 4 additional bytes
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class cRxFrameElmue : cHeader
        {
            public eFrameFlags  me_Flags;       // eFrameFlags    
            public UInt32       mu32_CanID;     // CAN ID + eCanIdFlags or error flags
            // ----- variable start ------
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4 + 64)]
            public Byte[]       mu8_TimeStampAndData; // optional 32 bit timestamp + max. 64 data bytes

            /// <summary>
            /// Get the size of the fix fields in the struct before the variable fields begin
            /// If the firmware sends less bytes than this minimum size an exception is thrown
            /// </summary>
            public override int GetMinSize(bool b_McuTimestamp)
            {
                // It is allowed that a remote frame has zero data bytes
                int s32_MinSize = (int)Marshal.OffsetOf(GetType(), "mu8_TimeStampAndData");
                if (b_McuTimestamp) s32_MinSize += 4;
                return s32_MinSize;
            }

            /// <summary>
            /// Call this only if transfer of timestamps is enabled (mb_McuTimestamp = true), otherwise garbage will be returned.
            /// </summary>
            public UInt32 Timestamp
            {
                get { return BitConverter.ToUInt32(mu8_TimeStampAndData, 0); }
            }
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class cTxEchoElmue : cHeader
        {
            public Byte    mu8_Marker;     // the same marker that was sent in kTxFrameElmue sent back to the host when the packet was ACKnowledged on CAN bus.
            // ----- variable start ------
            public UInt32  mu32_Timestamp; // only sent to host if GS_DevFlagTimestamp has been set

            /// <summary>
            /// Get the size of the fix fields in the struct before the variable fields begin
            /// If the firmware sends less bytes than this minimum size an exception is thrown
            /// </summary>
            public override int GetMinSize(bool b_McuTimestamp)
            {
                int s32_MinSize = (int)Marshal.OffsetOf(GetType(), "mu32_Timestamp");
                if (b_McuTimestamp) s32_MinSize += 4;
                return s32_MinSize;
            }
        };

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class cErrorElmue : cHeader
        {
            public eErrFlagsCanID  me_ErrID; 
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
            public Byte[]          mu8_ErrData;    // several error flags and error counters
            // ----- variable start ------
            public UInt32          mu32_Timestamp; // only sent to host if GS_DevFlagTimestamp has been set

            /// <summary>
            /// Get the size of the fix fields in the struct before the variable fields begin
            /// If the firmware sends less bytes than this minimum size an exception is thrown
            /// </summary>
            public override int GetMinSize(bool b_McuTimestamp)
            {
                int s32_MinSize = (int)Marshal.OffsetOf(GetType(), "mu32_Timestamp");
                if (b_McuTimestamp) s32_MinSize += 4;
                return s32_MinSize;
            }
        };

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class cStringElmue : cHeader
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 200)]
            // ----- variable start ------
            public Byte[]  mu8_AsciiMsg; // string data (always shorter than 200 bytes)

            /// <summary>
            /// The size of the fix fields in the struct before the variable fields begin
            /// If the firmware sends less bytes than this minimum size an exception is thrown
            /// </summary>
            public override int GetMinSize(bool b_McuTimestamp)
            {
                return (int)Marshal.OffsetOf(GetType(), "mu8_AsciiMsg"); 
            }

            /// <summary>
            /// The text message to printed to the Trace output
            /// </summary>
            public String Message
            {
                get { return Encoding.ASCII.GetString(mu8_AsciiMsg, 0, mu8_Size - GetMinSize(false)); }
            }
        };

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class cBusloadElmue : cHeader
        {
            public Byte mu8_BusLoad;  // current bus load in percent
            // ----- variable start ------
            // No variable fields here.

            /// <summary>
            /// Get the size of the fix fields in the struct before the variable fields begin
            /// If the firmware sends less bytes than this minimum size an exception is thrown
            /// </summary>
            public override int GetMinSize(bool b_McuTimestamp)
            {
                return Marshal.SizeOf(GetType());
            }
        };

        #endregion

        #region Firmware Update

        // This is sent in byte 0 of a DFU_RequGetStatus request
        enum eDfuStatus : byte
        {
            OK = 0,      // No error condition is present.
            ErrTarget,   // File is not targeted for use by this device. 
            ErrFile,     // File is for this device but fails some vendor-specific verification test. 
            ErrWrite,    // Device is unable to write memory. 
            ErrErase,    // Memory erase function failed.
            // and more... (not used here)
        }

        // This is sent in byte 4 of a DFU_RequGetStatus request
        enum eDfuState : byte
        {
            AppIdle = 0,   // Device is running its normal application mode.
            AppDetach,     // Device is running its normal application, has received the DFU_DETACH request, and is waiting for a USB reset. 
            DfuIdle,       // Device is operating in the DFU mode and is waiting for requests.
            DownloadSync,  // Device has received a block and is waiting for the host to solicit the status via DFU_GETSTATUS. 
            DownloadBusy,  // Device is programming a control-write block into its nonvolatile memories. 
            DownloadIdle,  // Device is processing a download operation, expecting DFU_DNLOAD requests. 
            // and more... (not used here)
        } 

        // response to DFU_RequGetStatus request (size = 6 byte)
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct kDfuStatus
        {
            public eDfuStatus  me_Status;  
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
            public Byte[]      mu8_PollTimeout;
            public eDfuState   me_State;    
            public Byte        mu8_StringIdx;
        } 

        #endregion

        #region struct kDevInfo

        public struct kDevInfo
        {
            public String               ms_Vendor;
            public String               ms_Product;
            public String               ms_SerialNo;
            public String               ms_Interface;
            public Byte                 mu8_EndpointIN;
            public Byte                 mu8_EndpointOUT;
            public UInt16               mu16_MaxPackSizeIN;
            public UInt16               mu16_MaxPackSizeOUT;
            public bool                 mb_IsElmueSoft;
            public bool                 mb_SupportsFD;
            public kDeviceDescriptor    mk_DeviceDescr;
            public kCapabilityClassic   mk_Capability;
            public kCapabilityFD        mk_CapabilityFD;
            public kDeviceVersion       mk_DeviceVersion;
            public kBoardInfo           mk_BoardInfo;
        };

        #endregion

        #region class CanPacket

        // Remote fames store the DLC value in the first data byte
        public class CanPacket
        {
            public int        ms32_ID = -1;
            public List<Byte> mi_Data = new List<Byte>();
            public bool       mb_29bit;  // Extended ID.
            public bool       mb_RTR;    // Remote Frame                     Only used if mb_FDF = false
            public bool       mb_FDF;    // CAN FD Frame
            public bool       mb_BRS;    // CAN FD Bit Rate Switching        Only used if mb_FDF = true
            public bool       mb_ESI;    // CAN FD Error State Passive flag  Only used if mb_FDF = true

            public override string ToString()
            {
                String s_Frame;
                if (mb_29bit) s_Frame = String.Format("{0:X8}: ", ms32_ID & (int)eCanIdFlags.MASK_29);
                else          s_Frame = String.Format("{0:X3}: ", ms32_ID & (int)eCanIdFlags.MASK_11);

                // For remote frames the DLC (0...8) may be transmitted in the first data byte.
                // The display of "7E8: RTR [5]" means that a remote request with DLC = 5 has been sent/received
                if (mb_RTR)
                {
                    // Remote Transmission Request
                    if (mi_Data.Count > 0) s_Frame += String.Format("RTR [{0}]", mi_Data[0]);
                    else                   s_Frame += "RTR [0]";
                }
                else
                {
                    s_Frame += Utils.BytesToHex(mi_Data.ToArray());

                    if (mb_FDF || mb_BRS || mb_ESI) s_Frame += " -";

                    if (mb_FDF) s_Frame += " FDF"; // Flexible Datarate Frame
                    if (mb_BRS) s_Frame += " BRS"; // Bitrate Switch
                    if (mb_ESI) s_Frame += " ESI"; // Error Indicator
                }
                return s_Frame;
            }
        }

        #endregion

        // Adapt this to the latest available CANable 2.5 firmware version.
        // It shows an error to upload the latest firmware to the adapter.
        // The version number is BCD encoded (0x251118 = 18.nov.2025)
        public const int MIN_FIRMWARE = 0x251118;

        WinUSB         mi_WinUSB;
        kDevInfo       mk_Info;
        StringBuilder  ms_Details;
        cPipeIn        mi_PipeIn;
        cPipeOut       mi_PipeOut;
        bool           mb_InitDone;
        bool           mb_Started;
        bool           mb_BaudFDSet;
        Stopwatch      mi_TxOverflow;  // firmware Tx buffer is full (64 + 3 packets sent)
        bool           mb_McuTimestamp;
        Byte           mu8_EchoMarker; // counter 0...255
        CanPacket[]    mi_TxEcho;      // the last 256 Tx packets
        Int64          ms64_LastMcuStamp;
        Int64          ms64_McuRollOver;
        Int64          ms64_ClockOffset;
        Int64          ms64_StampOffset;

        public kDevInfo DeviceInfo
        {
            get { return mk_Info; }
        }
        public String DeviceDetails
        {
            get { return ms_Details.ToString(); }
        }
        public override string ToString()
        {
            if (mi_WinUSB != null) 
                return mi_WinUSB.ToString();
            else
                return "Not open";
        }

        // --------------------------------

        ~Candlelight()
        {
            Dispose();
        }

        public void Dispose()
        {
            if (mi_WinUSB != null)
            {
                try { Reset(); } // close device
                catch {}

                mi_WinUSB.Dispose();
                mi_WinUSB = null;
            }
            mb_InitDone = false;
            mi_TxEcho   = null;
        }

        // -------------------------------------------------------------------------------------

        // Step 1)
        // Call SetupApi.EnumerateUsbDevices() to get the NT Path

        // -------------------------------------------------------------------------------------

        /// <summary>
        /// Step 2)
        /// Initialize WinUSB and get the Candlelight structures with board info, capabilities, etc from the firmware
        /// </summary>
        public void Open(String s_NtPath)
        {
            if (mi_WinUSB != null)
                throw new Exception("The Candlelight adapter is already open");

            mk_Info           = new kDevInfo();
            ms_Details        = new StringBuilder();
            mi_TxOverflow     = new Stopwatch();
            mb_InitDone       = false;
            mb_BaudFDSet      = false;
            mb_Started        = false;
            ms64_LastMcuStamp = -1;
            ms64_McuRollOver  =  0;
            ms64_ClockOffset  = -1;

            // ------------- WinUSB -----------------

            mi_WinUSB = new WinUSB();
            mi_WinUSB.Open(s_NtPath, 500);

            mk_Info.ms_Vendor      = mi_WinUSB.Vendor;
            mk_Info.ms_Product     = mi_WinUSB.Product;
            mk_Info.ms_SerialNo    = mi_WinUSB.SerialNo;
            mk_Info.ms_Interface   = mi_WinUSB.Interface.String;
            mk_Info.mk_DeviceDescr = mi_WinUSB.DeviceDescriptor;

            ms_Details.AppendFormat("USB Vendor:           \"{0}\"\n", mk_Info.ms_Vendor);
            ms_Details.AppendFormat("USB Product:          \"{0}\"\n", mk_Info.ms_Product);
            ms_Details.AppendFormat("USB Serial  Nº:       \"{0}\"\n", mk_Info.ms_SerialNo);
            ms_Details.AppendFormat("USB Interface:        \"{0}\"\n", mk_Info.ms_Interface);
            ms_Details.AppendFormat("USB Vendor  ID:       {0:X4}\n",  mk_Info.mk_DeviceDescr.idVendor);
            ms_Details.AppendFormat("USB Product ID:       {0:X4}\n",  mk_Info.mk_DeviceDescr.idProduct);
            ms_Details.AppendFormat("USB Device Version:   {0}\n",     Utils.FormatBcdVersion(mk_Info.mk_DeviceDescr.bcdDevice));

            if (mi_WinUSB.Interface.Number == 1)
            {
                // Interface 1 (DFU) has no IN / OUT endpoints. It supports only SETUP requests.
                if (mi_WinUSB.Interface.EndpointCount != 0)
                    throw new Exception("The USB device is not a valid Candlelight adapter");

                mb_InitDone = true;
                return;
            }

            // ------------- Pipes ------------------

            // Interface 0 (Candlelight) must have exactly 2 endpoints: IN (81) and OUT (02)
            if (mi_WinUSB.Interface.EndpointCount != 2)
                throw new Exception("The USB device is not a valid Candlelight adapter");

            // Get pipes early for debug messages
            mi_PipeIn  = mi_WinUSB.Interface.GetPipeIn (ePipeType.Bulk);
            mi_PipeOut = mi_WinUSB.Interface.GetPipeOut(ePipeType.Bulk);
            if (mi_PipeIn == null || mi_PipeOut == null)
                throw new Exception("The USB device is not a valid Candlelight adapter");

            mk_Info.mu8_EndpointIN      = mi_PipeIn .Endpoint;
            mk_Info.mu8_EndpointOUT     = mi_PipeOut.Endpoint;
            mk_Info.mu16_MaxPackSizeIN  = mi_PipeIn .MaxPacketSize;
            mk_Info.mu16_MaxPackSizeOUT = mi_PipeOut.MaxPacketSize;

            ms_Details.AppendFormat("USB Endpoint CTRL:    00,  max packet size: {0} byte\n",     mk_Info.mk_DeviceDescr.bMaxPacketSize0);
            ms_Details.AppendFormat("USB Endpoint IN:      {0:X2},  max packet size: {1} byte\n", mk_Info.mu8_EndpointIN,  mk_Info.mu16_MaxPackSizeIN);
            ms_Details.AppendFormat("USB Endpoint OUT:     {0:X2},  max packet size: {1} byte\n", mk_Info.mu8_EndpointOUT, mk_Info.mu16_MaxPackSizeOUT);

            mi_PipeIn.SetPolicy(ePipePolicy.RawIO, true);

            // Set timeout for OUT pipe (500 ms is far more than enough)
            // This timeout assures that pipe operations are not blocking eternally as an OVERLAPPED structure is not used.
            mi_PipeOut.SetTransferTimeout(500);

            // ------------- Close Device ---------------

            // Reset() should always be the first command.
            // The device may still be open --> close it, which resets all variables in the firmware.
            // And the CANable 2.5 firmware allows to set ELM_DevFlagProtocolElmue which enables debug messages at the very beginnning.
            Reset();

            // ------------------ Legacy ------------------

            mk_Info.mk_Capability = CtrlTransfer<kCapabilityClassic>((Byte)eUsbRequest.GetCapability, eDirection.In, 0); 

            mk_Info.mb_IsElmueSoft =  (mk_Info.mk_Capability.me_Feature & eDeviceFlags.ProtocolElmue) > 0;
            mk_Info.mb_SupportsFD  = ((mk_Info.mk_Capability.me_Feature & eDeviceFlags.CanFD)         > 0 && 
                                      (mk_Info.mk_Capability.me_Feature & eDeviceFlags.BitTimingFD)   > 0);

            if (mk_Info.mb_SupportsFD)
                mk_Info.mk_CapabilityFD = CtrlTransfer<kCapabilityFD> ((Byte)eUsbRequest.GetCapabilityFD,  eDirection.In, 0); 

            mk_Info.mk_DeviceVersion    = CtrlTransfer<kDeviceVersion>((Byte)eUsbRequest.GetDeviceVersion, eDirection.In, 0); 

            ms_Details.AppendFormat("Hardware Version:     {0}\n", mk_Info.mk_DeviceVersion.HardVersion);
            ms_Details.AppendFormat("Firmware Version:     {0}\n", mk_Info.mk_DeviceVersion.SoftVersion);
            ms_Details.AppendFormat("Firmware Type:        {0}\n", mk_Info.mb_IsElmueSoft ? "CANable 2.5" : "Legacy");
            ms_Details.AppendFormat("Supports CAN FD:      {0}\n", mk_Info.mb_SupportsFD  ? "Yes"         : "No");

            if (!mk_Info.mb_IsElmueSoft)
            {
                ms_Details.AppendFormat("CAN Clock:            {0} MHz\n", mk_Info.mk_Capability.ms32_CanClock / 1000000);
                throw new Exception("This class supports only devices that have the CANable 2.5 firmware from ElmüSoft.");
            }

            // ------------------ ElmüSoft ------------------

            mk_Info.mk_BoardInfo = CtrlTransfer<kBoardInfo>((Byte)eUsbRequest.GetBoardInfo, eDirection.In, 0); 
            UInt16 u16_PinStatus = CtrlTransfer<UInt16>((Byte)eUsbRequest.GetPinStatus, eDirection.In, (UInt16)ePinID.BOOT0);
            bool   b_Enabled     = (u16_PinStatus & (UInt16)ePinStatus.Enabled) > 0;


            ms_Details.AppendFormat("Target Board:         {0}\n", mk_Info.mk_BoardInfo.BoardName);
            ms_Details.AppendFormat("Processor:            {0}, CAN Clock: {1} MHz, DeviceID: 0x{2:X}\n",
                                                                   mk_Info.mk_BoardInfo.McuName,
                                                                   mk_Info.mk_Capability.ms32_CanClock / 1000000,
                                                                   mk_Info.mk_BoardInfo.mu16_McuDeviceID);
            ms_Details.AppendFormat("Pin BOOT0:            {0}\n", b_Enabled ? "Enabled" : "Disabeld");

            if (mk_Info.mk_DeviceVersion.mu32_SoftVersionBcd < MIN_FIRMWARE)
                throw new Exception("Please upload the latest firmware.");

            // Implement new features if available in the new firmware!
            Debug.Assert(mk_Info.mk_DeviceVersion.mu32_SoftVersionBcd == MIN_FIRMWARE, "Update MIN_FIRMWARE to the latest firmware version!");

            mi_TxEcho = new CanPacket[256];  // FIRST
            mi_PipeIn.StartThread();         // AFTER
            
            mb_InitDone = true;
        }

        // -------------------------------------------------------------------------------------

        /// <summary>
        /// STEP 3)
        /// Please read "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder Documentation
        /// returns the formatted baudrate and samplepoint in s_Display
        /// </summary>
        public void SetBitrate(bool b_FD, int s32_BRP, int s32_Seg1, int s32_Seg2, out String s_Display)
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            if (b_FD && !mk_Info.mb_SupportsFD)
                throw new Exception("The board does not support CAN FD.");
    
            // NOTE:
            // It is not necessary to check if BRP, Seg1, Seg2 are in the allowed range defined in kTimeMinMax in the Capabilities.
            // If an inalid value is sent the firmware will return an error.
            // The values in kTimeMinMax are only required if you write an alorithm that calculates BRP, Seg1, Seg2
            // automatically from a given baudrate and samplepoint.

            kBitTiming k_Timing;
            k_Timing.ms32_Brp  = s32_BRP;  // bitrate prescaler
            k_Timing.ms32_Prop = 0;        // Propagation segment, not used, this is already included in Segment 1
            k_Timing.ms32_Seg1 = s32_Seg1; // Time Segment 1 (Time quantums before samplepoint)
            k_Timing.ms32_Seg2 = s32_Seg2; // Time Segment 2 (Time quantums after  samplepoint)
            k_Timing.ms32_Sjw  = Math.Min(s32_Seg1, s32_Seg2); // Synchronization Jump Width (see "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation")

            eUsbRequest e_Requ = b_FD ? eUsbRequest.SetBitTimingFD : eUsbRequest.SetBitTiming;
            CtrlTransfer((Byte)e_Requ, eDirection.Out, 0, k_Timing);

            int s32_TotTQ  = 1 + s32_Seg1 + s32_Seg2;
            int s32_Baud   = mk_Info.mk_Capability.ms32_CanClock / s32_BRP / s32_TotTQ;
            int s32_Sample = 1000 * (1 + s32_Seg1) / s32_TotTQ;

            // Do not display 83333 baud as "83k"
            String s_Unit = "";
                 if (s32_Baud >= 1000000 && (s32_Baud % 1000000) == 0) { s32_Baud /= 1000000; s_Unit = "M"; }
            else if (s32_Baud >= 1000    && (s32_Baud % 1000)    == 0) { s32_Baud /= 1000;    s_Unit = "k"; }

            String s_Type = b_FD ? "Data   " : "Nominal";
            s_Display = String.Format("{0} Baudrate: {1}{2}, Samplepoint: {3}.{4}%", s_Type, s32_Baud, s_Unit, s32_Sample / 10, s32_Sample % 10);

            if (b_FD) mb_BaudFDSet = true;
        }

        // -------------------------------------------------------------------------------------

        /// <summary>
        /// STEP 4)  (optional)
        /// Add one to eight filters
        /// ATTENTION: If you set only an 11 bit filter, no 29 bit ID's will pass and vice versa.
        /// </summary>
        public void AddMaskFilter(bool b_29bit, int s32_Filter, int s32_Mask)
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            kFilter k_Filter;
            k_Filter.ms32_Filter    = s32_Filter;
            k_Filter.ms32_Mask      = s32_Mask;
            k_Filter.me_Operation   = b_29bit ? eFilterOperation.AcceptMask29bit : eFilterOperation.AcceptMask11bit;
            k_Filter.ms32_Reserved1 = 0;
            k_Filter.ms32_Reserved2 = 0;

            CtrlTransfer((Byte)eUsbRequest.SetFilter, eDirection.Out, 0, k_Filter);
        }

        // -------------------------------------------------------------------------------------

        /// <summary>
        /// STEP 5)
        /// Connect to CAN bus, turn off the green LED
        /// </summary>
        public void Start(eDeviceFlags e_Flags)
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            e_Flags |= eDeviceFlags.ProtocolElmue;
            CtrlTransfer((Byte)eUsbRequest.SetDeviceMode, eDirection.Out, 0, new kDeviceMode(eDevMode.Start, e_Flags));

            mb_McuTimestamp = (e_Flags & eDeviceFlags.HwTimestamp) > 0;
            mb_Started = true;
        }

        /// <summary>
        /// Stop CAN bus and reset all variables and user settings in the adapter, turn on green LED
        /// </summary>
        public void Reset()
        {
            mb_Started = false;

            // IMPORTANT: Set flag ProtocolElmue always to make sure that the device can send debug messages.
            // Should there be a legacy device connected, it will ignore all flags sent with GS_ModeReset
            CtrlTransfer((Byte)eUsbRequest.SetDeviceMode, eDirection.Out, 0, new kDeviceMode(eDevMode.Reset, eDeviceFlags.ProtocolElmue));
        }

        /// <summary>
        /// Flashes the green / blue LEDs on the board
        /// </summary>
        public void Identify(bool b_Blink)
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            int s32_Mode = b_Blink ? 1 : 0; 
            CtrlTransfer((Byte)eUsbRequest.Identify, eDirection.Out, 0, s32_Mode);
        }

        /// <summary>
        /// Interval = 7 --> report busload in percent every 700 ms.
        /// NOTE: The firmware does not report the busload if bus load is permanently 0%.
        /// </summary>
        public void EnableBusLoadReport(Byte u8_Interval)
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            CtrlTransfer((Byte)eUsbRequest.SetBusLoadReport, eDirection.Out, 0, u8_Interval);
        }

        /// <summary>
        /// Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
        /// Enabling the pin needs not to be implemented here.
        /// The pin is automatically enabled when entering DFU mode with EnterDfuMode()
        /// </summary>
        public void DisableBootPin()
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            kPinStatus k_PinStatus;
            k_PinStatus.me_Operation    = ePinOperation.Disable;
            k_PinStatus.me_PinID        = ePinID.BOOT0;
            k_PinStatus.ms32_Reserved1  = 0;
            k_PinStatus.ms32_Reserved2  = 0;
            CtrlTransfer((Byte)eUsbRequest.SetPinStatus, eDirection.Out, 0, k_PinStatus);
        }

        /// <summary>
        /// Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
        /// </summary>
        public bool IsBootPinEnabled()
        {
            if (!mb_InitDone || mi_WinUSB.Interface.Number != 0)
                throw new Exception("The device must be opened for interface 0 (Candlelight)");

            // The requested pin ID must be transmitted in SETUP.wValue because a USB IN request cannot otherwise transmit parameters to the firmware.
            UInt16  u16_PinStatus = CtrlTransfer<UInt16>((Byte)eUsbRequest.GetPinStatus, eDirection.In, (UInt16)ePinID.BOOT0);
            return (u16_PinStatus & (UInt16)ePinStatus.Enabled) > 0;
        }

        // -------------------------------------------------------------------------------------

        /// <summary>
        /// Send a SETUP request to the firmware. All structures must have a fix size (no optional fields like a Timestamps)
        /// u8_Request must be eUsbRequest for interface 0 and eDfuRequest for interface 1.
        /// This function can obtain the feedback from the ElmüSoft firmware, but works also with legacy firmware.
        /// </summary>
        T CtrlTransfer<T>(Byte u8_Request, eDirection e_Dir, UInt16 u16_Value, T k_Struct = default(T)) 
        {
            eSetupType e_Type;
            String s_Request;
            if (mi_WinUSB.Interface.Number == 0) // Candlelight
            {
                e_Type = eSetupType.Vendor;
                s_Request = ((eUsbRequest)u8_Request).ToString();
            }
            else // DFU
            {
                e_Type = eSetupType.Class;
                s_Request = ((eDfuRequest)u8_Request).ToString();
            }

            // The old firmware checks that wValue is a valid channel (must be == 0)
            // wValue is only used for ReqGetPinStatus
            UInt16 wValue = u16_Value;
            // wIndex is the interface number (0 = Candlelight, 1 = DFU)
            UInt16 wIndex = mi_WinUSB.Interface.Number;

            // ATTENTION: kCapabilityFD has 72 bytes
            // However the legacy firmware complains about wLength > 64 byte
            Byte[] u8_Buffer;
            if (e_Dir == eDirection.Out) u8_Buffer = Utils.StructureToBytesFix(k_Struct);
            else                         u8_Buffer = new Byte[Marshal.SizeOf(typeof(T))]; 

            int s32_CmdError = mi_WinUSB.CtrlTansfer(eSetupRecip.Interface, e_Type, e_Dir, 
                                                     u8_Request, wValue, wIndex, ref u8_Buffer);     
            // The DFU interface has no feedback
            if (mi_WinUSB.Interface.Number == 0)
            {
                // ---------- Get Feedback -------------

                // ALWAYS get the feedback, even if the previous command execution did NOT return an error!
                // In second stage of the SETUP request the firmware can NOT stall the endpoint which is the only way to alert an USB error.

                Byte[] u8_Feedback = new Byte[10];
                int s32_FbkError = mi_WinUSB.CtrlTansfer(eSetupRecip.Interface, eSetupType.Vendor, eDirection.In, 
                                                         (Byte)eUsbRequest.GetLastError, wValue, wIndex, ref u8_Feedback);  

                // An error feedback may also come if s32_CmdError == 0 !
                if (s32_FbkError == 0)
                {
                    eFeedback e_Feedback = (eFeedback)u8_Feedback[0];
                    if (e_Feedback != eFeedback.Success)
                        throw new Exception("The device has returned error feedback: " + e_Feedback.ToString().Replace('_', ' '));
                }
            }

            if (s32_CmdError != 0)
            {
                if (s32_CmdError == (int)eApiError.GEN_FAILURE) // this means mostly STALL
                    throw new Exception("The device has refused to execute command " + s_Request);
                else
                    Utils.ThrowApiError(s32_CmdError, "Error {0} executing Candlelight command: {1}");
            }

            if (e_Dir == eDirection.In)
            {
                int s32_Size = Marshal.SizeOf(typeof(T));
                if (u8_Buffer.Length < s32_Size)
                    throw new Exception(String.Format("Error executing command {0}. The device has returned {1} instead of {2} bytes",
                                        s_Request, u8_Buffer.Length, s32_Size));

                // throws if invalid byte count
                return Utils.BytesToStructureFix<T>(u8_Buffer);
            }
            
            return default(T); // Direction OUT --> no return value
        }


        // ======================================= Send ========================================

        /// <summary>
        /// CAN FD packets (mb_FDF) can only be sent if a data baudrate has been set before.
        /// For remote frames (mb_RTR = true) the first byte may contain the value for the DLC field.
        /// u8_EchoMarker returns the echo marker that you will get in a kTxEchoElmue struct back if ELM_DevFlagDisableTxEcho is not set.
        /// </summary>
        public void SendPacket(CanPacket i_Packet, out Int64 s64_WinTimestamp, out Byte u8_EchoMarker)
        {
            const Byte PADDING = 0;

            if (!mb_InitDone || !mb_Started)
                throw new Exception("The device must be open and started.");

            int s32_MaxData = mb_BaudFDSet ? 64 : 8;
            if (i_Packet.mi_Data.Count > s32_MaxData)
                throw new Exception("The CAN data must not be longer than " + s32_MaxData + " bytes");

            if (mb_BaudFDSet && i_Packet.mb_RTR)
                throw new Exception("A remote frame cannot be sent in CAN FD mode.");

            // FDF and BRS flags require CAN FD
            if (!mb_BaudFDSet && (i_Packet.mb_FDF || i_Packet.mb_BRS))
                throw new Exception("CAN FD frames can only be sent when a data baudrate has been set.");

            // 3 + 64 messages have been sent to the firmware which were not acknowledged. 
            // The adapter is blocked --> report error once only.
            // If no errors were reported in the last 3 seconds the buffer is not full anymore
            if (mi_TxOverflow.IsRunning && mi_TxOverflow.ElapsedMilliseconds < 4000)
            {
                mi_TxOverflow.Stop();
                throw new Exception("Sending is not possible because the Tx buffer is full.");
            }

            eCanIdFlags e_MaxID = i_Packet.mb_29bit ? eCanIdFlags.MASK_29 : eCanIdFlags.MASK_11;
            if (i_Packet.ms32_ID > (int)e_MaxID)
                throw new Exception("The CAN ID is invalid.");

            if (i_Packet.mb_RTR && i_Packet.mi_Data.Count > 1)
                throw new Exception("Remote frames contain no data or one byte that defines the DLC value.");

            int s32_PadLen = i_Packet.mi_Data.Count;
                 if (s32_PadLen > 48) s32_PadLen = 64;
            else if (s32_PadLen > 32) s32_PadLen = 48;
            else if (s32_PadLen > 24) s32_PadLen = 32;
            else if (s32_PadLen > 20) s32_PadLen = 24;
            else if (s32_PadLen > 16) s32_PadLen = 20;
            else if (s32_PadLen > 12) s32_PadLen = 16;
            else if (s32_PadLen >  8) s32_PadLen = 12;

            while (i_Packet.mi_Data.Count < s32_PadLen)
            {
                i_Packet.mi_Data.Add(PADDING);
            }

            // The STM32G431 supports to store a unique 8 bit marker for each sent frame which is returned when the frame has been acknowledged.
            // The firmware sends the marker back in kTxEchoElmue and we get the sent frame from mk_EchoFrames to display it to the user.
            // 256 markers are far more than enough because the processor has a Tx FIFO for 3 CAN packtes and the firmware can store
            // additionally 64 waiting frames in the queue. When a Tx buffer overflow is reported any further SendPacket() is blocked.
            cTxFrameElmue i_TxFrame = new cTxFrameElmue(i_Packet, mu8_EchoMarker);
            mi_TxEcho[mu8_EchoMarker] = i_Packet;

            Byte[] u8_Transmit = Utils.StructureToBytesVar(i_TxFrame, i_TxFrame.mu8_Size);

            // Get timestamp immediately before sending the packet
            s64_WinTimestamp = Utils.GetWinTimestamp();

            mi_PipeOut.Send(u8_Transmit);

            u8_EchoMarker = mu8_EchoMarker;
            mu8_EchoMarker ++;
        }

        // ======================================= Receive ========================================

        /// <summary>
        /// Receive a Rx packet, a Tx echo packet, an error frame, a debug message, a busload packet, or .......
        /// returns null on timeout 
        /// </summary>
        public cHeader ReceiveData(int s32_Timeout, out Int64 s64_RxTimestamp)
        {
            if (!mb_InitDone || !mb_Started)
                throw new Exception("The device must be open and started.");

            Byte[] u8_RxData = mi_PipeIn.Receive(s32_Timeout, out s64_RxTimestamp);
            if (u8_RxData == null)
                return null; // timeout

            cHeader i_Header = Utils.BytesToStructureVar<cHeader>(u8_RxData, 0, Marshal.SizeOf(typeof(cHeader)));
            if (i_Header.mu8_Size != u8_RxData.Length)
                throw new Exception("Received crippled USB data from device");

            cHeader i_Struct = null;
            switch (i_Header.me_MesgType)
            {
                case eMessageType.TxEcho:  i_Struct = Utils.BytesToStructureVar<cTxEchoElmue> (u8_RxData, 0); break;
                case eMessageType.RxFrame: i_Struct = Utils.BytesToStructureVar<cRxFrameElmue>(u8_RxData, 0); break;
                case eMessageType.Error:   i_Struct = Utils.BytesToStructureVar<cErrorElmue>  (u8_RxData, 0); break;
                case eMessageType.String:  i_Struct = Utils.BytesToStructureVar<cStringElmue> (u8_RxData, 0); break;
                case eMessageType.Busload: i_Struct = Utils.BytesToStructureVar<cBusloadElmue>(u8_RxData, 0); break;
                default:
                    throw new Exception("Received invalid USB data from device (MessageType = " + u8_RxData[1] + ")");
            }

            if (u8_RxData.Length < i_Struct.GetMinSize(mb_McuTimestamp))
                throw new Exception("Received incomplete USB data from device");

            return i_Struct;
        }

        public CanPacket RxFrameToCanPacket(cRxFrameElmue i_Frame)
        {
            if (i_Frame == null)
                return null;

            CanPacket i_Packet = new CanPacket();
            i_Packet.ms32_ID   = (int)(i_Frame.mu32_CanID & (UInt32)eCanIdFlags.MASK_29);
            i_Packet.mb_29bit  = (i_Frame.mu32_CanID & (uint)eCanIdFlags.Extended) != 0;
            i_Packet.mb_RTR    = (i_Frame.mu32_CanID & (uint)eCanIdFlags.RTR) != 0;
            i_Packet.mb_FDF    = (i_Frame.me_Flags   & eFrameFlags.FDF) != 0;
            i_Packet.mb_BRS    = i_Packet.mb_FDF && (i_Frame.me_Flags & eFrameFlags.BRS) != 0;
            i_Packet.mb_ESI    = i_Packet.mb_FDF && (i_Frame.me_Flags & eFrameFlags.ESI) != 0;

            int s32_Offset  = mb_McuTimestamp ? 4 : 0;
            int s32_DataLen = i_Frame.mu8_Size - i_Frame.GetMinSize(mb_McuTimestamp);
            Byte[] u8_Data  = Utils.ExtractByteArr(i_Frame.mu8_TimeStampAndData, s32_Offset, s32_DataLen);

            i_Packet.mi_Data.AddRange(u8_Data);
            return i_Packet;
        }

        // ======================================= Tx Echo ========================================

        /// <summary>
        /// Returns the echo packet.
        /// If this function returns null there is either a firmware error or a programming error.
        /// This should never happen!
        /// </summary>
        public CanPacket GetTxEchoPacket(cTxEchoElmue i_Echo)
        {
            if (!mb_InitDone || !mb_Started)
                throw new Exception("The device must be open and started.");

            return mi_TxEcho[i_Echo.mu8_Marker];
        }

        // ======================================= Timestamp ========================================

        /// <summary>
        /// Formats a timestamp with 1 µs precision
        /// returns "HH:MM:SS.mmm.µµµ"
        /// i_Header may contain a timestamp if GS_DevFlagTimestamp is set --> mb_McuTimestamp = true
        /// otherwise use s64_WinTimestamp which comes from GetWinTimestamp() at packet reception
        /// </summary>
        public String FormatTimestamp(cHeader i_Header, Int64 s64_WinTimestamp)
        {
            // the variable mb_McuTimestamp is not yet valid
            if (!mb_Started)
                throw new Exception("The device must be open and started.");

            Int64 s64_Stamp = -1;
            if (mb_McuTimestamp)
            {
                if (i_Header != null)
                {
                    switch (i_Header.me_MesgType)
                    {
                        // These 3 messages send firmware timestamps
                        case eMessageType.TxEcho:  s64_Stamp = ((cTxEchoElmue) i_Header).mu32_Timestamp; break;
                        case eMessageType.RxFrame: s64_Stamp = ((cRxFrameElmue)i_Header).Timestamp;      break;
                        case eMessageType.Error:   s64_Stamp = ((cErrorElmue)  i_Header).mu32_Timestamp; break;
                    }
                }

                if (s64_Stamp >= 0)
                {
                    // The 32 bit firmware timestamp will roll over after 1 hour, this must be detected here.
                    if (s64_Stamp < ms64_LastMcuStamp)
                        ms64_McuRollOver += 0x100000000;
            
                    ms64_LastMcuStamp = s64_Stamp;

                    // roll-over compensated 64 bit timestamp
                    s64_Stamp += ms64_McuRollOver;
                }
            }
            else // Windows performance counter timestamps are used
            {
                s64_Stamp = s64_WinTimestamp;
            }

            if (s64_Stamp < 0)
                return "No Timestamp    ";

            // get the clock offset in µs for the very first timestamp to be converted
            if (ms64_ClockOffset < 0)
            {
                DateTime k_Now = DateTime.Now;
                ms64_ClockOffset = (((((Int64)k_Now.Hour * 60) + k_Now.Minute) * 60 + k_Now.Second) * 1000 + k_Now.Millisecond) * 1000;
                // ATTENTION: This must be stored in a separate variable from ms64_ClockOffset!
                // Otherwise ms64_ClockOffset may become negative and is updated each time again.
                ms64_StampOffset = s64_Stamp; 
            }

            s64_Stamp += ms64_ClockOffset - ms64_StampOffset;

            int s32_Micro = (int)(s64_Stamp % 1000);
            s64_Stamp    /= 1000;
            int s32_Milli = (int)(s64_Stamp % 1000);
            s64_Stamp    /= 1000;
            int s32_Sec   = (int)(s64_Stamp % 60);
            s64_Stamp    /= 60;
            int s32_Min   = (int)(s64_Stamp % 60);
            s64_Stamp    /= 60;
            int s32_Hour  = (int)(s64_Stamp % 24);

            return String.Format("{0:D2}:{1:D2}:{2:D2}.{3:D3}.{4:D3}", s32_Hour, s32_Min, s32_Sec, s32_Milli, s32_Micro);
        }

        // ======================================= Errors ========================================

        /// <summary>
        /// From the multiple flags that have been defined by previous programmers we check only those which the CANable 2.5 firmware sets.
        /// e_BusStatus returns the current bus status (active, warning, passive, off)
        /// e_Level return the error level (low, ledium, high)
        /// </summary>
        public String FormatCanErrors(cErrorElmue i_Error, out eBusStatus e_BusStatus, out eErrorLevel e_Level)
        {
            eErrFlagsCanID e_ID    = (eErrFlagsCanID)i_Error.me_ErrID;
            eErrFlagsByte1 e_Byte1 = (eErrFlagsByte1)i_Error.mu8_ErrData[1];
            eErrFlagsByte2 e_Byte2 = (eErrFlagsByte2)i_Error.mu8_ErrData[2];
            eErrorAppFlags e_App   = (eErrorAppFlags)i_Error.mu8_ErrData[5];

            if ((e_App & eErrorAppFlags.CAN_Tx_overflow) > 0)
                mi_TxOverflow.Restart(); // block sending further packets
            else
                mi_TxOverflow.Stop();

            e_BusStatus = eBusStatus.Active;
            e_Level     = eErrorLevel.Low;

            String s_Mesg = "";
            if ((e_ID & eErrFlagsCanID.Bus_Off) > 0)
            {
                e_BusStatus = eBusStatus.Off;
                e_Level     = eErrorLevel.High;
                s_Mesg     += "Bus Off, ";
            }
            else if ((e_Byte1 & (eErrFlagsByte1.Rx_Bus_Passive | eErrFlagsByte1.Tx_Bus_Passive)) > 0)
            {
                e_BusStatus = eBusStatus.Passive;
                e_Level     = eErrorLevel.High;
                s_Mesg     += "Bus Passive, ";
            }
            else if ((e_Byte1 & (eErrFlagsByte1.Rx_Warning_Level | eErrFlagsByte1.Tx_Warning_Level)) > 0)
            {
                e_BusStatus = eBusStatus.Warning;
                e_Level     = eErrorLevel.Medium;
                s_Mesg     += "Bus Warning, ";
            }
            else // Active
            {
                if ((e_Byte1 & eErrFlagsByte1.Bus_is_back_active) > 0) s_Mesg += "Back to Active, ";
                else                                                   s_Mesg += "Bus Active, ";
            }

            // all errors generated by the firmware are bigger problems (Level High)
            if (e_App > 0) e_Level = eErrorLevel.High;
            if ((e_App & eErrorAppFlags.Rx_Failed)       > 0) s_Mesg += "Rx Failed, ";
            if ((e_App & eErrorAppFlags.Tx_Failed)       > 0) s_Mesg += "Tx Failed, ";
            if ((e_App & eErrorAppFlags.Tx_Timeout)      > 0) s_Mesg += "Tx Timeout, ";
            if ((e_App & eErrorAppFlags.CAN_Tx_overflow) > 0) s_Mesg += "CAN Tx Overflow, ";
            if ((e_App & eErrorAppFlags.USB_IN_overflow) > 0) s_Mesg += "USB IN Overflow, ";

            // Error cause
            if ((e_ID    & eErrFlagsCanID.No_ACK_received)     > 0) s_Mesg += "No ACK received, ";
            if ((e_ID    & eErrFlagsCanID.CRC_Error)           > 0) s_Mesg += "CRC Error, ";
            if ((e_Byte2 & eErrFlagsByte2.Bit_stuffing_error)  > 0) s_Mesg += "Bit Stuffing Error, ";
            if ((e_Byte2 & eErrFlagsByte2.Frame_format_error)  > 0) s_Mesg += "Frame Format Error, "; // e.g. CAN FD frame received in classic mode
            if ((e_Byte2 & eErrFlagsByte2.Dominant_bit_error)  > 0) s_Mesg += "Dominant Bit Error, ";
            if ((e_Byte2 & eErrFlagsByte2.Recessive_bit_error) > 0) s_Mesg += "Recessive Bit Error, ";

            if (i_Error.mu8_ErrData[6] > 0) 
                s_Mesg += String.Format("Tx Errors: {0}, ", i_Error.mu8_ErrData[6]);

            if (i_Error.mu8_ErrData[7] > 0) 
                s_Mesg += String.Format("Rx Errors: {0}, ", i_Error.mu8_ErrData[7]);

            return s_Mesg.TrimEnd(',', ' ');
        }

        // ================================== DFU ========================================

        /// <summary>
        /// Switch the Candlelight into firmware update mode.
        /// This function requires that you have called EnumDevices(Interface = 1) before to get access to interface 1.
        /// IMPORTANT:
        /// This will ONLY work if the Candlelight has the new CANable 2.5 firmware from ElmüSoft.
        /// ALL legacy Candlelights have a sloppy firmware that does not respond to the Microsoft OS descriptor request for interface 1.
        /// The consequence is that Windows cannot install the WinUSB driver for the DFU interface and EnumDevices() will not find the device.
        /// ATTENTION:
        /// This works only if the device is in Candlelight mode. If the device is already in DFU mode it will fail.
        /// If the device is already in DFU mode you cannot use the WinUSB driver, you need the STtube30 driver from ST Microelectronics.
        /// If you want to update the firmware use HUD ECU Hacker which comes with a very comfortable STM32 Firmware Updater.
        ///
        /// ATTENTION:
        /// When b_ReconnectRequired is true --> show a message to the user that he must reconnect the USB cable.
        /// The processor needs a hardware reset after wroting the Option Bytes
        /// </summary>
        public void EnterDfuMode(out bool b_ReconnectRequired)
        {
            b_ReconnectRequired = false;

            if (!mb_InitDone || mi_WinUSB.Interface.Number != 1)
                throw new Exception("The device must be opened for interface 1 (DFU)");

            // The legacy firmware would have entered immediately in DFU mode and CtrlTransfer() would have returned error 31 here.
            // But the CANable 2.5 firmware responds correctly to all SETUP requests because it makes a delay of 300 ms before entering DFU mode.
            CtrlTransfer((Byte)eDfuRequest.Detach, eDirection.Out, 0, new Byte[0]);

            try
            {
                kDfuStatus k_Status = CtrlTransfer<kDfuStatus>((Byte)eDfuRequest.GetStatus, eDirection.In, 0);

                // returning AppDetach has been added by ElmüSoft to the firmware and means that the user must reconnect the USB cable.
                // This happens only if the pin BOOT0 was disabled before calling EnterDfuMode()
                // k_Status.State is either AppIdle or AppDetach.
                if (k_Status.me_State == eDfuState.AppDetach)
                {
                    // The user must reconnect the USB cable now.
                    // This happens only if the pin BOOT0 was disabled before calling this function.
                    b_ReconnectRequired = true;
                }
            }
            catch
            {
                // returned Error must be ignored here because legacy devices enter boot mode immediately 
                // and CtrlTransfer() will get error 31.
            }

            // The device will enter DFU mode in 300 ms --> the WinUSB handle is not valid anymore.
            Dispose();
        }
    }
}

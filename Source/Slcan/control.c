/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "buffer.h"
#include "can.h"
#include "utils.h"
#include "error.h"
#include "led.h"
#include "dfu.h"
#include "control.h"

// ATTENTION:
// This version defines which Slcan commands are available.
// The first version was 100.
// In version 101 support for multi-channel adapters has been added.
// (Candlelight does not need a version number because it returns the supported features as bit flags)
#define SLCAN_VERSION          104

// If this is 1 all baudrates will be printed to verify all CAN_NOM_BITTIMING_xxx and CAN_DATA_BITTIMING_xxx
#define VERIFY_ALL_BAUDRATES   0

// If any of the following constants is defined as zero --> the processor does not support the baudrate --> FBK_UnsupportedFeature.
// The samplepoint is calculated as 75% if possible.
#if defined(STM32G0xx)
    // Constants for 60 MHz CAN clock
    // ATTENTION: This processor can not run at the maximum allowed frequency of 64 MHz. 
    // A clock of 64 MHz would not allow to generate a CAN FD baurate of 5 Mbaud.
   
    // Nominal Sample 75%, max BRP = 512, max Seg1 = 256, max Seg2 = 128
    // Data    Sample 75%, max BRP =  32, max Seg1 =  32, max Seg2 =  16
    //                                           BRP           Seg1          Seg2
    #define CAN_NOM_BITTIMING_5K     ((uint64_t)  40 << 32) | (224 << 16) | ( 75)
    #define CAN_NOM_BITTIMING_10K    ((uint64_t)  20 << 32) | (224 << 16) | ( 75)
    #define CAN_NOM_BITTIMING_20K    ((uint64_t)  10 << 32) | (224 << 16) | ( 75)
    #define CAN_NOM_BITTIMING_33_3K  ((uint64_t)   6 << 32) | (224 << 16) | ( 75)
    #define CAN_NOM_BITTIMING_50K    ((uint64_t)   4 << 32) | (224 << 16) | ( 75)
    #define CAN_NOM_BITTIMING_62_5K  ((uint64_t)   3 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_75K    ((uint64_t)   4 << 32) | (149 << 16) | ( 50)
    #define CAN_NOM_BITTIMING_83_3K  ((uint64_t)   3 << 32) | (179 << 16) | ( 60)
    #define CAN_NOM_BITTIMING_100K   ((uint64_t)   2 << 32) | (224 << 16) | ( 75)
    #define CAN_NOM_BITTIMING_125K   ((uint64_t)   2 << 32) | (179 << 16) | ( 60)
    #define CAN_NOM_BITTIMING_250K   ((uint64_t)   1 << 32) | (179 << 16) | ( 60)
    #define CAN_NOM_BITTIMING_500K   ((uint64_t)   1 << 32) | ( 89 << 16) | ( 30)
    #define CAN_NOM_BITTIMING_800K   ((uint64_t)   1 << 32) | ( 55 << 16) | ( 19) // A perfect match is not possible --> 800 kBaud, 74.6%
    #define CAN_NOM_BITTIMING_1M     ((uint64_t)   1 << 32) | ( 44 << 16) | ( 15)
    // ----------------------------
    #define CAN_DATA_BITTIMING_500K  ((uint64_t)   3 << 32) | ( 29 << 16) | ( 10)
    #define CAN_DATA_BITTIMING_1M    ((uint64_t)   3 << 32) | ( 14 << 16) | (  5)
    #define CAN_DATA_BITTIMING_2M    ((uint64_t)   1 << 32) | ( 17 << 16) | ( 12) // A perfect match is not possible --> 2 MBaud, 60%
    #define CAN_DATA_BITTIMING_4M    ((uint64_t)   1 << 32) | (  8 << 16) | (  6) // A perfect match is not possible --> 4 MBaud, 60%
    #define CAN_DATA_BITTIMING_5M    ((uint64_t)   1 << 32) | (  8 << 16) | (  3)
    #define CAN_DATA_BITTIMING_8M    0
    
#elif defined(STM32G4xx)

    // Constants for 160 MHz CAN clock and 
    // Nominal Sample 75%, max BRP = 512, max Seg1 = 256, max Seg2 = 128
    // Data    Sample 75%, max BRP =  32, max Seg1 =  32, max Seg2 =  16
    //                                           BRP           Seg1          Seg2
    #define CAN_NOM_BITTIMING_5K     ((uint64_t) 100 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_10K    ((uint64_t)  50 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_20K    ((uint64_t)  25 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_33_3K  ((uint64_t)  15 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_50K    ((uint64_t)  10 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_62_5K  ((uint64_t)   8 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_75K    ((uint64_t)   9 << 32) | (176 << 16) | ( 60) // A perfect match is not possible --> 75011 baud, 74.6%
    #define CAN_NOM_BITTIMING_83_3K  ((uint64_t)   6 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_100K   ((uint64_t)   5 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_125K   ((uint64_t)   4 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_250K   ((uint64_t)   2 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_500K   ((uint64_t)   1 << 32) | (239 << 16) | ( 80)
    #define CAN_NOM_BITTIMING_800K   ((uint64_t)   1 << 32) | (149 << 16) | ( 50)
    #define CAN_NOM_BITTIMING_1M     ((uint64_t)   1 << 32) | (119 << 16) | ( 40)
    // ----------------------------
    #define CAN_DATA_BITTIMING_500K  ((uint64_t)   8 << 32) | ( 29 << 16) | ( 10)
    #define CAN_DATA_BITTIMING_1M    ((uint64_t)   4 << 32) | ( 29 << 16) | ( 10)
    #define CAN_DATA_BITTIMING_2M    ((uint64_t)   2 << 32) | ( 29 << 16) | ( 10)
    #define CAN_DATA_BITTIMING_4M    ((uint64_t)   2 << 32) | ( 14 << 16) | (  5)
    #define CAN_DATA_BITTIMING_5M    ((uint64_t)   2 << 32) | ( 11 << 16) | (  4)
    #define CAN_DATA_BITTIMING_8M    ((uint64_t)   2 << 32) | (  4 << 16) | (  5) // Samplepoint must be 50%, otherwise communication errors.
#else
    #error "MCU_SERIE not implemented"
#endif

// ----- Globals
extern eUserFlags GLB_UserFlags[CHANNEL_COUNT];

// ----- Member
uint32_t  CanMode[CHANNEL_COUNT];

// ----- Private Methods
eFeedback control_parse_str    (uint8_t channel, char buf[], int len);
eFeedback control_host_filter  (uint8_t channel, char buf[]);
eFeedback control_bridge_filter(uint8_t channel, char buf[], bool enable);
eFeedback control_parse_flash  (uint8_t channel, char buf[]);
eFeedback control_set_baudrate (uint8_t channel, bool set_data, char baud_chr);

// ==================================================================================================================

void control_init()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        // all the other flags must be enabled by the user
        GLB_UserFlags[C] = USR_SlcanDefault;  // retransmit
        CanMode      [C] = FDCAN_MODE_NORMAL; // normal mode (not silent / loopback mode)
    }
}

void control_parse_command(char* buf, int len)
{
    // IMPORTANT: Terminate the command string with zero
    buf[len] = 0;

    uint8_t channel = 0;
    switch (buf[0])
    {
#if CHANNEL_COUNT > 1
        case '&': channel = 1; buf ++; len --; break;
#endif
#if CHANNEL_COUNT > 2
        case '$': channel = 2; buf ++; len --; break;
#endif
    }

    eFeedback e_Ret = control_parse_str(channel, buf, len);
    if ((GLB_UserFlags[channel] & USR_Feedback) == 0)
        return;

    switch (e_Ret)
    {
        case FBK_RetString:
            break; // response has already been written with buf_enqueue_cdc()
        case FBK_Success:
            buf_enqueue_cdc(channel, "#\r", 2); // return "#\r" for success
            break;
        default:
        {
            char s8_Error[3] = { '#', e_Ret, '\r' }; // return "#4\r" for error 4
            buf_enqueue_cdc(channel, s8_Error, 3);
            break;
        }
    }
}

// Parse an incoming slcan command from the USB CDC port.
// The termination '\r' has already been removed.
eFeedback control_parse_str(uint8_t channel, char buf[], int len)
{
    // Flash the Rx LED very shortly if bus is closed
    // ATTENTION: If the bus is closed the Tx LED is on, so this is not the same as Rx flashing with bus open.
    if (!can_is_open(channel))
        led_flash_RX(channel); // flash 15 ms

    // Reply Success ("#") to an empty command "\r" if the user has hit the Enter key in the terminal.
    if (len == 0)
        return FBK_Success;

    char tempbuf[200];

    eFeedback e_Ret = FBK_InvalidParameter;
    switch (buf[0])
    {
        // Set Auto Retransmit (legacy)
        case 'A':
            if (len != 2)
                return FBK_InvalidParameter;

            if (can_is_open(channel))
                return FBK_AdapterMustBeClosed;

            switch (buf[1])
            {
                case '0': GLB_UserFlags[channel] &= ~USR_Retransmit; break; // "A0" (legacy command) enable one shot mode
                case '1': GLB_UserFlags[channel] |=  USR_Retransmit; break; // "A1" (legacy command) try 128 times to send a packet
                default:  return FBK_InvalidParameter;
            }
            return FBK_Success;

        // Set Mode (example: "MEFS\r" --> enable error report, feeback and ESI report)
        case 'M':
            if (len < 2)
                return FBK_InvalidParameter;

            for (int i=1; i<len; i++)
            {
                // NOTE:
                // Transmitting timestamps is not imeplemented for Slcan as one timestamp would require 16 bytes to be transmitted.
                // Timestamps should be generated in the host application instead of slowing down the USB traffic.
                switch (buf[i])
                {
                    case 'A':                                        // "MA"  Enable Auto re-transmit (same as legacy "A1")
                        if (can_is_open(channel)) return FBK_AdapterMustBeClosed;
                        GLB_UserFlags[channel] |=  USR_Retransmit;
                        break;
                    case 'a':                                        // "Ma"
                        if (can_is_open(channel)) return FBK_AdapterMustBeClosed;
                        GLB_UserFlags[channel] &= ~USR_Retransmit;
                        break;
                    case 'D': GLB_UserFlags[channel] |=  USR_DebugReport; break; // "MD"  Enable string debug messages
                    case 'd': GLB_UserFlags[channel] &= ~USR_DebugReport; break; // "Md"
                    case 'E': GLB_UserFlags[channel] |=  USR_ErrorReport; break; // "ME"  Enable CAN bus error reports
                    case 'e': GLB_UserFlags[channel] &= ~USR_ErrorReport; break; // "Me"
                    case 'F': GLB_UserFlags[channel] |=  USR_Feedback;    break; // "MF"  Enable command execution Feedback mode
                    case 'f': GLB_UserFlags[channel] &= ~USR_Feedback;    break; // "Mf"
                    case 'M': GLB_UserFlags[channel] |=  USR_TxEcho;      break; // "MT"  Enable Tx echo report with Marker
                    case 'm': GLB_UserFlags[channel] &= ~USR_TxEcho;      break; // "Mt"
                    case 'S': GLB_UserFlags[channel] |=  USR_ReportESI;   break; // "MS"  Enable ESI report
                    case 's': GLB_UserFlags[channel] &= ~USR_ReportESI;   break; // "Ms"
                    // -----------------------------------------------------
                    case 'I': led_blink_identify(channel, true);          break; // "MI"  Identify device by blinking the LEDs
                    case 'i': led_blink_identify(channel, false);         break; // "Mi"  stop blinking
                    case '0':                                                    // "M0"  Use normal mode for Open (legacy command)
                    case '1':                                                    // "M1"  Use silent (bus monitoring) mode for Open (legacy command)
                        if (can_is_open(channel))
                            return FBK_AdapterMustBeClosed;
                        CanMode[channel] = (buf[i] == '1') ? FDCAN_MODE_BUS_MONITORING : FDCAN_MODE_NORMAL;
                        break;
                    case 'R':                                        // "MR" (enable  120 Ohm Termination Resistor)
                    case 'r':                                        // "Mr" (disable 120 Ohm Termination Resistor)
                        if (!can_set_termination(channel, buf[i] == 'R'))
                            return FBK_UnsupportedFeature;
                        break;
                    default:
                        return FBK_InvalidParameter;
                }
            }
            return FBK_Success;

        // ----------------------------

        // Open adapter
        case 'O':
        {
            if (len > 2)
                return FBK_InvalidParameter;

            if (len == 2)
            {
                // ATTENTION: CanMode is also set by command "M"
                // It must be only modified here if 2 characters have been sent!
                switch (buf[1])
                {
                    case 'N': CanMode[channel] = FDCAN_MODE_NORMAL;            break; // "ON"
                    case 'S': CanMode[channel] = FDCAN_MODE_BUS_MONITORING;    break; // "OS"
                    case 'I': CanMode[channel] = FDCAN_MODE_INTERNAL_LOOPBACK; break; // "OI"
                    case 'E': CanMode[channel] = FDCAN_MODE_EXTERNAL_LOOPBACK; break; // "OE"
                    default:  return FBK_InvalidParameter;
                }
            }
            
            // Only for debugging 
            // When CAN is opened -> calculate and print all baudrates + samplepoints for CAN_NOM_BITTIMING_xxx and CAN_DATA_BITTIMING_xxx
            // "S0: 10k baud, 75%"
            // "Y4: 4M baud, 75%"
            #if VERIFY_ALL_BAUDRATES
                for (int L=0; L<2; L++)
                {
                    bool set_data = L > 0;
                    for (int baud_chr = '0'; baud_chr <= 'Z'; baud_chr ++)
                    {
                        eFeedback e_Feedback = control_set_baudrate(channel, set_data, baud_chr);
                        if (e_Feedback == FBK_InvalidParameter)
                            continue; // skip undefined characters
                        
                        tempbuf[0] = '>'; // debug message
                        tempbuf[1] = set_data ? 'Y' : 'S';
                        tempbuf[2] = baud_chr;
                        switch (e_Feedback)
                        {
                            case FBK_Success:
                                utils_format_bitrate(tempbuf + 3, "", can_getBitrate(channel, set_data));
                                break;
                            case FBK_UnsupportedFeature:
                                strcpy(tempbuf + 3, ": Not supported");
                                break;
                            case FBK_ParamOutOfRange:
                                strcpy(tempbuf + 3, ": Out of range");
                                break;
                            default:
                                sprintf(tempbuf + 3, ": Feedback Error '#%c'", e_Feedback);
                                break;
                        }                        
                        strcat(tempbuf, "\r");
                        buf_enqueue_cdc(channel, tempbuf, strlen(tempbuf));
                    }
                }
            #endif

            return can_open(channel, CanMode[channel]); // returns error if already open
        }
        // Close adapter and reset variables (no error if already closed)
        // ATTENTION: This command does not send feedback although it is enabled!
        // This is by purpose. The application can execute this before command Open to assure that all variables are reset.
        // In this state the application does not know if feedbacks are still enabled from the last usage or not.
        case 'C':
            if (len == 1)
            {
                can_close(channel); // no error if already closed

                // reset the variables to their default
                CanMode      [channel] = FDCAN_MODE_NORMAL;
                GLB_UserFlags[channel] = USR_SlcanDefault;

                // Do not call buf_enqueue_cdc() here --> never send a response.
                // This is the only command that behaves the same way as in the legacy firmware.
                return FBK_RetString;
            }
            return e_Ret;

        // ----------------------------

        // Get version number, processor, clock,...
        case 'V':
            if (len == 1)
            {
                // The host application needs these limits to calculate the bitrates (commands 's' and 'y')
                bitlimits* lim_nom     = utils_get_bit_limits(false);
                bitlimits* lim_data    = utils_get_bit_limits(true);
                uint32_t   hal_version = HAL_GetHalVersion();

                char serial_no[20];
                USBD_GetSerialNumber(serial_no);

                // HAL_GetDEVID() returns a unique identifier (DBG_IDCODE) for each processor family.
                // The STM32G0xx serie uses 0x460, 0x465, 0x476, 0x477 and STM32G4xx uses 0x468, 0x469, 0x479.
                // String responses start with '+', all other command responses start with '#'
                sprintf(tempbuf, "+Board: "      TARGET_BOARD              // Multiboard  (from MakeFile)
                                 "\tMCU: "       TARGET_MCU                // STM32G431   (from MakeFile)
                                 "\tDevID: %lu"                            // 0x468       (from processor)
                                 "\tFirmware: %u"                          // 2427156     (from settings.h)
                                 "\tSlcan: "     STR(SLCAN_VERSION)        // 101         (from settings.h)
                                 "\tClock: %lu"                            // 160         (from system variable)
                                 "\tChannels: "  STR(CHANNEL_COUNT)        // 1           (from settings.h)
#if HSE_VALUE > 0
                                 "\tQuartz: Yes"                           // Yes / No    (from make file)
#else
                                 "\tQuartz: No"                            // Yes / No    (from make file)
#endif
                                 "\tLimits: %lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu"
                                 "\tHAL: %u.%u.%u"
                                 "\tSerial: %s\r",
                                 HAL_GetDEVID(),
                                 FIRMWARE_VERSION_BCD, // defined as hex, but sent as decimal for easier parsing in the host software
                                 system_get_can_clock() / 1000000,
                                 lim_nom ->brp_max, lim_nom ->seg1_max, lim_nom ->seg2_max, lim_nom ->sjw_max,
                                 lim_data->brp_max, lim_data->seg1_max, lim_data->seg2_max, lim_data->sjw_max,
                                 (uint8_t)(hal_version >> 24), (uint8_t)(hal_version >> 16), (uint8_t)(hal_version >> 8), 
                                 serial_no);

                buf_enqueue_cdc(channel, tempbuf, strlen(tempbuf));
                return FBK_RetString;
            }
            return e_Ret;

        // ----------------------------

        // Set baudrate (always samplepoint nominal: 87.5%, data: 75%)
        // ATTENTION: Deprecated! Read the manual.
        case 'S': // nominal
        case 'Y': // data
            if (len == 2) e_Ret = control_set_baudrate(channel, buf[0] == 'Y', buf[1]);
            return e_Ret;

        // Set bitrate (any samplepoint is possible)
        case 's': // nominal
        case 'y': // data
        {
            int pos = 1;
            uint32_t BRP, Seg1, Seg2, Sjw;
            if (!utils_parse_next_decimal(buf, &pos, ',', &BRP)  ||
                !utils_parse_next_decimal(buf, &pos, ',', &Seg1) ||
                !utils_parse_next_decimal(buf, &pos, ',', &Seg2) ||
                !utils_parse_next_decimal(buf, &pos,  0,  &Sjw))
                    return FBK_InvalidParameter;

            return can_set_bit_timing(channel, buf[0] == 'y', BRP, Seg1, Seg2, Sjw);
        }

        // ----------------------------

        // Set CAN filter:
        case 'F':
            if (buf[1] == ':')
                return control_bridge_filter(channel, buf, true); 
            else
                return control_host_filter  (channel, buf); // "F7E0,7FF"

        // Clear CAN filter:
        case 'f':
            if (buf[1] == ':')
                return control_bridge_filter(channel, buf, false); // "f:07"
            if (len == 1) 
                return can_clear_host_filters(channel); // "f"
            return e_Ret;

        // ----------------------------

        // Enable bus load report in percent (the precision is approx +/- 10%)
        // The firmware will send the current bus load in user defined intervals.
        // Command "L7\r" --> send busload every 700 ms
        case 'L':
        {
            uint32_t interval;
            int pos = 1;
            if (!utils_parse_next_decimal(buf, &pos, 0, &interval)) // "L0", "L7", "L30"
                return FBK_InvalidParameter;

            return can_enable_busload(channel, interval); // interval in 100 ms steps
        }

        // ----------------------------

        // Special ASCII commands.
        // These commands are by purpose somewhat longer than only 2 characters to avoid that they are executed accidentally.
        case '*':
        {
            // Enable pin BOOT0 and then switch the processor into DFU (Device Firmware Upgrade) mode.
            // The response will be received by the host because the bootloader is started with a delay of 300 ms.
            if (strcmp(buf, "*DFU") == 0)
                return dfu_switch_to_bootloader(); // closes adapter

            // Set register OPTR, bit nSWBOOT0 = 0 --> Disable processor pin BOOT0 --> always boot into main flash memory
            // Read https://netcult.ch/elmue/CANable Firmware Update
            // Enabling the pin needs not to be implemented here.
            // The pin is automatically enabled when entering DFU mode in dfu_switch_to_bootloader()
            if (strcmp(buf, "*Boot0:Off") == 0)
                return system_set_option_bytes(OPT_BOOT0_Disable);

            // return if the pin BOOT0 is enabled or disabled
            if (strcmp(buf, "*Boot0:?") == 0)
            {
                // String responses start with '+', all other command responses start with '#'
                eOptionStatus status = system_is_option_enabled(OPT_BOOT0_Enable);
                if (status == Option_Unavailable)
                    return FBK_UnsupportedFeature;
                
                char* resp = (status == Option_Active) ? "+1\r" : "+0\r";
                buf_enqueue_cdc(channel, resp, 3);
                return FBK_RetString;
            }

            if (strncmp(buf, "*Flash:", 7) == 0)
                return control_parse_flash(channel, buf);

            return FBK_InvalidCommand;
        }
    }

    // ================ Transmit Packet =================
    // "t600801020304050607083A\r"

    FDCAN_TxHeaderTypeDef tx_header;
    uint8_t               tx_data[CAN_MAX_DATALEN];

    // Set default header. All values overridden below as needed.
    tx_header.TxFrameType         = FDCAN_DATA_FRAME;
    tx_header.FDFormat            = FDCAN_CLASSIC_CAN;
    tx_header.IdType              = FDCAN_STANDARD_ID;
    tx_header.BitRateSwitch       = FDCAN_BRS_OFF;
    tx_header.ErrorStateIndicator = can_is_passive(channel) ? FDCAN_ESI_PASSIVE : FDCAN_ESI_ACTIVE;
    tx_header.TxEventFifoControl  = FDCAN_STORE_TX_EVENTS; // always! Tx Event flashes the Tx LED

    switch (buf[0])
    {
        // Transmit remote frame command
        case 'r':
            tx_header.TxFrameType   = FDCAN_REMOTE_FRAME;
            break;
        case 'R':
            tx_header.IdType        = FDCAN_EXTENDED_ID;
            tx_header.TxFrameType   = FDCAN_REMOTE_FRAME;
            break;

        // Transmit data frame command
        case 'T':
            tx_header.IdType        = FDCAN_EXTENDED_ID;
            break;
        case 't':
            break;

        // CANFD transmit - no BRS
        case 'd':
            tx_header.FDFormat      = FDCAN_FD_CAN;
            break;
        case 'D':
            tx_header.FDFormat      = FDCAN_FD_CAN;
            tx_header.IdType        = FDCAN_EXTENDED_ID;
            break;

        // CANFD transmit - with BRS
        case 'b':
            tx_header.FDFormat      = FDCAN_FD_CAN;
            tx_header.BitRateSwitch = FDCAN_BRS_ON;
            break;
        case 'B':
            tx_header.FDFormat      = FDCAN_FD_CAN;
            tx_header.BitRateSwitch = FDCAN_BRS_ON;
            tx_header.IdType        = FDCAN_EXTENDED_ID;
            break;

        // Invalid command
        default:
            return FBK_InvalidCommand;
    }

    // Sending a message with FDF flag requires a data baudrate to be set.
    // It is allowed that the data baudrate is the same as the nominal baudrate to send messages up to 64 bytes without BRS.
    if (tx_header.FDFormat == FDCAN_FD_CAN && !can_using_FD(channel))
        return FBK_BaudrateNotSet;

    // Start parsing at second byte (skip command byte)
    int parse_loc = 1;

    // standard ID / extended ID
    uint8_t id_len = (tx_header.IdType == FDCAN_EXTENDED_ID) ? 8 : 3;

    // parse CAN ID
    if (!utils_parse_hex_value(buf, &parse_loc, id_len, &tx_header.Identifier))
        return FBK_InvalidParameter;

    // check CAN ID
    if (tx_header.IdType == FDCAN_STANDARD_ID && tx_header.Identifier > 0x7FF)
        return FBK_ParamOutOfRange;

    if (tx_header.IdType == FDCAN_EXTENDED_ID && tx_header.Identifier > 0x1FFFFFFF)
        return FBK_ParamOutOfRange;

    // parse DLC
    uint32_t dlc_code;
    if (!utils_parse_hex_value(buf, &parse_loc, 1, &dlc_code))
        return FBK_InvalidParameter;

    // classic frames allow a DLC of 0...8
    if (tx_header.FDFormat == FDCAN_CLASSIC_CAN && dlc_code > 8)
        return FBK_InvalidParameter;

    tx_header.DataLength = dlc_code;

    // remote frames may have DLC > 0 but never send data bytes
    if (tx_header.TxFrameType != FDCAN_REMOTE_FRAME)
    {
        int8_t byte_count = utils_dlc_to_byte_count(dlc_code);
        // Parse data bytes
        for (uint8_t i = 0; i < byte_count && parse_loc < len; i++)
        {
            uint32_t byte_val;
            if (!utils_parse_hex_value(buf, &parse_loc, 2, &byte_val))
                return FBK_InvalidParameter;

            tx_data[i] = byte_val;
        }
    }

    // The host must generate a unique one-byte marker for each sent packet using a counter that increments with each Tx message.
    // The Tx FIFO can store 3 packets and the buffer can store 64 waiting messages.
    // So 3 + 64 different values are sufficient that each message that is waiting for an ACK has it's own unique marker.
    if (GLB_UserFlags[channel] & USR_TxEcho)
    {
        if (!utils_parse_hex_value(buf, &parse_loc, 2, &tx_header.MessageMarker))
            return FBK_InvalidParameter;
    }

    // If there are any remaining bytes at the end, this is a syntax error.
    if (parse_loc != len)
        return FBK_InvalidParameter;

    return buf_store_tx_packet(channel, &tx_header, tx_data);
}

// ================================================================================================================

// ATTENTION: Deprecated! Read the manual.
// Set the nominal / data bitrate of the CAN peripheral
// Always samplepoint 75%. (In previous versions 87.5% was used which may produce Rx/Tx errors)
// IMPORTANT: Read the chapter "Samplepoint & Baudrate" in the HTML manual.
// set_data = false -> Set the nominal baudrate configuration of the CAN peripheral
// set_data = true  -> Set the data    baudrate configuration of the CAN peripheral
eFeedback control_set_baudrate(uint8_t channel, bool set_data, char baud_chr)
{
    uint64_t timing_values;
    
    if (set_data)
    {
        switch (baud_chr)
        {
            case CAN_DATA_BAUDRATE_500K: timing_values = CAN_DATA_BITTIMING_500K; break;
            case CAN_DATA_BAUDRATE_1M:   timing_values = CAN_DATA_BITTIMING_1M;   break;
            case CAN_DATA_BAUDRATE_2M:   timing_values = CAN_DATA_BITTIMING_2M;   break;
            case CAN_DATA_BAUDRATE_4M:   timing_values = CAN_DATA_BITTIMING_4M;   break;
            case CAN_DATA_BAUDRATE_5M:   timing_values = CAN_DATA_BITTIMING_5M;   break;
            case CAN_DATA_BAUDRATE_8M:   timing_values = CAN_DATA_BITTIMING_8M;   break;
            default: return FBK_InvalidParameter;
        }
    }
    else
    {
        switch (baud_chr)
        {
            case CAN_NOM_BAUDRATE_5K:    timing_values = CAN_NOM_BITTIMING_5K;    break;
            case CAN_NOM_BAUDRATE_10K:   timing_values = CAN_NOM_BITTIMING_10K;   break;
            case CAN_NOM_BAUDRATE_20K:   timing_values = CAN_NOM_BITTIMING_20K;   break;
            case CAN_NOM_BAUDRATE_33_3K: timing_values = CAN_NOM_BITTIMING_33_3K; break;
            case CAN_NOM_BAUDRATE_50K:   timing_values = CAN_NOM_BITTIMING_50K;   break;
            case CAN_NOM_BAUDRATE_62_5K: timing_values = CAN_NOM_BITTIMING_62_5K; break;
            case CAN_NOM_BAUDRATE_75K:   timing_values = CAN_NOM_BITTIMING_75K;   break;
            case CAN_NOM_BAUDRATE_83_3K: timing_values = CAN_NOM_BITTIMING_83_3K; break;
            case CAN_NOM_BAUDRATE_100K:  timing_values = CAN_NOM_BITTIMING_100K;  break;
            case CAN_NOM_BAUDRATE_125K:  timing_values = CAN_NOM_BITTIMING_125K;  break;
            case CAN_NOM_BAUDRATE_250K:  timing_values = CAN_NOM_BITTIMING_250K;  break;
            case CAN_NOM_BAUDRATE_500K:  timing_values = CAN_NOM_BITTIMING_500K;  break;
            case CAN_NOM_BAUDRATE_800K:  timing_values = CAN_NOM_BITTIMING_800K;  break;
            case CAN_NOM_BAUDRATE_1M:    timing_values = CAN_NOM_BITTIMING_1M;    break;
            default: return FBK_InvalidParameter;
        }
    }
    
    // If CAN_NOM_BITTIMING_xxx is defined as zero --> the processor does not support the baudrate
    if (timing_values == 0)
        return FBK_UnsupportedFeature;
    
    uint16_t brp  = (timing_values >> 32);
    uint16_t seg1 = (timing_values >> 16);
    uint16_t seg2 = (timing_values);

    // See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
    // Set SJW to the same length as the shortest segment - 1
    bitlimits* limits = utils_get_bit_limits(set_data);
    uint32_t   sjw    = MIN(MIN(seg1, seg2), limits->sjw_max);
    if (sjw > 1) 
        sjw --;
    
    return can_set_bit_timing(channel, set_data, brp, seg1, seg2, sjw);
}

// -------------------------

// Command: "F7E0,7FF;1F005000,1FFFFFFF\r" --> set 11 bit filter: 0x7E0, mask: 0x7FF and 29 bit filter 0x1F005000.
// see comment for can_add_host_filter()
eFeedback control_host_filter(uint8_t channel, char buf[])
{
    int pos = 1;
    bool abort = false;
    while (!abort)
    {
        uint32_t filter, mask;
        int digitsF, digitsM;
        if (!utils_parse_hex_delimiter(buf, &pos, ',', &digitsF, &filter))
        {
            if (buf[pos] == 0) break;  // string zero termination found after semicolon
            return FBK_InvalidParameter;
        }

        if (!utils_parse_hex_delimiter(buf, &pos, ';', &digitsM, &mask))
        {
            if (buf[pos] == 0) abort = true;  // string zero termination found after mask
            else return FBK_InvalidParameter; // invalid character
        }

        bool extended;
             if (digitsF == 3 && digitsM == 3) extended = false;
        else if (digitsF == 8 && digitsM == 8) extended = true;
        else return FBK_InvalidParameter;

        eFeedback error = can_add_host_filter(channel, extended, filter, mask);
        if (error != FBK_Success)
            return error;
    }
    return FBK_Success;
}

// "F:P0A=7E0,7F0>2\r"  set Pass  filter Nº 0x0A for CAN ID 7E0...7EF to CAN channel 2
// "F:B11=7E5,7FF>2\r"  set Block filter Nº 0x11 for CAN ID 7E5       to CAN channel 2
// "f:07\r"             clear bridge filter Nº 0x07
// "f:FF\r"             clear all bridge filters
eFeedback control_bridge_filter(uint8_t channel, char buf[], bool enable)
{
    bool block    = false;   
    bool extended = false;
    uint32_t dest_channel = 0;
    uint32_t filter = 0;
    uint32_t mask   = 0;
    int pos = 2;    

    if (enable)
    {
        switch (buf[pos++])
        { 
            case 'B': block  = true;  break;
            case 'P': block  = false; break;
            default: return FBK_InvalidParameter;
        }
    }
    
    uint32_t filter_index;
    if (!utils_parse_hex_value(buf, &pos, 2, &filter_index))
        return FBK_InvalidParameter;

    if (enable)
    {
        if (buf[pos++] != '=')
            return FBK_InvalidParameter;
        
        int digitsF, digitsM;
        if (!utils_parse_hex_delimiter(buf, &pos, ',', &digitsF, &filter))
            return FBK_InvalidParameter;

        if (!utils_parse_hex_delimiter(buf, &pos, '>', &digitsM, &mask))
            return FBK_InvalidParameter; // invalid character

             if (digitsF == 3 && digitsM == 3) extended = false;
        else if (digitsF == 8 && digitsM == 8) extended = true;
        else return FBK_InvalidParameter;
        
        if (!utils_parse_hex_value(buf, &pos, 1, &dest_channel))
            return FBK_InvalidParameter;   
    }

    return can_set_bridge_filter(channel, dest_channel, filter_index, enable, extended, block, filter, mask);
}

// "*Flash:1A=48656C6C6F\r" writes "Hello" to   flash segment 1A
// "*Flash:1A?\r"           reads  "Hello" from flash segment 1A --> return "+48656C6C6F\r"
eFeedback control_parse_flash(uint8_t channel, char buf[])
{
    // Read the segment (00 ... FF)
    int pos = 7;
    uint32_t segment;
    if (!utils_parse_hex_value(buf, &pos, 2, &segment))
        return FBK_InvalidParameter; // syntax error or invalid digit count

    // 2 hex digits per flash byte
    uint8_t hex_data[MAX_FLASH_DATA_LEN * 2 + 8];
    int idx = 0;    

    switch (buf[pos++])
    {
        case '?': // Read
            if (buf[pos] != 0)
                return FBK_InvalidParameter; // invalid data behind '?'

            uint32_t flash_addr = system_get_flash_addr(segment);
            if (flash_addr == 0)
                return FBK_ParamOutOfRange; // segment is occupied by firmware

            uint8_t*  flash_data =  (uint8_t*)(flash_addr + 2);
            uint16_t  flash_len  = ((uint16_t*)flash_addr)[0];

            if (flash_len == 0xFFFF) flash_len = 0; // erased segment
            flash_len = MIN(flash_len, MAX_FLASH_DATA_LEN);

            // convert flash data bytes to hex digits
            hex_data[idx++] = '+';
            for (uint16_t i=0; i<flash_len; i++)
            {
                hex_data[idx++] = utils_nibble_to_ascii(flash_data[i] >> 4);
                hex_data[idx++] = utils_nibble_to_ascii(flash_data[i] & 0x0F);
            }
            hex_data[idx++] = '\r';

            buf_enqueue_cdc(channel, (char*)hex_data, idx);
            return FBK_RetString;

        case '=': // Write
            while (buf[pos] != 0)
            {
                if (idx >= MAX_FLASH_DATA_LEN)
                    return FBK_ParamOutOfRange;
                
                uint32_t byte;
                if (!utils_parse_hex_value(buf, &pos, 2, &byte))
                    return FBK_InvalidParameter; // syntax error or invalid digit count

                hex_data[idx++] = (uint8_t)byte;
            }
            return system_write_flash(segment, hex_data, idx);
            
        default:
            return FBK_InvalidParameter;
    }
}

// This function is called approx 100 times in one millisecond from the main loop
// if the error state has changed, report it every 100 ms
// if the error state did not change, report the same state only every 3000 ms.
void control_process(uint8_t channel, uint32_t tick_now)
{
    if (!error_is_report_due(channel, tick_now))
        return;

    // get errors that are still present after the last error_clear()
    kCanErrorState* state = error_get_state(channel);

    // Bus status and last protocol error (FDCAN_PROTOCOL_ERROR_ACK) have few values --> pack both into one byte
    char tempbuf[20];
    sprintf(tempbuf, "E%02X%02X%02X%02X\r", (uint8_t)(state->bus_status | state->last_proto_err),
                                            (uint8_t)state->app_flags,
                                            (uint8_t)state->tx_err_count,
                                            (uint8_t)state->rx_err_count);
    buf_enqueue_cdc(channel, tempbuf, 10);
    error_clear(channel);

    // Revover BusOff AFTER printing error BusOff to the Trace output!
    can_recover_bus_off(channel);
}

// send the busload in percet to the host in the user defined interval
void control_report_busload(uint8_t channel, uint8_t busload_percent)
{
    char buf[10];
    sprintf(buf, "L%u\r", busload_percent);
    buf_enqueue_cdc(channel, buf, strlen(buf));
}

// Send a debug message. Maximum length is 80 characters.
// The message may contain "\n" for multi-line output
// You will see this message in the Trace pane of HUD ECU Hacker if USR_DebugReport is enabled.
bool control_send_debug_mesg(uint8_t channel, const char* message)
{
    if ((GLB_UserFlags[channel] & USR_DebugReport) == 0)
        return false;

    int len = strlen(message);
    if (len > 80)
    {
        message = "*** Dbg msg too long";
        len = 20;
    }

    char buf[85];
    sprintf(buf, ">%s\r", message);

    buf_enqueue_cdc(channel, buf, len + 2);
    return true;
}


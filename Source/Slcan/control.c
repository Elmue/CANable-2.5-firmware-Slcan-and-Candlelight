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

extern eUserFlags USER_Flags;

uint32_t can_mode = FDCAN_MODE_NORMAL; // normal, silent, loopback modes

eFeedback control_parse_str (char buf[], int len);
eFeedback control_set_filter(char buf[], uint8_t len);

// ==================================================================================================================

void control_init()
{
    // all the other flags must be enabled by the user
    USER_Flags = USR_SlcanDefault;
}

void control_parse_command(char buf[], int len)
{
    eFeedback e_Ret = control_parse_str(buf, len);
    if ((USER_Flags & USR_Feedback) == 0)
        return;

    switch (e_Ret)
    {
        case FBK_RetString:
            break; // response has already been written with buf_enqueue_cdc()
        case FBK_Success:
            buf_enqueue_cdc("#\r", 2); // return "#\r" for success
            break;
        default:
        {
            char s8_Error[3] = { '#', e_Ret, '\r' }; // return "#4\r" for error 4
            buf_enqueue_cdc(s8_Error, 3);
            break;
        }
    }
}

// Parse an incoming slcan command from the USB CDC port.
// The termination '\r' has already been removed.
eFeedback control_parse_str(char buf[], int len)
{
    // Flash the blue LED very shortly if bus is closed
    // ATTENTION: If the bus is closed the green LED is on, so this is not the same as blue flashing with bus open.
    if (!can_is_opened())
        led_flash_RX(); // flash 15 ms

    // Reply OK to a blank command "\r"
    if (len == 0)
        return FBK_Success;

    // IMPORTANT: Terminate the string with zero
    buf[len] = 0;

    char tempbuf[200];

    eFeedback e_Ret = FBK_InvalidParameter;
    switch (buf[0])
    {
        // Set Auto Retransmit (legacy)
        case 'A':
            if (len != 2)
                return FBK_InvalidParameter;

            if (can_is_opened())
                return FBK_AdapterMustBeClosed;

            switch (buf[1])
            {
                case '0': USER_Flags &= ~USR_Retransmit; break; // "A0" (legacy command) enable one shot mode
                case '1': USER_Flags |=  USR_Retransmit; break; // "A1" (legacy command) try 128 times to send a packet
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
                        if (can_is_opened()) return FBK_AdapterMustBeClosed;
                        USER_Flags |=  USR_Retransmit; 
                        break;
                    case 'a':                                        // "Ma"
                        if (can_is_opened()) return FBK_AdapterMustBeClosed;
                        USER_Flags &= ~USR_Retransmit;  
                        break;
                    case 'D': USER_Flags |=  USR_DebugReport; break; // "MD"  Enable string debug messages
                    case 'd': USER_Flags &= ~USR_DebugReport; break; // "Md"
                    case 'E': USER_Flags |=  USR_ErrorReport; break; // "ME"  Enable CAN bus error reports
                    case 'e': USER_Flags &= ~USR_ErrorReport; break; // "Me"
                    case 'F': USER_Flags |=  USR_Feedback;    break; // "MF"  Enable command execution Feedback mode
                    case 'f': USER_Flags &= ~USR_Feedback;    break; // "Mf"
                    case 'M': USER_Flags |=  USR_ReportTX;    break; // "MT"  Enable Tx echo report with Marker
                    case 'm': USER_Flags &= ~USR_ReportTX;    break; // "Mt"
                    case 'S': USER_Flags |=  USR_ReportESI;   break; // "MS"  Enable ESI report
                    case 's': USER_Flags &= ~USR_ReportESI;   break; // "Ms"
                    // -----------------------------------------------------
                    case 'I': led_blink_identify(true);       break; // "MI"  Identify device by blinking the LEDs
                    case 'i': led_blink_identify(false);      break; // "Mi"  stop blinking
                    case '0':                                        // "M0"  Use normal mode for Open (legacy command)
                    case '1':                                        // "M1"  Use silent (bus monitoring) mode for Open (legacy command)
                        if (can_is_opened()) 
                            return FBK_AdapterMustBeClosed;
                        can_mode = (buf[i] == '1') ? FDCAN_MODE_BUS_MONITORING : FDCAN_MODE_NORMAL;
                        break;
                    case 'R':                                        // "MR" (enable  120 Ohm Termination Resistor)
                    case 'r':                                        // "Mr" (disable 120 Ohm Termination Resistor)
                        if (!can_set_termination(buf[i] == 'R'))
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
                // ATTENTION: can_mode is also set by command "M"
                // It must be only modified here if 2 characters have been sent!
                switch (buf[1])
                {
                    case 'N': can_mode = FDCAN_MODE_NORMAL;            break; // "ON"
                    case 'S': can_mode = FDCAN_MODE_BUS_MONITORING;    break; // "OS"
                    case 'I': can_mode = FDCAN_MODE_INTERNAL_LOOPBACK; break; // "OI"
                    case 'E': can_mode = FDCAN_MODE_EXTERNAL_LOOPBACK; break; // "OE"
                    default:  return FBK_InvalidParameter;
                }
            }
            return can_open(can_mode); // returns error if already open
        }
        // Close adapter and reset variables (no error if already closed)
        // ATTENTION: This command does not send feedback although it is enabled!
        // This is by purpose. The application can execute this before command Open to assure that all variables are reset.
        // In this state the application does not know if feedbacks are still enabled from the last usage or not.
        case 'C':
            if (len == 1)
            {
                can_close(); // no error if already closed

                // reset the variables to their default
                can_mode   = FDCAN_MODE_NORMAL;
                USER_Flags = USR_SlcanDefault;

                // Do not call buf_enqueue_cdc() here --> never send a response. 
                // This is the only command that behaves the same way as the legacy command.
                return FBK_RetString;
            }
            return e_Ret;

        // ----------------------------

        // Get version number, processor, clock,...
        case 'V':
            if (len == 1)
            {
                // The host application needs these limits to calculate the bitrates (commands 's' and 'y')
                bitlimits* lim = utils_get_bit_limits();

                // HAL_GetDEVID() returns a unique identifier (DBG_IDCODE) for each processor family.
                // The STM32G0xx serie uses 0x460, 0x465, 0x476, 0x477 and STM32G4xx uses 0x468, 0x469, 0x479.
                // String responses start with '+', all other command responses start with '#'
                sprintf(tempbuf, "+Board: "      TARGET_BOARD            // MksMakerbase           (from MakeFile)
                                 "\tMCU: %s"                             // STM32G431              (from MakeFile)
                                 "\tDevID: %lu"                          // 0x468                  (from processor)
                                 "\tFirmware: %u"                        // 0x250814               (from settings.h)
                                 "\tSlcan: "     STR(SLCAN_VERSION)      // 100                    (from settings.h)
                                 "\tClock: %lu"                          // 160                    (from system variable)
                                 "\tLimits: %lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\r",
                                 utils_get_MCU_name(),
                                 HAL_GetDEVID(),
                                 FIRMWARE_VERSION_BCD,
                                 system_get_can_clock() / 1000000,
                                 lim->nom_brp_max, lim->nom_seg1_max, lim->nom_seg2_max, lim->nom_sjw_max,
                                 lim->fd_brp_max,  lim->fd_seg1_max,  lim->fd_seg2_max,  lim->fd_sjw_max);

                buf_enqueue_cdc(tempbuf, strlen(tempbuf));
                return FBK_RetString;
            }
            return e_Ret;

        // ----------------------------

        // Set baudrate (always samplepoint nominal: 87.5%, data: 75%)
        case 'S':
            if (len == 2) e_Ret = can_set_baudrate((can_nom_bitrate)(buf[1] - '0')); // "S1"
            return e_Ret;
        case 'Y':
            if (len == 2) e_Ret = can_set_data_baudrate((can_data_bitrate)(buf[1] - '0')); // "Y2"
            return e_Ret;

        // Set bitrate (any samplepoint is possible)
        case 's':
        case 'y':
        {
            int pos = 1;
            uint32_t BRP, Seg1, Seg2, Sjw;
            if (!utils_parse_next_decimal(buf, &pos, ',', &BRP)  ||
                !utils_parse_next_decimal(buf, &pos, ',', &Seg1) ||
                !utils_parse_next_decimal(buf, &pos, ',', &Seg2) ||
                !utils_parse_next_decimal(buf, &pos,  0,  &Sjw))
                    return FBK_InvalidParameter;

            if (buf[0] == 's') return can_set_nom_bit_timing (BRP, Seg1, Seg2, Sjw); // "s40,16,2,2"
            else               return can_set_data_bit_timing(BRP, Seg1, Seg2, Sjw);
        }

        // ----------------------------

        // Set CAN filter:
        case 'F':
            return control_set_filter(buf, len); // "F7E0,7FF"
        // Clear all CAN filters:
        case 'f':
            if (len == 1) return can_clear_filters(); // "f"
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

            return can_enable_busload(interval); // interval in 100ms steps
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
                char* resp = system_is_option_enabled(OPT_BOOT0_Enable) ? "+1\r" : "+0\r";
                buf_enqueue_cdc(resp, 3);
                return FBK_RetString;
            }
            return FBK_InvalidParameter;
        }
        // Debug command
        case '?':
        {
            return FBK_InvalidCommand;
            /*
            // This code was written by Nakanishi Kiyomaro.

            char* dbgstr = (char*)buf_get_cdc_dest();
            snprintf(dbgstr, SLCAN_MTU - 1, ">%02X-%02X-%01X-%04X%04X-%04X\r",
                                        (uint8_t)(can_get_cycle_ave_time_ns() >= 255000 ? 255 : can_get_cycle_ave_time_ns() / 1000),
                                        (uint8_t)(can_get_cycle_max_time_ns() >= 255000 ? 255 : can_get_cycle_max_time_ns() / 1000),
                                        (uint8_t)(HAL_FDCAN_GetState(can_get_handle())),
                                        (uint16_t)(HAL_FDCAN_GetError(can_get_handle()) >> 16),
                                        (uint16_t)(HAL_FDCAN_GetError(can_get_handle()) & 0xFFFF),
                                        (uint16_t)(error_get_register()));
            buf_comit_cdc_dest(23);
            */

            /*
            uint8_t cycle_ave = (uint8_t)(can_get_cycle_ave_time_ns() >= 255000 ? 255 : can_get_cycle_ave_time_ns() / 1000);
            uint8_t cycle_max = (uint8_t)(can_get_cycle_max_time_ns() >= 255000 ? 255 : can_get_cycle_max_time_ns() / 1000);

            char dbgstr[7];
            dbgstr[0] = '>'; // debug message
            dbgstr[1] = cycle_ave >> 4;
            dbgstr[2] = cycle_ave & 0xF;
            for (uint8_t j = 1; j <= 2; j++)
            {
                if (dbgstr[j] < 0xA) dbgstr[j] += 0x30;
                else                 dbgstr[j] += 0x37;
            }
            dbgstr[3] = '-';
            dbgstr[4] = cycle_max >> 4;
            dbgstr[5] = cycle_max & 0xF;
            for (uint8_t j = 4; j <= 5; j++)
            {
                if (dbgstr[j] < 0xA) dbgstr[j] += 0x30;
                else                 dbgstr[j] += 0x37;
            }
            dbgstr[6] = '\r';
            can_clear_cycle_time();

            buf_enqueue_cdc(dbgstr, 7);
            return FBK_RetString;
            */
        }
    }

    // ================ Transmit Packet =================
    // "t600801020304050607083A\r"

    // Set default header. All values overridden below as needed.
    FDCAN_TxHeaderTypeDef* tx_header = buf_get_can_dest_header();
    uint8_t*               tx_data   = buf_get_can_dest_data();

    if (tx_header == NULL || tx_data == NULL)
        return FBK_TxBufferFull;

    tx_header->TxFrameType         = FDCAN_DATA_FRAME;
    tx_header->FDFormat            = FDCAN_CLASSIC_CAN;
    tx_header->IdType              = FDCAN_STANDARD_ID;
    tx_header->BitRateSwitch       = FDCAN_BRS_OFF;
    tx_header->ErrorStateIndicator = can_is_passive() ? FDCAN_ESI_PASSIVE : FDCAN_ESI_ACTIVE;
    tx_header->TxEventFifoControl  = FDCAN_STORE_TX_EVENTS; // always! Tx Event flashes the green LED

    switch (buf[0])
    {
        // Transmit remote frame command
        case 'r':
            tx_header->TxFrameType   = FDCAN_REMOTE_FRAME;
            break;
        case 'R':
            tx_header->IdType        = FDCAN_EXTENDED_ID;
            tx_header->TxFrameType   = FDCAN_REMOTE_FRAME;
            break;

        // Transmit data frame command
        case 'T':
            tx_header->IdType        = FDCAN_EXTENDED_ID;
            break;
        case 't':
            break;

        // CANFD transmit - no BRS
        case 'd':
            tx_header->FDFormat      = FDCAN_FD_CAN;
            break;
        case 'D':
            tx_header->FDFormat      = FDCAN_FD_CAN;
            tx_header->IdType        = FDCAN_EXTENDED_ID;
            break;

        // CANFD transmit - with BRS
        case 'b':
            tx_header->FDFormat      = FDCAN_FD_CAN;
            tx_header->BitRateSwitch = FDCAN_BRS_ON;
            break;
        case 'B':
            tx_header->FDFormat      = FDCAN_FD_CAN;
            tx_header->BitRateSwitch = FDCAN_BRS_ON;
            tx_header->IdType        = FDCAN_EXTENDED_ID;
            break;

        // Invalid command
        default:
            return FBK_InvalidCommand;
    }
    
    // Sending a message with FDF flag requires a data baudrate to be set.
    // It is allowed that the data baudrate is the same as the nominal baudrate to send messages up to 64 bytes without BRS.
    if (tx_header->FDFormat == FDCAN_FD_CAN && !can_using_FD())
        return FBK_BaudrateNotSet;

    // Start parsing at second byte (skip command byte)
    int parse_loc = 1;

    // standard ID / extended ID
    uint8_t id_len = (tx_header->IdType == FDCAN_EXTENDED_ID) ? 8 : 3;

    // parse CAN ID
    if (!utils_parse_hex_value(buf, &parse_loc, id_len, &tx_header->Identifier))
        return FBK_InvalidParameter;

    // check CAN ID
    if (tx_header->IdType == FDCAN_STANDARD_ID && tx_header->Identifier > 0x7FF)
        return FBK_InvalidParameter;

    if (tx_header->IdType == FDCAN_EXTENDED_ID && tx_header->Identifier > 0x1FFFFFFF)
        return FBK_InvalidParameter;

    // parse DLC
    uint32_t dlc_code;
    if (!utils_parse_hex_value(buf, &parse_loc, 1, &dlc_code))
        return FBK_InvalidParameter;

    // classic frames allow a DLC of 0...8
    if (tx_header->FDFormat == FDCAN_CLASSIC_CAN && dlc_code > 8)
        return FBK_InvalidParameter;
    
    // remote frames have no data bytes
    if (tx_header->TxFrameType == FDCAN_REMOTE_FRAME && dlc_code > 0)
        return FBK_InvalidParameter;

    // Shift bits up for direct storage in FIFO register
    // It is stupid that ST Microelectronics did not define a processor independent macro for this shift operation.
    // Will other processors also need this to be shifted 16 bits up ??
    tx_header->DataLength = dlc_code << 16;

    int8_t byte_count = utils_dlc_to_byte_count(dlc_code);
    // Parse data bytes
    for (uint8_t i = 0; i < byte_count && parse_loc < len; i++)
    {
        uint32_t byte_val;
        if (!utils_parse_hex_value(buf, &parse_loc, 2, &byte_val))
            return FBK_InvalidParameter;

        tx_data[i] = byte_val;
    }

    // The host must generate a unique one-byte marker for each sent packet using a counter that increments with each Tx message.
    // The Tx FIFO can store 3 packets and the buffer can store 64 waiting messages.
    // So 3 + 64 different values are sufficient that each message that is waiting for an ACK has it's own unique marker.
    if (USER_Flags & USR_ReportTX)
    {
        if (!utils_parse_hex_value(buf, &parse_loc, 2, &tx_header->MessageMarker))
            return FBK_InvalidParameter;
    }

    // Store the message in the buffer
    return buf_comit_can_dest();
}

// ================================================================================================================

// Command: "F7E0,7FF;1F005000,1FFFFFFF\r" --> set 11 bit filter: 0x7E0, mask: 0x7FF and 29 bit filter 0x1F005000.
// see comment for can_set_filter()
eFeedback control_set_filter(char buf[], uint8_t len)
{
    int  pos = 1;
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

        if (digitsF != digitsM)
            return FBK_InvalidParameter;

        bool extended;
             if (digitsF == 3) extended = false;
        else if (digitsF == 8) extended = true;
        else return FBK_InvalidParameter;

        eFeedback error = can_set_mask_filter(extended, filter, mask);
        if (error != FBK_Success)
            return error;
    }
    return FBK_Success;
}

// This function is called approx 100 times in one millisecond from the main loop
// if the error state has changed, report it every 100 ms
// if the error state did not change, report the same state only every 3000 ms.
void control_process(uint32_t tick_now)
{
    if (!error_is_report_due(tick_now))
        return;

    // get errors that are still present after the last error_clear()
    kCanErrorState* state = error_get_state();

    // Bus status and last protocol error (FDCAN_PROTOCOL_ERROR_ACK) have few values --> pack both into one byte
    char tempbuf[20];
    sprintf(tempbuf, "E%02X%02X%02X%02X\r", (uint8_t)(state->bus_status | state->last_proto_err),
                                            (uint8_t)state->app_flags,
                                            (uint8_t)state->tx_err_count,
                                            (uint8_t)state->rx_err_count);
    buf_enqueue_cdc(tempbuf, 10);
    error_clear();
}

// send the busload in percet to the host in the user defined interval
void control_report_busload(uint8_t busload_percent)
{
    char buf[10];
    sprintf(buf, "L%u\r", busload_percent);
    buf_enqueue_cdc(buf, strlen(buf));
}

// Send a debug message. Maximum length is 80 characters.
// The message may contain "\n" for multi-line output
// You will see this message in the Trace pane of HUD ECU Hacker if USR_DebugReport is enabled.
bool control_send_debug_mesg(const char* message)
{
    if ((USER_Flags & USR_DebugReport) == 0)
        return false;

    int len = strlen(message);
    if (len > 80)
    {
        message = "*** Dbg msg too long";
        len = 20;
    }

    char buf[85];
    sprintf(buf, ">%s\r", message);

    buf_enqueue_cdc(buf, len + 2);
    return true;
}


/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once
#include "settings.h"
#include "can.h"

// Settings that the user can enable / disable. Used only internally in the firmware, not sent over USB.
// This enum is used for Slcan and for Candlelight
typedef enum // 32 bit
{
    USR_Retransmit  = 0x01, // retransmit packets that have not been ACknowledged
    USR_ReportTX    = 0x02, // report packets to the host that have been sent successfully (enable Tx Echo)
    USR_ReportESI   = 0x04, // report the ESI flag of received packets (Candlelight: always ON)
    USR_ErrorReport = 0x08, // report error status every 100 ms if it has changed, report unchanged errors every 3 seconds. (Candlelight: always ON)
    USR_DebugReport = 0x10, // enable ASCII debug messages to the host (Candlelight: enabled with ELM_DevFlagProtocolElmue)
    USR_Feedback    = 0x20, // enable feedback mode (return execution status of a command with enum eFeedback) (Candlelight uses ELM_ReqGetLastError instead)
    USR_ProtoElmue  = 0x40, // enable the new ElmüSoft protocol for maximum USB throughput instead of the inefficient GS protocol (Candlelight only)
    USR_Timestamp   = 0x80, // send timestamps to the host
    // --------------------
    // IMPORTANT:
    // Never *EVER* modify these defaults!!! You will break all applications that have been written for CANable adapters!
    USR_SlcanDefault  = USR_Retransmit,
    USR_CandleDefault = USR_Retransmit | USR_ReportTX | USR_ErrorReport | USR_ReportESI,
} eUserFlags;

typedef struct
{
    uint32_t nom_brp_max;
    uint32_t nom_seg1_max;
    uint32_t nom_seg2_max;
    uint32_t nom_sjw_max;
    
    uint32_t fd_brp_max;
    uint32_t fd_seg1_max;
    uint32_t fd_seg2_max;
    uint32_t fd_sjw_max;
} bitlimits;

void        utils_init();
bitlimits*  utils_get_bit_limits();
void        utils_format_bitrate(char buf[], char* prefix, can_bitrate_cfg* bit_rate);
bool        utils_mem_is_empty(void* ptr, int size);
int8_t      utils_dlc_to_byte_count(uint32_t hal_dlc_code);
int8_t      utils_byte_count_to_dlc(uint32_t byte_count);
bool        utils_parse_next_decimal    (char buf[], int* pos, char separator, uint32_t* value);
bool        utils_parse_hex_value(char buf[], int* pos, int digits, uint32_t* value);
bool        utils_parse_hex_delimiter(char buf[], int* pos, char separator, int* digits, uint32_t* value);
bool        utils_to_hex_value(char buf[], int pos);
uint8_t     utils_nibble_to_ascii(uint8_t nibble);
const char* utils_get_MCU_name();



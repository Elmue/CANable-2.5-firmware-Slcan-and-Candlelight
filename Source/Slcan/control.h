/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

// Classic CAN / CANFD nominal bitrates
// always samplepoint 87.5%
typedef enum 
{
    CAN_NOM_BAUDRATE_10K   = '0', // S0
    CAN_NOM_BAUDRATE_20K   = '1', // S1
    CAN_NOM_BAUDRATE_50K   = '2', // S2
    CAN_NOM_BAUDRATE_100K  = '3', // S3
    CAN_NOM_BAUDRATE_125K  = '4', // S4
    CAN_NOM_BAUDRATE_250K  = '5', // S5
    CAN_NOM_BAUDRATE_500K  = '6', // S6
    CAN_NOM_BAUDRATE_800K  = '7', // S7
    CAN_NOM_BAUDRATE_1M    = '8', // S8
    CAN_NOM_BAUDRATE_83_3K = '9', // S9
    CAN_NOM_BAUDRATE_75K   = 'A', // SA (used by WeActStudio)
    CAN_NOM_BAUDRATE_62_5K = 'B', // SB (used by WeActStudio)
    CAN_NOM_BAUDRATE_33_3K = 'C', // SC (used by WeActStudio)
    CAN_NOM_BAUDRATE_5K    = 'D', // SD (used by WeActStudio)
} can_nom_baudrate;

// CANFD data bitrates
// always samplepoint 87.5%
typedef enum 
{
    CAN_DATA_BAUDRATE_500K = '0', // Y0
    CAN_DATA_BAUDRATE_1M   = '1', // Y1
    CAN_DATA_BAUDRATE_2M   = '2', // Y2
    CAN_DATA_BAUDRATE_4M   = '4', // Y4
    CAN_DATA_BAUDRATE_5M   = '5', // Y5
    CAN_DATA_BAUDRATE_8M   = '8', // Y8
} can_data_baudrate;


void control_init();
void control_parse_command(char* buf, int len);
void control_process(uint8_t channel, uint32_t tick_now);
void control_report_busload(uint8_t channel, uint8_t busload_percent);
bool control_send_debug_mesg(uint8_t channel, const char* message);




/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/


#pragma once

#include "settings.h"
#include "system.h"

// FDCAN_TxHeaderTypeDef.DataLength needs the DLC value to be shifted up by 16 bits.
// It is stupid that ST Microelectronics did not implement this shift operation into the HAL.
// Will other ST processor models also need the DLC to be shifted 16 bits up ??
#define DLC_SHIFT        16
#define DLC_TO_HAL(DLC) ((DLC & 0xF) << DLC_SHIFT)
#define HAL_TO_DLC(LEN) ((LEN >> DLC_SHIFT) & 0xF)

// Classic CAN / CANFD nominal bitrates
// always samplepoint 87.5%
typedef enum 
{
    CAN_BITRATE_10K = 0,  // S0
    CAN_BITRATE_20K,      // S1
    CAN_BITRATE_50K,      // S2
    CAN_BITRATE_100K,     // S3
    CAN_BITRATE_125K,     // S4
    CAN_BITRATE_250K,     // S5
    CAN_BITRATE_500K,     // S6
    CAN_BITRATE_800K,     // S7
    CAN_BITRATE_1000K,    // S8
    CAN_BITRATE_83K,      // S9

    CAN_BITRATE_INVALID,
} can_nom_bitrate;

// CANFD data bitrates
// always samplepoint 87.5%
typedef enum 
{
    CAN_DATA_BITRATE_500K = 0, // Y0
    CAN_DATA_BITRATE_1M   = 1, // Y1
    CAN_DATA_BITRATE_2M   = 2, // Y2
    CAN_DATA_BITRATE_4M   = 4, // Y4
    CAN_DATA_BITRATE_5M   = 5, // Y5
    CAN_DATA_BITRATE_8M   = 8, // Y8

    CAN_DATA_BITRATE_INVALID,
} can_data_bitrate;

// Structure for CAN/FD bitrate configuration
typedef struct 
{
    uint32_t  Brp;  // bitrate prescaler
    uint32_t  Seg1; // segment 1 without sync before samplepoint
    uint32_t  Seg2; // segment 2 after samplepoint
    uint32_t  Sjw;  // synchronization jump width  
} can_bitrate_cfg;

typedef struct
{
    uint8_t  marker;
    uint8_t  data[64];
} tx_packet;

static inline uint32_t can_calc_baud(can_bitrate_cfg* bitrate)
{
    return (bitrate->Brp == 0) ? 0 : system_get_can_clock() / bitrate->Brp / (1 + bitrate->Seg1 + bitrate->Seg2);
}
// returns 875 for 87.5%
static inline uint32_t can_calc_sample(can_bitrate_cfg* bitrate)
{
    return 1000 * (1 + bitrate->Seg1) / (1 + bitrate->Seg1 + bitrate->Seg2);
}

void can_init();
eFeedback can_open(uint32_t mode);
void      can_close();
void      can_process(uint32_t tick_now);
void      can_timer_100ms();
void      can_send_packet(FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data);
eFeedback can_set_baudrate     (can_nom_bitrate bitrate);
eFeedback can_set_data_baudrate(can_data_bitrate bitrate);
eFeedback can_set_nom_bit_timing (uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw);
eFeedback can_set_data_bit_timing(uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw);
eFeedback can_enable_busload(uint32_t interval);
bool      can_set_termination(bool enable);
bool      can_get_termination(bool* enabled);
void      can_print_info();
bool      can_is_opened();
bool      can_is_passive();
bool      can_using_FD();
bool      can_using_BRS();
eFeedback can_is_tx_allowed();
eFeedback can_set_mask_filter(bool extended, uint32_t filter, uint32_t mask);
eFeedback can_clear_filters();
uint32_t  can_get_cycle_ave_time_ns();
uint32_t  can_get_cycle_max_time_ns();
void      can_recover_bus_off();

FDCAN_HandleTypeDef *can_get_handle();



/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "settings.h"
#include "system.h"

// The user can define 8 mask filters for 11 bit or 29 bit packets
// The processor allows up to 28 standard filters and up to 8 extended filters.
#define MAX_FILTERS  8

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

typedef struct
{
    FDCAN_HandleTypeDef         handle;
    FDCAN_FilterTypeDef         filters[MAX_FILTERS];
    FDCAN_ProtocolStatusTypeDef cur_status; // current bus status
    can_bitrate_cfg             bitrate_nominal;
    can_bitrate_cfg             bitrate_data;

    bool is_open;
    bool recover_bus_off;
    bool termination_on; 
    bool bitrate_printed_once;
    bool delay_printed_once;

    uint32_t std_filter_count;    // standard filters
    uint32_t ext_filter_count;    // extended filters
    uint32_t bit_count_total;     // for calculation of bus load, total count of all bits of all Rx messages converted to nominal baudrate
    uint32_t nom_bit_len_ns;      // for calculation of bus load, length of one nominal bit in ns (constant)
    uint8_t  old_busload_pct;     // for calculation of bus load, last reported percent value
    uint32_t busload_interval;    // for calculation of bus load, report interval in 100 ms steps
    uint32_t busload_counter;     // for calculation of bus load, incremented every 100 ms until busload_interval is reached
    uint32_t tdc_offset;          // for Transceiver Delay Compensation
    uint32_t last_tx_tick;        // for Transmit Timeout
    int      tx_pending;          // for Transmit Timeout
} can_class;

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
eFeedback can_open(int channel, uint32_t mode);
void      can_close_all();
void      can_close(int channel);
void      can_process(int channel, uint32_t tick_now);
void      can_timer_100ms();
void      can_send_packet(int channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data);
eFeedback can_set_baudrate(int channel, can_nom_bitrate bitrate);
eFeedback can_set_data_baudrate(int channel, can_data_bitrate bitrate);
eFeedback can_set_nom_bit_timing (int channel, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw);
eFeedback can_set_data_bit_timing(int channel, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw);
eFeedback can_enable_busload(int channel, uint32_t interval);
bool      can_set_termination(int channel, bool enable);
bool      can_get_termination(int channel, bool* enabled);
bool      can_is_any_open();
bool      can_is_open(int channel);
bool      can_is_passive(int channel);
bool      can_using_FD(int channel);
bool      can_using_BRS(int channel);
bool      can_is_tx_fifo_free(int channel);
eFeedback can_is_tx_allowed(int channel);
eFeedback can_set_mask_filter(int channel, bool extended, uint32_t filter, uint32_t mask);
eFeedback can_clear_filters(int channel);
void      can_recover_bus_off(int channel);

FDCAN_HandleTypeDef *can_get_handle(int channel);



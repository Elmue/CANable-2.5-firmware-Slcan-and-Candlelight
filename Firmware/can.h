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
// SRAMCAN_FLE_NBR cannot be used here because STM is so STUPID to define it in a *c file instead of a *h file.
#define MAX_HOST_FILTERS    8 

// Bridge filters are not handled in the processor --> no limitation 
#define MAX_BRIDGE_FILTERS  20

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
    bool     enabled;   // active / inactive
    bool     block;     // block  / pass filter
    bool     extended;  // 11 bit / 29 bit ID
    uint8_t  dest_chan; // destination channel for packet forwarding
    uint32_t filter;
    uint32_t mask;     
} brg_filter;

typedef struct
{
    FDCAN_HandleTypeDef         handle;
    FDCAN_FilterTypeDef         host_filters[MAX_HOST_FILTERS];
    FDCAN_ProtocolStatusTypeDef cur_status; // current bus status
    can_bitrate_cfg             bitrate_nominal;
    can_bitrate_cfg             bitrate_data;

    bool is_open;
    bool recover_bus_off;
    bool termination_on; 
    bool bitrate_printed_once;
    bool delay_printed_once;

    uint32_t std_filter_count;    // standard host filters
    uint32_t ext_filter_count;    // extended host filters
    uint32_t bit_count_total;     // for calculation of bus load, total count of all bits of all Rx messages converted to nominal baudrate
    uint32_t nom_bit_len_ns;      // for calculation of bus load, length of one nominal bit in ns (constant)
    uint8_t  old_busload_pct;     // for calculation of bus load, last reported percent value
    uint32_t busload_interval;    // for calculation of bus load, report interval in 100 ms steps
    uint32_t busload_counter;     // for calculation of bus load, incremented every 100 ms until busload_interval is reached
    uint32_t tdc_offset;          // for Transceiver Delay Compensation
    uint32_t last_tx_tick;        // for Transmit Timeout
    int      tx_pending;          // for Transmit Timeout
    
    // ----- Bridge Filters
#if CHANNEL_COUNT > 1
    brg_filter bridge_filters[MAX_BRIDGE_FILTERS];
    bool       bridge_active;    
#endif
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
eFeedback  can_open(uint8_t channel, uint32_t mode);
void       can_close_all();
void       can_close(uint8_t channel);
void       can_process(uint8_t channel, uint32_t tick_now);
void       can_timer_100ms();
void       can_send_packet(uint8_t channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data);
eFeedback  can_set_bit_timing(uint8_t channel, bool set_data, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw);
eFeedback  can_enable_busload(uint8_t channel, uint32_t interval);
bool       can_set_termination(uint8_t channel, bool enable);
bool       can_get_termination(uint8_t channel, bool* enabled);
bool       can_is_any_open();
bool       can_is_open(uint8_t channel);
bool       can_is_passive(uint8_t channel);
bool       can_using_FD(uint8_t channel);
bool       can_using_BRS(uint8_t channel);
bool       can_is_tx_fifo_free(uint8_t channel);
eFeedback  can_is_tx_allowed(uint8_t channel);
eFeedback  can_add_host_filter(uint8_t channel, bool extended, uint32_t filter, uint32_t mask);
eFeedback  can_clear_host_filters(uint8_t channel);
eFeedback  can_set_bridge_filter(uint8_t src_channel, uint8_t dest_channel, uint8_t filter_index, bool enable, bool extended, bool block, uint32_t filter, uint32_t mask);
void       can_recover_bus_off(uint8_t channel);

can_bitrate_cfg*     can_getBitrate(uint8_t channel, bool get_data);
FDCAN_HandleTypeDef* can_get_handle(uint8_t channel);



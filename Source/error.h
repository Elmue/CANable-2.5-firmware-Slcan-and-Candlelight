/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "settings.h"

typedef struct 
{
    eErrorBusStatus  bus_status;     // set by hardware
    eErrorAppFlags   app_flags;      // set by firmware
    uint8_t          last_proto_err; // values = 0...7 FDCAN_PROTOCOL_ERROR_xxx
    uint8_t          tx_err_count;   // Tx Error Count (0 ... 255) >= 96 --> warning, >= 128 --> passive, >= 248 --> bus off
    uint8_t          rx_err_count;   // Rx Error Count (0 ... 255) >= 96 --> warning, >= 128 --> passive, >= 248 --> bus off
    bool             back_to_active; // the bus has returned from a previous Warning, Passive or Off state to Active
} kCanErrorState;

void error_init();
void error_assert(eErrorAppFlags flag, bool report_immediately);
bool error_is_report_due(uint32_t tick_now);
void error_clear();
kCanErrorState* error_get_state();




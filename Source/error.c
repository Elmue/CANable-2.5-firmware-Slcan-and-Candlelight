/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "error.h"
#include "utils.h"
#include "control.h"

extern eUserFlags USER_Flags;

bool           report_now   = false;
uint32_t       last_tick    = 0;
kCanErrorState cur_state    = {0};
kCanErrorState last_state   = {0};

// called from can_open()
void error_init()
{
    memset(&cur_state,  0, sizeof(kCanErrorState));
    memset(&last_state, 0, sizeof(kCanErrorState));
}

// sets an error flag
// use report_immediately only if it is a very important error.
// this is used for Tx buffer full to inform the host without delay that no more Tx packets can be received.
// report_immediately == false --> report in usual intervals of 100 ms or 3 seconds
void error_assert(eErrorAppFlags flag, bool report_immediately)
{
    cur_state.app_flags |= flag;
    if (report_immediately)
        report_now = true;
}

kCanErrorState* error_get_state()
{
    return &cur_state;
}

// return true if the error state should be reported now to the host.
bool error_is_report_due(uint32_t tick_now)
{   
    // user has turned off error reporting (not recommended!)
    if ((USER_Flags & USR_ErrorReport) == 0 || !can_is_opened())
        return false; 
    
    // ----------------
    
    // Refresh bus status and error counters
    FDCAN_ProtocolStatusTypeDef status;
    FDCAN_ErrorCountersTypeDef  counters;
    HAL_FDCAN_GetProtocolStatus(can_get_handle(), &status);
    HAL_FDCAN_GetErrorCounters (can_get_handle(), &counters);

    // error passive or bus off --> turn green + blue LED on permanently
    if (status.Warning)      cur_state.bus_status = BUS_StatusWarning; // MCU register FDCAN_PSR, flag EW (>  96 errors)
    if (status.ErrorPassive) cur_state.bus_status = BUS_StatusPassive; // MCU register FDCAN_PSR, flag EP (> 128 errors)
    if (status.BusOff)       cur_state.bus_status = BUS_StatusOff;     // MCU register FDCAN_PSR, flag BO (> 248 errors)

    // the bus has returned from a previous Warning, Passive or Off state to Active
    if (cur_state.bus_status == BUS_StatusActive && last_state.bus_status != BUS_StatusActive)
        cur_state.back_to_active = true;

    cur_state.tx_err_count = (uint8_t)counters.TxErrorCnt; // MCU register FDCAN_ECR, counter TEC
    cur_state.rx_err_count = (uint8_t)counters.RxErrorCnt; // MCU register FDCAN_ECR, counter REC
    
    // ----------------

    // Set last_proto_err to the very first error that occurred (e.g. No ACK received).
    // This error will be reported once to the host and then cleared. Otherwise it would repeat eternally.
    if (cur_state.last_proto_err == FDCAN_PROTOCOL_ERROR_NONE)
    {
        if (status.DataLastErrorCode != FDCAN_PROTOCOL_ERROR_NONE &&
            status.DataLastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE)
                cur_state.last_proto_err = status.DataLastErrorCode;               
        
        if (status.LastErrorCode != FDCAN_PROTOCOL_ERROR_NONE &&
            status.LastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE)
                cur_state.last_proto_err = status.LastErrorCode;               
    }
   
    // ----------------
            
    // Urgent error Tx buffer overflow --> inform the host immediatley so it stops sending more packets that will be lost.
    // This is relevant for Candlelight which sends over endpoint 02 while Slcan returns feedback FBK_TxBufferFull over CDC.
    if (report_now) 
    {
        report_now = false;
        goto _ReportNow;
    }

    // utils_mem_is_empty(&cur_state) if not a single error is reported
    if (utils_mem_is_empty(&cur_state, sizeof(kCanErrorState)) && !cur_state.back_to_active) 
        return false; // no errors present

    // If the error state changed right now to Bus Off, report this immediately.
    // This error must be reported before debug message "Start recovery from Bus Off"
    if (cur_state .bus_status == BUS_StatusOff &&
        last_state.bus_status != BUS_StatusOff)
        goto _ReportNow;
       
    // Do not flood the user with thousands of errors as the legacy Candlelight firmware did.
    // We do not want to occupy the USB transfer with unnecesaay error messages.
    // Errors are reported at a rate of 100 ms, but only if the error state has changed.
    uint32_t elapsed = tick_now - last_tick;
    if (elapsed < 100)
        return false;
    
    // report a change of error state after 100 ms
    // report also if only the error counters have changed.
    if (memcmp(&cur_state, &last_state, sizeof(kCanErrorState)) != 0)
        goto _ReportNow;

    // If errors are present but the state did not change, report them only every 3 seconds.
    // After a Tx/Rx error the error counters are decreased slowly to zero.
    if (elapsed < 3000)
        return false;

_ReportNow:
    last_tick  = tick_now;
    last_state = cur_state;
    return true;
}

// Clear all errors. If they are still present they will be set again in can_process()
void error_clear()
{
    memset(&cur_state, 0, sizeof(kCanErrorState));
}



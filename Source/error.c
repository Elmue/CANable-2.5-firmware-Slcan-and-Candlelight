/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "error.h"
#include "utils.h"
#include "control.h"

// ----- Globals
extern eUserFlags  GLB_UserFlags[CHANNEL_COUNT];

// ----- Class Instance
err_class   err_inst[CHANNEL_COUNT] = {0};

// called from can_open()
void error_init(int channel)
{
    memset(&err_inst[channel], 0, sizeof(err_class));
}

// sets an error flag
// use report_immediately = true only if it is a very important error.
// this is used for Tx buffer full to inform the host without delay that no more Tx packets can be received.
// report_immediately == false --> report in usual intervals of 100 ms or 3 seconds
void error_assert(int channel, eErrorAppFlags flag, bool report_immediately)
{
    err_class* inst = &err_inst[channel];
    
    inst->cur_state.app_flags |= flag;
    if (report_immediately)
        inst->report_now = true;
}

kCanErrorState* error_get_state(int channel)
{
    return &err_inst[channel].cur_state;
}

// return true if the error state should be reported now to the host.
bool error_is_report_due(int channel, uint32_t tick_now)
{   
    err_class* inst = &err_inst[channel];
    
    // user has turned off error reporting (not recommended!)
    if ((GLB_UserFlags[channel] & USR_ErrorReport) == 0 || !can_is_open(channel))
        return false; 
    
    // ----------------
    
    // Refresh bus status and error counters
    FDCAN_ProtocolStatusTypeDef status;
    FDCAN_ErrorCountersTypeDef  counters;
    HAL_FDCAN_GetProtocolStatus(can_get_handle(channel), &status);
    HAL_FDCAN_GetErrorCounters (can_get_handle(channel), &counters);

    // error passive or bus off --> turn green + blue LED on permanently
    if (status.Warning)      inst->cur_state.bus_status = BUS_StatusWarning; // MCU register FDCAN_PSR, flag EW (>  96 errors)
    if (status.ErrorPassive) inst->cur_state.bus_status = BUS_StatusPassive; // MCU register FDCAN_PSR, flag EP (> 128 errors)
    if (status.BusOff)       inst->cur_state.bus_status = BUS_StatusOff;     // MCU register FDCAN_PSR, flag BO (> 248 errors)

    // the bus has returned from a previous Warning, Passive or Off state to Active
    if (inst->cur_state.bus_status == BUS_StatusActive && inst->last_state.bus_status != BUS_StatusActive)
        inst->cur_state.back_to_active = true;

    inst->cur_state.tx_err_count = (uint8_t)counters.TxErrorCnt; // MCU register FDCAN_ECR, counter TEC
    inst->cur_state.rx_err_count = (uint8_t)counters.RxErrorCnt; // MCU register FDCAN_ECR, counter REC
    
    // ----------------

    // Set last_proto_err to the very first error that occurred (e.g. No ACK received).
    // This error will be reported once to the host and then cleared. Otherwise it would repeat eternally.
    if (inst->cur_state.last_proto_err == FDCAN_PROTOCOL_ERROR_NONE)
    {
        if (status.DataLastErrorCode != FDCAN_PROTOCOL_ERROR_NONE &&
            status.DataLastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE)
                inst->cur_state.last_proto_err = status.DataLastErrorCode;               
        
        if (status.LastErrorCode != FDCAN_PROTOCOL_ERROR_NONE &&
            status.LastErrorCode != FDCAN_PROTOCOL_ERROR_NO_CHANGE)
                inst->cur_state.last_proto_err = status.LastErrorCode;               
    }
   
    // ----------------
            
    // Urgent error Tx buffer overflow --> inform the host immediatley so it stops sending more packets that will be lost.
    // This is relevant for Candlelight which sends over the OUT endpoint while Slcan returns feedback FBK_TxBufferFull over CDC.
    if (inst->report_now) 
    {
        inst->report_now = false;
        goto _ReportNow;
    }

    // utils_mem_is_empty(&cur_state) if not a single error is reported
    if (utils_mem_is_empty(&inst->cur_state, sizeof(kCanErrorState)) && !inst->cur_state.back_to_active) 
        return false; // no errors present

    // If the error state changed right now to Bus Off, report this immediately.
    // This error must be reported before debug message "Start recovery from Bus Off"
    if (inst->cur_state .bus_status == BUS_StatusOff &&
        inst->last_state.bus_status != BUS_StatusOff)
        goto _ReportNow;
       
    // Do not flood the user with thousands of errors as the legacy Candlelight firmware did.
    // We do not want to occupy the USB transfer with unnecesaay error messages.
    // Errors are reported at a rate of 100 ms, but only if the error state has changed.
    uint32_t elapsed = tick_now - inst->last_tick;
    if (elapsed < 100)
        return false;
    
    // report a change of error state after 100 ms
    // report also if only the error counters have changed.
    if (memcmp(&inst->cur_state, &inst->last_state, sizeof(kCanErrorState)) != 0)
        goto _ReportNow;

    // If errors are present but the state did not change, report them only every 3 seconds.
    // After a Tx/Rx error the error counters are decreased slowly to zero.
    if (elapsed < 3000)
        return false;

_ReportNow:
    inst->last_tick  = tick_now;
    inst->last_state = inst->cur_state;
    return true;
}

// Clear all errors. If they are still present they will be set again in can_process()
void error_clear(int channel)
{
    memset(&err_inst[channel].cur_state, 0, sizeof(kCanErrorState));
}



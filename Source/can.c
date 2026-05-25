/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "can.h"
#include "error.h"
#include "led.h"
#include "utils.h"
#include "control.h"
#include "buffer.h"
#include "system.h"

#define SECOND_SAMPL_POINT_PERCENT     50  // Secondary Sample Point at 50% of data bit for TDC compensation
#define CAN_TX_TIMEOUT                500  // after 500 ms cancel pending Tx requests --> clear FIFO and packet buffer

// ----- Globals
eUserFlags           GLB_UserFlags[CHANNEL_COUNT];

// ---- Settings  (from settings.h)
FDCAN_GlobalTypeDef* SET_CanInterfaces[CHANNEL_COUNT] = { CAN_INTERFACES };
GPIO_TypeDef*        SET_CanPorts     [CHANNEL_COUNT] = { CAN_PORTS };
int                  SET_CanPins      [CHANNEL_COUNT] = { CAN_PINS  };
uint8_t              SET_CanAlternates[CHANNEL_COUNT] = { CAN_ALTERNATES };
GPIO_TypeDef*        SET_TermPorts    [CHANNEL_COUNT] = { TERMINATOR_PORTS };
int                  SET_TermPins     [CHANNEL_COUNT] = { TERMINATOR_PINS  };

// ----- Class Instance
can_class            can_inst[CHANNEL_COUNT] = {0};

// ----- Private Methods
void      can_reset(uint8_t channel);
void      can_print_info(uint8_t channel);
bool      can_apply_host_filters(can_class* inst);
uint32_t  can_calc_bit_count_in_frame(can_class* inst, uint32_t DataLength, uint32_t FrameType, uint32_t IdType, uint32_t FDFormat, uint32_t BitRateSwitch);
void      can_forward_bridge_packet(can_class* inst, FDCAN_RxHeaderTypeDef* rx_header, uint8_t* rx_data);

// Initialize CAN peripheral settings, but don't actually start the peripheral
// called from the main loop
void can_init()
{
    __HAL_RCC_FDCAN_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_reset(C);

        if (SET_TermPins[C] >= 0)
        {
            GPIO_InitStruct.Pin       = SET_TermPins[C];
            GPIO_InitStruct.Mode      = TERMINATOR_MODE;
            GPIO_InitStruct.Alternate = 0;
            GPIO_InitStruct.Pull      = GPIO_NOPULL;
            GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
            HAL_GPIO_Init(SET_TermPorts[C], &GPIO_InitStruct);
        }

        GPIO_InitStruct.Pin       = SET_CanPins[C];
        GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;      // AF = alternate function
        GPIO_InitStruct.Alternate = SET_CanAlternates[C]; // switch pin multiplexer to CAN module
        GPIO_InitStruct.Pull      = GPIO_NOPULL;
        GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        HAL_GPIO_Init(SET_CanPorts[C], &GPIO_InitStruct);

        can_inst[C].handle.Instance = SET_CanInterfaces[C];
    }

#if defined(CAN_TRX_ENABLE_PIN)
    // Enable the CAN transceiver chip, if required by the board (WeActStudio v2)
    GPIO_InitStruct.Pin       = CAN_TRX_ENABLE_PIN;
    GPIO_InitStruct.Mode      = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Alternate = 0;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CAN_TRX_ENABLE_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(CAN_TRX_ENABLE_PORT, CAN_TRX_ENABLE_PIN, CAN_TRX_ENABLE_ON);
#endif
}

// called from init() and close() --> reset variable for the next can_open().
// reset only variables here that cannot be reset in can_open()
void can_reset(uint8_t channel)
{
    can_class* inst = &can_inst[channel];

    inst->bitrate_nominal.Brp = 0; // invalid = baudrate not set
    inst->bitrate_data   .Brp = 0;
    inst->std_filter_count    = 0;
    inst->ext_filter_count    = 0;
    inst->busload_interval    = 0;
    inst->tx_pending          = 0;
    inst->is_open             = false;

    // clear all bridge filters
    can_set_bridge_filter(channel, 0, 0xFF, false, false, false, 0, 0);
    
    // this is indispensable here, otherwise Slcan is dead after a Tx buffer overlow and closing the adapter.
    buf_clear_can_buffer(channel);
}

// Start the FDCAN instance
// mode = FDCAN_MODE_NORMAL, FDCAN_MODE_BUS_MONITORING, FDCAN_MODE_INTERNAL_LOOPBACK, FDCAN_MODE_EXTERNAL_LOOPBACK
// ATTENTION: Set all GLB_UserFlags before opening
eFeedback can_open(uint8_t channel, uint32_t mode)
{
    can_class* inst = &can_inst[channel];
    if (inst->is_open)
        return FBK_AdapterMustBeClosed; // already open

    // Nominal baudrate is mandatory
    if (inst->bitrate_nominal.Brp == 0)
        return FBK_BaudrateNotSet;

    if (!can_is_any_open())
    {
        // Reset all CAN instances FDCAN1, FDCAN2, FDCAN3 only if no channel is open
        __HAL_RCC_FDCAN_FORCE_RESET();
        __HAL_RCC_FDCAN_RELEASE_RESET();
    }

    buf_clear_can_buffer(channel);
    error_init(channel);
    
    // stop identify blinking on all channels
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        led_blink_identify(C, false); 
    }

    FDCAN_InitTypeDef* init = &inst->handle.Init;
    init->ClockDivider          = FDCAN_CLOCK_DIV1;
    init->Mode                  = mode;
    init->AutoRetransmission    = (GLB_UserFlags[channel] & USR_Retransmit) ? ENABLE : DISABLE;
    init->TransmitPause         = DISABLE;
    init->ProtocolException     = ENABLE;
    init->TxFifoQueueMode       = FDCAN_TX_FIFO_OPERATION;
    init->StdFiltersNbr         = inst->std_filter_count;
    init->ExtFiltersNbr         = inst->ext_filter_count;

    // ------------------- baudrate ------------------------

    init->FrameFormat           = FDCAN_FRAME_CLASSIC;
    init->NominalPrescaler      = inst->bitrate_nominal.Brp;
    init->NominalTimeSeg1       = inst->bitrate_nominal.Seg1;
    init->NominalTimeSeg2       = inst->bitrate_nominal.Seg2;
    init->NominalSyncJumpWidth  = inst->bitrate_nominal.Sjw;

    // Data baudrate is optional (only required for CAN FD)
    // data baudrate == nominal baudrate --> CAN FD
    // data baudrate  > nominal baudrate --> CAN FD + BRS
    // NOTE:
    // The samplepoint for high data rates is critical.
    // 8 M baud does not work with 75%, but it works with 50%.
    // But strangely 10 M baud works with 50% and with 75% !
    if (can_using_FD(channel))
    {
        init->FrameFormat       = can_using_BRS(channel) ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_FD_NO_BRS;
        init->DataPrescaler     = inst->bitrate_data.Brp;
        init->DataTimeSeg1      = inst->bitrate_data.Seg1;
        init->DataTimeSeg2      = inst->bitrate_data.Seg2;
        init->DataSyncJumpWidth = inst->bitrate_data.Sjw;
    }

    // ------------------ init bus load ------------------------

    // Calculate the length of 1 nominal CAN bus bit in nanoseconds (500 kBaud --> nom_bit_len_ns = 2000)

    uint32_t clock_MHz = system_get_can_clock() / 1000000; // 160 or 64
    if (clock_MHz == 0)
        return FBK_ErrorFromHAL;

    inst->nom_bit_len_ns  = 1 + inst->bitrate_nominal.Seg1 + inst->bitrate_nominal.Seg2; // time quantums
    inst->nom_bit_len_ns *= inst->bitrate_nominal.Brp;     // clock prescaler
    inst->nom_bit_len_ns *= 1000;                          // µs -> ns
    inst->nom_bit_len_ns /= clock_MHz;

    inst->bitrate_printed_once = false;
    inst->delay_printed_once   = false;
    inst->recover_bus_off      = false;

    // ------------------ Init FDCAN ----------------------

    // sets inst->handle.State == HAL_FDCAN_STATE_READY
    if (HAL_FDCAN_Init(&inst->handle) != HAL_OK)
        return FBK_ErrorFromHAL; // error detail in inst->handle.ErrorCode

    // ---------------- Rx/Tx Timestamps ------------------

    if (HAL_FDCAN_ConfigTimestampCounter(&inst->handle, FDCAN_TIMESTAMP_PRESC_1)  != HAL_OK || // use no prescaler
        HAL_FDCAN_EnableTimestampCounter(&inst->handle, FDCAN_TIMESTAMP_EXTERNAL) != HAL_OK || // use timer TIM3 (see system.c)
        HAL_FDCAN_ConfigInterruptLines  (&inst->handle, FDCAN_IT_GROUP_MISC, FDCAN_INTERRUPT_LINE0) != HAL_OK ||
        HAL_FDCAN_ActivateNotification  (&inst->handle, FDCAN_IT_LIST_MISC | FDCAN_IT_TIMESTAMP_WRAPAROUND, 0) != HAL_OK) // wrap callback
        return FBK_ErrorFromHAL; // error detail in inst->handle.ErrorCode

    // ---------------- TDC compensation ------------------

    HAL_FDCAN_DisableTxDelayCompensation(&inst->handle);

    inst->tdc_offset = 0;
    if (can_using_FD(channel))
    {
        // The transceiver Delay Compensation (TDC) compensates for the delay between the CAN Tx pin and the CAN Rx pin of the processor.
        // The processor measures the delay of the transceiver chip while a CAN FD packet with BRS is sent to CAN bus.
        // The secondary samplepoint (SSP) does not need to be the same as the primary samplepoint specified in can_bitrate_data.
        // The SSP is only used to verify that the data bits are sent without error to CAN bus.
        // Here a fix SSP at 50% of the length of a data bit is calculated.
        // The SSP offset is measured in mtq (minimum time quantums = one period of 160 MHz)

        // Calculate the length of one data bit in 'mtq'.
        uint32_t data_bit_len = inst->bitrate_data.Brp * (1 + inst->bitrate_data.Seg1 + inst->bitrate_data.Seg2);

        // Calculate the offset of the SSP in 'mtq' at 50% of the data bit.
        // The secondary samplepoint is not critical. SECOND_SAMPL_POINT_PERCENT = 70% also works.
        // (However the samplepoint defined in can_bitrate_data is critical. See comment above)
        //  1.0 M baud --> offsetSSP = 80 mtq
        //  2.0 M baud --> offsetSSP = 40 mtq
        //  2.5 M baud --> offsetSSP = 32 mtq
        //  4.0 M baud --> offsetSSP = 20 mtq
        //  5.0 M baud --> offsetSSP = 16 mtq
        //  8.0 M baud --> offsetSSP = 10 mtq
        // 10.0 M baud --> offsetSSP =  8 mtq
        uint32_t offsetSSP = data_bit_len * SECOND_SAMPL_POINT_PERCENT / 100;

        // The SSP offset + measured transceiver delay (see calculation in can_process()) must not exceed 127 mtq.
        // If offsetSSP > 64 (== baudrate < 2 Mbaud) --> turn off compensation
        if (offsetSSP > 0 && offsetSSP < 64)
        {
            inst->tdc_offset = offsetSSP;

            // set TDCO and TDCF in register TDCR
            if (HAL_FDCAN_ConfigTxDelayCompensation(&inst->handle, inst->tdc_offset, 0) != HAL_OK) return FBK_ErrorFromHAL;
            // set TDC bit in register DBTP
            if (HAL_FDCAN_EnableTxDelayCompensation(&inst->handle) != HAL_OK) return FBK_ErrorFromHAL; // error detail in inst->handle.ErrorCode
        }
    }

    // -------------------- filters --------------------------

    // Store all user filters in can_filters into the processor's memory
    if (!can_apply_host_filters(inst))
        return FBK_ErrorFromHAL;

    // the user can define up to 8 filters
    bool has_filters = (inst->std_filter_count + inst->ext_filter_count) > 0;

    // If no user filters are defined --> accept all packets in FIFO 0 where they are sent over USB to the host.
    // Otherwise all packets that do not pass the user filters go to FIFO 1 where they only flash the Rx LED.
    uint32_t non_matching = has_filters ? FDCAN_ACCEPT_IN_RX_FIFO1 : FDCAN_ACCEPT_IN_RX_FIFO0;

    HAL_FDCAN_ConfigGlobalFilter(&inst->handle, non_matching, non_matching, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);

    // ----------------------- start ---------------------------

    // sets inst->handle.State == HAL_FDCAN_STATE_BUSY
    if (HAL_FDCAN_Start(&inst->handle) != HAL_OK) return FBK_ErrorFromHAL; // error detail in inst->handle.ErrorCode

    led_turn_TX(channel, false);
    inst->is_open = true;
    return FBK_Success;
}

void can_close_all()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_close(C);
    }
}

// Disable the CAN peripheral and go off-bus
void can_close(uint8_t channel)
{
    can_class* inst = &can_inst[channel];

    // It should not generate an error if the adapter is closed twice
    if (!inst->is_open)
        return;

    HAL_FDCAN_Stop  (&inst->handle);
    HAL_FDCAN_DeInit(&inst->handle);

    // reset all class variables, also is_open
    can_reset(channel);
    led_turn_TX(channel, true);
}

// Called from Buffer. Stores a packet in the Tx FIFO
// Check HAL_FDCAN_GetTxFifoFreeLevel() and can_is_tx_allowed() before calling this function!
void can_send_packet(uint8_t channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data)
{
    can_class* inst = &can_inst[channel];

    // Sending a message with BRS flag, but nominal and data baudrate are the same --> reset flag and send without BRS.
    if (!can_using_BRS(channel))
        tx_header->BitRateSwitch = FDCAN_BRS_OFF;

    HAL_StatusTypeDef status = HAL_FDCAN_AddMessageToTxFifoQ(&inst->handle, tx_header, tx_data);
    if (status != HAL_OK) // may be busy (state = HAL_FDCAN_STATE_BUSY)
    {
        // On error the HAL sets inst->handle.ErrorCode to HAL_FDCAN_ERROR_FIFO_FULL or HAL_FDCAN_ERROR_NOT_STARTED
        // Both errors can never happen, because this function is only called when CAN has been initialized and the FIFO is not full.
        error_assert(channel, APP_CanTxFail, true);
        return;
    }

    if (inst->handle.Init.AutoRetransmission == ENABLE)
    {
        inst->last_tx_tick = HAL_GetTick();
        inst->tx_pending ++;
    }

    // Do not flash the Tx LED here! This was wrong in the legacy Candlelight firmware.
    // The packet has not been sent yet. It is still in the Tx FIFO and will stay there until an ACK is received.
    // When an ACK is received HAL_FDCAN_GetTxEvent() will return the Tx Event and the Tx LED will be flashed.
}

// Process data from CAN tx/rx circular buffers
// This function is called approx 100 times in one millisecond (on STM32G431)
void can_process(uint8_t channel, uint32_t tick_now)
{
    can_class* inst = &can_inst[channel];
    if (!inst->is_open)
        return;

    uint8_t can_data_buf[64] = {0};
    char    dbg_msg_buf[100];

    // -------------------------- Tx Event ------------------------------------

    // This was competely wrong in the original Candlelight firmware (fixed by Elmüsoft).
    // Instead of sending a Tx Event to the host in the moment when the processor has really sent the packet to the CAN bus
    // they have sent a fake event immediately after dispatching the packet, no matter if it really was sent or not.
    FDCAN_TxEventFifoTypeDef tx_event;
    if (HAL_FDCAN_GetTxEvent(&inst->handle, &tx_event) == HAL_OK)
    {
        // Here tx_event.EventType is FDCAN_TX_EVENT if auto retransmission is enabled.
        // Here tx_event.EventType is FDCAN_TX_IN_SPITE_OF_ABORT if auto retransmission is disabled.
        // "In DAR mode (Disable Auto Retransmission) all transmissions are automatically canceled after
        // they have been started on the CAN bus." (see "STM32G4 Series - Chapter FDCAN.pdf" in subfolder "Documentation")
        // A marker of zero must not send an echo to the host! (forwarded bridge packets)
        if ((GLB_UserFlags[channel] & USR_ReportTX) && tx_event.MessageMarker > 0)
        {
            // convert 16 bit timestamp --> 32 bit
            tx_event.TxTimestamp = (system_get_timewrap() << 16) | tx_event.TxTimestamp;
            buf_store_tx_echo(channel, &tx_event);
        }

        // In loopback mode do not count the same packet twice (Tx == Rx at the same time without delay)
        // In bus montoring mode and restricted mode sending packets is not possible.
        if (inst->handle.Init.Mode == FDCAN_MODE_NORMAL)
        {
            // for bus load calculation
            inst->bit_count_total += can_calc_bit_count_in_frame(inst, tx_event.DataLength, tx_event.TxFrameType, tx_event.IdType, tx_event.FDFormat, tx_event.BitRateSwitch);
        }

        if (inst->tx_pending > 0)
        {
            inst->last_tx_tick = tick_now;
            inst->tx_pending --;
        }
        led_flash_TX(channel); // flash 15 ms
    }

    // -------------------------- Rx Packet ------------------------------------

    // Rx FIFO 0 receives all packets that have been accepted by the filters -> write to the USB buffer
    // Rx FIFO 0 and Rx FIFO 1 can store up to three packets each.
    FDCAN_RxHeaderTypeDef rx_header;
    if (HAL_FDCAN_GetRxMessage(&inst->handle, FDCAN_RX_FIFO0, &rx_header, can_data_buf) == HAL_OK)
    {
        // convert 16 bit timestamp --> 32 bit
        rx_header.RxTimestamp = (system_get_timewrap() << 16) | rx_header.RxTimestamp;
        buf_store_rx_packet(channel, &rx_header, can_data_buf);

#if CHANNEL_COUNT > 1
        if (inst->bridge_active)
            can_forward_bridge_packet(inst, &rx_header, can_data_buf);
#endif
        // for bus load calculation
        inst->bit_count_total += can_calc_bit_count_in_frame(inst, rx_header.DataLength, rx_header.RxFrameType, rx_header.IdType, rx_header.FDFormat, rx_header.BitRateSwitch);

        led_flash_RX(channel); // flash 15 ms
    }

    // Rx FIFO 1 receives all packets that have been rejected by the filters -> only flash the Rx LED
    // Rx FIFO 0 and Rx FIFO 1 can store up to three packets each.
    if (HAL_FDCAN_GetRxMessage(&inst->handle, FDCAN_RX_FIFO1, &rx_header, can_data_buf) == HAL_OK)
    {
#if CHANNEL_COUNT > 1
        if (inst->bridge_active)
            can_forward_bridge_packet(inst, &rx_header, can_data_buf);
#endif
        // for bus load calculation
        inst->bit_count_total += can_calc_bit_count_in_frame(inst, rx_header.DataLength, rx_header.RxFrameType, rx_header.IdType, rx_header.FDFormat, rx_header.BitRateSwitch);

        led_flash_RX(channel); // flash 15 ms
    }

    // -------------------------- Rx / Tx Errors ------------------------------------

    // Tx Event FIFO packet lost
    if (__HAL_FDCAN_GET_FLAG(&inst->handle, FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST))
    {
        error_assert(channel, APP_CanTxFail, false);
        __HAL_FDCAN_CLEAR_FLAG(&inst->handle, FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST);
    }

    // Rx FIFO 0 packet lost
    if (__HAL_FDCAN_GET_FLAG(&inst->handle, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST))
    {
        error_assert(channel, APP_CanRxFail, false);
        __HAL_FDCAN_CLEAR_FLAG(&inst->handle, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST);
    }

    // Rx FIFO 1 packet lost
    if (__HAL_FDCAN_GET_FLAG(&inst->handle, FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST))
    {
        error_assert(channel, APP_CanRxFail, false);
        __HAL_FDCAN_CLEAR_FLAG(&inst->handle, FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST);
    }

    // ------------------------- Refresh Bus Status ---------------------------------

    HAL_FDCAN_GetProtocolStatus(&inst->handle, &inst->cur_status);

    // ----------------------------- Transmit Timeout -----------------------------

    // If a message hangs longer than a few milliseconds in the Tx FIFO this means that it was not acknowledged.
    // The processor continues to send the message !!ETERNALLY!! producing a bus load of 95%.
    // Tx requests must be canceled by firmware to free CAN bus from the congestion.
    // the processor will never stop alone sending the same packet over and over again.
    if (inst->tx_pending > 0 && tick_now >= inst->last_tx_tick + CAN_TX_TIMEOUT)
    {
        inst->tx_pending = 0;
        HAL_FDCAN_AbortTxRequest(&inst->handle, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2);
        buf_clear_can_buffer(channel);
        error_assert(channel, APP_CanTxTimeout, false);
    }

    // ------------------------ print bitrate ---------------------------------

    // Important: This function must be called from the main loop, not from can_open()
    // otherwise the debug message is sent to the host before the response to command Open ("O") has been set.
    if (!inst->bitrate_printed_once && inst->is_open)
    {
        inst->bitrate_printed_once = true;
        can_print_info(channel);
    }

    // ------------------- print transceiver delay ----------------------------

    // The measurement of the transceiver delay is available after the first CAN FD message with BRS has been sent.
    // HAL_FDCAN_EnableTxDelayCompensation() must have been called during intialization to enable TDC.
    // The processor measures the delay through the transceiver chip, adds the TDC offset and stores it in TDCvalue.
    // It measures the delay from the dominant (falling) edge of the FDF bit between it's CAN Tx pin to the CAN Rx pin.
    // The unit of TDCvalue is mtq (minimum time quantums = one period of 160 MHz)
    // See page 26 in "ST - AN5348 - FDCAN Peripheral.pdf" in subfolder "Documentation".
    // If TDCvalue == 127 it is at the upper limit and must be ignored to avoid wrong results (this happens with 1M baud).
    // For the transceiver chip ADM3050E in the isolated CANable from MKS Makerbase the measured delay is 21 mtq = 131 ns.
    // The datasheet says maximum propagation delay TXD to RXD is 150 ns.
    // The ADM3050E supports up to 12 Mbit and works well even with 10 Mbit.
    if (!inst->delay_printed_once && inst->tdc_offset > 0 && inst->cur_status.TDCvalue > inst->tdc_offset && inst->cur_status.TDCvalue < 127)
    {
        inst->delay_printed_once = true;
        uint32_t clock_MHz = system_get_can_clock() / 1000000; // 160 or 64
        if (clock_MHz > 0)
        {
            uint32_t chip_delay = inst->cur_status.TDCvalue - inst->tdc_offset;

            // chip_delay = 21 mtq --> 21 * 1000 / 160 = 131 ns
            sprintf(dbg_msg_buf, "Measured transceiver chip delay: %lu ns", chip_delay * 1000 / clock_MHz);
            control_send_debug_mesg(channel, dbg_msg_buf);
        }
    }
}

// ATTENTION:
// The state BusOff (after 248 Tx errors) is a fatal situation where the CAN module is completely blocked.
// No further transmit operations are possible.
// And the FDCAN module will never recover alone from this situation.
// If the adapter is once in Bus Off state it will hang there eternally.
// The recovery process may take up to 200 ms for low baudrates (10 kBaud).
// The adapter easily goes into bus off state if you try to communicate between 2 adapters with a different baudrate.
// This function must be called after reporting BusOff to the host.
void can_recover_bus_off(uint8_t channel)
{
    can_class* inst = &can_inst[channel];
    if (!inst->is_open)
        return;

    if (inst->cur_status.BusOff)
    {
        if (!inst->recover_bus_off)
        {
            inst->recover_bus_off = true;
            control_send_debug_mesg(channel, ">> Start recovery from Bus Off");

            HAL_FDCAN_AbortTxRequest(&inst->handle, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2);
            HAL_FDCAN_Stop (&inst->handle);
            HAL_FDCAN_Start(&inst->handle);
        }
    }
    else
    {
        if (inst->recover_bus_off)
        {
            inst->recover_bus_off = false;
            control_send_debug_mesg(channel, "<< Successfully recovered from Bus Off");

            // Clear errors that are still stored in the error handler, but that are outdated now.
            error_clear(channel);
        }
    }
}

// Called every 100 ms from the main loop
// Calculate bus load, written (and hopefully tested well) by Nakanishi Kiyomaro.
void can_timer_100ms()
{
    // The bus load calculation is not exact because dynamic bit stuffing is not calculated in can_calc_bit_count_in_frame().
    // 1.) In a classic CAN frame one dynamic Stuff Bit (SB) is inserted after five equal payload bits.
    // 2.) In the CRC phase of a CAN FD frame there are Fix Stuff Bits (FSB) after every 4 payload bits.
    // Study the oscilloscope captures of https://netcult.ch/elmue/oszi-Waveform-Analyzer
    // STUFFING_FACTOR adds 12.5% so the bus load is the same as on the oscilloscope.
    const uint32_t STUFFING_FACTOR = 1125;

    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        can_class* inst = &can_inst[C];
        if (!inst->is_open || inst->busload_interval == 0)
            continue;

        if (inst->busload_counter >= inst->busload_interval) // interval elapsed
        {
            // This function is called every 100 ms = 100 * 1000 µs --> divide by 100000
            uint32_t rate_us_ppm = inst->bit_count_total * inst->nom_bit_len_ns / 100000;
            uint32_t busload_ppm = rate_us_ppm * STUFFING_FACTOR / inst->busload_interval;

            // divide by 1000 to remove stuff factor, which is multiplied with 1000, and divide by 10 to convert 1000 into 100%
            uint8_t  new_busload = MIN(99, busload_ppm / 10000);

            // Suppress report of "Bus load = 0%" eternally
            if (new_busload > 0 || inst->old_busload_pct > 0)
                control_report_busload(C, new_busload); // send busload report to the host

            inst->old_busload_pct = new_busload;
            inst->bit_count_total = 0;
            inst->busload_counter = 0;
        }

        inst->busload_counter ++;
    }
}

// ----------------------------------------------------------------------------------------------

// set_data = false -> Set the nominal bitrate configuration of the CAN peripheral
// set_data = true  -> Set the data    bitrate configuration of the CAN peripheral
// If all 4 nominal values are identical to the data values, CAN FD is enabled and packets up to 64 byte can be sent without BRS.
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_bit_timing(uint8_t channel, bool set_data, uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    can_class* inst = &can_inst[channel];
    if (inst->is_open)
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    if (set_data)
    {
        if (!IS_FDCAN_DATA_PRESCALER(BRP)  ||
            !IS_FDCAN_DATA_TSEG1    (Seg1) ||
            !IS_FDCAN_DATA_TSEG2    (Seg2) ||
            !IS_FDCAN_DATA_SJW      (Sjw))
                return FBK_ParamOutOfRange;
    }
    else // nominal
    {
        if (!IS_FDCAN_NOMINAL_PRESCALER(BRP)  ||
            !IS_FDCAN_NOMINAL_TSEG1    (Seg1) ||
            !IS_FDCAN_NOMINAL_TSEG2    (Seg2) ||
            !IS_FDCAN_NOMINAL_SJW      (Sjw))
                return FBK_ParamOutOfRange;
    }

    can_bitrate_cfg* bitrate = set_data ? &inst->bitrate_data : &inst->bitrate_nominal;

    bitrate->Brp  = BRP;
    bitrate->Seg1 = Seg1;
    bitrate->Seg2 = Seg2;
    bitrate->Sjw  = Sjw;

    if (can_calc_baud(bitrate) > MAX_CAN_BAUDRATE * 1000000)
        return FBK_ParamOutOfRange;

    return FBK_Success;
}

// Prints a string containing the baudrate and samplepoint to the debug output.
// Although this is very simple many application and firmware developers do this calculation wrong. (see sourcecode of Cangaroo)
// It is very important to verify that baudrate and samplepoint have been set correctly by the user.
// Therefore this firmware prints them as a debug message so the user can verify the settings.
// Additionally this function can print the TDC configuration and if the pin BOOT0 is enabled.
// This function is only called if CAN is opened.
void can_print_info(uint8_t channel)
{
    if ((GLB_UserFlags[channel] & USR_DebugReport) == 0)
        return;

    can_class* inst = &can_inst[channel];

    char buf[200];
    if (inst->handle.Init.Mode != FDCAN_MODE_NORMAL)
    {
        char* mode = "Invalid";
        switch (inst->handle.Init.Mode)
        {
            case FDCAN_MODE_RESTRICTED_OPERATION: mode = "Restricted";        break;
            case FDCAN_MODE_BUS_MONITORING:       mode = "Monitoring";        break;
            case FDCAN_MODE_INTERNAL_LOOPBACK:    mode = "Internal Loopback"; break;
            case FDCAN_MODE_EXTERNAL_LOOPBACK:    mode = "External Loopback"; break;
        }
        sprintf(buf, "Operation Mode: %s", mode);
        control_send_debug_mesg(channel, buf);
    }

    // print "Nominal: 500k baud, 87.5%; Data: 2M baud, 75.0%; Perfect match: Yes"
    utils_format_bitrate(buf,               "Nominal", &inst->bitrate_nominal);
    utils_format_bitrate(buf + strlen(buf), "; Data",  &inst->bitrate_data);

    if (can_using_FD(channel))
    {
        // See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
        strcat(buf, "; Perfect match: ");
        strcat(buf, inst->bitrate_nominal.Brp == inst->bitrate_data.Brp ? "Yes" : "No");
    }
    control_send_debug_mesg(channel, buf);

    // ------------------- Debugging --------------------
    // Optional additional information: pin BOOT0 and TDC offset

#if 0
    // Print Transceiver Delay Compensation
    if (inst->tdc_offset > 0)
        sprintf(buf, "TDC offset: %lu mtq", inst->tdc_offset);
    else
        strcpy(buf, "TDC: not used");

    // Print enable status of pin BOOT0
    const char* pin_status;
    switch (system_is_option_enabled(OPT_BOOT0_Enable))
    {
        case Option_Active:   pin_status = "enabled";       break;
        case Option_Inactive: pin_status = "disabled";      break;
        default:              pin_status = "not supported"; break;
    }

    strcat(buf, ", Pin BOOT0: ");
    strcat(buf, pin_status);

    control_send_debug_mesg(channel, buf);
#endif
}

// ------------------------------------- HOST FILTER -------------------------------------------

// Set filters for Rx packets to be sent to the host over USB.
// The G0 and G4 series allow up to 28 standard filters and up to 8 extended filters.
// Nobody needs so many filters -> allow 8 user filters.
// Rx FIFO 0 receives all packets that pass. They are sent to the host application over USB.
// Rx FIFO 1 receives all packets that are rejected, they only flash the Rx LED.
// Each FIFO can store 3 Rx packets before it is full.
// ---------------------------------------------------------
// While all industry CAN bus adapters allow to set filters after opening the adapter, the STM32 processor is very restricted.
// The values inst->handle.Init.StdFiltersNbr and ExtFiltersNbr cannot be modified anymore after opening the adapter.
// But HAL_FDCAN_ConfigFilter() can be called after opening the adapter.
// So the only possible filter modification after opening the adapter is to modify ONE existing filter.
// The filter type must be the same (11 bit or 29 bit).
eFeedback can_add_host_filter(uint8_t channel, bool extended, uint32_t filter, uint32_t mask)
{
    can_class* inst = &can_inst[channel];

    int tot_filters = inst->std_filter_count + inst->ext_filter_count;
    if (tot_filters >= MAX_HOST_FILTERS)
        return FBK_ParamOutOfRange;

    uint32_t maximum = extended ? 0x1FFFFFFF : 0x7FF;
    if (filter > maximum || mask > maximum)
        return FBK_ParamOutOfRange;

    if (inst->is_open)
    {
        // only one existing filter can be modified if the adapter is already open
        if (tot_filters != 1)
            return FBK_AdapterMustBeClosed;

        // the filter to be modified must be from the same type
        if (extended != (inst->ext_filter_count == 1))
            return FBK_AdapterMustBeClosed;

        // modify the one and only filter at index 0
        inst->ext_filter_count = 0;
        inst->std_filter_count = 0;
        tot_filters = 0;
    }

    FDCAN_FilterTypeDef* last_filter = &inst->host_filters[tot_filters];
    last_filter->IdType       = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    last_filter->FilterIndex  = extended ? inst->ext_filter_count : inst->std_filter_count;
    last_filter->FilterType   = FDCAN_FILTER_MASK;
    last_filter->FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    last_filter->FilterID1    = filter;
    last_filter->FilterID2    = mask;

    if (extended) inst->ext_filter_count ++;
    else          inst->std_filter_count ++;

    if (inst->is_open && !can_apply_host_filters(inst))
        return FBK_ErrorFromHAL;

    return FBK_Success;
}

// Store all user filters in can_filters into the processor's memory
bool can_apply_host_filters(can_class* inst)
{
    // the user can define up to 8 filters
    int tot_filters = inst->std_filter_count + inst->ext_filter_count;
    for (int i=0; i<tot_filters; i++)
    {
        if (HAL_FDCAN_ConfigFilter(&inst->handle, &inst->host_filters[i]) != HAL_OK)
            return false; // error detail in inst->handle.ErrorCode
    }
    return true;
}

// clear all host filters (adapter must be open)
eFeedback can_clear_host_filters(uint8_t channel)
{
    can_class* inst = &can_inst[channel];

    if (inst->is_open)
        return FBK_AdapterMustBeClosed; // cannot clear filters while on bus

    inst->ext_filter_count = 0;
    inst->std_filter_count = 0;
    return FBK_Success;
}

// -------------------------------------- BRIDGE FILTER ------------------------------------------

// Set or remove a specific bridge filter for Rx packets to be forwarded from src_channel to dest_channel.
// This function can also be called after the adapter has been opened.
// All filters can be set / cleared individually by their index.
// To clear all filters at once set enable= false and filter_index = 0xFF.
eFeedback can_set_bridge_filter(uint8_t src_channel, uint8_t dest_channel, uint8_t filter_index,
                                bool enable, bool extended, bool block, uint32_t filter, uint32_t mask)
{
#if CHANNEL_COUNT > 1
    can_class* inst = &can_inst[src_channel];
    
    if (enable) // set filter
    {
        if (dest_channel == src_channel || dest_channel >= CHANNEL_COUNT)
            return FBK_ParamOutOfRange;
        
        uint32_t maximum = extended ? 0x1FFFFFFF : 0x7FF;
        if (filter > maximum || mask > maximum)
            return FBK_ParamOutOfRange;
    }
    else // clear filter
    {
        // Index == 0xFF --> clear all filters
        if (filter_index == 0xFF)
        {
            inst->bridge_active = false;
            memset(&inst->bridge_filters, 0, sizeof(inst->bridge_filters));
            return FBK_Success;
        }
    }

    if (filter_index >= MAX_BRIDGE_FILTERS)
        return FBK_ParamOutOfRange;

    brg_filter* cur_filter = &inst->bridge_filters[filter_index];
    cur_filter->enabled   = enable;
    cur_filter->extended  = extended;
    cur_filter->block     = block;
    cur_filter->dest_chan = dest_channel;
    cur_filter->filter    = filter & mask;
    cur_filter->mask      = mask;

    bool active = false;
    for (int F=0; F<MAX_BRIDGE_FILTERS; F++)
    {
        if (inst->bridge_filters[F].enabled && !inst->bridge_filters[F].block)
        {
            active = true;
            break;
        }
    }
    
    // The variable bridge_active is only for speed optimization if bridge mode is not used.    
    inst->bridge_active = active;
    return FBK_Success;
#else
    return FBK_UnsupportedFeature; // not a multi-channel adapter
#endif
}

#if CHANNEL_COUNT > 1
// Forward a Rx packet to the channel(s) that are defined in the bridge filter(s)
void can_forward_bridge_packet(can_class* inst, FDCAN_RxHeaderTypeDef* rx_header, uint8_t* rx_data)
{
    bool extended = rx_header->IdType == FDCAN_EXTENDED_ID;
    uint32_t ID   = rx_header->Identifier;

    bool pass_chan [CHANNEL_COUNT] = {0};
    bool block_chan[CHANNEL_COUNT] = {0};

    for (int i=0; i<MAX_BRIDGE_FILTERS; i++)
    {
        brg_filter* cur_filter = &inst->bridge_filters[i];

        if (cur_filter->enabled &&
            cur_filter->extended   == extended &&
           (ID & cur_filter->mask) == cur_filter->filter)
        {
            if (cur_filter->block) block_chan[cur_filter->dest_chan] = true;
            else                   pass_chan [cur_filter->dest_chan] = true;
        }
    }

    FDCAN_TxHeaderTypeDef tx_header;
    tx_header.Identifier          = rx_header->Identifier;
    tx_header.IdType              = rx_header->IdType;
    tx_header.TxFrameType         = rx_header->RxFrameType;
    tx_header.DataLength          = rx_header->DataLength;
    tx_header.ErrorStateIndicator = rx_header->ErrorStateIndicator;
    tx_header.TxEventFifoControl  = FDCAN_STORE_TX_EVENTS; // always! Tx Event flashes the Tx LED
    tx_header.MessageMarker       = 0;                     // no Tx echo for forwarded packets possible

    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        if (!pass_chan[C] || block_chan[C] || !can_is_open(C))
            continue;
        
        if (can_using_FD(C))
        {
            tx_header.FDFormat      = rx_header->FDFormat;            
            tx_header.BitRateSwitch = rx_header->BitRateSwitch;
        }
        else
        {
            // a packet with more than 8 data bytes cannot be forwarded to a classic CAN bus
            if (tx_header.DataLength > 8)
                continue;
            
            // convert FD packet into classic packet
            tx_header.FDFormat      = FDCAN_CLASSIC_CAN;            
            tx_header.BitRateSwitch = FDCAN_BRS_OFF;
        }
        
        buf_store_tx_packet(C, &tx_header, rx_data);
    }
}
#endif

// ----------------------------------------------------------------------------------------------

// Some boards have a 120 Ohm termination resistor that can be enabled by a GPIO pin.
// returns false if the board does not support this feature
bool can_set_termination(uint8_t channel, bool enable)
{
    if (SET_TermPins[channel] <= 0)
        return false; // --> return FBK_UnsupportedFeature to host

    HAL_GPIO_WritePin(SET_TermPorts[channel], SET_TermPins[channel], enable ? TERMINATOR_ON : TERMINATOR_OFF);
    can_inst[channel].termination_on = enable;
    return true;
}

bool can_get_termination(uint8_t channel, bool* enabled)
{
    if (SET_TermPins[channel] <= 0)
        return false; // --> return FBK_UnsupportedFeature to host

    *enabled = can_inst[channel].termination_on;
    return true;
}

// ----------------------------------------------------------------------------------------------

// interval =   0 --> disable busload report
// interval =   1 --> report busload every 100 ms     (minimum)
// interval =   7 --> report busload every 700 ms
// interval = 100 --> report busload every 10 seconds (maximum)
eFeedback can_enable_busload(uint8_t channel, uint32_t interval)
{
    if (interval > 100)
        return FBK_ParamOutOfRange;

    can_inst[channel].busload_interval = interval;
    return FBK_Success;
}

// ----------------------------------------------------------------------------------------------

// check if any channel is open
bool can_is_any_open()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        if (can_inst[C].is_open)
            return true;
    }
    return false;
}

// Return bus status
bool can_is_open(uint8_t channel)
{
    return can_inst[channel].is_open;
}

// > 128 errors have occurred
bool can_is_passive(uint8_t channel)
{
    return can_inst[channel].cur_status.ErrorPassive;
}

// true if data baudrate has been set, otherwise CAN classic
bool can_using_FD(uint8_t channel)
{
    return can_inst[channel].bitrate_data.Brp > 0;
}

// true if data bitrate greater than nominal bitrate
bool can_using_BRS(uint8_t channel)
{
    can_class* inst = &can_inst[channel];
    return can_calc_baud(&inst->bitrate_data) > can_calc_baud(&inst->bitrate_nominal);
}

eFeedback can_is_tx_allowed(uint8_t channel)
{
    can_class* inst = &can_inst[channel];

    if (!inst->is_open)
        return FBK_AdapterMustBeOpen;
    if (inst->handle.Init.Mode == FDCAN_MODE_BUS_MONITORING)
        return FBK_NoTxInSilentMode;
    if (inst->cur_status.BusOff)
        return FBK_BusIsOff;
    return FBK_Success;
}

// Check if at least one Tx FIFO buffer is available
bool can_is_tx_fifo_free(uint8_t channel)
{
    return HAL_FDCAN_GetTxFifoFreeLevel(&can_inst[channel].handle) > 0;
}

// Return reference to CAN handle
FDCAN_HandleTypeDef *can_get_handle(uint8_t channel)
{
    return &can_inst[channel].handle;
}

// get the nominal or data bitrate
can_bitrate_cfg* can_getBitrate(uint8_t channel, bool get_data)
{
    can_class* inst = &can_inst[channel];
    if (get_data) return &inst->bitrate_data;
    else          return &inst->bitrate_nominal;
}

// ---------------------------------------------------------------------------------------------------

// Calculate the bit count of a CAN frame. Data bits with BRS are converted to nominal baudrate.
// This code was written by Nakanishi Kiyomaro and was hopefully tested well.
uint32_t can_calc_bit_count_in_frame(can_class* inst, uint32_t DataLength,
                                                      uint32_t FrameType,
                                                      uint32_t IdType,
                                                      uint32_t FDFormat,
                                                      uint32_t BitRateSwitch)
{
    if (inst->busload_interval == 0)
        return 0;

    // Bit number for each frame type with zero data length
    const uint32_t CAN_BIT_NBR_WOD_CBFF        = 47;
    const uint32_t CAN_BIT_NBR_WOD_CEFF        = 67;
    const uint32_t CAN_BIT_NBR_WOD_FBFF_ARBIT  = 30;
    const uint32_t CAN_BIT_NBR_WOD_FEFF_ARBIT  = 49;
    const uint32_t CAN_BIT_NBR_WOD_FXFF_DATA_S = 26;
    const uint32_t CAN_BIT_NBR_WOD_FXFF_DATA_L = 30;

    uint32_t byte_count = utils_dlc_to_byte_count(DataLength);

    uint32_t bit_count;
    if (FrameType == FDCAN_REMOTE_FRAME && IdType == FDCAN_STANDARD_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CBFF;
    }
    else if (FrameType == FDCAN_REMOTE_FRAME && IdType == FDCAN_EXTENDED_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CEFF;
    }
    else if (FDFormat == FDCAN_CLASSIC_CAN && IdType == FDCAN_STANDARD_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CBFF + byte_count * 8;
    }
    else if (FDFormat == FDCAN_CLASSIC_CAN && IdType == FDCAN_EXTENDED_ID)
    {
        bit_count = CAN_BIT_NBR_WOD_CEFF + byte_count * 8;
    }
    else // for FD frames
    {
        if (IdType == FDCAN_STANDARD_ID) bit_count = CAN_BIT_NBR_WOD_FBFF_ARBIT;
        else                             bit_count = CAN_BIT_NBR_WOD_FEFF_ARBIT;

        // calculate all bits separately that are affected by the bitrate switching (data + CRC)
        uint32_t brs_bits = byte_count * 8;
        if (byte_count <= 16)
            brs_bits += CAN_BIT_NBR_WOD_FXFF_DATA_S; // Short CRC
        else
            brs_bits += CAN_BIT_NBR_WOD_FXFF_DATA_L; // Long CRC

        // Convert data bit time to nominal bit time
        if (BitRateSwitch == FDCAN_BRS_ON)
        {
            // As double arithmetic is not available here --> calculate with integers multiplied with 1 million
            uint32_t factor = 1 + inst->bitrate_data.Seg1 + inst->bitrate_data.Seg2; // Tq in one bit (data)
            factor *= inst->bitrate_data.Brp;
            factor *= 1000000;
            factor /= 1 + inst->bitrate_nominal.Seg1 + inst->bitrate_nominal.Seg2;   // Tq in one bit (nominal)
            factor /= inst->bitrate_nominal.Brp;

            bit_count += ((uint32_t)brs_bits * factor) / 1000000;
        }
        else // BRS = off
        {
            bit_count += brs_bits;
        }
    }
    return bit_count;
}


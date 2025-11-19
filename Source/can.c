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

// Bit number for each frame type with zero data length
#define CAN_BIT_NBR_WOD_CBFF            47
#define CAN_BIT_NBR_WOD_CEFF            67
#define CAN_BIT_NBR_WOD_FBFF_ARBIT      30
#define CAN_BIT_NBR_WOD_FEFF_ARBIT      49
#define CAN_BIT_NBR_WOD_FXFF_DATA_S     26
#define CAN_BIT_NBR_WOD_FXFF_DATA_L     30

// The user can define 8 mask filters for 11 bit or 29 bit packets
// The processor allows up to 28 standard filters and up to 8 extended filters.
#define MAX_FILTERS                     8
#define SECOND_SAMPL_POINT_PERCENT     50  // Secondary Sample Point at 50% of data bit for TDC compensation
#define CAN_TX_TIMEOUT                500  // after 500 ms cancel pending Tx requests --> clear FIFO and packet buffer

// global variable, used in several places
eUserFlags USER_Flags;

// Private variables
FDCAN_HandleTypeDef         can_handle;
FDCAN_FilterTypeDef         can_filters[MAX_FILTERS];
FDCAN_ProtocolStatusTypeDef cur_status; // current bus status

uint32_t std_filter_count = 0;
uint32_t ext_filter_count = 0;
uint32_t last_tx_tick     = 0;
int      tx_pending       = 0;

can_bitrate_cfg can_bitrate_nominal;
can_bitrate_cfg can_bitrate_data;

bool can_is_open           = false;
bool recover_bus_off       = false;
bool termination_on        = false; // switch 120 Ohm resistor
bool print_bitrate_once    = true;
bool print_chip_delay_once = true;

uint32_t bit_cnt_message     = 0;
uint32_t cycle_max_time_ns   = 0;
uint32_t cycle_ave_time_ns   = 0;
uint32_t nom_bit_len_ns      = 0; // for calculation of bus load
uint32_t busload_ppm         = 0;
uint8_t  old_busload_percent = 0;
uint32_t busload_interval    = 0;
uint32_t busload_counter     = 0;
uint32_t tdc_offset          = 0;

// Private methods
void      can_reset();
bool      can_apply_filters();
uint16_t  can_calc_bit_count_in_frame(FDCAN_RxHeaderTypeDef *header);

// Initialize CAN peripheral settings, but don't actually start the peripheral
void can_init()
{
    __HAL_RCC_FDCAN_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    if (ISOLATOR_PWR_Pin > 0)
    {
        GPIO_InitStruct.Pin   = ISOLATOR_PWR_Pin;
        GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull  = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(ISOLATOR_PWR_Port, &GPIO_InitStruct);
    }
    if (TERMINATOR_Pin > 0)
    {
        GPIO_InitStruct.Pin   = TERMINATOR_Pin;
        GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
        GPIO_InitStruct.Pull  = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(TERMINATOR_Port, &GPIO_InitStruct);
    }

    // PB8 = CAN_RX   these seem to be the same pins for all processor models
    // PB9 = CAN_TX
    GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;           // AF = alternate function
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    can_reset();
    can_handle.Instance = CAN_INTERFACE; // see settings.h
}

// called from init() and close() --> reset variable for the next can_open().
// reset only variables here that cannot be reset in can_open()
void can_reset()
{
    can_bitrate_nominal.Brp = 0; // invalid = baudrate not set
    can_bitrate_data   .Brp = 0;

    std_filter_count = 0;
    ext_filter_count = 0;
    busload_interval = 0;
    tx_pending       = 0;
    can_is_open      = false;

    // this is indispensable here, otherwise Slcan is dead after a Tx buffer overlow and closing the adapter.
    buf_clear_can_buffer();

    // turn off power supply of isolator chip
    if (ISOLATOR_PWR_Pin > 0)
        HAL_GPIO_WritePin(ISOLATOR_PWR_Port, ISOLATOR_PWR_Pin, ISOLATOR_OFF);
}

// Start the FDCAN module
// mode = FDCAN_MODE_NORMAL, FDCAN_MODE_BUS_MONITORING, FDCAN_MODE_INTERNAL_LOOPBACK, FDCAN_MODE_EXTERNAL_LOOPBACK
// ATTENTION: Set all USER_Flags before opening
eFeedback can_open(uint32_t mode)
{
    if (can_is_open)
        return FBK_AdapterMustBeClosed; // already open

    // Nominal baudrate is mandatory
    if (can_bitrate_nominal.Brp == 0)
        return FBK_BaudrateNotSet;

    // Reset error counter etc.
    __HAL_RCC_FDCAN_FORCE_RESET();
    __HAL_RCC_FDCAN_RELEASE_RESET();

    buf_clear_can_buffer();
    error_init();

    can_handle.Init.ClockDivider          = FDCAN_CLOCK_DIV1;
    can_handle.Init.Mode                  = mode;
    can_handle.Init.AutoRetransmission    = (USER_Flags & USR_Retransmit) ? ENABLE : DISABLE;
    can_handle.Init.TransmitPause         = DISABLE;
    can_handle.Init.ProtocolException     = ENABLE;
    can_handle.Init.TxFifoQueueMode       = FDCAN_TX_FIFO_OPERATION;
    can_handle.Init.StdFiltersNbr         = std_filter_count;
    can_handle.Init.ExtFiltersNbr         = ext_filter_count;

    // ------------------- baudrate ------------------------

    can_handle.Init.FrameFormat           = FDCAN_FRAME_CLASSIC;
    can_handle.Init.NominalPrescaler      = can_bitrate_nominal.Brp;
    can_handle.Init.NominalTimeSeg1       = can_bitrate_nominal.Seg1;
    can_handle.Init.NominalTimeSeg2       = can_bitrate_nominal.Seg2;
    can_handle.Init.NominalSyncJumpWidth  = can_bitrate_nominal.Sjw;

    // Data baudrate is optional (only required for CAN FD)
    // data baudrate == nominal baudrate --> CAN FD
    // data baudrate  > nominal baudrate --> CAN FD + BRS
    // NOTE:
    // The samplepoint for high data rates is critical.
    // 8 M baud does not work with 75%, but it works with 50%.
    // But strangely 10 M baud works with 50% and with 75% !
    if (can_using_FD())
    {
        can_handle.Init.FrameFormat       = can_using_BRS() ? FDCAN_FRAME_FD_BRS : FDCAN_FRAME_FD_NO_BRS;
        can_handle.Init.DataPrescaler     = can_bitrate_data.Brp;
        can_handle.Init.DataTimeSeg1      = can_bitrate_data.Seg1;
        can_handle.Init.DataTimeSeg2      = can_bitrate_data.Seg2;
        can_handle.Init.DataSyncJumpWidth = can_bitrate_data.Sjw;
    }

    // ------------------ init bus load ------------------------

    // Calculate the length of 1 nominal CAN bus bit in nanoseconds (500 kBaud --> nom_bit_len_ns = 2000)

    uint32_t clock_MHz = system_get_can_clock() / 1000000; // 160
    nom_bit_len_ns  = 1 + can_bitrate_nominal.Seg1 + can_bitrate_nominal.Seg2; // time quantums
    nom_bit_len_ns *= can_bitrate_nominal.Brp; // clock prescaler
    nom_bit_len_ns *= 1000;                    // µs -> ns
    nom_bit_len_ns /= clock_MHz;

    busload_ppm  = 0;

    // ------------------ init FDCAN ----------------------

    // sets can_handle.State == HAL_FDCAN_STATE_READY
    if (HAL_FDCAN_Init(&can_handle) != HAL_OK)
        return FBK_ErrorFromHAL; // error detail in can_handle.ErrorCode

    // ---------------- TDC compensation ------------------

    HAL_FDCAN_DisableTxDelayCompensation(&can_handle);

    tdc_offset = 0;
    if (can_using_FD())
    {
        // The transceiver Delay Compensation (TDC) compensates for the delay between the CAN Tx pin and the CAN Rx pin of the processor.
        // The processor measures the delay of the transceiver chip while a CAN FD packet with BRS is sent to CAN bus.
        // The secondary samplepoint (SSP) does not need to be the same as the primary samplepoint specified in can_bitrate_data.
        // The SSP is only used to verify that the data bits are sent without error to CAN bus.
        // Here a fix SSP at 50% of the length of a data bit is calculated.
        // The SSP offset is measured in mtq (minimum time quantums = one period of 160 MHz)

        // Calculate the length of one data bit in 'mtq'.
        uint32_t data_bit_len = can_bitrate_data.Brp * (1 + can_bitrate_data.Seg1 + can_bitrate_data.Seg2);

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
            tdc_offset = offsetSSP;

            // set TDCO and TDCF in register TDCR
            if (HAL_FDCAN_ConfigTxDelayCompensation(&can_handle, tdc_offset, 0) != HAL_OK) return FBK_ErrorFromHAL;
            // set TDC bit in register DBTP
            if (HAL_FDCAN_EnableTxDelayCompensation(&can_handle) != HAL_OK) return FBK_ErrorFromHAL; // error detail in can_handle.ErrorCode
        }
    }

    // -------------------- filters --------------------------
    
    // Store all user filters in can_filters into the processor's memory
    if (!can_apply_filters())
        return FBK_ErrorFromHAL;
    
    // the user can define up to 8 filters
    bool has_filters = (std_filter_count + ext_filter_count) > 0;
    
    // If no user filters are defined --> accept all packets in FIFO 0 where they are sent over USB to the host.
    // Otherwise all packets that do not pass the user filters go to FIFO 1 where they only flash the blue LED.
    uint32_t non_matching = has_filters ? FDCAN_ACCEPT_IN_RX_FIFO1 : FDCAN_ACCEPT_IN_RX_FIFO0;

    HAL_FDCAN_ConfigGlobalFilter(&can_handle, non_matching, non_matching, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE);    

    // --------------------- timestamp -------------------------

    // Create a timestamp that is equal to the CAN bitrate
    // Only needed to calculate cycle time (disabled)
    // HAL_FDCAN_ConfigTimestampCounter(&can_handle, FDCAN_TIMESTAMP_PRESC_1);
    // Internal does not work to get time. External use TIM3 as source. See RM0440.
    // HAL_FDCAN_EnableTimestampCounter(&can_handle, FDCAN_TIMESTAMP_EXTERNAL);

    // ---------------------- cleanup ---------------------------

    cycle_max_time_ns     = 0;
    cycle_ave_time_ns     = 0;
    print_bitrate_once    = true;
    print_chip_delay_once = true;

    led_turn_TX(LED_OFF); // green off

    // turn on power supply of isolator chip
    if (ISOLATOR_PWR_Pin > 0)
        HAL_GPIO_WritePin(ISOLATOR_PWR_Port, ISOLATOR_PWR_Pin, ISOLATOR_ON);

    // ----------------------- start ---------------------------

    // sets can_handle.State == HAL_FDCAN_STATE_BUSY
    if (HAL_FDCAN_Start(&can_handle) != HAL_OK) return FBK_ErrorFromHAL; // error detail in can_handle.ErrorCode

    can_is_open = true;
    return FBK_Success;
}

// Disable the CAN peripheral and go off-bus
void can_close()
{
    // It should not generate an error if the adapter is closed twice
    if (!can_is_open)
        return;

    HAL_FDCAN_Stop  (&can_handle);
    HAL_FDCAN_DeInit(&can_handle);

    // Reset error counter etc.
    __HAL_RCC_FDCAN_FORCE_RESET();
    __HAL_RCC_FDCAN_RELEASE_RESET();

    can_reset();
    led_turn_TX(LED_ON); // green on
}

// Called from Buffer. Stores a packet in the Tx FIFO
// Check HAL_FDCAN_GetTxFifoFreeLevel() and can_is_tx_allowed() before calling this function!
void can_send_packet(FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data)
{
    // Sending a message with BRS flag, but nominal and data baudrate are the same --> reset flag and send without BRS.
    if (!can_using_BRS())
        tx_header->BitRateSwitch = FDCAN_BRS_OFF;

    HAL_StatusTypeDef status = HAL_FDCAN_AddMessageToTxFifoQ(&can_handle, tx_header, tx_data);
    if (status != HAL_OK) // may be busy (state = HAL_FDCAN_STATE_BUSY)
    {
        // On error the HAL sets can_handle.ErrorCode to HAL_FDCAN_ERROR_FIFO_FULL or HAL_FDCAN_ERROR_NOT_STARTED        
        // Both errors can never happen, because this function is only called when CAN has been initialized and the FIFO is not full.
        error_assert(APP_CanTxFail, true);
        return;
    }
    
    if (can_handle.Init.AutoRetransmission == ENABLE)
    {
        last_tx_tick = HAL_GetTick();
        tx_pending ++;
    }

    // Do not flash the Tx LED here! This was wrong in the legacy Candlelight firmware.
    // The packet has not been sent yet. It is still in the Tx FIFO and will stay there until an ACK is received.
    // When an ACK is received HAL_FDCAN_GetTxEvent() will return the Tx Event and the Tx LED will be flashed.
}

// Process data from CAN tx/rx circular buffers
// This function is called approx 100 times in one millisecond
void can_process(uint32_t tick_now)
{
    // -------------------------- Tx Event ------------------------------------

    uint8_t can_data_buf[64] = {0};
    char    dbg_msg_buf[100];

    // This was competely wrong in the original Candlelight firmware (fixed by Elmüsoft).
    // Instead of sending a Tx Event to the host in the moment when the processor has really sent the packet to the CAN bus
    // they have sent a fake event immediately after dispatching the packet, no matter if it really was sent or not.
    FDCAN_TxEventFifoTypeDef tx_event;
    if (HAL_FDCAN_GetTxEvent(&can_handle, &tx_event) == HAL_OK)
    {
        // Here tx_event.EventType is FDCAN_TX_EVENT if auto retransmission is enabled.
        // Here tx_event.EventType is FDCAN_TX_IN_SPITE_OF_ABORT if auto retransmission is disabled.
        // "In DAR mode (Disable Auto Retransmission) all transmissions are automatically canceled after
        // they have been started on the CAN bus." (see "STM32G4 Series - Chapter FDCAN.pdf" in subfolder "Documentation")
        if (USER_Flags & USR_ReportTX)
            buf_store_tx_echo(&tx_event);

        // In loopback mode do not count the same packet twice (Tx == Rx at the same time without delay)
        // In bus montoring mode and restricted mode sending packets is not possible.
        if (can_handle.Init.Mode == FDCAN_MODE_NORMAL)
        {
            // TxEvent and RxHeader are identical except the last 2 members, which are not needed for busload calculation.
            FDCAN_RxHeaderTypeDef* rx_header = (FDCAN_RxHeaderTypeDef*)&tx_event;

            // for bus load calculation
            bit_cnt_message += can_calc_bit_count_in_frame(rx_header);
        }
        
        if (tx_pending > 0)
        {
            last_tx_tick = tick_now;
            tx_pending --;
        }
        led_flash_TX(); // flash green 15 ms
    }

    // -------------------------- Rx Packet ------------------------------------

    // Rx FIFO 0 receives all packets that have been accepted by the filters -> write to the USB buffer
    // Rx FIFO 0 and Rx FIFO 1 can store up to three packets each.
    FDCAN_RxHeaderTypeDef rx_header;
    if (HAL_FDCAN_GetRxMessage(&can_handle, FDCAN_RX_FIFO0, &rx_header, can_data_buf) == HAL_OK)
    {
        buf_store_rx_packet(&rx_header, can_data_buf);

        // for bus load calculation
        bit_cnt_message += can_calc_bit_count_in_frame(&rx_header);

        led_flash_RX(); // flash 15 ms
    }

    // Rx FIFO 1 receives all packets that have been rejected by the filters -> only flash the blue LED
    // Rx FIFO 0 and Rx FIFO 1 can store up to three packets each.
    if (HAL_FDCAN_GetRxMessage(&can_handle, FDCAN_RX_FIFO1, &rx_header, can_data_buf) == HAL_OK)
    {
        // for bus load calculation
        bit_cnt_message += can_calc_bit_count_in_frame(&rx_header);

        led_flash_RX(); // flash 15 ms
    }

    // -------------------------- Rx / Tx Errors ------------------------------------

    // Tx Event FIFO packet lost
    if (__HAL_FDCAN_GET_FLAG(&can_handle, FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST))
    {
        error_assert(APP_CanTxFail, false);
        __HAL_FDCAN_CLEAR_FLAG(&can_handle, FDCAN_FLAG_TX_EVT_FIFO_ELT_LOST);
    }

    // Rx FIFO 0 packet lost
    if (__HAL_FDCAN_GET_FLAG(&can_handle, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST))
    {
        error_assert(APP_CanRxFail, false);
        __HAL_FDCAN_CLEAR_FLAG(&can_handle, FDCAN_FLAG_RX_FIFO0_MESSAGE_LOST);
    }

    // Rx FIFO 1 packet lost
    if (__HAL_FDCAN_GET_FLAG(&can_handle, FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST))
    {
        error_assert(APP_CanRxFail, false);
        __HAL_FDCAN_CLEAR_FLAG(&can_handle, FDCAN_FLAG_RX_FIFO1_MESSAGE_LOST);
    }

    // ------------------------- Refresh Bus Status ---------------------------------

    HAL_FDCAN_GetProtocolStatus(&can_handle, &cur_status);
   
    // ----------------------------- Transmit Timeout -----------------------------
    
    // If a message hangs longer than a few milliseconds in the Tx FIFO this means that it was not acknowledged.
    // The processor continues to send the message !!ETERNALLY!! producing a bus load of 95%.
    // Tx requests must be canceled by firmware to free CAN bus from the congestion.
    // the processor will never stop alone sending the same packet over and over again.
    if (tx_pending > 0 && tick_now >= last_tx_tick + CAN_TX_TIMEOUT)
    {  
        tx_pending = 0;
        HAL_FDCAN_AbortTxRequest(&can_handle, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2);
        buf_clear_can_buffer();
        error_assert(APP_CanTxTimeout, false);        
    }   

    // --------------------------- Calculate Cycle Time ---------------------------

    /*
    // this code was written by Nakanishi Kiyomaro.
    static uint32_t last_time_stamp_cnt = 0;

    uint16_t curr_time_stamp_cnt = HAL_FDCAN_GetTimestampCounter(&can_handle);
    uint32_t cycle_time_ns;
    if (last_time_stamp_cnt <= curr_time_stamp_cnt)
        cycle_time_ns = ((uint32_t)curr_time_stamp_cnt - last_time_stamp_cnt) * 1000;
    else // counter rollover
        cycle_time_ns = ((uint32_t)UINT16_MAX - last_time_stamp_cnt + 1 + curr_time_stamp_cnt) * 1000;

    if (cycle_max_time_ns < cycle_time_ns)
        cycle_max_time_ns = cycle_time_ns;

    cycle_ave_time_ns = ((uint32_t)cycle_ave_time_ns * 15 + cycle_time_ns) >> 4;

    last_time_stamp_cnt = curr_time_stamp_cnt;
    */

    // ------------------------ print bitrate ---------------------------------

    // Important: This function must be called from the main loop, not from can_open()
    // otherwise the debug message is sent to the host before the response to command Open ("O") has been set.
    if (print_bitrate_once && can_is_open)
    {
        print_bitrate_once = false;
        can_print_info();
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
    if (print_chip_delay_once && tdc_offset > 0 && cur_status.TDCvalue > tdc_offset && cur_status.TDCvalue < 127)
    {
        print_chip_delay_once = false;
        uint32_t clock_MHz    = system_get_can_clock() / 1000000; // 160
        uint32_t chip_delay   = cur_status.TDCvalue - tdc_offset;

        // chip_delay = 21 mtq --> 21 * 1000 / 160 = 131 ns
        sprintf(dbg_msg_buf, "Measured transceiver chip delay: %lu ns", chip_delay * 1000 / clock_MHz);
        control_send_debug_mesg(dbg_msg_buf);
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
void can_recover_bus_off()
{
    if (cur_status.BusOff)
    {
        if (!recover_bus_off)
        {
            recover_bus_off = true;
            control_send_debug_mesg(">> Start recovery from Bus Off");

            HAL_FDCAN_AbortTxRequest(&can_handle, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2);
            HAL_FDCAN_Stop (&can_handle);
            HAL_FDCAN_Start(&can_handle);
        }
    }
    else
    {
        if (recover_bus_off)
        {
            recover_bus_off = false;
            control_send_debug_mesg("<< Successfully recovered from Bus Off");

            // Clear errors that are still stored in the error handler, but that are outdated now.
            error_clear();
        }
    }
}

// Called every 100 ms from main()
// Calculate bus load, written (and hopefully tested well) by Nakanishi Kiyomaro.
void can_timer_100ms()
{
    if (busload_interval == 0 || !can_is_open)
        return;
    
    // The bus load calculation is very rough, as bit stuffing is not considered.
    // There may be 1 stuff bit for 5 payload bits.
    // This constant increases the busload by 10% to compensate this.
    const uint32_t BUS_LOAD_BUILDUP_PPM = 1125000;

    uint32_t rate_us_per_ms = (uint32_t)bit_cnt_message * nom_bit_len_ns / 1000 / 100; // MAX: 1000 @ 1Mbps

    // This calculation is a kind of moving average.
    // It is implemented to suppress excessive fluctuations.
    busload_ppm = (busload_ppm * 7 + BUS_LOAD_BUILDUP_PPM * rate_us_per_ms / 1000) >> 3;
    bit_cnt_message = 0;

    // --------------

    if (busload_counter >= busload_interval)
    {
        uint8_t new_busload = MIN(99, busload_ppm / 10000);

        // Suppress displaying "Bus load: 0%" eternally
        if (new_busload == 0 && old_busload_percent == 0)
            return;

        old_busload_percent = new_busload;
        control_report_busload(new_busload); // send busload report to the host
        busload_counter = 0;
    }
    busload_counter ++;
}

// ----------------------------------------------------------------------------------------------

// ATTENTION: Deprecated! Read the manual.
// Set the nominal bitrate of the CAN peripheral
// Always samplepoint 87.5%
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_baudrate(can_nom_bitrate bitrate)
{
    if (can_is_open)
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus
    
    can_bitrate_nominal.Seg1 = 174;
    can_bitrate_nominal.Seg2 =  25;   

    // IMPORTANT:
    // The same setting "Nominal: 500k baud, 87.5%; Data: 2M baud, 75.0%;" can be achieved with different values:
    // If you set Nominal = [P:2,  S1:139, S2:20, J:20] and Data = [P:2,  S1:29, S2:10, J:10] a transfer with BRS works fine.
    // But with   Nominal = [P:10, S1:27,  S2:4,  J:4]  and Data = [P:10, S1:5,  S2:2,  J:2]  you get Bus Off errors !!!
    switch (bitrate)
    {
        case CAN_BITRATE_10K:
            can_bitrate_nominal.Brp  = 80;
            break;
        case CAN_BITRATE_20K:
            can_bitrate_nominal.Brp  = 40;
            break;
        case CAN_BITRATE_50K:
            can_bitrate_nominal.Brp  = 16;
            break;
        case CAN_BITRATE_83K:
            can_bitrate_nominal.Brp  = 8;  
            can_bitrate_nominal.Seg1 = 209;
            can_bitrate_nominal.Seg2 = 30;
            break;
        case CAN_BITRATE_100K:
            can_bitrate_nominal.Brp  = 8;
            break;
        case CAN_BITRATE_125K:
            can_bitrate_nominal.Brp  = 5;   // 160 MHz / 5 / (1 + 223 + 32) = 125 kBaud
            can_bitrate_nominal.Seg1 = 223; // (1 + 223)   / (1 + 223 + 32) = 87.5%
            can_bitrate_nominal.Seg2 = 32;
            break;
        case CAN_BITRATE_250K:
            can_bitrate_nominal.Brp  = 4;
            can_bitrate_nominal.Seg1 = 139;
            can_bitrate_nominal.Seg2 = 20;
            break;
        case CAN_BITRATE_500K:
            can_bitrate_nominal.Brp  = 2;
            can_bitrate_nominal.Seg1 = 139;
            can_bitrate_nominal.Seg2 = 20;
            break;
        case CAN_BITRATE_800K:
            can_bitrate_nominal.Brp  = 1;
            break;
        case CAN_BITRATE_1000K:
            can_bitrate_nominal.Brp  = 1;
            can_bitrate_nominal.Seg1 = 139;
            can_bitrate_nominal.Seg2 = 20;
            break;
        default:
            return FBK_InvalidParameter;
    }

    bitlimits* limits = utils_get_bit_limits();
    can_bitrate_nominal.Sjw = MIN(can_bitrate_nominal.Seg2, limits->nom_sjw_max);
    
    // Check if the settings are supported by the processor.
    // If not the user must call can_set_nom_bit_timing() instead.
    if (!IS_FDCAN_NOMINAL_PRESCALER(can_bitrate_nominal.Brp)  ||
        !IS_FDCAN_NOMINAL_TSEG1    (can_bitrate_nominal.Seg1) ||
        !IS_FDCAN_NOMINAL_TSEG2    (can_bitrate_nominal.Seg2))
    {
        can_bitrate_nominal.Brp = 0; // baudrate not valid
        return FBK_InvalidParameter;
    }
    return FBK_Success; 
}

// ATTENTION: Deprecated! Read the manual.
// Set the data bitrate of the CAN peripheral
// Samplepoint = 75%, except for 8 MBaud it must be 50% because 75% does not work.
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_data_baudrate(can_data_bitrate bitrate)
{
    if (can_is_open)
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    can_bitrate_data.Seg1 = 29;
    can_bitrate_data.Seg2 = 10;

    // IMPORTANT:
    // The same setting "Nominal: 500k baud, 87.5%; Data: 2M baud, 75.0%;" can be achieved with different values:
    // If you set Nominal = [P:2,  S1:139, S2:20, J:20] and Data = [P:2,  S1:29, S2:10, J:10] a transfer with BRS works fine.
    // But with   Nominal = [P:10, S1:27,  S2:4,  J:4]  and Data = [P:10, S1:5,  S2:2,  J:2]  you get Bus Off errors !!!
    switch (bitrate)
    {
        case CAN_DATA_BITRATE_500K: 
            can_bitrate_data.Brp  = 8;
            break;
        case CAN_DATA_BITRATE_1M:
            can_bitrate_data.Brp  = 4;
            break;
        case CAN_DATA_BITRATE_2M:
            can_bitrate_data.Brp  = 2;
            break;
        case CAN_DATA_BITRATE_4M:
            can_bitrate_data.Brp  = 2;  // 160 MHz / 2 / (1 + 14 + 5) = 4 MBaud
            can_bitrate_data.Seg1 = 14; // (1 + 14)    / (1 + 14 + 5) = 75%
            can_bitrate_data.Seg2 = 5;
            break;
        case CAN_DATA_BITRATE_5M:
            can_bitrate_data.Brp  = 2;
            can_bitrate_data.Seg1 = 11;
            can_bitrate_data.Seg2 = 4;
            break;
        // For any strange reason the STM32G431 works at 8 Mbaud only if the samplepoint is 50%.
        // But at 10 Mbaud it works with 75%. Very weird!
        case CAN_DATA_BITRATE_8M:
            can_bitrate_data.Brp  = 2; // 160 MHz / 2 / (1 + 4 + 5) = 8 MBaud
            can_bitrate_data.Seg1 = 4; // (1 + 4)     / (1 + 4 + 5) = 50%
            can_bitrate_data.Seg2 = 5;
            break;
        default:
            return FBK_InvalidParameter;
    }

    bitlimits* limits = utils_get_bit_limits();
    can_bitrate_data.Sjw = MIN(can_bitrate_data.Seg2, limits->fd_sjw_max);
    
    // Check if the settings are supported by the processor.
    // If not the user must call can_set_data_bit_timing() instead.
    if (!IS_FDCAN_DATA_PRESCALER(can_bitrate_data.Brp)  ||
        !IS_FDCAN_DATA_TSEG1    (can_bitrate_data.Seg1) ||
        !IS_FDCAN_DATA_TSEG2    (can_bitrate_data.Seg2))
    {
        can_bitrate_data.Brp = 0; // baudrate not valid
        return FBK_InvalidParameter;
    }
    return FBK_Success; 
}

// Set the nominal bitrate configuration of the CAN peripheral
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_nom_bit_timing(uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    if (can_is_open)
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    if (!IS_FDCAN_NOMINAL_PRESCALER(BRP)  ||
        !IS_FDCAN_NOMINAL_TSEG1    (Seg1) ||
        !IS_FDCAN_NOMINAL_TSEG2    (Seg2) ||
        !IS_FDCAN_NOMINAL_SJW      (Sjw))
            return FBK_InvalidParameter;

    can_bitrate_nominal.Brp  = BRP;
    can_bitrate_nominal.Seg1 = Seg1;
    can_bitrate_nominal.Seg2 = Seg2;
    can_bitrate_nominal.Sjw  = Sjw;
    return FBK_Success;
}

// Set the data bitrate configuration of the CAN peripheral
// If all 4 values are identical to the nominal settings, CAN FD is enabled and packets up to 64 byte can be sent without BRS.
// See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
eFeedback can_set_data_bit_timing(uint32_t BRP, uint32_t Seg1, uint32_t Seg2, uint32_t Sjw)
{
    if (can_is_open)
        return FBK_AdapterMustBeClosed; // cannot set bitrate while on bus

    if (!IS_FDCAN_DATA_PRESCALER(BRP)  ||
        !IS_FDCAN_DATA_TSEG1    (Seg1) ||
        !IS_FDCAN_DATA_TSEG2    (Seg2) ||
        !IS_FDCAN_DATA_SJW      (Sjw))
            return FBK_InvalidParameter;

    can_bitrate_data.Brp  = BRP;
    can_bitrate_data.Seg1 = Seg1;
    can_bitrate_data.Seg2 = Seg2;
    can_bitrate_data.Sjw  = Sjw;
    return FBK_Success;
}

// Prints a string containing the baudrate and samplepoint to the debug output.
// Although this is very simple many application and firmware developers do this calculation wrong. (see sourcecode of Cangaroo)
// It is very important to verify that baudrate and samplepoint have been set correctly by the user.
// Therefore this firmware prints them as a debug message so the user can verify the settings.
// Additionally this function can print the TDC configuration and if the pin BOOT0 is enabled.
// This function is only called if CAN is opened.
void can_print_info()
{
    if ((USER_Flags & USR_DebugReport) == 0)
        return;

    char buf[200];
    if (can_handle.Init.Mode != FDCAN_MODE_NORMAL)
    {
        char* mode = "Invalid";
        switch (can_handle.Init.Mode)
        {
            case FDCAN_MODE_RESTRICTED_OPERATION: mode = "Restricted";        break;
            case FDCAN_MODE_BUS_MONITORING:       mode = "Monitoring";        break;
            case FDCAN_MODE_INTERNAL_LOOPBACK:    mode = "Internal Loopback"; break;
            case FDCAN_MODE_EXTERNAL_LOOPBACK:    mode = "External Loopback"; break;
        }
        sprintf(buf, "Operation Mode: %s", mode);
        control_send_debug_mesg(buf);
    }

    // print "Nominal: 500k baud, 87.5%; Data: 2M baud, 75.0%; Perfect match: Yes"
    utils_format_bitrate(buf,               "Nominal", &can_bitrate_nominal);
    utils_format_bitrate(buf + strlen(buf), "; Data",  &can_bitrate_data);

    if (can_using_FD())
    {
        // See "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation"
        strcat(buf, "; Perfect match: ");
        strcat(buf, can_bitrate_nominal.Brp == can_bitrate_data.Brp ? "Yes" : "No");
    }
    control_send_debug_mesg(buf);

    // optional additional information: pin BOOT0 and TDC offset
    if (false)
    {
        // Print Transceiver Delay Compensation
        if (tdc_offset > 0)
            sprintf(buf, "TDC offset: %lu mtq", tdc_offset);
        else
            strcpy(buf, "TDC: not used");   
        
        // Print enable status of pin BOOT0
        strcat(buf, ", Pin BOOT0: ");
        strcat(buf, (system_is_option_enabled(OPT_BOOT0_Enable)) ? "enabled" : "disabled");
        
        control_send_debug_mesg(buf);
    }
}

// ----------------------------------------------------------------------------------------------

// The processor allows up to 28 standard filters and up to 8 extended filters.
// Nobody needs so many filters -> allow 8 user filters.
// Rx FIFO 0 receives all packets that pass. They are sent to the host application over USB.
// Rx FIFO 1 receives all packets that are rejected, they only flash the blue LED.
// Each FIFO can store 3 Rx packets before it is full.
// ---------------------------------------------------------
// While all industry CAN bus adapters allow to set filters after opening the adapter, the STM32 processor is very restricted.
// The values can_handle.Init.StdFiltersNbr and ExtFiltersNbr cannot be modified anymore after opening the adapter.
// But HAL_FDCAN_ConfigFilter() can be called after opening the adapter.
// So the only possible filter modification after opening the adapter is to modify ONE existing filter.
// The filter type must be the same (11 bit or 29 bit).
eFeedback can_set_mask_filter(bool extended, uint32_t filter, uint32_t mask)
{
    int tot_filters = std_filter_count + ext_filter_count;
    if (tot_filters >= MAX_FILTERS)
        return FBK_InvalidParameter;

    uint32_t maximum = extended ? 0x1FFFFFFF : 0x7FF;
    if (filter > maximum || mask > maximum)
        return FBK_InvalidParameter;
    
    if (can_is_open)
    {
        // only one existing filter can be modified if the adapter is already open
        if (tot_filters != 1)
            return FBK_AdapterMustBeClosed;

        // the filter to be modified must be from the same type
        if (extended != (ext_filter_count == 1))
            return FBK_AdapterMustBeClosed;
        
        // modify the one and only filter at index 0
        ext_filter_count = 0; 
        std_filter_count = 0;
        tot_filters      = 0;
    }

    can_filters[tot_filters].IdType       = extended ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    can_filters[tot_filters].FilterIndex  = extended ? ext_filter_count  : std_filter_count;
    can_filters[tot_filters].FilterType   = FDCAN_FILTER_MASK;
    can_filters[tot_filters].FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    can_filters[tot_filters].FilterID1    = filter;
    can_filters[tot_filters].FilterID2    = mask;

    if (extended) ext_filter_count ++;
    else          std_filter_count ++;
    
    if (can_is_open && !can_apply_filters())
        return FBK_ErrorFromHAL;

    return FBK_Success;
}

// Store all user filters in can_filters into the processor's memory
bool can_apply_filters()
{
    // the user can define up to 8 filters
    int tot_filters = std_filter_count + ext_filter_count;
    for (int i=0; i<tot_filters; i++)
    {
        if (HAL_FDCAN_ConfigFilter(&can_handle, &can_filters[i]) != HAL_OK) return false; // error detail in can_handle.ErrorCode
    }
    return true;
}

// clear all filters
eFeedback can_clear_filters()
{
    if (can_is_open)
        return FBK_AdapterMustBeClosed; // cannot clear filters while on bus

    ext_filter_count = 0;
    std_filter_count = 0;
    return FBK_Success;
}

// ----------------------------------------------------------------------------------------------

// Some boards have a 120 Ohm termination resistor that can be enabled by a GPIO pin.
// returns false if the board does not support this feature
bool can_set_termination(bool enable)
{
    if (TERMINATOR_Pin <= 0)
        return false; // --> return FBK_UnsupportedFeature to host

    HAL_GPIO_WritePin(TERMINATOR_Port, TERMINATOR_Pin, enable ? TERMINATOR_ON : TERMINATOR_OFF);
    termination_on = enable;
    return true;
}

bool can_get_termination(bool* enabled)
{
    if (TERMINATOR_Pin <= 0)
        return false; // --> return FBK_UnsupportedFeature to host

    *enabled = termination_on;
    return true;
}

// ----------------------------------------------------------------------------------------------

// interval =   0 --> disable busload report
// interval =   1 --> report busload every 100 ms     (minimum)
// interval =   7 --> report busload every 700 ms
// interval = 100 --> report busload every 10 seconds (maximum)
eFeedback can_enable_busload(uint32_t interval)
{
    if (interval > 100)
        return FBK_InvalidParameter;

    busload_interval = interval;
    return FBK_Success;
}

// ----------------------------------------------------------------------------------------------

// Return bus status
bool can_is_opened()
{
    return can_is_open;
}

// > 128 errors have occurred
bool can_is_passive()
{
    return cur_status.ErrorPassive;
}

// true if data baudrate has been set, otherwise CAN classic
bool can_using_FD()
{
    return can_bitrate_data.Brp > 0;
}

bool can_using_BRS()
{
    return can_calc_baud(&can_bitrate_data) > can_calc_baud(&can_bitrate_nominal);
}

eFeedback can_is_tx_allowed()
{
    if (!can_is_open)
        return FBK_AdapterMustBeOpen;
    if (can_handle.Init.Mode == FDCAN_MODE_BUS_MONITORING)
        return FBK_NoTxInSilentMode;
    if (cur_status.BusOff)
        return FBK_BusIsOff;
    return FBK_Success;
}

// Return reference to CAN handle
FDCAN_HandleTypeDef *can_get_handle()
{
    return &can_handle;
}

// Return the maximum cycle time in nano seconds
uint32_t can_get_cycle_max_time_ns()
{
    return cycle_max_time_ns;
}

// Return the average cycle time in nano seconds
uint32_t can_get_cycle_ave_time_ns()
{
    return cycle_ave_time_ns;
}

// ---------------------------------------------------------------------------------------------------

// Calculate the transmission duration of a CAN frame.
// This code was written by Nakanishi Kiyomaro (and hopefully tested well).
uint16_t can_calc_bit_count_in_frame(FDCAN_RxHeaderTypeDef* header)
{
    if (busload_interval == 0)
        return 0;

    uint32_t byte_count = utils_dlc_to_byte_count(HAL_TO_DLC(header->DataLength));

    uint16_t time_msg, time_data;
    if (header->RxFrameType == FDCAN_REMOTE_FRAME && header->IdType == FDCAN_STANDARD_ID)
    {
        time_msg = CAN_BIT_NBR_WOD_CBFF;
    }
    else if (header->RxFrameType == FDCAN_REMOTE_FRAME && header->IdType == FDCAN_EXTENDED_ID)
    {
        time_msg = CAN_BIT_NBR_WOD_CEFF;
    }
    else if (header->FDFormat == FDCAN_CLASSIC_CAN && header->IdType == FDCAN_STANDARD_ID)
    {
        time_msg = CAN_BIT_NBR_WOD_CBFF + (uint16_t)byte_count * 8;
    }
    else if (header->FDFormat == FDCAN_CLASSIC_CAN && header->IdType == FDCAN_EXTENDED_ID)
    {
        time_msg = CAN_BIT_NBR_WOD_CEFF + (uint16_t)byte_count * 8;
    }
    else // for FD frames
    {
        if (header->IdType == FDCAN_STANDARD_ID) time_msg = CAN_BIT_NBR_WOD_FBFF_ARBIT;
        else                                        time_msg = CAN_BIT_NBR_WOD_FEFF_ARBIT;

        if (byte_count <= 16)
            time_data = CAN_BIT_NBR_WOD_FXFF_DATA_S;    // Short CRC
        else
            time_data = CAN_BIT_NBR_WOD_FXFF_DATA_L;    // Long CRC

        time_data = time_data + (uint16_t)byte_count * 8;

        if (header->BitRateSwitch == FDCAN_BRS_ON)
        {
            if (can_bitrate_nominal.Brp == 0) return 0;   // Uninitialized bitrate (avoid zero-div)

            uint32_t rate_ppm;  // Nominal bit time vs data bit time
            rate_ppm = ((uint32_t)1 + can_bitrate_data.Seg1 + can_bitrate_data.Seg2);
            rate_ppm = rate_ppm * can_bitrate_data.Brp;     // Tq in one bit (data)
            rate_ppm = rate_ppm * 1000000;  // MAX: 32 * (32 + 16) * 1000000
            rate_ppm = rate_ppm / ((uint32_t)1 + can_bitrate_nominal.Seg1 + can_bitrate_nominal.Seg2);
            rate_ppm = rate_ppm / can_bitrate_nominal.Brp;

            time_msg = time_msg + ((uint32_t)time_data * rate_ppm) / 1000000;
        }
        else
        {
            time_msg = time_msg + time_data;
        }
    }
    return time_msg;
}


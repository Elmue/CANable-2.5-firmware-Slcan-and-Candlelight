/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

// This acumulates 3 USB packets to be sent as a blob to the host
#define  DEBUG_TEST_BLOB    0

#include "buffer.h"
#include "error.h"
#include "control.h"
#include "system.h"
#include "utils.h"
#include "settings.h"
#include "usb_class.h"
#include "candlelight_def.h"
#include "can.h"

// ----- Globals
extern eUserFlags GLB_UserFlags[CHANNEL_COUNT];

// Global flag that enables the new ElmüSoft protocol for maximum USB throughput (Candlelight only).
// It is not possible to enable the ElmüSoft protocol only for an individual channel,
// because ElmüSoft uses different USB interfaces while Legacy routes all traffic through the first USB interface.
// To interpret the bytes of a USB packet, that was received from the host in usb_class.c, the protocol must be known.
bool GLB_ProtoElmue = false;

// ----- Class Instance
buf_class  buf_inst[CHANNEL_COUNT] = {0};

// ----- Private Methods
void              buf_process_host (uint8_t channel, buf_class* usb_buf);
void              buf_process_can  (uint8_t channel, buf_class* can_buf);
void              buf_clear_buffers(uint8_t channel, bool clear_can, bool clear_host);
kHostFrameObject* buf_peek_host_frame_locked(list_item* list_head);
buf_class*        buf_get_inst_for_usb(uint8_t channel);
bool              buf_store_can_frame(uint8_t channel, uint8_t* can_frame);
void              buf_store_rx_packet_echo(uint8_t channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data, uint32_t fake_echo);

// public
void buf_init()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        buf_clear_buffers(C, true, true);
    }
}
// public
void buf_clear_can_buffer(uint8_t channel)
{
    buf_clear_buffers(channel, true, false);
}
// private
void buf_clear_buffers(uint8_t channel, bool clear_can, bool clear_host)
{
    buf_class* inst = &buf_inst[channel];

    if (clear_can)
    {
        list_init(&inst->list_can_pool);
        list_init(&inst->list_to_can);

        // add the 64 entries to the can pool ringbuffers
        for (int i=0; i < CAN_QUEUE_SIZE; i++)
        {
            list_add_tail(&inst->can_pool_buffer[i].list, &inst->list_can_pool);
        }
    }
    if (clear_host)
    {
        list_init(&inst->list_host_pool);
        list_init(&inst->list_to_host);

        // add the 70 entries to the host pool ringbuffers
        for (int i=0; i < HOST_QUEUE_SIZE; i++)
        {
            list_add_tail(&inst->host_pool_buffer[i].list, &inst->list_host_pool);
        }
    }
}

// ---------------------------------------------------------------------------------------------------

// called approx 100 times in one millisecond from the main loop
void buf_process(uint8_t channel, uint32_t tick_now)
{
    buf_class* can_buf = &buf_inst[channel];
    buf_class* usb_buf = buf_get_inst_for_usb(channel);

    buf_process_can (channel, can_buf);
    buf_process_host(channel, usb_buf);

    // The APP_xxx errors are deleted after sending them to the host.
    // They must be refreshed here, so the Rx + Tx LED stay ON permanently and show that there is a problem.
    if (list_is_empty(&can_buf->list_can_pool))  error_assert(channel, APP_CanTxOverflow, false);
    if (list_is_empty(&usb_buf->list_host_pool)) error_assert(channel, APP_UsbInOverflow, false);
}

// called from the main loop
// send a CAN packet to the host if list_to_host has data
void buf_process_host(uint8_t channel, buf_class* usb_buf)
{
    if (usb_buf->TxBusy)
        return; // USB IN transfer to the host is still in progress

    // only for testing: wait until there are 3 pending frames to be sent to the host in one blob
#if DEBUG_TEST_BLOB
    if (HOST_QUEUE_SIZE - count_free_entries(usb_buf, true) < 3)
        return;
#endif

    kHostFrameObject* obj_to_host = buf_get_host_frame_locked(&usb_buf->list_to_host);
    if (!obj_to_host)
        return; // nothing to be sent

    uint16_t len;
    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        // Using the optimized new ElmüSoft protocol reduces unnecessary USB overhead as it was sent by the legacy firmware.
        // If a CAN frame has only 2 data bytes, send only 2 data bytes over USB.
        // All ElmüSoft messages use the same header, no matter if CAN packet or an ASCII message.
        // If ELM_DevFlagSendUsbBlobs is set --> send multiple fames in one blob to the host.

        kHostFrameObject* next_obj = buf_peek_host_frame_locked(&usb_buf->list_to_host);

        // Send blob with multiple frames
        if ((GLB_UserFlags[channel] & USR_SendBlobs) && next_obj != NULL)
        {
            kBlob* blob = (kBlob*)usb_buf->to_host_buf;
            blob->frame_count = 0;
            blob->msg_type    = MSG_RxBlob;
            len = sizeof(kBlob);

            // Copy all frames in list_to_host into to_host_buf
            while (obj_to_host)
            {
                uint16_t size = ((kHeader*)obj_to_host->frame)->size;
                memcpy(usb_buf->to_host_buf + len, obj_to_host->frame, size);
                len += size;
                blob->frame_count ++;

                // packet was stored --> give the frame back to the pool
                list_add_tail_locked(&obj_to_host->list, &usb_buf->list_host_pool);

                // frame_count is a byte --> max count = 255
                if (next_obj == NULL || blob->frame_count > 250)
                    break;

                // check if the next frame also fits into to_host_buf
                int next_size = ((kHeader*)next_obj->frame)->size;
                if (len + next_size >= MAX_BLOB_SIZE)
                    break;

                obj_to_host = buf_get_host_frame_locked (&usb_buf->list_to_host);
                next_obj    = buf_peek_host_frame_locked(&usb_buf->list_to_host);
            }
        }
        else // only one frame to be sent
        {
            len = ((kHeader*)obj_to_host->frame)->size;

            memcpy(usb_buf->to_host_buf, obj_to_host->frame, len);

            // packet was stored --> give the frame back to the pool
            list_add_tail_locked(&obj_to_host->list, &usb_buf->list_host_pool);
        }
    }
    else // legacy Geschwister Schneider protocol
    {
        kHostFrameLegacy* pk_Legacy = (kHostFrameLegacy*)obj_to_host->frame;

        // The legacy protocol is not intelligently designed. The timestamp is behind a fix 64 byte data array.
        // For CAN FD it sends ALWAYS 76 or 80 bytes over USB no matter how many bytes the frame really has.
        len = sizeof(kHostFrameLegacy); // 80 bytes
        if ((pk_Legacy->flags & FRM_FDF) == 0) len -= 56;
        if ((GLB_UserFlags[pk_Legacy->channel] & USR_Timestamp) == 0) len -= 4;

        memcpy(usb_buf->to_host_buf, obj_to_host->frame, len);

        // packet was stored --> give the frame back to the pool
        list_add_tail_locked(&obj_to_host->list, &usb_buf->list_host_pool);
    }

    USBD_SendInDataToHost(channel, usb_buf->to_host_buf, len);
}

// called from the main loop
// send a host packet to CAN bus if list_to_can has data
void buf_process_can(uint8_t channel, buf_class* can_buf)
{
    if (!can_is_tx_fifo_free(channel))
        return; // all 3 CAN Tx FIFO's are full

    kCanFrameObject* obj_to_can = buf_get_can_frame_locked(&can_buf->list_to_can);
    if (!obj_to_can)
        return; // nothing to be sent

    // ------------------------------

    // abort if the silent mode is enabled or bus is off.
    if (can_is_tx_allowed(channel) != FBK_Success)
    {
        error_assert(channel, APP_CanTxFail, true); // both LED ON

        // give the CAN frame back to where it came from.
        list_add_tail_locked(&obj_to_can->list, &can_buf->list_can_pool);
        return; // do not send the message
    }

    can_send_packet(channel, &obj_to_can->header, obj_to_can->data);
    // At this point the Tx packet is in the CAN Tx FIFO, but it has not yet been transmitted to CAN bus.

    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        // The new ElmüSoft firmware sends an echo marker when the packet has REALLY been dispatched to CAN bus.
        // This is when HAL_FDCAN_GetTxEvent() received the Tx event.
        // Here is nothing to be sent now because the packet is in the Tx FIFO and may wait there eternally until an ACK is received.
    }
    else // legacy --> send fake echo
    {
        // The legacy Candlelight firmware sends a fake echo packet to the host after the packet was stored in the Tx FIFO.
        // An exactly identical packet with a new timestamp is immediately sent back to the host.
        // The host can recognize the echo packet because it has the same echo_id that he has put into the Tx packet.
        // But this echo is useless because it gives no information if the packet has really been sent to CAN bus or not.
        // If the packet stays a longer time in the Tx FIFO until an ACK is received, the echo has a wrong timestamp.
        // But to maintain backwards compatibility with legacy software, this design error is left unchanged.
        // If Linux cangen does not receive this fake echo, it stops sending after 10 USB OUT transfers
        // and throws a not understandable and misleading error message: "No buffer space available".

        FDCAN_RxHeaderTypeDef rx_header;
        rx_header.Identifier          = obj_to_can->header.Identifier;
        rx_header.IdType              = obj_to_can->header.IdType;
        rx_header.RxFrameType         = obj_to_can->header.TxFrameType;
        rx_header.DataLength          = obj_to_can->header.DataLength;
        rx_header.ErrorStateIndicator = obj_to_can->header.ErrorStateIndicator;
        rx_header.BitRateSwitch       = obj_to_can->header.BitRateSwitch;
        rx_header.FDFormat            = obj_to_can->header.FDFormat;
        rx_header.RxTimestamp         = system_get_timestamp(); // 32 bit

        buf_store_rx_packet_echo(channel, &rx_header, obj_to_can->data, obj_to_can->header.MessageMarker);
    }

    // give the CAN frame back to where it came from.
    list_add_tail_locked(&obj_to_can->list, &can_buf->list_can_pool);
}

// public function
// Called from USB_DataOut() in usb_class.c in an interrupt
// Handle Tx blobs from the host
void buf_store_can_frame_blob(uint8_t channel, uint8_t* can_frame)
{
    kBlob* blob = (kBlob*)can_frame;
    if (blob->msg_type == MSG_TxBlob)
    {
        int offset = sizeof(kBlob);
        for (uint8_t i=0; i<blob->frame_count; i++)
        {
            kTxFrameElmue *tx_frame = (kTxFrameElmue*)(can_frame + offset);
            if (offset + tx_frame->header.size > MAX_BLOB_SIZE)
            {
                error_assert(channel, APP_CanTxOverflow, true); // both LED ON
                return; // host has sent an invalid blob
            }

            if (!buf_store_can_frame(channel, can_frame + offset))
                return; // invalid frame or buffer overflow

            offset += tx_frame->header.size;
        }
        return;
    }

    buf_store_can_frame(channel, can_frame);
}

// private function
// Enqueue a Tx frame (kTxFrameElmue or kHostFrameLegacy) received from USB
bool buf_store_can_frame(uint8_t channel, uint8_t* can_frame)
{
    uint32_t can_id;
    uint8_t  flags;
    uint8_t  can_dlc = 0;
    uint8_t  marker  = 0;
    uint8_t* frame_data;
    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        kTxFrameElmue *tx_frame = (kTxFrameElmue*)can_frame;
        if (tx_frame->header.msg_type != MSG_TxFrame)
        {
            error_assert(channel, APP_CanTxFail, true); // both LED ON
            return false; // host has sent an invalid frame
        }

        can_id     = tx_frame->can_id;
        flags      = tx_frame->flags;
        marker     = tx_frame->marker;
        frame_data = tx_frame->data_start;

        int byte_count = tx_frame->header.size - sizeof(kTxFrameElmue);

        // Remote frames never send data bytes. The host can write the DLC value into the first data byte, otherwise DLC = 0 is sent.
        if (can_id & CAN_ID_RTR)
        {
            if (byte_count > 0)
                can_dlc = MIN(frame_data[0], 8);
        }
        else can_dlc = utils_byte_count_to_dlc(byte_count);
    }
    else // legacy Geschwister Schneider protocol
    {
        kHostFrameLegacy* tx_frame = (kHostFrameLegacy*)can_frame;

        // Although the multi channel firmware creates one USB interface for each CAN channel,
        // The legacy protocol routes all traffic of all CAN channels through the first USB interface (EP 81 / 02) for backward compatibility.
        // kHostFrameLegacy.channel tells the host which channel is the origin/destination of the packet.
        channel = tx_frame->channel;
        if (channel >= CHANNEL_COUNT)
        {
            error_assert(channel, APP_CanTxFail, true); // both LED ON
            return false; // host has sent an invalid channel
        }

        can_id     = tx_frame->can_id;
        marker     = tx_frame->echo_id;
        flags      = tx_frame->flags;
        frame_data = tx_frame->pack_FD.data;
        can_dlc    = tx_frame->can_dlc;
    }

    // ------------------------------

    FDCAN_TxHeaderTypeDef tx_header;
    tx_header.TxFrameType         = FDCAN_DATA_FRAME;
    tx_header.FDFormat            = FDCAN_CLASSIC_CAN;
    tx_header.IdType              = FDCAN_STANDARD_ID;
    tx_header.BitRateSwitch       = FDCAN_BRS_OFF;
    tx_header.TxEventFifoControl  = FDCAN_STORE_TX_EVENTS; // always! Tx Event flashes the Tx LED
    tx_header.ErrorStateIndicator = can_is_passive(channel) ? FDCAN_ESI_PASSIVE : FDCAN_ESI_ACTIVE;
    tx_header.MessageMarker       = marker;

    if (can_id & CAN_ID_29Bit)
    {
         tx_header.IdType     = FDCAN_EXTENDED_ID;
         tx_header.Identifier = can_id & CAN_MASK_29;
    }
    else tx_header.Identifier = can_id & CAN_MASK_11;

    if (can_id & CAN_ID_RTR)
        tx_header.TxFrameType = FDCAN_REMOTE_FRAME;

    if (can_dlc > 8)
        flags |= FRM_FDF;

    if (flags & FRM_FDF) // FDF bit is set if recessive
    {
        if (!can_using_FD(channel))
        {
            // the host tries to send a CAN FD packet in classic mode (data baudrate has not been set)
            error_assert(channel, APP_CanTxFail, true);
            return false;
        }

        tx_header.FDFormat = FDCAN_FD_CAN;

        // This was totally wrong in the orginal code (fixed by Elmüsoft)
        if (flags & FRM_BRS) // BRS bit is set if recessive
            tx_header.BitRateSwitch = FDCAN_BRS_ON;
    }

    tx_header.DataLength = can_dlc;

    return buf_store_tx_packet(channel, &tx_header, frame_data);
}

// Enqueue a packet for CAN bus.
bool buf_store_tx_packet(uint8_t channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data)
{
    buf_class* can_buf = &buf_inst[channel];

    kCanFrameObject* obj_to_can = buf_get_can_frame_locked(&can_buf->list_can_pool);
    if (obj_to_can)
    {
        memcpy(&obj_to_can->header, tx_header, sizeof(obj_to_can->header));
        memcpy(&obj_to_can->data,   tx_data,   sizeof(obj_to_can->data));
        list_add_tail_locked(&obj_to_can->list, &can_buf->list_to_can);
        return true;
    }
    else // CAN buffer overflow
    {
        // in case of buffer overflow inform the host immediately, so the host stops sending more packets and displays an error to the user.
        error_assert(channel, APP_CanTxOverflow, true); // Both LED's = ON --> indicate severe error
        return false;
    }
}

// ---------------------------------------------------------------------------------------------------

// public function
// Enqueue a CAN Rx packet for the host.
// rx_data is a 64 byte buffer with the received / sent data bytes
// append the frame to list_to_host
void buf_store_rx_packet(uint8_t channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data)
{
    buf_store_rx_packet_echo(channel, rx_header, rx_data, ECHO_RxData);
}
// private function
// fake_echo is only used for legacy mode
void buf_store_rx_packet_echo(uint8_t channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data, uint32_t fake_echo)
{
    buf_class* usb_buf = buf_get_inst_for_usb(channel);

    kHostFrameObject* obj_to_host = buf_get_host_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
        return; // buffer overflow! buf_process() will report this error to the host

    uint32_t can_id;
    if (rx_header->IdType == FDCAN_EXTENDED_ID)
        can_id = (rx_header->Identifier & CAN_MASK_29) | CAN_ID_29Bit;
    else
        can_id = (rx_header->Identifier & CAN_MASK_11);

    if (rx_header->RxFrameType == FDCAN_REMOTE_FRAME)
        can_id |= CAN_ID_RTR;

    uint8_t flags = 0;
    if (rx_header->FDFormat == FDCAN_FD_CAN)
    {
        flags |= FRM_FDF;
        if (rx_header->BitRateSwitch       == FDCAN_BRS_ON)      flags |= FRM_BRS;
        if (rx_header->ErrorStateIndicator == FDCAN_ESI_PASSIVE) flags |= FRM_ESI;
    }

    uint8_t can_dlc = rx_header->DataLength;

    // ------------------------

    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        uint8_t byte_count;
        if (can_id & CAN_ID_RTR)
        {
            // For remote frames the DLC from the Rx packet is transmitted in the first data byte to the host.
            rx_data[0] = can_dlc;
            byte_count = 1;
        }
        else byte_count = utils_dlc_to_byte_count(can_dlc);

        kRxFrameElmue* frame   = (kRxFrameElmue*)obj_to_host->frame;
        frame->header.size     = sizeof(kRxFrameElmue) + byte_count;
        frame->header.msg_type = MSG_RxFrame;
        frame->flags           = flags;
        frame->can_id          = can_id;
        frame->timestamp       = rx_header->RxTimestamp; // 32 bit

        if (GLB_UserFlags[channel] & USR_Timestamp)
        {
            memcpy(frame->data_use_stamp, rx_data, byte_count);
        }
        else
        {
            frame->header.size -= 4;
            memcpy(frame->data_no_stamp, rx_data, byte_count);
        }
    }
    else // legacy Geschwister Schneider protocol
    {
        kHostFrameLegacy* frame = (kHostFrameLegacy*)obj_to_host->frame;
        frame->channel  = channel;
        frame->reserved = 0;
        frame->flags    = flags;
        frame->can_id   = can_id;
        frame->can_dlc  = can_dlc;
        frame->echo_id  = fake_echo;
        memcpy(frame->raw_data, rx_data, 64);

        if (rx_header->FDFormat == FDCAN_FD_CAN)
            frame->pack_FD.timestamp_us = rx_header->RxTimestamp; // 32 bit
        else // classic frame
            frame->pack_classic.timestamp_us = rx_header->RxTimestamp; // 32 bit
    }

    // add the frame to list_to_host with IRQs disabled
    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
}

// a CAN packet from the Tx FIFO has been sent and acknowledged on CAN bus --> send marker to host.
// the legacy protocol never comes here. It sends a fake echo.
void buf_store_tx_echo(uint8_t channel, FDCAN_TxEventFifoTypeDef* tx_event)
{
    if (!GLB_ProtoElmue) // legacy protocol -> Tx Echo not supported
        return;

    buf_class* usb_buf = buf_get_inst_for_usb(channel);

    kHostFrameObject* obj_to_host = buf_get_host_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
        return; // buffer overflow! buf_process() will report this error to the host

    kTxEchoElmue* frame    = (kTxEchoElmue*)obj_to_host->frame;
    frame->header.size     = sizeof(kTxEchoElmue);
    frame->header.msg_type = MSG_TxEcho;
    frame->marker          = tx_event->MessageMarker;
    frame->timestamp       = tx_event->TxTimestamp; // 32 bit

    if ((GLB_UserFlags[channel] & USR_Timestamp) == 0)
        frame->header.size -= 4;

    // add the frame to list_to_host with IRQs disabled
    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
}

// append an error frame to the list_to_host
void buf_store_error(uint8_t channel)
{
    buf_class* usb_buf = buf_get_inst_for_usb(channel);

    kHostFrameObject* obj_to_host = buf_get_host_frame_locked(&usb_buf->list_host_pool);
    if (!obj_to_host)
        return; // buffer overflow! buf_process() will report this error to the host

    kHostFrameLegacy* frame_gs    = (kHostFrameLegacy*)obj_to_host->frame;
    kErrorElmue*      frame_elmue = (kErrorElmue*)     obj_to_host->frame;
    memset(frame_gs, 0, sizeof(kHostFrameLegacy));

    uint8_t* frame_data;
    if (GLB_ProtoElmue) // new ElmüSoft protocol
        frame_data = frame_elmue->err_data;
    else // legacy Geschwister Schneider protocol
        frame_data = frame_gs->pack_classic.data;

    uint32_t can_id = 0;

    // get errors that are still present after the last error_clear()
    kCanErrorState* state = error_get_state(channel);
    switch (state->bus_status)
    {
        case BUS_StatusOff:
            can_id |= ERID_Bus_is_off;
            break;
        case BUS_StatusPassive:
            if (state->tx_err_count > 0) frame_data[1] |= ER1_Tx_Passive_status_reached;
            if (state->rx_err_count > 0) frame_data[1] |= ER1_Rx_Passive_status_reached;
            break;
        case BUS_StatusWarning: // status Warning may be reported although there are only 60 errors (normally > 96) !!!
            if (state->tx_err_count > 0) frame_data[1] |= ER1_Tx_Errors_at_warning_level;
            if (state->rx_err_count > 0) frame_data[1] |= ER1_Rx_Errors_at_warning_level;
            break;
        default:
            if (state->back_to_active) // the bus has returned from a previous Warning, Passive or Off state to Active
                frame_data[1] |= ER1_Bus_is_back_active;
            break;
    }

    switch (state->last_proto_err)
    {
        case FDCAN_PROTOCOL_ERROR_ACK:
            can_id |= ERID_No_ACK_received;
            break;
        case FDCAN_PROTOCOL_ERROR_CRC:
            can_id |= ERID_CRC_Error;
            break;
        case FDCAN_PROTOCOL_ERROR_STUFF:
            frame_data[2] |= ER2_Bit_stuffing_error;
            break;
        case FDCAN_PROTOCOL_ERROR_FORM:
            frame_data[2] |= ER2_Frame_format_error;
            break;
        case FDCAN_PROTOCOL_ERROR_BIT1:
            frame_data[2] |= ER2_Unable_to_send_recessive_bit;
            break;
        case FDCAN_PROTOCOL_ERROR_BIT0:
            frame_data[2] |= ER2_Unable_to_send_dominant_bit;
            break;
    }

    if (!GLB_ProtoElmue) // legacy mode
    {
        // The host uses the new protocol    --> all the app_flags are sent in byte 5
        // The host uses the legacy protocol --> clone the flags to ID and Byte 1
        // There is no legacy error flag availabe to transmit APP_CanRxFail or APP_CanTxFail.
        if (state->app_flags & APP_CanTxTimeout)  can_id |= ERID_Tx_Timeout;
        if (state->app_flags & APP_UsbInOverflow) frame_data[1] |= ER1_Rx_Buffer_Overflow;
        if (state->app_flags & APP_CanTxOverflow) frame_data[1] |= ER1_Tx_Buffer_Overflow;

        // These flags are useless, the information is already in the bytes 1 and 2,
        // but for compatibility with legacy software they are also set.
        if (frame_data[1] > 0) can_id |= ERID_Controller_problem;
        if (frame_data[2] > 0) can_id |= ERID_Protocol_violation;
    }

    // Byte 5 was unused in legacy firmware. The new firmware transmits more error details here.
    // The legacy firmware only supported ER1_Rx/Tx_Buffer_Overflow. The app_flags give more details.
    frame_data[5] = state->app_flags;
	frame_data[6] = state->tx_err_count;
	frame_data[7] = state->rx_err_count;

    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        frame_elmue->header.size     = sizeof(kErrorElmue);
        frame_elmue->header.msg_type = MSG_Error;
        frame_elmue->err_id          = can_id; // the flag CAN_ID_Error is not needed as we have MSG_Error
        frame_elmue->timestamp       = system_get_timestamp();

        if ((GLB_UserFlags[channel] & USR_Timestamp) == 0)
            frame_elmue->header.size -= 4;
    }
    else // legacy Geschwister Schneider protocol
    {
        frame_gs->channel = channel;
        frame_gs->echo_id = ECHO_RxData;
        frame_gs->can_id  = can_id | CAN_ID_Error;
        frame_gs->can_dlc = 8;
        frame_gs->pack_classic.timestamp_us = system_get_timestamp();
    }

    // add the frame to list_to_host with IRQs disabled
    list_add_tail_locked(&obj_to_host->list, &usb_buf->list_to_host);
    error_clear(channel);
}

// ---------------------------------------------------------------------------------------------------

// Helper function: Get the the next frame after the head frame whith IRQs disabled
// returns NULL if there is no next frame
kHostFrameObject* buf_peek_host_frame_locked(list_item* list_head)
{
    system_disable_irq();
    kHostFrameObject* frame_obj = list_get_head_or_null(list_head, kHostFrameObject, list);
    system_enable_irq();
    return frame_obj;
}

// Helper function: Get the head frame and remove it from it's list whith IRQs disabled
// returns NULL if the list is empty
kHostFrameObject* buf_get_host_frame_locked(list_item* list_head)
{
    system_disable_irq();
    kHostFrameObject* frame_obj = list_get_head_or_null(list_head, kHostFrameObject, list);
    if (frame_obj)
        list_remove(&frame_obj->list); // remove frame_obj from it's list
    system_enable_irq();
    return frame_obj;
}

kCanFrameObject* buf_get_can_frame_locked(list_item* list_head)
{
    system_disable_irq();
    kCanFrameObject* frame_obj = list_get_head_or_null(list_head, kCanFrameObject, list);
    if (frame_obj)
        list_remove(&frame_obj->list); // remove frame_obj from it's list
    system_enable_irq();
    return frame_obj;
}

buf_class* buf_get_instance(uint8_t channel)
{
    return &buf_inst[channel];
}

// Although the multi channel firmware creates one USB interface for each CAN channel,
// The legacy protocol routes all traffic of all CAN channels through the first USB interface (EP 81 / 02) for backward compatibility.
// kHostFrameLegacy.channel tells the host which channel is the origin/destination of the packet.
buf_class* buf_get_inst_for_usb(uint8_t channel)
{
    if (GLB_ProtoElmue)
        return &buf_inst[channel]; // ElmüSoft -> send each CAN channel through it's own USB interface 0, 2 or 3
    else
        return &buf_inst[0];       // Legacy   -> send all CAN channels through USB interface 0
}
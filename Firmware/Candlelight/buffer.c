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

// ----- private enums
typedef enum
{
    FIFO_ReadNext, // read the current Fifo element and advance to the next
    FIFO_Peek,     // only read the current Fifo element
} eFifoRead;

// ----- private FIFO
void  FifoReset(kFifo* Fifo, void* Ptr,  int Size, int Capacity);
bool  FifoWrite(kFifo* Fifo, void* Data, int Size);
bool  FifoRead (kFifo* Fifo, void* Data, int Size, eFifoRead  e_Read);
// ----- private Methods
void       buf_process_host (uint8_t channel, buf_class* usb_buf);
void       buf_process_can  (uint8_t channel, buf_class* can_buf);
bool       buf_store_can_frame (uint8_t channel, uint8_t* can_frame);
void       buf_store_rx_packet_echo(uint8_t channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data, uint32_t fake_echo);
buf_class* buf_get_inst_for_usb(uint8_t channel);

// public
void buf_init()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        buf_clear_fifos(C, true, true);
    }
}

// public
void buf_clear_fifos(uint8_t channel, bool clear_can, bool clear_host)
{
    buf_class* inst = &buf_inst[channel];

    if (clear_can)
        FifoReset(&inst->can_fifo,  inst->can_buffer,  sizeof(kCanFrameObject), CAN_QUEUE_SIZE);
        
    if (clear_host)
        FifoReset(&inst->host_fifo, inst->host_buffer, sizeof(kHostFrameUnion), HOST_QUEUE_SIZE);
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
    if (can_buf->can_fifo .IsFull) error_assert(channel, APP_CanTxOverflow, false);
    if (usb_buf->host_fifo.IsFull) error_assert(channel, APP_UsbInOverflow, false);
}

// called from the main loop
// send a CAN packet to the host if host_buffer has data
void buf_process_host(uint8_t channel, buf_class* usb_buf)
{
    if (usb_buf->TxBusy)
        return; // USB IN transfer to the host is still in progress

    // only for testing: wait until there are 3 pending frames to be sent to the host in one blob
#if DEBUG_TEST_BLOB
    if (usb_buf->host_fifo.Count < 3)
        return;
#endif

    kHostFrameUnion k_HostFrame; // size = 80 byte
    if (!FifoRead(&usb_buf->host_fifo, &k_HostFrame, sizeof(k_HostFrame), FIFO_ReadNext))
        return; // nothing to be sent

    uint16_t len;
    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        // Using the optimized new ElmüSoft protocol reduces unnecessary USB overhead as it was sent by the legacy firmware.
        // If a CAN frame has only 2 data bytes, send only 2 data bytes over USB.
        // All ElmüSoft messages use the same header, no matter if CAN packet or an ASCII message.
        // If ELM_DevFlagSendUsbBlobs is set --> send multiple fames in one blob to the host.

        // Send blob with multiple frames
        if ((GLB_UserFlags[channel] & USR_SendBlobs) && !usb_buf->host_fifo.IsEmpty)
        {
            kBlob* blob = (kBlob*)usb_buf->to_host_buf;
            blob->frame_count = 0;
            blob->msg_type    = MSG_RxBlob;
            len = sizeof(kBlob);

            // Copy all frames in host_buffer into to_host_buf
            while (true)
            {
                uint16_t size = k_HostFrame.Header.size;
                memcpy(usb_buf->to_host_buf + len, &k_HostFrame, size);
                len += size;
                blob->frame_count ++;
                
                // frame_count is a byte --> max count = 255
                if (blob->frame_count > 250)
                    break;               
                
                // peek the next frame without incrementing the Fifo read index
                if (!FifoRead(&usb_buf->host_fifo, &k_HostFrame, sizeof(k_HostFrame), FIFO_Peek))
                    break;

                // check if the next frame still fits into to_host_buf
                if (len + k_HostFrame.Header.size >= MAX_BLOB_SIZE)
                    break;

                // read the next frame and increment the Fifo index
                FifoRead(&usb_buf->host_fifo, &k_HostFrame, sizeof(k_HostFrame), FIFO_ReadNext);
            }
        }
        else // only one ElmüSoft frame to be sent
        {
            len = k_HostFrame.Header.size;
            memcpy(usb_buf->to_host_buf, &k_HostFrame, len);
        }
    }
    else // legacy Geschwister Schneider protocol
    {
        // The legacy protocol is not intelligently designed. The timestamp is behind a fix 64 byte data array.
        // For CAN FD it sends ALWAYS 76 or 80 bytes over USB no matter how many bytes the frame really has.
        len = sizeof(kHostFrameLegacy); // 80 bytes
        if ((k_HostFrame.Legacy.flags & FRM_FDF) == 0) len -= 56;
        if ((GLB_UserFlags[k_HostFrame.Legacy.channel] & USR_Timestamp) == 0) len -= 4;

        memcpy(usb_buf->to_host_buf, &k_HostFrame, len);
    }

    // USBD_SendInDataToHost needs a buffer that remains unchanged until the send process has finished, not a local variable!
    USBD_SendInDataToHost(channel, usb_buf->to_host_buf, len);
}

// called from the main loop
// send a host packet to CAN bus if can_buffer has data
void buf_process_can(uint8_t channel, buf_class* can_buf)
{
    if (!can_is_tx_fifo_free(channel))
        return; // all 3 CAN Tx FIFO's are full
    
    kCanFrameObject k_CanFrame;
    if (!FifoRead(&can_buf->can_fifo, &k_CanFrame, sizeof(k_CanFrame), FIFO_ReadNext))
        return; // nothing to be sent

    // ------------------------------

    // abort if the silent mode is enabled or bus is off.
    if (can_is_tx_allowed(channel) != FBK_Success)
    {
        error_assert(channel, APP_CanTxFail, true); // both LED ON
        return; // do not send the message
    }

    can_send_packet(channel, &k_CanFrame.header, k_CanFrame.data);
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
        
        FDCAN_TxHeaderTypeDef* tx_header = &k_CanFrame.header;
        FDCAN_RxHeaderTypeDef  rx_header;
        rx_header.Identifier          = tx_header->Identifier;
        rx_header.IdType              = tx_header->IdType;
        rx_header.RxFrameType         = tx_header->TxFrameType;
        rx_header.DataLength          = tx_header->DataLength;
        rx_header.ErrorStateIndicator = tx_header->ErrorStateIndicator;
        rx_header.BitRateSwitch       = tx_header->BitRateSwitch;
        rx_header.FDFormat            = tx_header->FDFormat;

        buf_store_rx_packet_echo(channel, &rx_header, k_CanFrame.data, tx_header->MessageMarker);
    }
}

// public function
// Called from USB_DataOut() in usb_class.c in an interrupt
// Handle Tx blobs from the host
void buf_store_can_frame_blob(uint8_t channel, uint8_t* can_frame)
{
    kBlob* blob = (kBlob*)can_frame;
    if (GLB_ProtoElmue && blob->msg_type == MSG_TxBlob)
    {
        kFifo* can_fifo = &buf_inst[channel].can_fifo;
        if (can_fifo->Count + blob->frame_count > can_fifo->MaxCount)
        {
            // If the host has sent more packets than fit into the FIFO -> reject them all.
            // The host must send the entire blob again after a short delay.
            error_assert(channel, APP_CanTxOverflow, true); // both LED ON
            return; 
        }
        
        int offset = sizeof(kBlob);
        for (uint8_t i=0; i<blob->frame_count; i++)
        {
            kTxFrameElmue* tx_frame = (kTxFrameElmue*)(can_frame + offset);
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
            error_assert(channel, APP_CanTxFail, true); // both LED ON
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
    
    kCanFrameObject k_CanFrame;
    memcpy(&k_CanFrame.header, tx_header, sizeof(k_CanFrame.header));
    memcpy(&k_CanFrame.data,   tx_data,   sizeof(k_CanFrame.data));
    if (FifoWrite(&can_buf->can_fifo, &k_CanFrame, sizeof(k_CanFrame)))
        return true;

    // in case of buffer overflow inform the host immediately, so the host stops sending more packets and displays an error to the user.
    error_assert(channel, APP_CanTxOverflow, true); // Both LED's = ON
    return false;
}

// ---------------------------------------------------------------------------------------------------

// public function
// Enqueue a CAN Rx packet for the host.
// rx_data is a 64 byte buffer with the received / sent data bytes
// append the frame to host_buffer
void buf_store_rx_packet(uint8_t channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data)
{
    buf_store_rx_packet_echo(channel, rx_header, rx_data, ECHO_RxData);
}
// private function
// fake_echo is only used for legacy mode
void buf_store_rx_packet_echo(uint8_t channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *rx_data, uint32_t fake_echo)
{      
    uint32_t u32_Timestamp = system_get_timestamp();    

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

    kHostFrameUnion k_HostFrame; // size = 80 byte
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

        kRxFrameElmue* frame   = &k_HostFrame.RxFrame;
        frame->header.size     = sizeof(kRxFrameElmue) + byte_count;
        frame->header.msg_type = MSG_RxFrame;
        frame->flags           = flags;
        frame->can_id          = can_id;
        frame->timestamp       = u32_Timestamp;

        // Append only the data bytes that are in use.
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
        kHostFrameLegacy* frame = &k_HostFrame.Legacy;
        frame->channel  = channel;
        frame->reserved = 0;
        frame->flags    = flags;
        frame->can_id   = can_id;
        frame->can_dlc  = can_dlc;
        frame->echo_id  = fake_echo;
        memcpy(frame->raw_data, rx_data, 64);

        // In the inefficient legacy GS protocol the timestamp comes behind the data bytes
        // For CAN FD always 64 bytes are sent over USB although only 3 data bytes may be in use.
        if (rx_header->FDFormat == FDCAN_FD_CAN)
            frame->pack_FD.timestamp_us = u32_Timestamp;
        else // classic frame
            frame->pack_classic.timestamp_us = u32_Timestamp;
    }

    buf_class* usb_buf = buf_get_inst_for_usb(channel);
    FifoWrite(&usb_buf->host_fifo, &k_HostFrame, sizeof(k_HostFrame)); // Fifo overflow is reported in buf_process()
}

// a CAN packet from the Tx FIFO has been sent and acknowledged on CAN bus --> send marker to host.
// the legacy protocol never comes here. It sends a fake echo.
void buf_store_tx_echo(uint8_t channel, FDCAN_TxEventFifoTypeDef* tx_event)
{
    if (!GLB_ProtoElmue) // legacy protocol -> Tx Echo not supported
        return;

    kTxEchoElmue k_Echo;
    k_Echo.header.size     = sizeof(kTxEchoElmue);
    k_Echo.header.msg_type = MSG_TxEcho;
    k_Echo.marker          = tx_event->MessageMarker;
    k_Echo.timestamp       = system_get_timestamp();

    if ((GLB_UserFlags[channel] & USR_Timestamp) == 0)
        k_Echo.header.size -= 4;
    
    buf_class* usb_buf = buf_get_inst_for_usb(channel);    
    FifoWrite(&usb_buf->host_fifo, &k_Echo, sizeof(k_Echo)); // Fifo overflow is reported in buf_process()
}

// append an error frame to the host_buffer
void buf_store_error(uint8_t channel)
{
    uint32_t u32_Timestamp = system_get_timestamp();
    
    kHostFrameUnion k_HostFrame; // size = 80 byte
    memset(&k_HostFrame, 0, sizeof(k_HostFrame));    

    kHostFrameLegacy* frame_gs    = &k_HostFrame.Legacy;
    kErrorElmue*      frame_elmue = &k_HostFrame.Error;

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
        // APP_CanRxFail and APP_CanTxFail cannot be sent as there is no legacy error flag available.
        if (state->app_flags & APP_CanTxTimeout)  can_id        |= ERID_Tx_Timeout;
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
        frame_elmue->timestamp       = u32_Timestamp;

        if ((GLB_UserFlags[channel] & USR_Timestamp) == 0)
            frame_elmue->header.size -= 4;
    }
    else // legacy Geschwister Schneider protocol
    {
        frame_gs->channel = channel;
        frame_gs->echo_id = ECHO_RxData;
        frame_gs->can_id  = can_id | CAN_ID_Error;
        frame_gs->can_dlc = 8;
        frame_gs->pack_classic.timestamp_us = u32_Timestamp;
    }
    
    buf_class* usb_buf = buf_get_inst_for_usb(channel);
    FifoWrite(&usb_buf->host_fifo, &k_HostFrame, sizeof(k_HostFrame)); // Fifo overflow is reported in buf_process()

    error_clear(channel);
}

// Send a debug message or busload report to the host
bool buf_store_host_packet(uint8_t channel, void* packet, int size)
{
    buf_class* usb_buf = buf_get_inst_for_usb(channel);
    return FifoWrite(&usb_buf->host_fifo, packet, size); // Fifo overflow is reported in buf_process()
}

// ---------------------------------------------

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

// ============================================ FIFO ===================================================
// Fifo added by ElmüSoft
// This Fifo replaces the extremely ugly and clumsy ringbuffer of the legacy firmware

void FifoReset(kFifo* Fifo, void* Ptr, int Size, int Capacity)
{
    Fifo->Buffer   = (uint8_t*)Ptr;
    Fifo->ElemSize = Size;
    Fifo->MaxCount = Capacity;
    Fifo->Count    = 0;    
    Fifo->ReadIdx  = 0;
    Fifo->IsFull   = false;
    Fifo->IsEmpty  = true;
}

// e_Write == FIFO_Urgent --> insert a Tx error frame before the FIFO data to be reported immediately to the host.
bool FifoWrite(kFifo* Fifo, void* Data, int Size)
{
    system_disable_irq();
    
    bool Success = !Fifo->IsFull && Size <= Fifo->ElemSize;
    if (Success)
    {
        int WriteIdx = (Fifo->ReadIdx + Fifo->Count) % Fifo->MaxCount;
        uint8_t* Pointer = Fifo->Buffer + WriteIdx * Fifo->ElemSize;
        memcpy(Pointer, Data, Size);
        Fifo->Count ++;
        Fifo->IsFull  = Fifo->Count == Fifo->MaxCount;
        Fifo->IsEmpty = false;
    }
    
    system_enable_irq();
    return Success;
}

// e_Read == FIFO_Peek --> return the current read element without incrementing the read index
bool FifoRead(kFifo* Fifo, void* Data, int Size, eFifoRead e_Read)
{
    system_disable_irq();
    
    bool Success = !Fifo->IsEmpty && Size <= Fifo->ElemSize;
    if (Success)
    {
        uint8_t* Pointer = Fifo->Buffer + Fifo->ReadIdx * Fifo->ElemSize;
        memcpy(Data, Pointer, Size);
        
        if (e_Read == FIFO_ReadNext)
        {
            Fifo->ReadIdx = (Fifo->ReadIdx + 1) % Fifo->MaxCount;
            Fifo->Count --;
            Fifo->IsFull  = false;
            Fifo->IsEmpty = Fifo->Count == 0;
        }
    }
    
    system_enable_irq();
    return Success;
}


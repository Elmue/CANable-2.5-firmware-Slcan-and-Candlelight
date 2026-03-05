/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
    TODO: Check IRQ handling which seems wrong.
*/

#include "usb_interface.h"
#include "buffer.h"
#include "error.h"
#include "control.h"
#include "system.h"
#include "utils.h"

// ----- Globals
extern eUserFlags GLB_UserFlags[CHANNEL_COUNT];

// ----- Member
cdc_tx_buf   buf_cdc_tx = {0};
cdc_rx_buf   buf_cdc_rx = {0};
can_tx_buf   buf_can_tx[CHANNEL_COUNT] = {0};
char         slcan_str[SLCAN_MTU];
uint8_t      slcan_str_index = 0;

// ----- Private Methods
int32_t buf_frame_to_ascii(uint8_t *buf, bool b_TX, FDCAN_RxHeaderTypeDef* rx_header, uint8_t* frame_data);

void buf_init()
{
    buf_cdc_tx.head = 1;
}

void buf_clear_can_buffer(int channel)
{
    can_tx_buf* txbuf = &buf_can_tx[channel];
    
    txbuf->tail = txbuf->head;
    txbuf->send = txbuf->head;
    txbuf->full = false;
}

// This function is called approx 100 times in one millisecond from the main loop
void buf_process(int channel, uint32_t tick_now)
{
    // disable interrupts because buf_cdc_rx.head is modified in the interrupt callback CDC_Receive_FS()
    system_disable_irq();
    uint32_t tmp_head = buf_cdc_rx.head;
    system_enable_irq();
    
    if (buf_cdc_rx.tail != tmp_head)
    {
        // Fill up slcan_str until a carriage return is found, then parse the command
        for (uint32_t i = 0; i < buf_cdc_rx.msglen[buf_cdc_rx.tail]; i++)
	    {
            if (buf_cdc_rx.data[buf_cdc_rx.tail][i] == '\r')
            {
                control_parse_command(slcan_str, slcan_str_index);
                slcan_str_index = 0;
            }
            else
            {
                // Check for overflow of buffer
                if (slcan_str_index >= SLCAN_MTU)
                {
                    error_assert(channel, APP_UsbInOverflow, false);
                    slcan_str_index = 0;
                }
                slcan_str[slcan_str_index++] = buf_cdc_rx.data[buf_cdc_rx.tail][i];
            }
        }

        // Move on to next buffer
        system_disable_irq();
        buf_cdc_rx.tail = (buf_cdc_rx.tail + 1) % BUF_CDC_RX_NUM_BUFS;
        system_enable_irq();
    }

    // Process CDC transmit buffer
    uint32_t new_head = (buf_cdc_tx.head + 1) % BUF_CDC_TX_NUM_BUFS;
    if (new_head != buf_cdc_tx.tail)
    {
        if (0 < buf_cdc_tx.msglen[buf_cdc_tx.head])
        {
            buf_cdc_tx.head = new_head;
            buf_cdc_tx.msglen[new_head] = 0;
        }
    }
    
    system_disable_irq();
    uint32_t new_tail = (buf_cdc_tx.tail + 1) % BUF_CDC_TX_NUM_BUFS;
    if (new_tail != buf_cdc_tx.head)
    {
        if (CDC_Transmit_FS((uint8_t *)buf_cdc_tx.data[new_tail], buf_cdc_tx.msglen[new_tail]) == USBD_OK)
        {
            buf_cdc_tx.tail = new_tail;
        }
    }
    system_enable_irq();
    
    // ------ Process can transmit buffer
    
    can_tx_buf* txbuf = &buf_can_tx[channel];
    while ((txbuf->send != txbuf->head || txbuf->full) && can_is_tx_fifo_free(channel))
    {
        // Transmit can frame
        can_send_packet(channel, &txbuf->header[txbuf->send], txbuf->data[txbuf->send]);
        
        // At this point the Tx packet is in the CAN Tx FIFO, but it has not yet been transmitted to CAN bus.

        txbuf->send = (txbuf->send + 1) % BUF_CAN_TXQUEUE_LEN;
        txbuf->tail = (txbuf->tail + 1) % BUF_CAN_TXQUEUE_LEN;
        txbuf->full = false;
    }
    
    // report buffer full always --> green + blue LED are permanently ON
    if (txbuf->full)
        error_assert(channel, APP_CanTxOverflow, false);
}

// Enqueue data for transmission over USB CDC to host 
void buf_enqueue_cdc(int channel, char* buf, uint16_t len)
{
    if (BUF_CDC_TX_BUF_SIZE - len < buf_cdc_tx.msglen[buf_cdc_tx.head] + 1) // +1 for channel identidier
    {
        error_assert(channel, APP_UsbInOverflow, false); // The data does not fit in the buffer
        return;
    }

    char* cdc_data = &buf_cdc_tx.data[buf_cdc_tx.head][buf_cdc_tx.msglen[buf_cdc_tx.head]];

#if CHANNEL_COUNT > 1
    if (channel > 0)
    {
        // store channel identidier character        
        cdc_data[0] = (channel > 1) ? '$' : '&';
        cdc_data ++;
        buf_cdc_tx.msglen[buf_cdc_tx.head] ++;
    }
#endif

    // copy data
    memcpy(cdc_data, buf, len);
    buf_cdc_tx.msglen[buf_cdc_tx.head] += len;
}

// -------------------------------------

// Get destination pointer of can tx frame header
bool buf_get_can_dest(int channel, FDCAN_TxHeaderTypeDef** tx_header, uint8_t** tx_data)
{
    can_tx_buf* txbuf = &buf_can_tx[channel];
    if (txbuf->full)
    {
        error_assert(channel, APP_CanTxOverflow, false);
        return false;
    }
    *tx_header = &txbuf->header[txbuf->head];
    *tx_data   = txbuf->data   [txbuf->head];
    return true;
}

// Append the message in destination slot to the buffer.
eFeedback buf_commit_can_dest(int channel)
{
    eFeedback e_Feedback = can_is_tx_allowed(channel);
    if (e_Feedback != FBK_Success)
        return e_Feedback;
    
    can_tx_buf* txbuf = &buf_can_tx[channel];
    if (txbuf->full)
    {
        error_assert(channel, APP_CanTxOverflow, false);
        return FBK_TxBufferFull;
    }

    // Increment the head pointer
    txbuf->head = (txbuf->head + 1) % BUF_CAN_TXQUEUE_LEN;
    if (txbuf->head == txbuf->tail) 
        txbuf->full = true;
    
    return FBK_Success;
}

// ===========================================================================

// a RX packet has been received from CAN bus or a Tx Packet has been successfully sent to CAN bus
// frame_data is a 64 byte buffer with the received / sent data bytes
void buf_store_rx_packet(int channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *frame_data)
{
    char buf[SLCAN_MTU];
    if (rx_header->FDFormat == FDCAN_CLASSIC_CAN)
    {
        if (rx_header->RxFrameType == FDCAN_REMOTE_FRAME) buf[0] = 'r'; // 'R' for 29 bit (remote frame)
        else                                              buf[0] = 't'; // 'T' for 29 bit (FDCAN_DATA_FRAME)
    }
    else // CAN FD (remote frames do not exist in CAN FD)
    {
        if (rx_header->BitRateSwitch == FDCAN_BRS_ON)     buf[0] = 'b'; // Frame with BRS enabled  'B' for 29 bit
        else                                              buf[0] = 'd'; // Frame with BRS disabled 'D' for 29 bit
    }

    uint8_t id_len = 3;    
    if (rx_header->IdType == FDCAN_EXTENDED_ID)
    {
        id_len = 8;        
        buf[0] -= 32; // make uppercase for 29 bit ID
    }
    
    // Add identifier 
    uint32_t ident = rx_header->Identifier;    
    for (uint8_t j = id_len; j > 0; j--)
    {
        buf[j] = utils_nibble_to_ascii(ident & 0xF);
        ident >>= 4;
    }
    uint8_t pos = 1 + id_len;
    
    // Add DLC
    uint32_t dlc_code = rx_header->DataLength;
    buf[pos++]        = utils_nibble_to_ascii(dlc_code);

    // Add data bytes (not for remote frames)
    if (rx_header->RxFrameType != FDCAN_REMOTE_FRAME)
    {
        int8_t byte_count = utils_dlc_to_byte_count(dlc_code); // returns -1 if invalid
        for (uint8_t j = 0; j < byte_count; j++)
        {
            buf[pos++] = utils_nibble_to_ascii(frame_data[j] >> 4);
            buf[pos++] = utils_nibble_to_ascii(frame_data[j] & 0x0F);
        }
    }
    
    if (GLB_UserFlags[channel] & USR_ReportESI) // Append ESI Error Passive status if enabled by the user
    {
        if (rx_header->FDFormat            == FDCAN_FD_CAN &&
            rx_header->ErrorStateIndicator == FDCAN_ESI_PASSIVE)
            buf[pos++] = 'S';
    }   

    buf[pos++] = '\r';
    buf_enqueue_cdc(channel, buf, pos);
}

// Send the same message marker to the host that has been sent4 with the Tx packet
void buf_store_tx_echo(int channel, FDCAN_TxEventFifoTypeDef* tx_event)
{
    char buf[10];
    sprintf(buf, "M%02X\r", (uint8_t)tx_event->MessageMarker);
    buf_enqueue_cdc(channel, buf, 4);
}
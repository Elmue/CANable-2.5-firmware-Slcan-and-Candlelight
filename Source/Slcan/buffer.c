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

extern eUserFlags USER_Flags;

volatile struct buf_cdc_tx buf_cdc_tx = {0};
volatile struct buf_cdc_rx buf_cdc_rx = {0};
static   struct buf_can_tx buf_can_tx = {0};
static uint8_t slcan_str[SLCAN_MTU];
static uint8_t slcan_str_index = 0;

int32_t buf_frame_to_ascii(uint8_t *buf, bool b_TX, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *frame_data);

// Initializes
void buf_init()
{
    buf_cdc_rx.head = 0;
    buf_cdc_rx.tail = 0;

    buf_cdc_tx.head = 1;
    buf_cdc_tx.msglen[buf_cdc_tx.head] = 0;
    buf_cdc_tx.tail = 0;
    buf_cdc_tx.msglen[buf_cdc_tx.tail] = 0;

    buf_can_tx.head = 0;
    buf_can_tx.send = 0;
    buf_can_tx.tail = 0;
    buf_can_tx.full = 0;
}

// Clear can tx buffer
void buf_clear_can_buffer()
{
    buf_can_tx.tail = buf_can_tx.head;
    buf_can_tx.send = buf_can_tx.head;
    buf_can_tx.full = 0;
}

// This function is called approx 100 times in one millisecond from the main loop
void buf_process(uint32_t tick_now)
{
    // disable interrupts because buf_cdc_rx.head is modified in the interrupt callback CDC_Receive_FS()
    system_disable_irq();
    uint32_t tmp_head = buf_cdc_rx.head;
    system_enable_irq();
    
    if (buf_cdc_rx.tail != tmp_head)
    {
        // Process one whole buffer
        for (uint32_t i = 0; i < buf_cdc_rx.msglen[buf_cdc_rx.tail]; i++)
	    {
            if (buf_cdc_rx.data[buf_cdc_rx.tail][i] == '\r')
            {
                control_parse_command((char*)slcan_str, slcan_str_index);
                slcan_str_index = 0;
            }
            else
            {
                // Check for overflow of buffer
                if (slcan_str_index >= SLCAN_MTU)
                {
                    // TODO: Return here and discard this CDC buffer?
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

    // Process can transmit buffer
    while ((buf_can_tx.send != buf_can_tx.head || buf_can_tx.full) && (HAL_FDCAN_GetTxFifoFreeLevel(can_get_handle()) > 0))
    {
        // Transmit can frame
        if (can_send_packet(&buf_can_tx.header[buf_can_tx.send], buf_can_tx.data[buf_can_tx.send]))
        {
            buf_can_tx.send = (buf_can_tx.send + 1) % BUF_CAN_TXQUEUE_LEN;
            buf_can_tx.tail = (buf_can_tx.tail + 1) % BUF_CAN_TXQUEUE_LEN;
            buf_can_tx.full = 0;
        }
    }
    
    // report buffer full always --> green + blue LED are permanently ON
    if (buf_can_tx.full)
        error_assert(APP_CanTxOverflow, false);
}

// Enqueue data for transmission over USB CDC to host 
void buf_enqueue_cdc(char* buf, uint16_t len)
{
    if (BUF_CDC_TX_BUF_SIZE - len < buf_cdc_tx.msglen[buf_cdc_tx.head])
    {
        error_assert(APP_UsbInOverflow, false); // The data does not fit in the buffer
    }
    else
    {
        // Copy data
        memcpy((uint8_t *)&buf_cdc_tx.data[buf_cdc_tx.head][buf_cdc_tx.msglen[buf_cdc_tx.head]], buf, len);
        buf_cdc_tx.msglen[buf_cdc_tx.head] += len;
    }
}

// Get destination pointer of cdc buffer (Start position of write access)
uint8_t *buf_get_cdc_dest()
{
    if (BUF_CDC_TX_BUF_SIZE - SLCAN_MTU < buf_cdc_tx.msglen[buf_cdc_tx.head])
    {
        error_assert(APP_UsbInOverflow, false); // The data will not fit in the buffer
        return NULL;
    }
    return (uint8_t *)&buf_cdc_tx.data[buf_cdc_tx.head][buf_cdc_tx.msglen[buf_cdc_tx.head]];
}

// Send the data bytes in destination area over USB CDC to host
void buf_comit_cdc_dest(uint32_t len)
{
    buf_cdc_tx.msglen[buf_cdc_tx.head] += len;
}

// Get destination pointer of can tx frame header
FDCAN_TxHeaderTypeDef *buf_get_can_dest_header()
{
    if (buf_can_tx.full)
    {
        error_assert(APP_CanTxOverflow, false);
        return NULL;
    }
    return &buf_can_tx.header[buf_can_tx.head];
}

// Get destination pointer of can tx frame data bytes
uint8_t *buf_get_can_dest_data()
{
    if (buf_can_tx.full)
    {
        error_assert(APP_CanTxOverflow, false);
        return NULL;
    }
    return buf_can_tx.data[buf_can_tx.head];
}

// Append the message in destination slot to the buffer.
eFeedback buf_comit_can_dest()
{
    eFeedback e_Feedback = can_is_tx_allowed();
    if (e_Feedback != FBK_Success)
        return e_Feedback;
    
    if (buf_can_tx.full)
    {
        error_assert(APP_CanTxOverflow, false);
        return FBK_TxBufferFull;
    }

    // Increment the head pointer
    buf_can_tx.head = (buf_can_tx.head + 1) % BUF_CAN_TXQUEUE_LEN;
    if (buf_can_tx.head == buf_can_tx.tail) 
        buf_can_tx.full = 1;
    
    return FBK_Success;
}

// ===========================================================================

// a RX packet has been received from CAN bus or a Tx Packet has been successfully sent to CAN bus
// frame_data is a 64 byte buffer with the received / sent data bytes
void buf_store_rx_packet(FDCAN_RxHeaderTypeDef *rx_header, uint8_t *frame_data)
{
    uint8_t *buf = buf_get_cdc_dest();
    if (buf == NULL) 
        return; // buffer is full
    
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
    // Shift bits down from FIFO register
    // It is stupid that ST Microelectronics did not define a processor independent macro for this shift operation.
    // Will other processors also need this to be shifted 16 bits down ??
    uint32_t dlc_code = rx_header->DataLength >> 16;    
    buf[pos++]        = utils_nibble_to_ascii  (dlc_code);
    int8_t byte_count = utils_dlc_to_byte_count(dlc_code); // returns -1 if invalid

    // Add data bytes (not for remote frames)
    if (rx_header->RxFrameType != FDCAN_REMOTE_FRAME)
    {
        for (uint8_t j = 0; j < byte_count; j++)
        {
            buf[pos++] = utils_nibble_to_ascii(frame_data[j] >> 4);
            buf[pos++] = utils_nibble_to_ascii(frame_data[j] & 0x0F);
        }
    }
    
    if (USER_Flags & USR_ReportESI) // Append ESI Error Passive status if enabled by the user
    {
        if (rx_header->FDFormat            == FDCAN_FD_CAN &&
            rx_header->ErrorStateIndicator == FDCAN_ESI_PASSIVE)
            buf[pos++] = 'S';
    }   

    buf[pos++] = '\r';
    buf_comit_cdc_dest(pos);
}

// Send the same message marker to the host that has been sent4 with the Tx packet
void buf_store_tx_echo(FDCAN_TxEventFifoTypeDef* tx_event)
{
    char* buf = (char*)buf_get_cdc_dest();
    if (buf == NULL) 
        return; // buffer is full

    sprintf(buf, "M%02X\r", (uint8_t)tx_event->MessageMarker);
    buf_comit_cdc_dest(4);
}
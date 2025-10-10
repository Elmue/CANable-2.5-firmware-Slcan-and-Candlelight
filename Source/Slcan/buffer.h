/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once
#include "can.h"
#include "usb_class.h"

// Maximum command buffer len (z/Z plus frame 138 plus timestamp 8 plus ESI plus \r plus some padding
#define SLCAN_MTU (1 + 138 + 8 + 1 + 1 + 16) 

// CDC receive buffering
#define BUF_CDC_RX_NUM_BUFS    8
#define BUF_CDC_RX_BUF_SIZE    CDC_DATA_FS_MAX_PACKET_SIZE // = 64 Size of RX buffer item

// CDC transmit buffering (packets + debug messages)
#define BUF_CDC_TX_NUM_BUFS    3
#define BUF_CDC_TX_BUF_SIZE    4096 // Set to 64 * 64 for max single packet size

// CAN transmit buffering
#define BUF_CAN_TXQUEUE_LEN    64   // Number of buffers allocated
#define CAN_MAX_DATALEN        64   // CAN maximum data length. Must be 64 for canfd.

// Receive buffering: circular buffer FIFO
// buf_cdc_rx is written in the interrupt handler CDC_Receive_FS() where ASCII characters are received
// when a Crarriage Return is found they are passed to control_parse_command()
struct buf_cdc_rx
{
	uint8_t  data  [BUF_CDC_RX_NUM_BUFS][BUF_CDC_RX_BUF_SIZE];
	uint32_t msglen[BUF_CDC_RX_NUM_BUFS];
	uint32_t head;
	uint32_t tail;
};

// Transmit buffering: triple buffers
// buf_cdc_tx is written in buf_enqueue_cdc() when the firmware sends ASCII characters to the host
struct buf_cdc_tx
{
	uint8_t  data  [BUF_CDC_TX_NUM_BUFS][BUF_CDC_TX_BUF_SIZE];
	uint32_t msglen[BUF_CDC_TX_NUM_BUFS];
	uint32_t head;
	uint32_t tail;
};

// Cirbuf structure for CAN TX frames
// buf_can_tx is written in control_parse_command() -> buf_comit_can_dest() when a frame has been received from the host
struct buf_can_tx
{
    FDCAN_TxHeaderTypeDef header[BUF_CAN_TXQUEUE_LEN];   // Header buffer
    uint8_t  data[BUF_CAN_TXQUEUE_LEN][CAN_MAX_DATALEN]; // Data buffer
    uint16_t head;                                       // Head pointer
    uint16_t send;                                       // Send pointer
    uint16_t tail;                                       // Tail pointer
    uint8_t  full;                                       // Set this when we are full, clear when the tail moves one.
};

extern volatile struct buf_cdc_tx buf_cdc_tx;
extern volatile struct buf_cdc_rx buf_cdc_rx;

void buf_init();
void buf_process(uint32_t tick_now);

void buf_enqueue_cdc(char* buf, uint16_t len);
uint8_t *buf_get_cdc_dest();
void buf_comit_cdc_dest(uint32_t len);

FDCAN_TxHeaderTypeDef *buf_get_can_dest_header();
uint8_t *buf_get_can_dest_data();
eFeedback buf_comit_can_dest();
void buf_clear_can_buffer();
void buf_store_tx_echo(FDCAN_TxEventFifoTypeDef* tx_event);
void buf_store_rx_packet(FDCAN_RxHeaderTypeDef *frame_header, uint8_t *frame_data);



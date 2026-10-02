/*
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * FreeBSD License
*/

#pragma once
#include "system.h"
#include "error.h"
#include "candlelight_def.h"
#include "usb_def.h"

// If 3 Tx messages are in the Tx FIFO of the processor while 64 more Tx messages are in list_to_host, we have 67 messages waiting for an ACK.
// If now another adapter is opened and acknowledges them all we are flooded with 67 Tx events to be sent to the host.
// So the host buffer should be larger than the CAN buffer to avoid error APP_UsbInOverflow.
#define CAN_QUEUE_SIZE      64
#define HOST_QUEUE_SIZE     70

typedef struct 
{
    uint8_t*   Buffer;   // private, fix! 
    int        ElemSize; // private, fix! The size of one element in Buffer in bytes
    int        MaxCount; // private, fix! The maximum count of elements that Buffer can store
    int        ReadIdx;  // private!      The read index in Buffer
    __IO int   Count;    // read only!    The count of elements that are currently stored in Buffer    
    __IO bool  IsFull;   // read only!    Interrupt-safe reading FIFO status. This error is reported in buf_process()
    __IO bool  IsEmpty;  // read only!    Interrupt-safe reading FIFO status
} kFifo;

// sent to CAN bus
typedef struct 
{
    FDCAN_TxHeaderTypeDef header;  
    uint8_t               data[64];   
} kCanFrameObject;

typedef struct 
{
    // Currently a USB packet is sent to the host --> wait until the bus is free for the next packet.
    __IO bool  TxBusy;
    // Send a Zero Length Packet after the IN transfer
    __IO bool  SendZLP;
    
    // This was totally wrong in the legacy firmware.
    // They used only one pool buffer for everything.
    // If you sent more than 64 messages to the CAN bus, but no ACK was received, the buffer got full.
    // The sloppy firmware did not even set an error flag.
    // But even if it would, it would have been useless, because if the one and only buffer is full,
    // not even an error message could be sent to the host.
    // So the adapter simply stopped responding and was dead.
    // Addionally due to another bug it could even crash when the buffer got full.
    kCanFrameObject   can_buffer [CAN_QUEUE_SIZE];
    kHostFrameUnion   host_buffer[HOST_QUEUE_SIZE];   
    kFifo             can_fifo;  // manage can_buffer
    kFifo             host_fifo; // manage host_buffer
       
    // ATTENTION:
    // The legacy Candlelight firmware from Github was competely buggy.
    // Instead of these fix buffers they used pointers to the ringbuffer which is totally wrong.
    // The result was an adapter not sending anymore and even crashes when the buffer got full!
    // Nobody ever noticed that because of a complete lack of proper error handling.
    // The legacy firmware did not even set an error flag when a buffer overflow occurred.
    uint8_t  to_host_buf  [MAX_BLOB_SIZE]; // stores USB IN  data during transmission (fixed by ElmüSoft)
    uint8_t  from_host_buf[MAX_BLOB_SIZE]; // stores USB OUT data after reception     (fixed by ElmüSoft)   
    
}  __attribute__ ((aligned (4))) buf_class;

// ----------------------------------------------------------------------------------------

void buf_init();
void buf_process(uint8_t channel, uint32_t tick_now);
void buf_clear_fifos(uint8_t channel, bool clear_can, bool clear_host);
void buf_store_error(uint8_t channel);
void buf_store_can_frame_blob(uint8_t channel, uint8_t* can_frame);
bool buf_store_tx_packet   (uint8_t channel, FDCAN_TxHeaderTypeDef* tx_header, uint8_t* tx_data);
void buf_store_rx_packet   (uint8_t channel, FDCAN_RxHeaderTypeDef* rx_header, uint8_t* rx_data);
void buf_store_tx_echo     (uint8_t channel, FDCAN_TxEventFifoTypeDef* tx_event);
bool buf_store_host_packet (uint8_t channel, void* packet, int size);
buf_class* buf_get_instance(uint8_t channel);
     

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

// ----------------------------------------------------------------------------------------

#define container_of(ptr, type, member) \
	({ \
		__typeof(((type *)0)->member) *_p = (ptr); \
		(type *)((char *)_p - offsetof(type, member)); \
	})

#define list_entry(ptr, type, field)             container_of(ptr, type, field)
#define list_get_head(ptr, type, member)         list_entry((ptr)->next, type, member)
#define list_get_head_or_null(ptr, type, member) (!list_is_empty(ptr) ? list_get_head(ptr, type, member) : NULL)

// implements a double chained list as ringbuffer.
typedef struct list_item
{
    struct list_item *next;
    struct list_item *prev;
} list_item;

// --------------

// set empty
static inline void list_init(list_item *head)
{
    head->next = head; 
    head->prev = head;
}

static inline bool list_is_empty(const list_item *head)
{
    return head->next == head;
}

// static inline bool list_is_full(const list_item *head)
// {
//     The list will never be full, as only items are added and removed.
//     Items are taken from the pool and inserted into list_to_host / list_to_can as needed.
//     When the list items are not used anymore they are given back to the pool.
//     So after initialization the pool is full and the other list is empty.
// }

// for debugging: returns 0 ... CAN_QUEUE_SIZE or HOST_QUEUE_SIZE
static inline int count_free_entries(const list_item *head)
{
    list_item* item = head->next;
    for (int i=0; true; i++)
    {
        if (item == head)
            return i;
        
        item = item->next;
    }
}

// removes entry form it's list by connecting the previous element directly to the next in both directions.
static inline void list_remove(list_item *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
}

// insert item between prev and next. This requires to modify 4 pointers.
static inline void list_insert(list_item *item, list_item *prev, list_item *next)
{
    next->prev = item;
    item->next = next;
    item->prev = prev;
    prev->next = item;
}

// insert item at the begin of the list
static inline void list_add_head(list_item *item, list_item *head)
{
    list_insert(item, head, head->next);
}
static inline void list_add_head_locked(list_item *entry, list_item *head)
{
    system_disable_irq();
    list_add_head(entry, head);
    system_enable_irq();
}

// insert item at the end of the list
static inline void list_add_tail(list_item *item, list_item *head)
{
    list_insert(item, head->prev, head);
}
static inline void list_add_tail_locked(list_item *entry, list_item *head)
{
    system_disable_irq();
    list_add_tail(entry, head);
    system_enable_irq();
}

// ----------------------------------------------------------------------------------------

typedef struct 
{
    list_item        list;
    kHostFrameLegacy frame;
} kHostFrameObject;

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
    kHostFrameObject   can_pool_buffer [CAN_QUEUE_SIZE];
    kHostFrameObject   host_pool_buffer[HOST_QUEUE_SIZE];   

    // The frame pool contains 64 kHostFrameObject's
    // These can be taken and appended to list_to_can or list_to_host.
    // When they are not used anymore they must be given back to the pool.
    // When the frame pool is empty no more data can be sent, a buffer overflow error is generated.
    list_item  list_can_pool;   // initialized to point to can_pool_buffer
    list_item  list_host_pool;  // initialized to point to host_pool_buffer
    list_item  list_to_can;     // FIFO for packtes USB --> CAN bus
    list_item  list_to_host;    // FIFO for packtes CAN bus --> USB
       
    // ATTENTION:
    // The legacy Candlelight firmware from Github was competely buggy.
    // Instead of these fix buffers they used pointers to the ringbuffer which is totally wrong.
    // The result was an adapter not sending anymore and even crashes when the buffer got full!
    // Nobody ever noticed that because of a complete lack of proper error handling.
    // The legacy firmware did not even set an error flag when a buffer overflow occurred.
    uint8_t    to_host_buf  [sizeof(kHostFrameLegacy)]; // stores USB IN  data during transmission (fixed by ElmüSoft)
    uint8_t    from_host_buf[sizeof(kHostFrameLegacy)]; // stores USB OUT data after reception     (fixed by ElmüSoft)   
    
}  __attribute__ ((aligned (4))) buf_class;

// ----------------------------------------------------------------------------------------

void buf_init();
void buf_process(int channel, uint32_t tick_now);
void buf_clear_can_buffer(int channel);
void buf_store_error(int channel);
void buf_store_rx_packet(int channel, FDCAN_RxHeaderTypeDef *rx_header, uint8_t *frame_data);
void buf_store_tx_echo(int channel, FDCAN_TxEventFifoTypeDef* tx_event);
buf_class* buf_get_instance(int channel);
buf_class* buf_get_inst_for_usb(int channel);
kHostFrameObject* buf_get_frame_locked(list_item* list_head);
     

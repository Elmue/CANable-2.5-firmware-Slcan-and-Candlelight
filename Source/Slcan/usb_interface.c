  /*******************************************************************************
  * @file    usb_interface.c
  * @author  MCD Application Team
  * @brief   This file provides the CDC interface (virtual COM port).
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at: www.st.com/SLA0044
  *
  ******************************************************************************/

#include "usb_interface.h"
#include "buffer.h"
#include "error.h"
#include "system.h"

extern USBD_HandleTypeDef USB_Device;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_InterfaceCallbacks =
{
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS
};

// Initializes the CDC media low layer over the FS USB IP
static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&USB_Device, (uint8_t *)buf_cdc_tx.data[buf_cdc_tx.tail], 0);
    USBD_CDC_SetRxBuffer(&USB_Device, (uint8_t *)buf_cdc_rx.data[buf_cdc_rx.head]);
    return (USBD_OK);
}

// DeInitializes the CDC media low layer
static int8_t CDC_DeInit_FS(void)
{
    return (USBD_OK);
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
    switch (cmd)
    {
        case CDC_SEND_ENCAPSULATED_COMMAND:
            break;

        case CDC_GET_ENCAPSULATED_RESPONSE:
            break;

        case CDC_SET_COMM_FEATURE:
            break;

        case CDC_GET_COMM_FEATURE:
            break;

        case CDC_CLEAR_COMM_FEATURE:
            break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
        case CDC_SET_LINE_CODING:
            break;

        case CDC_GET_LINE_CODING:
            pbuf[0] = (uint8_t)(115200);
            pbuf[1] = (uint8_t)(115200 >> 8);
            pbuf[2] = (uint8_t)(115200 >> 16);
            pbuf[3] = (uint8_t)(115200 >> 24);
            pbuf[4] = 0; // stop bits (1)
            pbuf[5] = 0; // parity (none)
            pbuf[6] = 8; // number of bits (8)
            break;

        case CDC_SET_CONTROL_LINE_STATE:
            // The hosts sets the lines DTR or RTS.
            break;

        case CDC_SEND_BREAK:
            break;
    }

    return (USBD_OK);
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will block any OUT packet reception on USB endpoint
  *         until exiting this function. If you exit this function before transfer
  *         is complete on CDC interface (ie. using DMA controller) it will result
  *         in receiving more data while previous ones are still not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
    // Check for overflow!
    uint32_t new_head = (buf_cdc_rx.head + 1) % BUF_CDC_RX_NUM_BUFS;
    if (new_head == buf_cdc_rx.tail)
    {
        error_assert(APP_CanTxOverflow, false);

        // Listen again on the same buffer. Old data will be overwritten.
        USBD_CDC_SetRxBuffer(&USB_Device, (uint8_t *)buf_cdc_rx.data[buf_cdc_rx.head]);
        USBD_CDC_ReceivePacket(&USB_Device);
        return HAL_ERROR;
    }
    else
    {
        // Save off length
        buf_cdc_rx.msglen[buf_cdc_rx.head] = *Len;
        buf_cdc_rx.head = new_head;

        // Start listening on next buffer. Previous buffer will be processed in main loop.
        USBD_CDC_SetRxBuffer(&USB_Device, (uint8_t *)buf_cdc_rx.data[buf_cdc_rx.head]);
        USBD_CDC_ReceivePacket(&USB_Device);
        return (USBD_OK);
    }
}

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
    uint8_t result = USBD_OK;

    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)USB_Device.pClassData;
    if (hcdc->TxState != 0)
        return USBD_BUSY;

    USBD_CDC_SetTxBuffer(&USB_Device, Buf, Len);
    result = USBD_CDC_TransmitPacket(&USB_Device);

    return result;
}


  /*******************************************************************************
  * @file    usb_ioreq.c
  * @author  MCD Application Team
  * @brief   This file provides the IO requests APIs for control endpoints.
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

#include "usb_ioreq.h"

extern USBD_HandleTypeDef  USB_Handle;

// @brief  send data on the ctl pipe
// @param  buff: pointer to data buffer
// @param  len: length of data to be sent
// @retval status
USBD_StatusTypeDef USBD_CtlSendData(uint8_t *pbuf, uint16_t len)
{
  /* Set EP0 State */
  USB_Handle.ep0_state = USBD_EP0_DATA_IN;
  USB_Handle.ep_in[0].total_length = len;
  USB_Handle.ep_in[0].rem_length   = len;

  /* Start the transfer */
  USBD_LL_Transmit(0, pbuf, len);
  return USBD_OK;
}

// @brief  continue sending data on the ctl pipe
// @param  buff: pointer to data buffer
// @param  len: length of data to be sent
// @retval status
USBD_StatusTypeDef USBD_CtlContinueSendData(uint8_t *pbuf, uint16_t len)
{
  /* Start the next transfer */
  USBD_LL_Transmit(0, pbuf, len);
  return USBD_OK;
}

// @brief  receive data on the ctl pipe
// @param  buff: pointer to data buffer
// @param  len: length of data to be received
// @retval status
USBD_StatusTypeDef USBD_CtlPrepareRx(uint8_t *pbuf, uint16_t len)
{
  /* Set EP0 State */
  USB_Handle.ep0_state = USBD_EP0_DATA_OUT;
  USB_Handle.ep_out[0].total_length = len;
  USB_Handle.ep_out[0].rem_length   = len;

  /* Start the transfer */
  USBD_LL_PrepareReceive(0, pbuf, len);
  return USBD_OK;
}

// @brief  continue receive data on the ctl pipe
// @param  buff: pointer to data buffer
// @param  len: length of data to be received
// @retval status
USBD_StatusTypeDef USBD_CtlContinueRx(uint8_t *pbuf, uint16_t len)
{
  USBD_LL_PrepareReceive(0, pbuf, len);
  return USBD_OK;
}

// @brief  send zero length packet on the ctl pipe
// @retval status
USBD_StatusTypeDef USBD_CtlSendStatus()
{
  /* Set EP0 State */
  USB_Handle.ep0_state = USBD_EP0_STATUS_IN;

  /* Start the transfer */
  USBD_LL_Transmit(0, NULL, 0);
  return USBD_OK;
}

// @brief  receive zero length packet on the ctl pipe
// @retval status
USBD_StatusTypeDef USBD_CtlReceiveStatus()
{
  /* Set EP0 State */
  USB_Handle.ep0_state = USBD_EP0_STATUS_OUT;

  /* Start the transfer */
  USBD_LL_PrepareReceive(0, NULL, 0);
  return USBD_OK;
}

// @brief  returns the received data length
// @param  ep_addr: endpoint address
// @retval Rx Data blength
uint32_t USBD_GetRxCount(uint8_t ep_addr)
{
  return USBD_LL_GetRxDataSize(ep_addr);
}

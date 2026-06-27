  /******************************************************************************
  * @file    usb_core.c
  * @author  MCD Application Team
  *  This file provides all the USBD core functions.
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

#include "usb_core.h"

// global variables
extern USBD_ClassTypeDef   USBD_ClassCallbacks;

// member variables
USBD_HandleTypeDef  __aligned(4) USB_Handle;

// Configure and Start the USB module.
// called from main()
bool USBD_Init()
{
    USB_Handle.id        = DEVICE_FS;  
    USB_Handle.dev_state = USBD_STATE_DEFAULT;
    
    return (USBD_LL_Init()  == USBD_OK &&
            USBD_LL_Start() == USBD_OK);
}

// De-Initialize and Stop the USB module
USBD_StatusTypeDef USBD_DeInit()
{
  // Set Default State
  USB_Handle.dev_state = USBD_STATE_DEFAULT;

  // Free Class Resources
  USBD_ClassCallbacks.DeInit((uint8_t)USB_Handle.dev_config);

  // Stop the low level driver 
  USBD_LL_Stop();

  // Initialize low level driver
  USBD_LL_DeInit();
  return USBD_OK;
}

// Stop the USB Device Core.
USBD_StatusTypeDef USBD_Stop()
{
  // Free Class Resources
  USBD_ClassCallbacks.DeInit((uint8_t)USB_Handle.dev_config);

  // Stop the low level driver
  USBD_LL_Stop();
  return USBD_OK;
}

// Launch test mode process
USBD_StatusTypeDef USBD_RunTestMode()
{
  return USBD_OK;
}

// Configure device and start the interface
// @param  cfgidx: configuration index
USBD_StatusTypeDef USBD_SetClassConfig(uint8_t cfgidx)
{
    // Set configuration and start the class
    if (USBD_ClassCallbacks.Init(cfgidx) == USBD_OK)
        return USBD_OK;
    else
        return USBD_FAIL;
}

// Clear current configuration
// @param  cfgidx: configuration index
USBD_StatusTypeDef USBD_ClrClassConfig(uint8_t cfgidx)
{
  // Clear configuration  and De-initialize the Class process
  USBD_ClassCallbacks.DeInit(cfgidx);
  return USBD_OK;
}

// Handle the setup stage
USBD_StatusTypeDef USBD_LL_SetupStage(uint8_t *psetup)
{
  USBD_ParseSetupRequest(&USB_Handle.request, psetup);

  USB_Handle.ep0_state = USBD_EP0_SETUP;
  USB_Handle.ep0_data_len = USB_Handle.request.wLength;

  switch (USB_Handle.request.bRequestType & USB_REQ_RECIPIENT_MASK)
  {
    case USB_REQ_RECIPIENT_DEVICE:
      USBD_StdDevReq(&USB_Handle.request);
      break;
    case USB_REQ_RECIPIENT_INTERFACE:
      USBD_StdItfReq(&USB_Handle.request);
      break;
    case USB_REQ_RECIPIENT_ENDPOINT:
      USBD_StdEPReq(&USB_Handle.request);
      break;
    default:
      USBD_LL_StallEP(USB_Handle.request.bRequestType & 0x80);
      break;
  }
  return USBD_OK;
}

// Handle data OUT stage
// @param  epnum: endpoint index
USBD_StatusTypeDef USBD_LL_DataOutStage(uint8_t epnum, uint8_t *pdata)
{
  USBD_EndpointTypeDef *pep;
  if (epnum == 0)
  {
    pep = &USB_Handle.ep_out[0];
    if (USB_Handle.ep0_state == USBD_EP0_DATA_OUT)
    {
      if (pep->rem_length > pep->maxpacket)
      {
        pep->rem_length -= pep->maxpacket;

        USBD_CtlContinueRx(pdata, (uint16_t)MIN(pep->rem_length, pep->maxpacket));
      }
      else
      {
        if ((USBD_ClassCallbacks.EP0_RxReady != NULL) && (USB_Handle.dev_state == USBD_STATE_CONFIGURED))
             USBD_ClassCallbacks.EP0_RxReady();

        USBD_CtlSendStatus(); // send zero length packet on the ctl pipe
      }
    }
    else
    {
      if (USB_Handle.ep0_state == USBD_EP0_STATUS_OUT)
      {
        // STATUS PHASE completed, update ep0_state to idle
        USB_Handle.ep0_state = USBD_EP0_IDLE;
        USBD_LL_StallEP(0);
      }
    }
  }
  else if ((USBD_ClassCallbacks.DataOut != NULL) &&
           (USB_Handle.dev_state == USBD_STATE_CONFIGURED))
  {
    USBD_ClassCallbacks.DataOut(epnum);
  }
  else
  {
    // should never be in this condition
    return USBD_FAIL;
  }

  return USBD_OK;
}

// Handle data in stage
// @param  epnum: endpoint index
USBD_StatusTypeDef USBD_LL_DataInStage(uint8_t epnum, uint8_t *pdata)
{
  USBD_EndpointTypeDef *pep;

  if (epnum == 0)
  {
    pep = &USB_Handle.ep_in[0];
    if (USB_Handle.ep0_state == USBD_EP0_DATA_IN)
    {
      if (pep->rem_length > pep->maxpacket)
      {
        pep->rem_length -= pep->maxpacket;
        USBD_CtlContinueSendData(pdata, (uint16_t)pep->rem_length);

        // Prepare endpoint for premature end of transfer
        USBD_LL_PrepareReceive(0, NULL, 0);
      }
      else
      {
        // last packet is MPS multiple, so send ZLP packet
        if ((pep->total_length %  pep->maxpacket == 0) &&
            (pep->total_length >= pep->maxpacket) &&
            (pep->total_length <  USB_Handle.ep0_data_len))
        {
          USBD_CtlContinueSendData(NULL, 0);
          USB_Handle.ep0_data_len = 0;

          // Prepare endpoint for premature end of transfer
          USBD_LL_PrepareReceive(0, NULL, 0);
        }
        else
        {
          if ((USBD_ClassCallbacks.EP0_TxSent != NULL) &&
              (USB_Handle.dev_state == USBD_STATE_CONFIGURED))
          {
            USBD_ClassCallbacks.EP0_TxSent();
          }
          USBD_LL_StallEP(0x80U);
          USBD_CtlReceiveStatus();
        }
      }
    }
    else
    {
      if ((USB_Handle.ep0_state == USBD_EP0_STATUS_IN) ||
          (USB_Handle.ep0_state == USBD_EP0_IDLE))
      {
        USBD_LL_StallEP(0x80U);
      }
    }

    if (USB_Handle.dev_test_mode == 1U)
    {
      USBD_RunTestMode();
      USB_Handle.dev_test_mode = 0;
    }
  }
  else if ((USBD_ClassCallbacks.DataIn != NULL) &&
           (USB_Handle.dev_state == USBD_STATE_CONFIGURED))
  {
    USBD_ClassCallbacks.DataIn(epnum);
  }
  else
  {
    // should never be in this condition
    return USBD_FAIL;
  }

  return USBD_OK;
}

// Handle Reset event
USBD_StatusTypeDef USBD_LL_Reset()
{
  // Open EP0 OUT
  USBD_LL_OpenEP(0x00U, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
  USB_Handle.ep_out[0x00U & 0xFU].is_used = 1U;

  USB_Handle.ep_out[0].maxpacket = USB_MAX_EP0_SIZE;

  // Open EP0 IN
  USBD_LL_OpenEP(0x80U, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
  USB_Handle.ep_in[0x80U & 0xFU].is_used = 1U;

  USB_Handle.ep_in[0].maxpacket = USB_MAX_EP0_SIZE;

  // Upon Reset call user call back
  USB_Handle.dev_state = USBD_STATE_DEFAULT;
  USB_Handle.ep0_state = USBD_EP0_IDLE;
  USB_Handle.dev_config = 0;
  USB_Handle.dev_remote_wakeup = 0;

  return USBD_ClassCallbacks.DeInit((uint8_t)USB_Handle.dev_config);
}

USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_SpeedTypeDef speed)
{
  USB_Handle.dev_speed = speed;
  return USBD_OK;
}

// Handle Suspend event
USBD_StatusTypeDef USBD_LL_Suspend()
{
  USB_Handle.dev_old_state =  USB_Handle.dev_state;
  USB_Handle.dev_state  = USBD_STATE_SUSPENDED;
  return USBD_OK;
}

// Handle Resume event
USBD_StatusTypeDef USBD_LL_Resume()
{
  if (USB_Handle.dev_state == USBD_STATE_SUSPENDED)
  {
    USB_Handle.dev_state = USB_Handle.dev_old_state;
  }
  return USBD_OK;
}

// Handle SOF event
USBD_StatusTypeDef USBD_LL_SOF()
{
  if (USB_Handle.dev_state == USBD_STATE_CONFIGURED)
  {
    if (USBD_ClassCallbacks.SOF != NULL)
    {
      USBD_ClassCallbacks.SOF();
    }
  }
  return USBD_OK;
}

// Handle iso in incomplete event
USBD_StatusTypeDef USBD_LL_IsoINIncomplete(uint8_t epnum)
{
  UNUSED(epnum);
  return USBD_OK;
}

// Handle iso out incomplete event
USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(uint8_t epnum)
{
  UNUSED(epnum);
  return USBD_OK;
}

// Handle device connection event
USBD_StatusTypeDef USBD_LL_DevConnected()
{
  return USBD_OK;
}

// Handle device disconnection event
USBD_StatusTypeDef USBD_LL_DevDisconnected()
{
  // Free Class Resources
  USB_Handle.dev_state = USBD_STATE_DEFAULT;
  USBD_ClassCallbacks.DeInit((uint8_t)USB_Handle.dev_config);
  return USBD_OK;
}

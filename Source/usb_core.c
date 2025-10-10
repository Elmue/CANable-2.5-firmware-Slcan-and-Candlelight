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

extern USBD_DescriptorsTypeDef USBD_DescriptorCallbacks;
extern USBD_ClassTypeDef       USBD_ClassCallbacks;

USBD_HandleTypeDef USB_Device;

// Configure and Start the USB module.
bool USBD_Init()
{
    USB_Device.id        = DEVICE_FS;  
    USB_Device.dev_state = USBD_STATE_DEFAULT;
    USB_Device.pDesc     = &USBD_DescriptorCallbacks; // get device, config, string descriptors
    USB_Device.pClass    = &USBD_ClassCallbacks;      // device specific (Slcan / Candlelight)

    return (USBD_LL_Init (&USB_Device) == USBD_OK &&
            USBD_LL_Start(&USB_Device) == USBD_OK);
}

// This simulates an USB disconnect / reconnect which is noticed by the host.
// But this does not help to replace a hardware reset after enabling pin BOOT0.
bool USBD_DetachAttach()
{
    USBD_DeInit(&USB_Device); // Stop USB
    HAL_Delay(50);
    return USBD_Init(); // Start USB
}

// De-Initialize and Stop the USB module
USBD_StatusTypeDef USBD_DeInit(USBD_HandleTypeDef *pdev)
{
  /* Set Default State */
  pdev->dev_state = USBD_STATE_DEFAULT;

  /* Free Class Resources */
  pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);

  /* Stop the low level driver  */
  USBD_LL_Stop(pdev);

  /* Initialize low level driver */
  USBD_LL_DeInit(pdev);
  return USBD_OK;
}

// Stop the USB Device Core.
// @param  pdev: Device Handle
USBD_StatusTypeDef USBD_Stop(USBD_HandleTypeDef *pdev)
{
  /* Free Class Resources */
  pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);

  /* Stop the low level driver */
  USBD_LL_Stop(pdev);
  return USBD_OK;
}

// Launch test mode process
USBD_StatusTypeDef  USBD_RunTestMode(USBD_HandleTypeDef  *pdev)
{
  /* Prevent unused argument compilation warning */
  UNUSED(pdev);
  return USBD_OK;
}

// Configure device and start the interface
// @param  cfgidx: configuration index
USBD_StatusTypeDef USBD_SetClassConfig(USBD_HandleTypeDef  *pdev, uint8_t cfgidx)
{
  USBD_StatusTypeDef ret = USBD_FAIL;

  if (pdev->pClass != NULL)
  {
    /* Set configuration  and Start the Class*/
    if (pdev->pClass->Init(pdev, cfgidx) == 0U)
    {
      ret = USBD_OK;
    }
  }
  return ret;
}

// Clear current configuration
// @param  cfgidx: configuration index
USBD_StatusTypeDef USBD_ClrClassConfig(USBD_HandleTypeDef  *pdev, uint8_t cfgidx)
{
  /* Clear configuration  and De-initialize the Class process*/
  pdev->pClass->DeInit(pdev, cfgidx);
  return USBD_OK;
}

// Handle the setup stage
USBD_StatusTypeDef USBD_LL_SetupStage(USBD_HandleTypeDef *pdev, uint8_t *psetup)
{
  USBD_ParseSetupRequest(&pdev->request, psetup);

  pdev->ep0_state = USBD_EP0_SETUP;
  pdev->ep0_data_len = pdev->request.wLength;

  switch (pdev->request.bmRequest & 0x1FU)
  {
    case USB_REQ_RECIPIENT_DEVICE:
      USBD_StdDevReq(pdev, &pdev->request);
      break;
    case USB_REQ_RECIPIENT_INTERFACE:
      USBD_StdItfReq(pdev, &pdev->request);
      break;
    case USB_REQ_RECIPIENT_ENDPOINT:
      USBD_StdEPReq(pdev, &pdev->request);
      break;
    default:
      USBD_LL_StallEP(pdev, (pdev->request.bmRequest & 0x80U));
      break;
  }
  return USBD_OK;
}

// Handle data OUT stage
// @param  epnum: endpoint index
USBD_StatusTypeDef USBD_LL_DataOutStage(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *pdata)
{
  USBD_EndpointTypeDef *pep;
  if (epnum == 0U)
  {
    pep = &pdev->ep_out[0];
    if (pdev->ep0_state == USBD_EP0_DATA_OUT)
    {
      if (pep->rem_length > pep->maxpacket)
      {
        pep->rem_length -= pep->maxpacket;

        USBD_CtlContinueRx(pdev, pdata, (uint16_t)MIN(pep->rem_length, pep->maxpacket));
      }
      else
      {
        if ((pdev->pClass->EP0_RxReady != NULL) && (pdev->dev_state == USBD_STATE_CONFIGURED))
             pdev->pClass->EP0_RxReady(pdev);

        USBD_CtlSendStatus(pdev); // send zero length packet on the ctl pipe
      }
    }
    else
    {
      if (pdev->ep0_state == USBD_EP0_STATUS_OUT)
      {
        // STATUS PHASE completed, update ep0_state to idle
        pdev->ep0_state = USBD_EP0_IDLE;
        USBD_LL_StallEP(pdev, 0U);
      }
    }
  }
  else if ((pdev->pClass->DataOut != NULL) &&
           (pdev->dev_state == USBD_STATE_CONFIGURED))
  {
    pdev->pClass->DataOut(pdev, epnum);
  }
  else
  {
    /* should never be in this condition */
    return USBD_FAIL;
  }

  return USBD_OK;
}

// Handle data in stage
// @param  epnum: endpoint index
USBD_StatusTypeDef USBD_LL_DataInStage(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *pdata)
{
  USBD_EndpointTypeDef *pep;

  if (epnum == 0U)
  {
    pep = &pdev->ep_in[0];
    if (pdev->ep0_state == USBD_EP0_DATA_IN)
    {
      if (pep->rem_length > pep->maxpacket)
      {
        pep->rem_length -= pep->maxpacket;
        USBD_CtlContinueSendData(pdev, pdata, (uint16_t)pep->rem_length);

        /* Prepare endpoint for premature end of transfer */
        USBD_LL_PrepareReceive(pdev, 0U, NULL, 0U);
      }
      else
      {
        /* last packet is MPS multiple, so send ZLP packet */
        if ((pep->total_length %  pep->maxpacket == 0U) &&
            (pep->total_length >= pep->maxpacket) &&
            (pep->total_length <  pdev->ep0_data_len))
        {
          USBD_CtlContinueSendData(pdev, NULL, 0U);
          pdev->ep0_data_len = 0U;

          /* Prepare endpoint for premature end of transfer */
          USBD_LL_PrepareReceive(pdev, 0U, NULL, 0U);
        }
        else
        {
          if ((pdev->pClass->EP0_TxSent != NULL) &&
              (pdev->dev_state == USBD_STATE_CONFIGURED))
          {
            pdev->pClass->EP0_TxSent(pdev);
          }
          USBD_LL_StallEP(pdev, 0x80U);
          USBD_CtlReceiveStatus(pdev);
        }
      }
    }
    else
    {
      if ((pdev->ep0_state == USBD_EP0_STATUS_IN) ||
          (pdev->ep0_state == USBD_EP0_IDLE))
      {
        USBD_LL_StallEP(pdev, 0x80U);
      }
    }

    if (pdev->dev_test_mode == 1U)
    {
      USBD_RunTestMode(pdev);
      pdev->dev_test_mode = 0U;
    }
  }
  else if ((pdev->pClass->DataIn != NULL) &&
           (pdev->dev_state == USBD_STATE_CONFIGURED))
  {
    pdev->pClass->DataIn(pdev, epnum);
  }
  else
  {
    /* should never be in this condition */
    return USBD_FAIL;
  }

  return USBD_OK;
}

// Handle Reset event
USBD_StatusTypeDef USBD_LL_Reset(USBD_HandleTypeDef *pdev)
{
  /* Open EP0 OUT */
  USBD_LL_OpenEP(pdev, 0x00U, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
  pdev->ep_out[0x00U & 0xFU].is_used = 1U;

  pdev->ep_out[0].maxpacket = USB_MAX_EP0_SIZE;

  /* Open EP0 IN */
  USBD_LL_OpenEP(pdev, 0x80U, USBD_EP_TYPE_CTRL, USB_MAX_EP0_SIZE);
  pdev->ep_in[0x80U & 0xFU].is_used = 1U;

  pdev->ep_in[0].maxpacket = USB_MAX_EP0_SIZE;

  /* Upon Reset call user call back */
  pdev->dev_state = USBD_STATE_DEFAULT;
  pdev->ep0_state = USBD_EP0_IDLE;
  pdev->dev_config = 0U;
  pdev->dev_remote_wakeup = 0U;

  if (pdev->pClassData)
      pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);

  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_HandleTypeDef *pdev, USBD_SpeedTypeDef speed)
{
  pdev->dev_speed = speed;
  return USBD_OK;
}

// Handle Suspend event
USBD_StatusTypeDef USBD_LL_Suspend(USBD_HandleTypeDef *pdev)
{
  pdev->dev_old_state =  pdev->dev_state;
  pdev->dev_state  = USBD_STATE_SUSPENDED;
  return USBD_OK;
}

// Handle Resume event
USBD_StatusTypeDef USBD_LL_Resume(USBD_HandleTypeDef *pdev)
{
  if (pdev->dev_state == USBD_STATE_SUSPENDED)
  {
    pdev->dev_state = pdev->dev_old_state;
  }
  return USBD_OK;
}

// Handle SOF event
USBD_StatusTypeDef USBD_LL_SOF(USBD_HandleTypeDef *pdev)
{
  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if (pdev->pClass->SOF != NULL)
    {
      pdev->pClass->SOF(pdev);
    }
  }
  return USBD_OK;
}

// Handle iso in incomplete event
USBD_StatusTypeDef USBD_LL_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  /* Prevent unused arguments compilation warning */
  UNUSED(pdev);
  UNUSED(epnum);
  return USBD_OK;
}

// Handle iso out incomplete event
USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  /* Prevent unused arguments compilation warning */
  UNUSED(pdev);
  UNUSED(epnum);
  return USBD_OK;
}

// Handle device connection event
USBD_StatusTypeDef USBD_LL_DevConnected(USBD_HandleTypeDef *pdev)
{
  /* Prevent unused argument compilation warning */
  UNUSED(pdev);
  return USBD_OK;
}

// Handle device disconnection event
USBD_StatusTypeDef USBD_LL_DevDisconnected(USBD_HandleTypeDef *pdev)
{
  /* Free Class Resources */
  pdev->dev_state = USBD_STATE_DEFAULT;
  pdev->pClass->DeInit(pdev, (uint8_t)pdev->dev_config);
  return USBD_OK;
}

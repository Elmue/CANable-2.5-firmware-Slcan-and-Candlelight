  /*******************************************************************************
  * @file    usb_lowlevel.c
  * @author  MCD Application Team
  * @brief   This file provides the low level USB API.
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

#include "settings.h"
#include "usb_def.h"
#include "usb_core.h"
#include "usb_class.h"
#include "led.h"
#include "can.h"

PCD_HandleTypeDef hpcd_USB_FS;
bool volatile bSuspended = false;

USBD_StatusTypeDef ConvStatus(HAL_StatusTypeDef hal_status);

/*******************************************************************************
                       LL Driver Callbacks (PCD -> USB Device Library)
*******************************************************************************/

void HAL_PCD_MspInit(PCD_HandleTypeDef* pcdHandle)
{
  if(pcdHandle->Instance==USB)
  {
    __HAL_RCC_USB_CLK_ENABLE();
    HAL_NVIC_SetPriority(USB_LP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USB_LP_IRQn);
  }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* pcdHandle)
{
  if(pcdHandle->Instance==USB)
  {
    __HAL_RCC_USB_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(USB_LP_IRQn);
  }
}

// called from interrupt handler PCD_EP_ISR_Handler
// SETUP stage 1
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
    // call into usb_class.c
    bool req_handled = USBD_SetupStageRequest(hpcd);
    if (!req_handled) // not a recognized Device or Interface request
        USBD_LL_SetupStage((USBD_HandleTypeDef*)hpcd->pData, (uint8_t *)hpcd->Setup);  
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataOutStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);  
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataInStage((USBD_HandleTypeDef*)hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);  
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SOF((USBD_HandleTypeDef*)hpcd->pData);  
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{ 
  USBD_LL_SetSpeed((USBD_HandleTypeDef*)hpcd->pData, USBD_SPEED_FULL);
  USBD_LL_Reset((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  bSuspended = true;  
  can_close();
  
  USBD_LL_Suspend((USBD_HandleTypeDef*)hpcd->pData);
  if (hpcd->Init.low_power_enable)
  {
    /* Set SLEEPDEEP bit and SleepOnExit of Cortex System Control Register. */
    SCB->SCR |= (uint32_t)((uint32_t)(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
  }
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd->Init.low_power_enable)
  {
    /* Reset SLEEPDEEP bit of Cortex System Control Register. */
    SCB->SCR &= (uint32_t)~((uint32_t)(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
  }
  USBD_LL_Resume((USBD_HandleTypeDef*)hpcd->pData);
  
  bSuspended = false;
}

bool HAL_PCD_Is_Suspended()
{
    return bSuspended;
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef*)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevConnected((USBD_HandleTypeDef*)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevDisconnected((USBD_HandleTypeDef*)hpcd->pData);
}

/*******************************************************************************
                       LL Driver Interface (USB Device Library --> PCD)
*******************************************************************************/

// @brief  Initializes the low level portion of the device driver.
// @param  pdev: Device handle
USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  /* Init USB Ip. */
  hpcd_USB_FS.pData = pdev;
  /* Link the driver to the stack. */
  pdev->pData = &hpcd_USB_FS;

  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.ep0_mps = DEP0CTL_MPS_64;
  hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.Sof_enable = ENABLE;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;

  HAL_PCD_Init(&hpcd_USB_FS);
  USBD_ConfigureEndpoints(pdev);
  return USBD_OK;
}

// @brief  De-Initializes the low level portion of the device driver.
// @param  pdev: Device handle
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
    return ConvStatus(HAL_PCD_DeInit(pdev->pData));
}

// @brief  Starts the low level portion of the device driver.
// @param  pdev: Device handle
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
    return ConvStatus(HAL_PCD_Start(pdev->pData));
}

// @brief  Stops the low level portion of the device driver.
// @param  pdev: Device handle
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
    return ConvStatus(HAL_PCD_Stop(pdev->pData));
}

// @brief  Opens an endpoint of the low level driver.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
// @param  ep_type: Endpoint type
// @param  ep_mps: Endpoint max packet size
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
    return ConvStatus(HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type));
}

// @brief  Closes an endpoint of the low level driver.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_Close(pdev->pData, ep_addr));
}

// @brief  Flushes an endpoint of the Low Level Driver.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_Flush(pdev->pData, ep_addr));
}

// @brief  Sets a Stall condition on an endpoint of the Low Level Driver.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_SetStall(pdev->pData, ep_addr));
}

// @brief  Clears a Stall condition on an endpoint of the Low Level Driver.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_ClrStall(pdev->pData, ep_addr));
}

// @brief  Returns Stall condition.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
// @retval Stall (1: Yes, 0: No)
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef*) pdev->pData;
  
  if((ep_addr & 0x80) == 0x80)
      return hpcd->IN_ep[ep_addr & 0x7F].is_stall; 
  else
      return hpcd->OUT_ep[ep_addr & 0x7F].is_stall; 
  }

// @brief  Assigns a USB address to the device.
// @param  pdev: Device handle
// @param  dev_addr: Device address
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    return ConvStatus(HAL_PCD_SetAddress(pdev->pData, dev_addr));
}

// @brief  Transmits data over an endpoint.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
// @param  pbuf: Pointer to data to be sent
// @param  size: Data size    
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint16_t size)
{
   return ConvStatus(HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size));
}

// @brief  Prepares an endpoint for reception.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
// @param  pbuf: Pointer to data to be received
// @param  size: Data size
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint16_t size)
{
    return ConvStatus(HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size));
}

// @brief  Returns the last transfered packet size.
// @param  pdev: Device handle
// @param  ep_addr: Endpoint number
// @retval Recived Data Size
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*) pdev->pData, ep_addr);
}

// @brief  Delays routine for the USB Device Library.
// @param  Delay: Delay in ms
void USBD_LL_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

// @brief  Retuns the USB status depending on the HAL status:
// @param  hal_status: HAL status
// @retval USB status
USBD_StatusTypeDef ConvStatus(HAL_StatusTypeDef hal_status)
{
  switch (hal_status)
  {
    case HAL_OK:      return USBD_OK;
    case HAL_BUSY:    return USBD_BUSY;
    case HAL_ERROR:   
    case HAL_TIMEOUT: 
    default:          return USBD_FAIL;
  }
}


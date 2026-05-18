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

PCD_HandleTypeDef PCD_Handle;
bool volatile     bSuspended = false;

USBD_StatusTypeDef ConvStatus(HAL_StatusTypeDef hal_status);

/*******************************************************************************
           LowLevel Driver Callbacks (PCD -> USB Device Library)
*******************************************************************************/

void HAL_PCD_MspInit(PCD_HandleTypeDef* dummy)
{
  if (PCD_Handle.Instance == USB_Instance)
  {
    __HAL_RCC_USB_CLK_ENABLE();
    
    // USB_LP_IRQn = USB_UCPD1_2_IRQn for the STM32G0xx
    HAL_NVIC_SetPriority(USB_LP_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USB_LP_IRQn);
  }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef* dummy)
{
  if (PCD_Handle.Instance == USB_Instance)
  {
    __HAL_RCC_USB_CLK_DISABLE();
    
    // USB_LP_IRQn = USB_UCPD1_2_IRQn for the STM32G0xx
    HAL_NVIC_DisableIRQ(USB_LP_IRQn);
  }
}

// called from interrupt handler PCD_EP_ISR_Handler
// SETUP stage 1
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef* dummy)
{
    // call into usb_class.c
    bool req_handled = USBD_SetupStageRequest();
    if (!req_handled) // not a recognized Device or Interface request
        USBD_LL_SetupStage((uint8_t *)PCD_Handle.Setup);  
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef* dummy, uint8_t epnum)
{
  USBD_LL_DataOutStage(epnum, PCD_Handle.OUT_ep[epnum].xfer_buff);  
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef* dummy, uint8_t epnum)
{
  USBD_LL_DataInStage(epnum, PCD_Handle.IN_ep[epnum].xfer_buff);  
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef* dummy)
{
  USBD_LL_SOF();  
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef* dummy)
{ 
  USBD_LL_SetSpeed(USBD_SPEED_FULL);
  USBD_LL_Reset();
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef* dummy)
{
  bSuspended = true;  
  can_close_all();
  
  USBD_LL_Suspend();
  if (PCD_Handle.Init.low_power_enable)
  {
    /* Set SLEEPDEEP bit and SleepOnExit of Cortex System Control Register. */
    SCB->SCR |= (uint32_t)((uint32_t)(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
  }
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef* dummy)
{
  if (PCD_Handle.Init.low_power_enable)
  {
    /* Reset SLEEPDEEP bit of Cortex System Control Register. */
    SCB->SCR &= (uint32_t)~((uint32_t)(SCB_SCR_SLEEPDEEP_Msk | SCB_SCR_SLEEPONEXIT_Msk));
  }
  USBD_LL_Resume();
  
  bSuspended = false;
}

bool HAL_PCD_Is_Suspended()
{
    return bSuspended;
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef* dummy, uint8_t epnum)
{
  USBD_LL_IsoOUTIncomplete(epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef* dummy, uint8_t epnum)
{
  USBD_LL_IsoINIncomplete(epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef* dummy)
{
  USBD_LL_DevConnected();
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef* dummy)
{
  USBD_LL_DevDisconnected();
}

/*******************************************************************************
                       LL Driver Interface (USB Device Library --> PCD)
*******************************************************************************/

// @brief  Initializes the low level portion of the device driver.
USBD_StatusTypeDef USBD_LL_Init()
{
  PCD_Handle.Instance = USB_Instance;
  PCD_Handle.Init.dev_endpoints            = 8;               // maximum count of endpoints in Device mode (max 15)
  PCD_Handle.Init.speed                    = PCD_SPEED_FULL;  // 12 Mbit/s
  PCD_Handle.Init.ep0_mps                  = PCD_EP0MPS_64;   // MPS = Maximum Packet Size = 64 bytes
  PCD_Handle.Init.phy_itface               = PCD_PHY_EMBEDDED;
  PCD_Handle.Init.Sof_enable               = ENABLE;
  PCD_Handle.Init.low_power_enable         = DISABLE;
  PCD_Handle.Init.lpm_enable               = DISABLE;
  PCD_Handle.Init.battery_charging_enable  = DISABLE;

  HAL_PCD_Init(&PCD_Handle);
  return USBD_ConfigureEndpoints();
}

// assign a PMA memory buffer with bufsize bytes to the endpoint.
// *pmaadress is the offset in the Packet Memory Area which has USB_PMA_SIZE bytes.
// called from USBD_ConfigureEndpoints()
bool USBD_LL_ConfigurePMA(uint8_t endpoint, bool doublebuf, uint32_t* pmaadress, uint32_t bufsize)
{
    if (doublebuf)
    {
        uint32_t addr = *pmaadress << 16;
        *pmaadress = *pmaadress + bufsize;
        addr |= *pmaadress;
        HAL_PCDEx_PMAConfig(&PCD_Handle, endpoint, PCD_DBL_BUF, addr);
    }
    else
    {
        HAL_PCDEx_PMAConfig(&PCD_Handle, endpoint, PCD_SNG_BUF, *pmaadress);
    }
    
    // All addresses must be at 8 byte boundary
    *pmaadress = ((*pmaadress + bufsize + 7) / 8) * 8;

    return *pmaadress <= USB_PMA_SIZE; // return false --> buffer overflow (too many endpoints)
}

// @brief  De-Initializes the low level portion of the device driver.
USBD_StatusTypeDef USBD_LL_DeInit()
{
    return ConvStatus(HAL_PCD_DeInit(&PCD_Handle));
}

// @brief  Starts the low level portion of the device driver.
USBD_StatusTypeDef USBD_LL_Start()
{
    return ConvStatus(HAL_PCD_Start(&PCD_Handle));
}

// @brief  Stops the low level portion of the device driver.
USBD_StatusTypeDef USBD_LL_Stop()
{
    return ConvStatus(HAL_PCD_Stop(&PCD_Handle));
}

// @brief  Opens an endpoint of the low level driver.
// @param  ep_addr: Endpoint number
// @param  ep_type: Endpoint type
// @param  ep_mps: Endpoint max packet size
USBD_StatusTypeDef USBD_LL_OpenEP(uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
    return ConvStatus(HAL_PCD_EP_Open(&PCD_Handle, ep_addr, ep_mps, ep_type));
}

// @brief  Closes an endpoint of the low level driver.
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_CloseEP(uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_Close(&PCD_Handle, ep_addr));
}

// @brief  Flushes an endpoint of the Low Level Driver.
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_FlushEP(uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_Flush(&PCD_Handle, ep_addr));
}

// @brief  Sets a Stall condition on an endpoint of the Low Level Driver.
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_StallEP(uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_SetStall(&PCD_Handle, ep_addr));
}

// @brief  Clears a Stall condition on an endpoint of the Low Level Driver.
// @param  ep_addr: Endpoint number
USBD_StatusTypeDef USBD_LL_ClearStallEP(uint8_t ep_addr)
{
    return ConvStatus(HAL_PCD_EP_ClrStall(&PCD_Handle, ep_addr));
}

// @brief  Returns Stall condition.
// @param  ep_addr: Endpoint number
// @retval Stall (1: Yes, 0: No)
uint8_t USBD_LL_IsStallEP(uint8_t ep_addr)
{
  if((ep_addr & 0x80) == 0x80)
      return PCD_Handle.IN_ep[ep_addr & 0x7F].is_stall; 
  else
      return PCD_Handle.OUT_ep[ep_addr & 0x7F].is_stall; 
  }

// @brief  Assigns a USB address to the device.
// @param  dev_addr: Device address
USBD_StatusTypeDef USBD_LL_SetUSBAddress(uint8_t dev_addr)
{
    return ConvStatus(HAL_PCD_SetAddress(&PCD_Handle, dev_addr));
}

// @brief  Transmits data over an endpoint.
// @param  ep_addr: Endpoint number
// @param  pbuf: Pointer to data to be sent
// @param  size: Data size    
USBD_StatusTypeDef USBD_LL_Transmit(uint8_t ep_addr, uint8_t *pbuf, uint16_t size)
{
   return ConvStatus(HAL_PCD_EP_Transmit(&PCD_Handle, ep_addr, pbuf, size));
}

// @brief  Prepares an endpoint for reception.
// @param  ep_addr: Endpoint number
// @param  pbuf: Pointer to data to be received
// @param  size: Data size
USBD_StatusTypeDef USBD_LL_PrepareReceive(uint8_t ep_addr, uint8_t *pbuf, uint16_t size)
{
    return ConvStatus(HAL_PCD_EP_Receive(&PCD_Handle, ep_addr, pbuf, size));
}

// @brief  Returns the last transfered packet size.
// @param  ep_addr: Endpoint number
// @retval Recived Data Size
uint32_t USBD_LL_GetRxDataSize(uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount(&PCD_Handle, ep_addr);
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


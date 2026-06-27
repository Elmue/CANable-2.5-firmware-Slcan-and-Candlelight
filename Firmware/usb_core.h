/**
  ******************************************************************************
  * @file    usbd_core.h
  * @author  MCD Application Team
  * @brief   Header file for usbd_core.c file
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
  *
  ******************************************************************************
  */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "usb_lowlevel.h"
#include "usb_def.h"
#include "usb_ioreq.h"
#include "usb_ctrlreq.h"

#ifndef USBD_DEBUG_LEVEL
#define USBD_DEBUG_LEVEL           0U
#endif /* USBD_DEBUG_LEVEL */

#define USBD_SOF          USBD_LL_SOF

bool               USBD_Init();
USBD_StatusTypeDef USBD_DeInit();
USBD_StatusTypeDef USBD_Stop();

USBD_StatusTypeDef USBD_RunTestMode();
USBD_StatusTypeDef USBD_SetClassConfig(uint8_t cfgidx);
USBD_StatusTypeDef USBD_ClrClassConfig(uint8_t cfgidx);

USBD_StatusTypeDef USBD_LL_SetupStage(uint8_t *psetup);
USBD_StatusTypeDef USBD_LL_DataOutStage(uint8_t epnum, uint8_t *pdata);
USBD_StatusTypeDef USBD_LL_DataInStage(uint8_t epnum, uint8_t *pdata);

USBD_StatusTypeDef USBD_LL_Reset();
USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_SpeedTypeDef speed);
USBD_StatusTypeDef USBD_LL_Suspend();
USBD_StatusTypeDef USBD_LL_Resume();

USBD_StatusTypeDef USBD_LL_SOF();
USBD_StatusTypeDef USBD_LL_IsoINIncomplete(uint8_t epnum);
USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(uint8_t epnum);

USBD_StatusTypeDef USBD_LL_DevConnected();
USBD_StatusTypeDef USBD_LL_DevDisconnected();

/* USBD Low Level Driver */
USBD_StatusTypeDef  USBD_LL_Init();
USBD_StatusTypeDef  USBD_LL_DeInit();
USBD_StatusTypeDef  USBD_LL_Start();
USBD_StatusTypeDef  USBD_LL_Stop();
USBD_StatusTypeDef  USBD_LL_OpenEP(uint8_t  ep_addr, uint8_t  ep_type, uint16_t ep_mps);

USBD_StatusTypeDef  USBD_LL_CloseEP(uint8_t ep_addr);
USBD_StatusTypeDef  USBD_LL_FlushEP(uint8_t ep_addr);
USBD_StatusTypeDef  USBD_LL_StallEP(uint8_t ep_addr);
USBD_StatusTypeDef  USBD_LL_ClearStallEP(uint8_t ep_addr);
uint8_t             USBD_LL_IsStallEP(uint8_t ep_addr);
USBD_StatusTypeDef  USBD_LL_SetUSBAddress(uint8_t dev_addr);
USBD_StatusTypeDef  USBD_LL_Transmit(uint8_t  ep_addr, uint8_t  *pbuf, uint16_t  size);

USBD_StatusTypeDef  USBD_LL_PrepareReceive(uint8_t  ep_addr, uint8_t  *pbuf, uint16_t  size);

uint32_t USBD_LL_GetRxDataSize(uint8_t  ep_addr);

#ifdef __cplusplus
}
#endif







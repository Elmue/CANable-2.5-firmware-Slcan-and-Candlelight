/*
    The MIT License
    Implemenatation of USB GS Class (Geschwister Schneider)
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "usb_def.h"
#include "usb_core.h"

// IMPORTANT: All interface descriptors must have an ascending bInterfaceNumber: 0, 1, 2,...
// interface 0 = Candlelight 1
// interface 1 = DFU Firmware Update
// interface 2 = Candlelight 2
// interface 3 = Candlelight 3
#define CANDLE_INRERFACE_COUNT    CHANNEL_COUNT                // number of CAN channels
#define DFU_INTERFACE_NUMBER      1                            // interface 1 is always DFU for backward compatibiliy
#define USBD_INTERFACES_COUNT    (CANDLE_INRERFACE_COUNT + 1)  // total count of USB interfaces

void               USBD_SendFrameToHost(int channel, void *frame);
USBD_StatusTypeDef USBD_ConfigureEndpoints(USBD_HandleTypeDef *pdev);
bool               USBD_SetupStageRequest(PCD_HandleTypeDef *hpcd);
uint8_t*           USBD_GetUserStringDescr(USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length);


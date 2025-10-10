/*
    The MIT License
    Implemenatation of USB GS Class (Geschwister Schneider)
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "usb_def.h"
#include "usb_core.h"

void    USBD_SendFrameToHost(void *frame);
bool    USBD_IsTxBusy();
void    USBD_ConfigureEndpoints(USBD_HandleTypeDef *pdev);
bool    USBD_SetupStageRequest(PCD_HandleTypeDef *hpcd);


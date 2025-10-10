/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "usb_class.h"

extern USBD_CDC_ItfTypeDef USBD_InterfaceCallbacks;

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len);



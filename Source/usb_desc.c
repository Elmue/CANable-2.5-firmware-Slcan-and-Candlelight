  /*******************************************************************************
  * @file    usb_desc.c
  * @author  MCD Application Team
  * @brief   This file provides the device, configuration and string descriptors.
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
#include "usb_class.h"
#include "usb_def.h"
#include "usb_lowlevel.h"
#include "settings.h"
#include "utils.h"

extern uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC];

uint8_t * USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

// callback functions
USBD_DescriptorsTypeDef USBD_DescriptorCallbacks =
{
    USBD_DeviceDescriptor, 
    USBD_LangIDStrDescriptor, 
    USBD_ManufacturerStrDescriptor, 
    USBD_ProductStrDescriptor, 
    USBD_SerialStrDescriptor, 
    USBD_ConfigStrDescriptor,
    USBD_InterfaceStrDescriptor
};

// USB lang indentifier descriptor. 
// USB_LEN_LANGID_STR_DESC = 4
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END =
{
     USB_LEN_LANGID_STR_DESC,
     USB_DESC_TYPE_STRING,
     LOBYTE(0x0409), // english
     HIBYTE(0x0409)
};

// USBD_MAX_STR_DESC_SIZE = 512
__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZE] __ALIGN_END;

uint8_t * USBD_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_DeviceDesc);
    return USBD_DeviceDesc;
}

uint8_t * USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_LangIDDesc);
    return USBD_LangIDDesc;
}

#define USBD_PRODUCT_STRING  "CANable 2.5 " TARGET_FIRMWARE

uint8_t * USBD_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

uint8_t * USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)"ElmueSoft (netcult.ch/elmue)", USBD_StrDesc, length);
    return USBD_StrDesc;
}

uint8_t * USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);

    // get the 96 bit serial number which is unique for each processor that ST Microelectrons has ever produced.
    uint32_t deviceserial0 = *(uint32_t*)(UID_BASE    );
    uint32_t deviceserial1 = *(uint32_t*)(UID_BASE + 4);
    uint32_t deviceserial2 = *(uint32_t*)(UID_BASE + 8);

    // reduce 96 bit to 64 bit
    deviceserial0 += deviceserial2;

    // format 16 digit serial number
    char s8_Serial[20];
    sprintf(s8_Serial, "%08lX%08lX", deviceserial0, deviceserial1);

    USBD_GetString((uint8_t *)s8_Serial, USBD_StrDesc, length);
    return USBD_StrDesc;
}

// not used (iConfiguration = 0)
uint8_t * USBD_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    USBD_GetString((uint8_t *)"", USBD_StrDesc, length);
    return USBD_StrDesc;
}

// not used (iInterface = 0)
uint8_t * USBD_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    USBD_GetString((uint8_t *)"", USBD_StrDesc, length);
    return USBD_StrDesc;
}


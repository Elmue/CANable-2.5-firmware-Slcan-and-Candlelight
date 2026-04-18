  /******************************************************************************
  * @file    usb_class.c
  * @author  MCD Application Team
  * @brief   This file provides the high layer firmware functions to manage the
  *          following functionalities of the USB CDC Class:
  *           - Initialization and Configuration of high and low layer
  *           - Enumeration as CDC Device (and enumeration for each implemented memory interface)
  *           - OUT/IN data transfer
  *           - Command IN transfer (class requests management)
  *           - Error management
  *
  *          ===================================================================
  *                                CDC Class Driver Description
  *          ===================================================================
  *           This driver manages the "Universal Serial Bus Class Definitions for Communications Devices
  *           Revision 1.2 November 16, 2007" and the sub-protocol specification of "Universal Serial Bus
  *           Communications Class Subclass Specification for PSTN Devices Revision 1.2 February 9, 2007"
  *           This driver implements the following aspects of the specification:
  *             - Device descriptor management
  *             - Configuration descriptor management
  *             - Enumeration as CDC device with 2 data endpoints (IN and OUT) and 1 command endpoint (IN)
  *             - Requests management (as described in section 6.2 in specification)
  *             - Abstract Control Model compliant
  *             - Union Functional collection (using 1 IN endpoint for control)
  *             - Data interface class
  *
  *           These aspects may be enriched or modified for a specific user application.
  *
  *            This driver doesn't implement the following aspects of the specification
  *            (but it is possible to manage these features with some modifications on this driver):
  *             - Any class-specific aspect relative to communication classes should be managed by user application.
  *             - All communication classes other than PSTN are not managed
  *
  *  @endverbatim
  *
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

#include "usb_class.h"
#include "usb_ctrlreq.h"
#include "usb_interface.h" 

// The PMA Buffer has a size of 1024 bytes and starts with the BTABLE (Buffer Description Table)
// For each single buffered endpoint 8 bytes are reserved in the BTABLE for IN or OUT or both.
// For each double buffered endpoint 8 bytes are reserved in the BTABLE for only one direction: IN or OUT. Both is not possible!
// See "STM32G4 Series - Chapter USB.pdf" --> page 6 in subfolder Documentation
// See USB_ActivateEndpoint() --> PCD_SET_EP_TX_ADDRESS() and PCD_SET_EP_DBUF_ADDR()
// -----------------------------------
// Endpoint 0 IN + OUT single = 8 byte 
// Endpoint 1 IN + OUT single = 8 byte
// Endpoint 2 IN + --- single = 8 byte (4 byte unused)
// Total:                      24 byte
#define BTABLE_SIZE      24

static uint8_t  USBD_CDC_Init       (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_CDC_DeInit     (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_CDC_Setup      (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t  USBD_CDC_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t  USBD_CDC_DataIn     (USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t  USBD_CDC_DataOut    (USBD_HandleTypeDef *pdev, uint8_t epnum);

USBD_CDC_HandleTypeDef __aligned(4) USBD_CDC_Handle;

// CDC class callbacks structure
// These functions are called over usb_core and usb_lowlevel from PCD_EP_ISR_Handler() interrupts
USBD_ClassTypeDef  USBD_ClassCallbacks =
{
    .Init              = USBD_CDC_Init,
    .DeInit            = USBD_CDC_DeInit,
    .Setup             = USBD_CDC_Setup,
    .EP0_TxSent        = NULL,  
    .EP0_RxReady       = USBD_CDC_EP0_RxReady,
    .DataIn            = USBD_CDC_DataIn,
    .DataOut           = USBD_CDC_DataOut,
    .SOF               = NULL,
    .IsoINIncomplete   = NULL, // ISO endpoints not used
    .IsoOUTIncomplete  = NULL, // ISO endpoints not used
};

// Device descriptor CDC
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[] __ALIGN_END =
{
    USB_LEN_DEV_DESC,                  // bLength = 18 byte
    USB_DESC_TYPE_DEVICE,              // bDescriptorType = Device Descriptor
    0x00,                              // bcdUSB version
    0x02,                              // bcdUSB version  = 2.0
    0x02,                              // bDeviceClass    = CDC Control (virtual COM port)
    0x02,                              // bDeviceSubClass = Abstract Control Model
    0x00,                              // bDeviceProtocol
    USB_MAX_EP0_SIZE,                  // bMaxPacketSize  = 64 bytes
    LOBYTE(0x16D0),                    // idVendor  MCS
    HIBYTE(0x16D0),                    // idVendor  MCS
    LOBYTE(0x117E),                    // idProduct CANable Slcan
    HIBYTE(0x117E),                    // idProduct CANable Slcan 
    LOBYTE(FIRMWARE_VERSION_BCD >> 8), // bcdDevice firmware version (month)
    HIBYTE(FIRMWARE_VERSION_BCD >> 8), // bcdDevice firmware version (year)
    USBD_IDX_MFC_STR,                  // Index of manufacturer  string
    USBD_IDX_PRODUCT_STR,              // Index of product string
    USBD_IDX_SERIAL_STR,               // Index of serial number string
    USBD_CONFIGURATIONS_COUNT          // bNumConfigurations
};

// USB CDC Full Speed Device Configuration Descriptor 
__ALIGN_BEGIN uint8_t USBD_ConfigDescFS[] __ALIGN_END =
{
    // Configuration Descriptor
    0x09,                           // bLength: Configuration Descriptor size 
    USB_DESC_TYPE_CONFIGURATION,    // bDescriptorType: Configuration 
    USB_CDC_CONFIG_DESC_SIZE,       // wTotalLength
    0x00,
    USBD_INTERFACES_COUNT,          // bNumInterfaces
    0x01,                           // bConfigurationValue: Configuration value 
    0x00,                           // iConfiguration:  (String not used) 
    0x80 | USBD_SELF_POWERED,       // bmAttributes
    0x4B,                           // MaxPower 150 mA 

    // ---------------------------------------------------------------------------

    // Interface Descriptor 
    0x09,                           // bLength: Interface Descriptor size 
    USB_DESC_TYPE_INTERFACE,        // bDescriptorType: Interface 
    0x00,                           // bInterfaceNumber: Number of Interface 
    0x00,                           // bAlternateSetting: Alternate setting 
    0x01,                           // bNumEndpoints: One endpoints used 
    0x02,                           // bInterfaceClass: Communication Interface Class 
    0x02,                           // bInterfaceSubClass: Abstract Control Model 
    0x01,                           // bInterfaceProtocol: Common AT commands 
    0x00,                           // iInterface:  (String not used) 

    // Header Functional Descriptor
    0x05,                           // bLength: Endpoint Descriptor size 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x00,                           // bDescriptorSubtype: Header Func Desc 
    0x10,                           // bcdCDC: spec release number 
    0x01,

    // Call Management Functional Descriptor
    0x05,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x01,                           // bDescriptorSubtype: Call Management Func Desc 
    0x00,                           // bmCapabilities: D0+D1 
    0x01,                           // bDataInterface: 1 

    // ACM Functional Descriptor
    0x04,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x02,                           // bDescriptorSubtype: Abstract Control Management desc 
    0x02,                           // bmCapabilities 

    // Union Functional Descriptor
    0x05,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x06,                           // bDescriptorSubtype: Union func desc 
    0x00,                           // bMasterInterface: Communication class interface 
    0x01,                           // bSlaveInterface0: Data Class Interface 

    // Notification Endpoint Descriptor
    0x07,                           // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,         // bDescriptorType: Endpoint 
    CDC_NOTI_EP,                    // bEndpointAddress 
    0x03,                           // bmAttributes: Interrupt 
    LOBYTE(CDC_NOTI_PACKET_SIZE),   // wMaxPacketSize: 
    HIBYTE(CDC_NOTI_PACKET_SIZE),
    CDC_FS_BINTERVAL,               // bInterval: 16 ms 

    //---------------------------------------------------------------------------

    // Data class interface descriptor
    0x09,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_INTERFACE,             // bDescriptorType: 
    0x01,                                // bInterfaceNumber: Number of Interface 
    0x00,                                // bAlternateSetting: Alternate setting 
    0x02,                                // bNumEndpoints: Two endpoints used 
    0x0A,                                // bInterfaceClass: CDC 
    0x00,                                // bInterfaceSubClass: 
    0x00,                                // bInterfaceProtocol: 
    0x00,                                // iInterface:  (String not used)  

    // Data OUT Endpoint Descriptor
    0x07,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,              // bDescriptorType: Endpoint 
    CDC_OUT_EP,                          // bEndpointAddress 
    0x02,                                // bmAttributes: Bulk 
    LOBYTE(CDC_DATA_FS_OUT_PACKET_SIZE), // wMaxPacketSize: 
    HIBYTE(CDC_DATA_FS_OUT_PACKET_SIZE),
    0x00,                                // bInterval: ignored for Bulk transfer 

    // Data IN Endpoint Descriptor
    0x07,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,              // bDescriptorType: Endpoint 
    CDC_IN_EP,                           // bEndpointAddress 
    0x02,                                // bmAttributes: Bulk 
    LOBYTE(CDC_DATA_FS_IN_PACKET_SIZE),  // wMaxPacketSize: 
    HIBYTE(CDC_DATA_FS_IN_PACKET_SIZE),
    0x00                                 // bInterval: ignored for Bulk transfer 
};


/*
// High Speed Configuration Descriptor
// not used for Full speed devices
__ALIGN_BEGIN uint8_t USBD_ConfigDescHS[] __ALIGN_END =
{
    // Configuration Descriptor
    0x09,                           // bLength: Configuration Descriptor size 
    USB_DESC_TYPE_CONFIGURATION,    // bDescriptorType: Configuration 
    USB_CDC_CONFIG_DESC_SIZE,       // wTotalLength
    0x00,
    USBD_INTERFACES_COUNT,          // bNumInterfaces
    0x01,                           // bConfigurationValue: Configuration value 
    0x00,                           // iConfiguration:  (String not used) 
    0x80 | USBD_SELF_POWERED,       // bmAttributes
    0x4B,                           // MaxPower 150 mA 

    // ---------------------------------------------------------------------------

    // Interface Descriptor 
    0x09,                           // bLength: Interface Descriptor size 
    USB_DESC_TYPE_INTERFACE,        // bDescriptorType: Interface 
    0x00,                           // bInterfaceNumber: Number of Interface 
    0x00,                           // bAlternateSetting: Alternate setting 
    0x01,                           // bNumEndpoints: One endpoints used 
    0x02,                           // bInterfaceClass: Communication Interface Class 
    0x02,                           // bInterfaceSubClass: Abstract Control Model 
    0x01,                           // bInterfaceProtocol: Common AT commands 
    0x00,                           // iInterface:  (String not used) 

    // Header Functional Descriptor
    0x05,                           // bLength: Endpoint Descriptor size 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x00,                           // bDescriptorSubtype: Header Func Desc 
    0x10,                           // bcdCDC: spec release number 
    0x01,

    // Call Management Functional Descriptor
    0x05,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x01,                           // bDescriptorSubtype: Call Management Func Desc 
    0x00,                           // bmCapabilities: D0+D1 
    0x01,                           // bDataInterface: 1 

    // ACM Functional Descriptor
    0x04,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x02,                           // bDescriptorSubtype: Abstract Control Management desc 
    0x02,                           // bmCapabilities 

    // Union Functional Descriptor
    0x05,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x06,                           // bDescriptorSubtype: Union func desc 
    0x00,                           // bMasterInterface: Communication class interface 
    0x01,                           // bSlaveInterface0: Data Class Interface 

    // Notification Endpoint Descriptor
    0x07,                           // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,         // bDescriptorType: Endpoint 
    CDC_NOTI_EP,                    // bEndpointAddress 
    0x03,                           // bmAttributes: Interrupt 
    LOBYTE(CDC_NOTI_PACKET_SIZE),   // wMaxPacketSize: 
    HIBYTE(CDC_NOTI_PACKET_SIZE),
    CDC_HS_BINTERVAL,               // bInterval: 16 ms

    // ---------------------------------------------------------------------------

    // Data class interface descriptor
    0x09,                           // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_INTERFACE,        // bDescriptorType: 
    0x01,                           // bInterfaceNumber: Number of Interface 
    0x00,                           // bAlternateSetting: Alternate setting 
    0x02,                           // bNumEndpoints: Two endpoints used 
    0x0A,                           // bInterfaceClass: CDC 
    0x00,                           // bInterfaceSubClass: 
    0x00,                           // bInterfaceProtocol: 
    0x00,                           // iInterface:  (String not used) 

    // Data OUT Endpoint Descriptor
    0x07,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,              // bDescriptorType: Endpoint 
    CDC_OUT_EP,                          // bEndpointAddress 
    0x02,                                // bmAttributes: Bulk 
    LOBYTE(CDC_DATA_HS_OUT_PACKET_SIZE), // wMaxPacketSize: 
    HIBYTE(CDC_DATA_HS_OUT_PACKET_SIZE),
    0x00,                                // bInterval: ignored for Bulk transfer 

    // Data IN Endpoint Descriptor
    0x07,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,              // bDescriptorType: Endpoint 
    CDC_IN_EP,                           // bEndpointAddress 
    0x02,                                // bmAttributes: Bulk 
    LOBYTE(CDC_DATA_HS_IN_PACKET_SIZE),  // wMaxPacketSize: 
    HIBYTE(CDC_DATA_HS_IN_PACKET_SIZE),
    0x00                                 // bInterval: ignored for Bulk transfer 
};


// not used for Full Speed device
__ALIGN_BEGIN uint8_t USBD_OtherSpeedDesc[] __ALIGN_END =
{
    // Configuration Descriptor
    0x09,                           // bLength: Configuation Descriptor size 
    USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION,
    USB_CDC_CONFIG_DESC_SIZE,       // wTotalLength
    0x00,
    USBD_INTERFACES_COUNT,          // bNumInterfaces
    0x01,                           // bConfigurationValue: 
    0x00,                           // iConfiguration:  (String not used)
    0x80 | USBD_SELF_POWERED,       // bmAttributes
    0x4B,                           // MaxPower 150 mA 

    // ---------------------------------------------------------------------------

    // Interface Descriptor 
    0x09,                           // bLength: Interface Descriptor size 
    USB_DESC_TYPE_INTERFACE,        // bDescriptorType: Interface 
    0x00,                           // bInterfaceNumber: Number of Interface 
    0x00,                           // bAlternateSetting: Alternate setting 
    0x01,                           // bNumEndpoints: One endpoints used 
    0x02,                           // bInterfaceClass: Communication Interface Class 
    0x02,                           // bInterfaceSubClass: Abstract Control Model 
    0x01,                           // bInterfaceProtocol: Common AT commands 
    0x00,                           // iInterface:  (String not used) 

    // Header Functional Descriptor
    0x05,                           // bLength: Endpoint Descriptor size 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x00,                           // bDescriptorSubtype: Header Func Desc 
    0x10,                           // bcdCDC: spec release number 
    0x01,

    // Call Management Functional Descriptor
    0x05,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x01,                           // bDescriptorSubtype: Call Management Func Desc 
    0x00,                           // bmCapabilities: D0+D1 
    0x01,                           // bDataInterface: 1 

    // ACM Functional Descriptor
    0x04,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x02,                           // bDescriptorSubtype: Abstract Control Management desc 
    0x02,                           // bmCapabilities 

    // Union Functional Descriptor
    0x05,                           // bFunctionLength 
    0x24,                           // bDescriptorType: CS_INTERFACE 
    0x06,                           // bDescriptorSubtype: Union func desc 
    0x00,                           // bMasterInterface: Communication class interface 
    0x01,                           // bSlaveInterface0: Data Class Interface 

    // Notification Endpoint Descriptor
    0x07,                           // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,         // bDescriptorType: Endpoint 
    CDC_NOTI_EP,                    // bEndpointAddress 
    0x03,                           // bmAttributes: Interrupt 
    LOBYTE(CDC_NOTI_PACKET_SIZE),   // wMaxPacketSize: 
    HIBYTE(CDC_NOTI_PACKET_SIZE),
    CDC_FS_BINTERVAL,               // bInterval: 16 ms

    //---------------------------------------------------------------------------

    // Data class interface descriptor
    0x09,                           // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_INTERFACE,        // bDescriptorType: 
    0x01,                           // bInterfaceNumber: Number of Interface 
    0x00,                           // bAlternateSetting: Alternate setting 
    0x02,                           // bNumEndpoints: Two endpoints used 
    0x0A,                           // bInterfaceClass: CDC 
    0x00,                           // bInterfaceSubClass: 
    0x00,                           // bInterfaceProtocol: 
    0x00,                           // iInterface:  (String not used) 

    // Data OUT Endpoint Descriptor
    0x07,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,              // bDescriptorType: Endpoint 
    CDC_OUT_EP,                          // bEndpointAddress 
    0x02,                                // bmAttributes: Bulk 
    LOBYTE(CDC_DATA_HS_OUT_PACKET_SIZE), // wMaxPacketSize: 
    HIBYTE(CDC_DATA_HS_OUT_PACKET_SIZE),
    0x00,                                // bInterval: ignored for Bulk transfer 

    // Data IN Endpoint Descriptor
    0x07,                                // bLength: Endpoint Descriptor size 
    USB_DESC_TYPE_ENDPOINT,              // bDescriptorType: Endpoint 
    CDC_IN_EP,                           // bEndpointAddress 
    0x02,                                // bmAttributes: Bulk 
    LOBYTE(CDC_DATA_HS_IN_PACKET_SIZE),  // wMaxPacketSize: 
    HIBYTE(CDC_DATA_HS_IN_PACKET_SIZE),
    0x00                                 // bInterval: ignored for Bulk transfer 
};


// not used for Full speed device
__ALIGN_BEGIN static uint8_t USBD_DeviceQualifierDesc[] __ALIGN_END =
{
    USB_LEN_DEV_QUALIFIER_DESC,
    USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00,
    0x02,
    0x00,
    0x00,
    0x00,
    0x40,
    0x01,
    0x00,
};
*/

// ============================================================================================================

// Configue Packet Memory Area (PMA) for all endpoints
// Called from USBD_LL_Init()
USBD_StatusTypeDef USBD_ConfigureEndpoints(USBD_HandleTypeDef *pdev)
{
    PCD_HandleTypeDef* hpcd = (PCD_HandleTypeDef*)pdev->pData;   
    uint32_t addr = BTABLE_SIZE;
    
    if (!USBD_LL_ConfigurePMA(hpcd, 0x00,        false, &addr, USB_MAX_EP0_SIZE)            || // EP 0 OUT, 64 byte, bulk
        !USBD_LL_ConfigurePMA(hpcd, 0x80,        false, &addr, USB_MAX_EP0_SIZE)            || // EP 0 IN,  64 byte, bulk
        !USBD_LL_ConfigurePMA(hpcd, CDC_OUT_EP,  false, &addr, CDC_DATA_FS_OUT_PACKET_SIZE) || // EP 1 OUT, 64 byte, bulk
        !USBD_LL_ConfigurePMA(hpcd, CDC_IN_EP,   false, &addr, CDC_DATA_FS_IN_PACKET_SIZE)  || // EP 1 IN,  64 byte, bulk
        !USBD_LL_ConfigurePMA(hpcd, CDC_NOTI_EP, false, &addr, CDC_NOTI_PACKET_SIZE))          // EP 2 IN,   8 byte, interrupt, 16 ms
        return USBD_FAIL; // PMA buffer overflow
        
    return USBD_OK;
}

// ============================================================================================================

/**
  * @brief  Initialize the CDC interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_CDC_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    pdev->pClassData = &USBD_CDC_Handle;
    
    /*
    if (pdev->dev_speed == USBD_SPEED_HIGH)
    {
        // Open EP IN 
        USBD_LL_OpenEP(pdev, CDC_IN_EP, USBD_EP_TYPE_BULK, CDC_DATA_HS_IN_PACKET_SIZE);

        pdev->ep_in[CDC_IN_EP & 0xFU].is_used = 1U;

        // Open EP OUT 
        USBD_LL_OpenEP(pdev, CDC_OUT_EP, USBD_EP_TYPE_BULK, CDC_DATA_HS_OUT_PACKET_SIZE);

        pdev->ep_out[CDC_OUT_EP & 0xFU].is_used = 1U;
    }
    else // USBD_SPEED_FULL
    */
    {
        /* Open EP IN */
        USBD_LL_OpenEP(pdev, CDC_IN_EP, USBD_EP_TYPE_BULK, CDC_DATA_FS_IN_PACKET_SIZE);

        pdev->ep_in[CDC_IN_EP & 0xFU].is_used = 1U;

        /* Open EP OUT */
        USBD_LL_OpenEP(pdev, CDC_OUT_EP, USBD_EP_TYPE_BULK, CDC_DATA_FS_OUT_PACKET_SIZE);

        pdev->ep_out[CDC_OUT_EP & 0xFU].is_used = 1U;
    }
    
    /* Open Notification IN EP */
    USBD_LL_OpenEP(pdev, CDC_NOTI_EP, USBD_EP_TYPE_INTR, CDC_NOTI_PACKET_SIZE);
    pdev->ep_in[CDC_NOTI_EP & 0xFU].is_used = 1U;

    USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;

    /* Init  physical Interface components */
    USBD_InterfaceCallbacks.Init();

    /* Init Xfer states */
    hcdc->TxState = 0U;
    hcdc->RxState = 0U;

    /*
    if (pdev->dev_speed == USBD_SPEED_HIGH)
    {
      // Prepare Out endpoint to receive next packet 
      USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc->RxBuffer, CDC_DATA_HS_OUT_PACKET_SIZE);
    }
    else // USBD_SPEED_FULL
    */
    {
      // Prepare Out endpoint to receive next packet 
      USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc->RxBuffer, CDC_DATA_FS_OUT_PACKET_SIZE);
    }
    return 0;
}

/**
  * @brief  DeInitialize the CDC layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_CDC_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  uint8_t ret = 0U;

  /* Close EP IN */
  USBD_LL_CloseEP(pdev, CDC_IN_EP);
  pdev->ep_in[CDC_IN_EP & 0xFU].is_used = 0U;

  /* Close EP OUT */
  USBD_LL_CloseEP(pdev, CDC_OUT_EP);
  pdev->ep_out[CDC_OUT_EP & 0xFU].is_used = 0U;

  /* Close Notification IN EP */
  USBD_LL_CloseEP(pdev, CDC_NOTI_EP);
  pdev->ep_in[CDC_NOTI_EP & 0xFU].is_used = 0U;

  /* DeInit  physical Interface components */
  if (pdev->pClassData != NULL)
  {
      USBD_InterfaceCallbacks.DeInit();
      pdev->pClassData = NULL;
  }
  return ret;
}

/**
  * @brief  Handle the CDC specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_CDC_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;
  uint8_t ifalt = 0;
  uint16_t status_info = 0;
  uint8_t ret = USBD_OK;

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS :
      if (req->wLength)
      {
        if ((req->bRequestType & USB_REQ_DIRECTION_MASK) == USB_REQ_DIRECTION_IN) // IN
        {
          USBD_InterfaceCallbacks.Control(req->bRequest, (uint8_t *)(void *)hcdc->data, req->wLength);

          USBD_CtlSendData(pdev, (uint8_t *)(void *)hcdc->data, req->wLength);
        }
        else // OUT
        {
          hcdc->CmdOpCode = req->bRequest;
          hcdc->CmdLength = (uint8_t)req->wLength;

          USBD_CtlPrepareRx(pdev, (uint8_t *)(void *)hcdc->data, req->wLength);
        }
      }
      else
      {
        USBD_InterfaceCallbacks.Control(req->bRequest, (uint8_t *)(void *)req, 0U);
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            USBD_CtlSendData(pdev, (uint8_t *)(void *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            USBD_CtlSendData(pdev, &ifalt, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state != USBD_STATE_CONFIGURED)
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }
  return ret;
}

/**
  * @brief  Data sent on non-control IN endpoint
  * @param  pdev: device instance
  * @param  epnum: endpoint number
  * @retval status
  */
static uint8_t USBD_CDC_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)pdev->pClassData;
  PCD_HandleTypeDef *hpcd = pdev->pData;

  if (pdev->pClassData != NULL)
  {
    if ((pdev->ep_in[epnum].total_length > 0U) && ((pdev->ep_in[epnum].total_length % hpcd->IN_ep[epnum].maxpacket) == 0U))
    {
      // Reset the packet total length
      pdev->ep_in[epnum].total_length = 0U;

      // Send ZLP
      // A ZLP is a USB packet that contains no data payload. It's length is zero.
      // ZLP's are important to signal the end of a data transfer when the last packet sent 
      // is exactly the maximum packet size (e.g. 64 bytes for full-speed USB).
      // Otherwise, if the last packet in a transfer is exactly wMaxPacketSize, the host cannot tell if more data is coming.
      // If the ZLP is missing the host will expect another packet to come. 
      USBD_LL_Transmit(pdev, epnum, NULL, 0U);
    }
    else
    {
      hcdc->TxState = 0U;
    }
    return USBD_OK;
  }
  return USBD_FAIL;
}

/**
  * @brief  Data received on non-control Out endpoint
  * @param  pdev: device instance
  * @param  epnum: endpoint number
  * @retval status
  */
static uint8_t  USBD_CDC_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;

  /* Get the received data length */
  hcdc->RxLength = USBD_LL_GetRxDataSize(pdev, epnum);

  /* USB data will be immediately processed, this allow next USB traffic being
  NAKed till the end of the application Xfer */
  if (pdev->pClassData != NULL)
  {
    USBD_InterfaceCallbacks.Receive(hcdc->RxBuffer, &hcdc->RxLength);
    return USBD_OK;
  }
  return USBD_FAIL;
}

/**
  * @brief  Handle EP0 Rx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t  USBD_CDC_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;

  if (hcdc->CmdOpCode != 0xFFU)
  {
     USBD_InterfaceCallbacks.Control(hcdc->CmdOpCode, (uint8_t *)(void *)hcdc->data, (uint16_t)hcdc->CmdLength);
     hcdc->CmdOpCode = 0xFFU;
  }
  return USBD_OK;
}

/**
  * @param  pdev: device instance
  * @param  pbuff: Tx Buffer
  * @retval status
  */
uint8_t  USBD_CDC_SetTxBuffer(USBD_HandleTypeDef* pdev, uint8_t *pbuff, uint16_t length)
{
  USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;
  hcdc->TxBuffer = pbuff;
  hcdc->TxLength = length;
  return USBD_OK;
}

/**
  * @param  pdev: device instance
  * @param  pbuff: Rx Buffer
  * @retval status
  */
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef* pdev, uint8_t  *pbuff)
{
  USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;
  hcdc->RxBuffer = pbuff;
  return USBD_OK;
}

/**
  * @brief  Transmit packet on IN endpoint
  * @param  pdev: device instance
  * @retval status
  */
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev)
{
  USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;

  if (pdev->pClassData != NULL)
  {
    if (hcdc->TxState == 0U)
    {
      /* Tx Transfer in progress */
      hcdc->TxState = 1U;

      /* Update the packet total length */
      pdev->ep_in[CDC_IN_EP & 0xFU].total_length = hcdc->TxLength;

      /* Transmit next packet */
      USBD_LL_Transmit(pdev, CDC_IN_EP, hcdc->TxBuffer, (uint16_t)hcdc->TxLength);
      return USBD_OK;
    }
    return USBD_BUSY;
  }
  return USBD_FAIL;
}

/**
  * @brief  prepare OUT Endpoint for reception
  * @param  pdev: device instance
  * @retval status
  */
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  USBD_CDC_HandleTypeDef   *hcdc = (USBD_CDC_HandleTypeDef *) pdev->pClassData;

  /* Suspend or Resume USB Out process */
  if (pdev->pClassData != NULL)
  {
    /*
    if (pdev->dev_speed == USBD_SPEED_HIGH)
    {
      // Prepare Out endpoint to receive next packet
      USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc->RxBuffer, CDC_DATA_HS_OUT_PACKET_SIZE);
    }
    else // USBD_SPEED_FULL
    */
    {
      // Prepare Out endpoint to receive next packet
      USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc->RxBuffer, CDC_DATA_FS_OUT_PACKET_SIZE);
    }
    return USBD_OK;
  }
  return USBD_FAIL;
}

// Called from lowlevel.c
// return true if handled
// not used for Slcan
bool USBD_SetupStageRequest(PCD_HandleTypeDef *hpcd)
{
    return false;
}
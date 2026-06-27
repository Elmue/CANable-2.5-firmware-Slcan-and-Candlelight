  /******************************************************************************
  * @file    usb_ctrlreq.c
  * @author  MCD Application Team
  * @brief   This file provides the standard USB requests following chapter 9.
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

#include "usb_ctrlreq.h"
#include "usb_ioreq.h"
#include "usb_class.h"

// global variables
extern uint8_t             USBD_DeviceDesc[];
extern uint8_t             USBD_ConfigDescFS[];
extern USBD_ClassTypeDef   USBD_ClassCallbacks;
extern USBD_HandleTypeDef  USB_Handle;

// extern uint8_t USBD_ConfigDescHS[];        // only for High Speed USB devices
// extern uint8_t USBD_OtherSpeedDesc[];      // only for High Speed USB devices
// extern uint8_t USBD_DeviceQualifierDesc[]; // only for High Speed USB devices

#if defined(Candlelight)
    #define USBD_PRODUCT_STRING  "Candlelight 2.5 - " TARGET_BOARD
#else
    #define USBD_PRODUCT_STRING  "Slcan 2.5 - " TARGET_BOARD
#endif

// USB lang indentifier descriptor.
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[] __ALIGN_END =
{
     USB_LEN_LANGID_STR_DESC, // = 4
     USB_DESC_TYPE_STRING,
     LOBYTE(0x0409), // english
     HIBYTE(0x0409)
};

// maximum 254 Unicode characters
__ALIGN_BEGIN uint8_t USBD_StrDesc[512] __ALIGN_END;

static void USBD_GetDescriptor(USBD_SetupReqTypedef *req);
static void USBD_SetAddress   (USBD_SetupReqTypedef *req);
static void USBD_SetConfig    (USBD_SetupReqTypedef *req);
static void USBD_GetConfig    (USBD_SetupReqTypedef *req);
static void USBD_GetStatus    (USBD_SetupReqTypedef *req);
static void USBD_SetFeature   (USBD_SetupReqTypedef *req);
static void USBD_ClrFeature   (USBD_SetupReqTypedef *req);

// @brief  Handle standard usb device requests
// @param  req: usb request
USBD_StatusTypeDef  USBD_StdDevReq(USBD_SetupReqTypedef *req)
{
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
      USBD_ClassCallbacks.Setup(req);
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_DESCRIPTOR:
          USBD_GetDescriptor(req);
          break;

        case USB_REQ_SET_ADDRESS:
          USBD_SetAddress(req);
          break;

        case USB_REQ_SET_CONFIGURATION:
          USBD_SetConfig(req);
          break;

        case USB_REQ_GET_CONFIGURATION:
          USBD_GetConfig(req);
          break;

        case USB_REQ_GET_STATUS:
          USBD_GetStatus(req);
          break;

        case USB_REQ_SET_FEATURE:
          USBD_SetFeature(req);
          break;

        case USB_REQ_CLEAR_FEATURE:
          USBD_ClrFeature(req);
          break;

        default:
          USBD_CtlError(req);
          break;
      }
      break;

    default:
      USBD_CtlError(req);
      break;
  }

  return ret;
}

// @brief  Handle standard usb interface requests
// @param  req: usb request
USBD_StatusTypeDef  USBD_StdItfReq(USBD_SetupReqTypedef  *req)
{
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
    case USB_REQ_TYPE_STANDARD:
      switch (USB_Handle.dev_state)
      {
        case USBD_STATE_DEFAULT:
        case USBD_STATE_ADDRESSED:
        case USBD_STATE_CONFIGURED:

          // wIndex = interface number
          if (LOBYTE(req->wIndex) >= USBD_INTERFACES_COUNT)
          {
            USBD_CtlError(req);
          }
          else
          {
            ret = (USBD_StatusTypeDef)USBD_ClassCallbacks.Setup(req);

            if ((req->wLength == 0) && (ret == USBD_OK))
            {
              USBD_CtlSendStatus();
            }
          }
          break;

        default:
          USBD_CtlError(req);
          break;
      }
      break;

    default:
      USBD_CtlError(req);
      break;
  }

  return USBD_OK;
}

// @brief  Handle standard usb endpoint requests
// @param  req: usb request
USBD_StatusTypeDef  USBD_StdEPReq(USBD_SetupReqTypedef  *req)
{
  USBD_EndpointTypeDef *pep;
  uint8_t   ep_addr;
  USBD_StatusTypeDef ret = USBD_OK;
  ep_addr  = LOBYTE(req->wIndex);

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
      USBD_ClassCallbacks.Setup(req);
      break;

    case USB_REQ_TYPE_STANDARD:
      if ((req->bRequestType & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS)
        return (USBD_StatusTypeDef)USBD_ClassCallbacks.Setup(req);

      switch (req->bRequest)
      {
        case USB_REQ_SET_FEATURE:
          switch (USB_Handle.dev_state)
          {
            case USBD_STATE_ADDRESSED:
              if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
              {
                USBD_LL_StallEP(ep_addr);
                USBD_LL_StallEP(0x80U);
              }
              else
              {
                USBD_CtlError(req);
              }
              break;

            case USBD_STATE_CONFIGURED:
              if (req->wValue == USB_FEATURE_EP_HALT)
              {
                if ((ep_addr != 0x00U) && (ep_addr != 0x80U) && (req->wLength == 0x00U))
                    USBD_LL_StallEP(ep_addr);
              }
              USBD_CtlSendStatus();
              break;

            default:
              USBD_CtlError(req);
              break;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:

          switch (USB_Handle.dev_state)
          {
            case USBD_STATE_ADDRESSED:
              if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
              {
                USBD_LL_StallEP(ep_addr);
                USBD_LL_StallEP(0x80U);
              }
              else
              {
                USBD_CtlError(req);
              }
              break;

            case USBD_STATE_CONFIGURED:
              if (req->wValue == USB_FEATURE_EP_HALT)
              {
                if ((ep_addr & 0x7FU) != 0x00U)
                {
                  USBD_LL_ClearStallEP(ep_addr);
                }
                USBD_CtlSendStatus();
              }
              break;

            default:
              USBD_CtlError(req);
              break;
          }
          break;

        case USB_REQ_GET_STATUS:
          switch (USB_Handle.dev_state)
          {
            case USBD_STATE_ADDRESSED:
              if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
              {
                USBD_CtlError(req);
                break;
              }
              pep = ((ep_addr & 0x80U) == 0x80U) ? &USB_Handle.ep_in[ep_addr & 0x7FU] : &USB_Handle.ep_out[ep_addr & 0x7FU];
              pep->status = 0x0000U;
              USBD_CtlSendData((uint8_t *)(void *)&pep->status, 2U);
              break;

            case USBD_STATE_CONFIGURED:
              if ((ep_addr & 0x80U) == 0x80U)
              {
                if (USB_Handle.ep_in[ep_addr & 0xFU].is_used == 0)
                {
                  USBD_CtlError(req);
                  break;
                }
              }
              else
              {
                if (USB_Handle.ep_out[ep_addr & 0xFU].is_used == 0)
                {
                  USBD_CtlError(req);
                  break;
                }
              }

              pep = ((ep_addr & 0x80U) == 0x80U) ? &USB_Handle.ep_in[ep_addr & 0x7FU] : &USB_Handle.ep_out[ep_addr & 0x7FU];
              if ((ep_addr == 0x00U) || (ep_addr == 0x80U))
              {
                pep->status = 0x0000U;
              }
              else if (USBD_LL_IsStallEP(ep_addr))
              {
                pep->status = 0x0001U;
              }
              else
              {
                pep->status = 0x0000U;
              }

              USBD_CtlSendData((uint8_t *)(void *)&pep->status, 2U);
              break;

            default:
              USBD_CtlError(req);
              break;
          }
          break;

        default:
          USBD_CtlError(req);
          break;
      }
      break;

    default:
      USBD_CtlError(req);
      break;
  }
  return ret;
}


// @brief  Handle Get Descriptor requests
// @param  req: usb request
static void USBD_GetDescriptor(USBD_SetupReqTypedef *req)
{
    uint16_t len  = 0;
    uint8_t* pbuf = NULL;

    switch (req->wValue >> 8)
    {
        case USB_DESC_TYPE_DEVICE:
            pbuf = USBD_DeviceDesc;
            len  = USB_LEN_DEV_DESC; // always 18 byte
            break;

        case USB_DESC_TYPE_CONFIGURATION:
            /*
            if (USB_Handle.dev_speed == USBD_SPEED_HIGH)
                pbuf = USBD_ConfigDescHS;
            else */
                pbuf = USBD_ConfigDescFS;

            // The primitive GCC is not able to compile sizeof(USBD_ConfigDescFS) so we use byte 2 = wTotalLength
            len = pbuf[2];
            break;

        /*
        // only needed for High Speed USB devices
        case USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION:
            pbuf = USBD_OtherSpeedDesc;
            // The primitive GCC is not able to compile sizeof(USBD_OtherSpeedDesc) so we use byte 2 = wTotalLength
            len  = pbuf[2];
            break;

        // only needed for High Speed USB devices
        case USB_DESC_TYPE_DEVICE_QUALIFIER:
            pbuf = USBD_DeviceQualifierDesc;
            len  = USB_LEN_DEV_QUALIFIER_DESC;
            break;
        */

        case USB_DESC_TYPE_BOS:
            // TODO: Implement if needed
            break;

        case USB_DESC_TYPE_STRING:
        {
            switch ((uint8_t)req->wValue)
            {
                case USBD_IDX_LANGID_STR: // supported languages for strings
                    pbuf = USBD_LangIDDesc;
                    len  = sizeof(USBD_LangIDDesc);
                    break;

                case USBD_IDX_MFC_STR:     // USB Device Manufacrturer String
                    pbuf = USBD_GetStringDescr("ElmueSoft (netcult.ch/elmue)", &len);
                    break;

                case USBD_IDX_PRODUCT_STR: // USB Device Product String
                    pbuf = USBD_GetStringDescr(USBD_PRODUCT_STRING, &len);
                    break;

                case USBD_IDX_SERIAL_STR:  // USB Device Serial String
                {
                    char serial_no[20];
                    USBD_GetSerialNumber(serial_no);
                    pbuf = USBD_GetStringDescr(serial_no, &len);
                    break;
                }
                default: // Interface string descriptors and MS OS descriptors (Candlelight only)
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
                    pbuf = USBD_GetUserStringDescr(req->wValue, &len);
#endif
                    break;
            } // switch
            break;
        } // case USB_DESC_TYPE_STRING
    } // switch

    if (!pbuf)
    {
        USBD_CtlError(req); // Stall Endpoint (unknown descriptor requested)
        return;
    }

    if (req->wLength == 0)
    {
        USBD_CtlSendStatus();
    }
    else if (len > 0)
    {
        len = MIN(len, req->wLength);
        USBD_CtlSendData(pbuf, len);
    }
}

// @brief  Set device address
// @param  req: usb request
static void USBD_SetAddress(USBD_SetupReqTypedef *req)
{
  if ((req->wIndex == 0) && (req->wLength == 0) && (req->wValue < 128U))
  {
    uint8_t dev_addr = (uint8_t)(req->wValue) & 0x7FU;

    if (USB_Handle.dev_state == USBD_STATE_CONFIGURED)
    {
      USBD_CtlError(req);
    }
    else
    {
      USB_Handle.dev_address = dev_addr;
      USBD_LL_SetUSBAddress(dev_addr);
      USBD_CtlSendStatus();

      if (dev_addr != 0)
      {
        USB_Handle.dev_state = USBD_STATE_ADDRESSED;
      }
      else
      {
        USB_Handle.dev_state = USBD_STATE_DEFAULT;
      }
    }
  }
  else
  {
    USBD_CtlError(req);
  }
}

// @brief  Handle Set device configuration request
// @param  req: usb request
static void USBD_SetConfig(USBD_SetupReqTypedef *req)
{
  static uint8_t cfgidx;
  cfgidx = (uint8_t)(req->wValue);

  // cfgidx = 0 --> device is not configured (only SETUP commands allowed)
  // cfgidx = 1 --> select the first Configuration
  if (cfgidx > USBD_CONFIGURATIONS_COUNT)
  {
    USBD_CtlError(req);
  }
  else
  {
    switch (USB_Handle.dev_state)
    {
      case USBD_STATE_ADDRESSED:
        if (cfgidx)
        {
          USB_Handle.dev_config = cfgidx;
          USB_Handle.dev_state = USBD_STATE_CONFIGURED;
          if (USBD_SetClassConfig(cfgidx) == USBD_FAIL)
          {
            USBD_CtlError(req);
            return;
          }
          USBD_CtlSendStatus();
        }
        else
        {
          USBD_CtlSendStatus();
        }
        break;

      case USBD_STATE_CONFIGURED:
        if (cfgidx == 0)
        {
          USB_Handle.dev_state = USBD_STATE_ADDRESSED;
          USB_Handle.dev_config = cfgidx;
          USBD_ClrClassConfig(cfgidx);
          USBD_CtlSendStatus();
        }
        else if (cfgidx != USB_Handle.dev_config)
        {
          /* Clear old configuration */
          USBD_ClrClassConfig((uint8_t)USB_Handle.dev_config);

          /* set new configuration */
          USB_Handle.dev_config = cfgidx;
          if (USBD_SetClassConfig(cfgidx) == USBD_FAIL)
          {
            USBD_CtlError(req);
            return;
          }
          USBD_CtlSendStatus();
        }
        else
        {
          USBD_CtlSendStatus();
        }
        break;

      default:
        USBD_CtlError(req);
        USBD_ClrClassConfig(cfgidx);
        break;
    }
  }
}

// @brief  Handle Get device configuration request
// @param  req: usb request
static void USBD_GetConfig(USBD_SetupReqTypedef *req)
{
  if (req->wLength != 1U)
  {
    USBD_CtlError(req);
  }
  else
  {
    switch (USB_Handle.dev_state)
    {
      case USBD_STATE_DEFAULT:
      case USBD_STATE_ADDRESSED:
        USB_Handle.dev_default_config = 0;
        USBD_CtlSendData((uint8_t *)(void *)&USB_Handle.dev_default_config, 1U);
        break;

      case USBD_STATE_CONFIGURED:
        USBD_CtlSendData((uint8_t *)(void *)&USB_Handle.dev_config, 1U);
        break;

      default:
        USBD_CtlError(req);
        break;
    }
  }
}

// @brief  Handle Get Status request
// @param  req: usb request
static void USBD_GetStatus(USBD_SetupReqTypedef *req)
{
  switch (USB_Handle.dev_state)
  {
    case USBD_STATE_DEFAULT:
    case USBD_STATE_ADDRESSED:
    case USBD_STATE_CONFIGURED:
      if (req->wLength != 0x2U)
      {
          USBD_CtlError(req);
          break;
      }

#if (USBD_SELF_POWERED > 0)
        USB_Handle.dev_config_status = USB_CONFIG_SELF_POWERED; // status flag
#else
        USB_Handle.dev_config_status = 0;
#endif
        if (USB_Handle.dev_remote_wakeup)
            USB_Handle.dev_config_status |= USB_CONFIG_REMOTE_WAKEUP; // status flag

        USBD_CtlSendData((uint8_t *)(void *)&USB_Handle.dev_config_status, 2U);
        break;

    default:
        USBD_CtlError(req);
        break;
  }
}


// @brief  Handle Set device feature request
// @param  req: usb request
static void USBD_SetFeature(USBD_SetupReqTypedef *req)
{
  if (req->wValue == USB_FEATURE_REMOTE_WAKEUP)
  {
    USB_Handle.dev_remote_wakeup = 1U;
    USBD_CtlSendStatus();
  }
}


// @brief  Handle clear device feature request
// @param  req: usb request
static void USBD_ClrFeature(USBD_SetupReqTypedef *req)
{
  switch (USB_Handle.dev_state)
  {
    case USBD_STATE_DEFAULT:
    case USBD_STATE_ADDRESSED:
    case USBD_STATE_CONFIGURED:
      if (req->wValue == USB_FEATURE_REMOTE_WAKEUP)
      {
        USB_Handle.dev_remote_wakeup = 0;
        USBD_CtlSendStatus();
      }
      break;

    default:
      USBD_CtlError(req);
      break;
  }
}

// @brief  Copy buffer into setup structure
// @param  req: usb request
void USBD_ParseSetupRequest(USBD_SetupReqTypedef *req, uint8_t *pdata)
{
    req->bRequestType = *(uint8_t *)(pdata);
    req->bRequest     = *(uint8_t *)(pdata + 1U);
    req->wValue       = SWAPBYTE(pdata + 2U);
    req->wIndex       = SWAPBYTE(pdata + 4U);
    req->wLength      = SWAPBYTE(pdata + 6U);
}

// @brief  Handle USB low level Error
void USBD_CtlError(USBD_SetupReqTypedef *req)
{
    USBD_LL_StallEP(0x80U);
    USBD_LL_StallEP(0);
}

// Gets the serial number of the processor as ASCII string.
// STM guarantees that each procesor has a unique serial number.
void USBD_GetSerialNumber(char serial_no[20])
{
    // get the 96 bit serial number which is unique for each processor that ST Microelectrons has ever produced.
    uint32_t serial_0 = *(uint32_t*)(UID_BASE    );
    uint32_t serial_1 = *(uint32_t*)(UID_BASE + 4);
    uint32_t serial_2 = *(uint32_t*)(UID_BASE + 8);

    // reduce 96 bit to 64 bit
    serial_0 += serial_2;

#if defined(Candlelight)
    // Depending on the firmware that the user uploads the same device may have one or multiple Candlelight interfaces.
    // The operating system must install different drivers per interface depending on the interface count.
    sprintf(serial_no, "%08lX%08lX%u", serial_0, serial_1, USBD_INTERFACES_COUNT);
#else // Slcan
    sprintf(serial_no, "%08lX%08lX",   serial_0, serial_1);
#endif
}

// @brief  Convert Ascii string into unicode one
// @param  desc : descriptor buffer (ASCII)
// @param  unicode : Formatted string buffer (unicode)
// @param  len : descriptor length in bytes
uint8_t* USBD_GetStringDescr(char* descr, uint16_t *len)
{
    if (descr != NULL)
    {
        *len = (uint16_t)strlen(descr) * 2 + 2;
        uint8_t idx = 0;
        USBD_StrDesc[idx++] = *(uint8_t*)(void*)len;
        USBD_StrDesc[idx++] = USB_DESC_TYPE_STRING;

        while (*descr != '\0')
        {
            USBD_StrDesc[idx++] = *descr ++;
            USBD_StrDesc[idx++] = 0;
        }
    }
    return USBD_StrDesc;
}



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

extern uint8_t USBD_DeviceDesc[];
extern uint8_t USBD_ConfigDescFS[];
// extern uint8_t USBD_ConfigDescHS[];        // only for High Speed USB devices
// extern uint8_t USBD_OtherSpeedDesc[];      // only for High Speed USB devices
// extern uint8_t USBD_DeviceQualifierDesc[]; // only for High Speed USB devices

#define USBD_PRODUCT_STRING  "CANable 2.5 " TARGET_FIRMWARE

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

static void USBD_GetDescriptor(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void USBD_SetAddress   (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void USBD_SetConfig    (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void USBD_GetConfig    (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void USBD_GetStatus    (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void USBD_SetFeature   (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void USBD_ClrFeature   (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

// @brief  Handle standard usb device requests
// @param  pdev: device instance
// @param  req: usb request
USBD_StatusTypeDef  USBD_StdDevReq(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
      pdev->pClass->Setup(pdev, req);
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_DESCRIPTOR:
          USBD_GetDescriptor(pdev, req);
          break;

        case USB_REQ_SET_ADDRESS:
          USBD_SetAddress(pdev, req);
          break;

        case USB_REQ_SET_CONFIGURATION:
          USBD_SetConfig(pdev, req);
          break;

        case USB_REQ_GET_CONFIGURATION:
          USBD_GetConfig(pdev, req);
          break;

        case USB_REQ_GET_STATUS:
          USBD_GetStatus(pdev, req);
          break;

        case USB_REQ_SET_FEATURE:
          USBD_SetFeature(pdev, req);
          break;

        case USB_REQ_CLEAR_FEATURE:
          USBD_ClrFeature(pdev, req);
          break;

        default:
          USBD_CtlError(pdev, req);
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      break;
  }

  return ret;
}

// @brief  Handle standard usb interface requests
// @param  pdev: device instance
// @param  req: usb request
USBD_StatusTypeDef  USBD_StdItfReq(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef  *req)
{
  USBD_StatusTypeDef ret = USBD_OK;

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
    case USB_REQ_TYPE_STANDARD:
      switch (pdev->dev_state)
      {
        case USBD_STATE_DEFAULT:
        case USBD_STATE_ADDRESSED:
        case USBD_STATE_CONFIGURED:

          // wIndex = interface number
          if (LOBYTE(req->wIndex) >= USBD_INTERFACES_COUNT)
          {
            USBD_CtlError(pdev, req);
          }
          else
          {
            ret = (USBD_StatusTypeDef)pdev->pClass->Setup(pdev, req);

            if ((req->wLength == 0U) && (ret == USBD_OK))
            {
              USBD_CtlSendStatus(pdev);
            }
          }
          break;

        default:
          USBD_CtlError(pdev, req);
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      break;
  }

  return USBD_OK;
}

// @brief  Handle standard usb endpoint requests
// @param  pdev: device instance
// @param  req: usb request
USBD_StatusTypeDef  USBD_StdEPReq(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef  *req)
{
  USBD_EndpointTypeDef *pep;
  uint8_t   ep_addr;
  USBD_StatusTypeDef ret = USBD_OK;
  ep_addr  = LOBYTE(req->wIndex);

  switch (req->bRequestType & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
    case USB_REQ_TYPE_VENDOR:
      pdev->pClass->Setup(pdev, req);
      break;

    case USB_REQ_TYPE_STANDARD:
      if ((req->bRequestType & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS)
        return (USBD_StatusTypeDef)pdev->pClass->Setup(pdev, req);

      switch (req->bRequest)
      {
        case USB_REQ_SET_FEATURE:
          switch (pdev->dev_state)
          {
            case USBD_STATE_ADDRESSED:
              if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
              {
                USBD_LL_StallEP(pdev, ep_addr);
                USBD_LL_StallEP(pdev, 0x80U);
              }
              else
              {
                USBD_CtlError(pdev, req);
              }
              break;

            case USBD_STATE_CONFIGURED:
              if (req->wValue == USB_FEATURE_EP_HALT)
              {
                if ((ep_addr != 0x00U) && (ep_addr != 0x80U) && (req->wLength == 0x00U))
                    USBD_LL_StallEP(pdev, ep_addr);
              }
              USBD_CtlSendStatus(pdev);
              break;

            default:
              USBD_CtlError(pdev, req);
              break;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:

          switch (pdev->dev_state)
          {
            case USBD_STATE_ADDRESSED:
              if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
              {
                USBD_LL_StallEP(pdev, ep_addr);
                USBD_LL_StallEP(pdev, 0x80U);
              }
              else
              {
                USBD_CtlError(pdev, req);
              }
              break;

            case USBD_STATE_CONFIGURED:
              if (req->wValue == USB_FEATURE_EP_HALT)
              {
                if ((ep_addr & 0x7FU) != 0x00U)
                {
                  USBD_LL_ClearStallEP(pdev, ep_addr);
                }
                USBD_CtlSendStatus(pdev);
              }
              break;

            default:
              USBD_CtlError(pdev, req);
              break;
          }
          break;

        case USB_REQ_GET_STATUS:
          switch (pdev->dev_state)
          {
            case USBD_STATE_ADDRESSED:
              if ((ep_addr != 0x00U) && (ep_addr != 0x80U))
              {
                USBD_CtlError(pdev, req);
                break;
              }
              pep = ((ep_addr & 0x80U) == 0x80U) ? &pdev->ep_in[ep_addr & 0x7FU] : &pdev->ep_out[ep_addr & 0x7FU];
              pep->status = 0x0000U;
              USBD_CtlSendData(pdev, (uint8_t *)(void *)&pep->status, 2U);
              break;

            case USBD_STATE_CONFIGURED:
              if ((ep_addr & 0x80U) == 0x80U)
              {
                if (pdev->ep_in[ep_addr & 0xFU].is_used == 0U)
                {
                  USBD_CtlError(pdev, req);
                  break;
                }
              }
              else
              {
                if (pdev->ep_out[ep_addr & 0xFU].is_used == 0U)
                {
                  USBD_CtlError(pdev, req);
                  break;
                }
              }

              pep = ((ep_addr & 0x80U) == 0x80U) ? &pdev->ep_in[ep_addr & 0x7FU] : &pdev->ep_out[ep_addr & 0x7FU];
              if ((ep_addr == 0x00U) || (ep_addr == 0x80U))
              {
                pep->status = 0x0000U;
              }
              else if (USBD_LL_IsStallEP(pdev, ep_addr))
              {
                pep->status = 0x0001U;
              }
              else
              {
                pep->status = 0x0000U;
              }

              USBD_CtlSendData(pdev, (uint8_t *)(void *)&pep->status, 2U);
              break;

            default:
              USBD_CtlError(pdev, req);
              break;
          }
          break;

        default:
          USBD_CtlError(pdev, req);
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      break;
  }
  return ret;
}


// @brief  Handle Get Descriptor requests
// @param  pdev: device instance
// @param  req: usb request
static void USBD_GetDescriptor(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
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
            if (pdev->dev_speed == USBD_SPEED_HIGH)
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
                    // get the 96 bit serial number which is unique for each processor that ST Microelectrons has ever produced.
                    uint32_t serial_0 = *(uint32_t*)(UID_BASE    );
                    uint32_t serial_1 = *(uint32_t*)(UID_BASE + 4);
                    uint32_t serial_2 = *(uint32_t*)(UID_BASE + 8);

                    // reduce 96 bit to 64 bit
                    serial_0 += serial_2;

                    // Format USB serial number
                    char s8_Serial[20];
#if defined(Candlelight)
                    // Depending on the firmware that the user uploads the same device may have one or multiple Candlelight interfaces.
                    // The operating system must install different drivers per interface depending on the interface count.
                    sprintf(s8_Serial, "%08lX%08lX%u", serial_0, serial_1, USBD_INTERFACES_COUNT);
#else // Slcan
                    sprintf(s8_Serial, "%08lX%08lX",   serial_0, serial_1);
#endif
                    pbuf = USBD_GetStringDescr(s8_Serial, &len);
                    break;
                }
                default: // Interface string descriptors and MS OS descriptors (Candlelight only)
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
                    pbuf = USBD_GetUserStringDescr(pdev, req->wValue, &len);
#endif
                    break;
            } // switch
            break;
        } // case USB_DESC_TYPE_STRING
    } // switch

    if (!pbuf)
    {
        USBD_CtlError(pdev, req); // Stall Endpoint (unknown descriptor requested)
        return;
    }

    if (req->wLength == 0)
    {
        USBD_CtlSendStatus(pdev);
    }
    else if (len > 0)
    {
        len = MIN(len, req->wLength);
        USBD_CtlSendData(pdev, pbuf, len);
    }
}

// @brief  Set device address
// @param  pdev: device instance
// @param  req: usb request
static void USBD_SetAddress(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  if ((req->wIndex == 0U) && (req->wLength == 0U) && (req->wValue < 128U))
  {
    uint8_t dev_addr = (uint8_t)(req->wValue) & 0x7FU;

    if (pdev->dev_state == USBD_STATE_CONFIGURED)
    {
      USBD_CtlError(pdev, req);
    }
    else
    {
      pdev->dev_address = dev_addr;
      USBD_LL_SetUSBAddress(pdev, dev_addr);
      USBD_CtlSendStatus(pdev);

      if (dev_addr != 0U)
      {
        pdev->dev_state = USBD_STATE_ADDRESSED;
      }
      else
      {
        pdev->dev_state = USBD_STATE_DEFAULT;
      }
    }
  }
  else
  {
    USBD_CtlError(pdev, req);
  }
}

// @brief  Handle Set device configuration request
// @param  pdev: device instance
// @param  req: usb request
static void USBD_SetConfig(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  static uint8_t cfgidx;
  cfgidx = (uint8_t)(req->wValue);

  // cfgidx = 0 --> device is not configured (only SETUP commands allowed)
  // cfgidx = 1 --> select the first Configuration
  if (cfgidx > USBD_CONFIGURATIONS_COUNT)
  {
    USBD_CtlError(pdev, req);
  }
  else
  {
    switch (pdev->dev_state)
    {
      case USBD_STATE_ADDRESSED:
        if (cfgidx)
        {
          pdev->dev_config = cfgidx;
          pdev->dev_state = USBD_STATE_CONFIGURED;
          if (USBD_SetClassConfig(pdev, cfgidx) == USBD_FAIL)
          {
            USBD_CtlError(pdev, req);
            return;
          }
          USBD_CtlSendStatus(pdev);
        }
        else
        {
          USBD_CtlSendStatus(pdev);
        }
        break;

      case USBD_STATE_CONFIGURED:
        if (cfgidx == 0U)
        {
          pdev->dev_state = USBD_STATE_ADDRESSED;
          pdev->dev_config = cfgidx;
          USBD_ClrClassConfig(pdev, cfgidx);
          USBD_CtlSendStatus(pdev);
        }
        else if (cfgidx != pdev->dev_config)
        {
          /* Clear old configuration */
          USBD_ClrClassConfig(pdev, (uint8_t)pdev->dev_config);

          /* set new configuration */
          pdev->dev_config = cfgidx;
          if (USBD_SetClassConfig(pdev, cfgidx) == USBD_FAIL)
          {
            USBD_CtlError(pdev, req);
            return;
          }
          USBD_CtlSendStatus(pdev);
        }
        else
        {
          USBD_CtlSendStatus(pdev);
        }
        break;

      default:
        USBD_CtlError(pdev, req);
        USBD_ClrClassConfig(pdev, cfgidx);
        break;
    }
  }
}

// @brief  Handle Get device configuration request
// @param  pdev: device instance
// @param  req: usb request
static void USBD_GetConfig(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  if (req->wLength != 1U)
  {
    USBD_CtlError(pdev, req);
  }
  else
  {
    switch (pdev->dev_state)
    {
      case USBD_STATE_DEFAULT:
      case USBD_STATE_ADDRESSED:
        pdev->dev_default_config = 0U;
        USBD_CtlSendData(pdev, (uint8_t *)(void *)&pdev->dev_default_config, 1U);
        break;

      case USBD_STATE_CONFIGURED:
        USBD_CtlSendData(pdev, (uint8_t *)(void *)&pdev->dev_config, 1U);
        break;

      default:
        USBD_CtlError(pdev, req);
        break;
    }
  }
}

// @brief  Handle Get Status request
// @param  pdev: device instance
// @param  req: usb request
static void USBD_GetStatus(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  switch (pdev->dev_state)
  {
    case USBD_STATE_DEFAULT:
    case USBD_STATE_ADDRESSED:
    case USBD_STATE_CONFIGURED:
      if (req->wLength != 0x2U)
      {
          USBD_CtlError(pdev, req);
          break;
      }

#if (USBD_SELF_POWERED > 0)
        pdev->dev_config_status = USB_CONFIG_SELF_POWERED; // status flag
#else
        pdev->dev_config_status = 0U;
#endif
        if (pdev->dev_remote_wakeup)
            pdev->dev_config_status |= USB_CONFIG_REMOTE_WAKEUP; // status flag

        USBD_CtlSendData(pdev, (uint8_t *)(void *)&pdev->dev_config_status, 2U);
        break;

    default:
        USBD_CtlError(pdev, req);
        break;
  }
}


// @brief  Handle Set device feature request
// @param  pdev: device instance
// @param  req: usb request
static void USBD_SetFeature(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  if (req->wValue == USB_FEATURE_REMOTE_WAKEUP)
  {
    pdev->dev_remote_wakeup = 1U;
    USBD_CtlSendStatus(pdev);
  }
}


// @brief  Handle clear device feature request
// @param  pdev: device instance
// @param  req: usb request
static void USBD_ClrFeature(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  switch (pdev->dev_state)
  {
    case USBD_STATE_DEFAULT:
    case USBD_STATE_ADDRESSED:
    case USBD_STATE_CONFIGURED:
      if (req->wValue == USB_FEATURE_REMOTE_WAKEUP)
      {
        pdev->dev_remote_wakeup = 0U;
        USBD_CtlSendStatus(pdev);
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      break;
  }
}

// @brief  Copy buffer into setup structure
// @param  pdev: device instance
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
// @param  pdev: device instance
void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    USBD_LL_StallEP(pdev, 0x80U);
    USBD_LL_StallEP(pdev, 0U);
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



/*
    The MIT License
    Implemenatation of USB GS Class (Geschwister Schneider)
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "usb_class.h"
#include "usb_ctrlreq.h"
#include "usb_core.h"
#include "control.h"
#include "buffer.h"
#include "usb_lowlevel.h"
#include "system.h"
#include "utils.h"
#include "dfu.h"
#include "can.h"

#define EP_DATA_PACKET_SIZE         64                      // Data endpoints IN + OUT = max 64 byte
#define DFU_INTERFACE_STR_IDX       (USBD_IDX_NEXT_STR + 0) // "Firmware Update Interface"
#define CANDLE_INTERFACE_STR_1_IDX  (USBD_IDX_NEXT_STR + 1) // "CAN FD Interface 1"
#define CANDLE_INTERFACE_STR_2_IDX  (USBD_IDX_NEXT_STR + 2) // "CAN FD Interface 2"
#define CANDLE_INTERFACE_STR_3_IDX  (USBD_IDX_NEXT_STR + 3) // "CAN FD Interface 3"
#define USBD_MS_OS_VENDOR_CODE      0x20                    // MS OS String: "MSFT100" + Vendor Code

// calculate total size of Configuration descriptor
#define USB_LEN_CANDLE_DESC         (USB_LEN_IF_DESC  + USB_LEN_EP_DESC * 2)
#define USB_LEN_FIRMWARE_DESC       (USB_LEN_IF_DESC  + USB_LEN_DFU_DESC)
#define USB_LEN_CONF_TOT_DESC       (USB_LEN_CFG_DESC + USB_LEN_CANDLE_DESC * CANDLE_INRERFACE_COUNT + USB_LEN_FIRMWARE_DESC)

// calculate total size of MS OS Feature descriptor
#define USB_LEN_FEATURE_DESC        16
#define USB_LEN_WINUSB_DESC         24
#define USB_LEN_MS_OS_TOT_DESC      (USB_LEN_FEATURE_DESC + USB_LEN_WINUSB_DESC * USBD_INTERFACES_COUNT)

// The PMA Buffer has a size of 1024 bytes and starts with the BTABLE (Buffer Description Table)
// For each single buffered endpoint 8 bytes are reserved in the BTABLE for IN or OUT or both.
// For each double buffered endpoint 8 bytes are reserved in the BTABLE for only one direction: IN or OUT. Both is not possible!
// See "STM32G4 Series - Chapter USB.pdf" --> page 6 in subfolder Documentation
// See USB_ActivateEndpoint() --> PCD_SET_EP_TX_ADDRESS() and PCD_SET_EP_DBUF_ADDR()
// Therefore: IN + OUT for Enpoints 0,1,2,3 is NOT possible here!
// -----------------------------------
// Endpoint 0 IN + OUT single = 8 byte
// Endpoint 1 IN + --- single = 8 byte (4 byte unused)
// Endpoint 2 -- + OUT double = 8 byte
// Endpoint 3 IN + --- single = 8 byte (4 byte unused)
// Endpoint 4 -- + OUT double = 8 byte
// Endpoint 5 IN + --- single = 8 byte (4 byte unused)
// Endpoint 6 -- + OUT double = 8 byte
// Total:                      56 byte
#define MAX_BTABLE_SIZE        56

// ----- Globals
extern USBD_HandleTypeDef    GLB_UsbDevice;
extern eUserFlags            GLB_UserFlags[CHANNEL_COUNT];
extern bool                  GLB_ProtoElmue;

// ----- Constants
// ATTENTION: Using the same endpoint number for IN and OUT (IN = 0x81 + OUT = 0x01) is not possible with double buffering.
uint8_t EndpointsIN [] = { 0x81, 0x83, 0x85 };
uint8_t EndpointsOUT[] = { 0x02, 0x04, 0x06 };

// ----- Variables
// reverse lookup table endpoint --> channel
uint8_t EpToChannel[16] = {0};
kDfuStatus DFU_Status   = {0};

uint8_t  USBD_GS_Init       (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
uint8_t  USBD_GS_DeInit     (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
uint8_t  USBD_GS_Setup      (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
uint8_t  USBD_GS_EP0_RxReady(USBD_HandleTypeDef *pdev);
uint8_t  USBD_GS_DataIn     (USBD_HandleTypeDef *pdev, uint8_t epnum);
uint8_t  USBD_GS_DataOut    (USBD_HandleTypeDef *pdev, uint8_t epnum);
// -------------
void     USBD_GS_Vendor_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
bool     USBD_GS_DFU_Request   (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
bool     USBD_GS_CustomRequest (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

// WinUSB class callbacks structure
// These functions are all called over usb_core and usb_lowlevel from PCD_EP_ISR_Handler() interrupts
USBD_ClassTypeDef USBD_ClassCallbacks =
{
    .Init              = USBD_GS_Init,
    .DeInit            = USBD_GS_DeInit,
    .Setup             = USBD_GS_Setup,
    .EP0_TxSent        = NULL,
    .EP0_RxReady       = USBD_GS_EP0_RxReady,
    .DataIn            = USBD_GS_DataIn,
    .DataOut           = USBD_GS_DataOut,
    .SOF               = NULL,
    .IsoINIncomplete   = NULL, // ISO endpoints not used
    .IsoOUTIncomplete  = NULL, // ISO endpoints not used
};

// Device descriptor Candlelight
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[] __ALIGN_END =
{
    USB_LEN_DEV_DESC,                  // bLength = 18 byte
    USB_DESC_TYPE_DEVICE,              // bDescriptorType = Device Descriptor
    0x00,                              // bcdUSB version
    0x02,                              // bcdUSB version  = 2.0
    0x00,                              // bDeviceClass    = Class info in interface descriptors
    0x00,                              // bDeviceSubClass
    0x00,                              // bDeviceProtocol
    USB_MAX_EP0_SIZE,                  // bMaxPacketSize  = 64 bytes
    LOBYTE(0x1D50),                    // idVendor  OpenMoko
    HIBYTE(0x1D50),                    // idVendor  OpenMoko
    LOBYTE(0x606F),                    // idProduct CANable Candlelight
    HIBYTE(0x606F),                    // idProduct CANable Candlelight
    LOBYTE(FIRMWARE_VERSION_BCD >> 8), // bcdDevice firmware version (month)
    HIBYTE(FIRMWARE_VERSION_BCD >> 8), // bcdDevice firmware version (year)
    USBD_IDX_MFC_STR,                  // Index of manufacturer  string
    USBD_IDX_PRODUCT_STR,              // Index of product string
    USBD_IDX_SERIAL_STR,               // Index of serial number string
    USBD_CONFIGURATIONS_COUNT          // bNumConfigurations
};

// Configuration Descriptor Candlelight
__ALIGN_BEGIN uint8_t USBD_ConfigDescFS[] __ALIGN_END =
{
    // ------ Configuration Descriptor ------
    // length = USB_LEN_CFG_DESC:
    USB_LEN_CFG_DESC,                 // bLength = 9 byte
    USB_DESC_TYPE_CONFIGURATION,      // bDescriptorType: Configuration
    USB_LEN_CONF_TOT_DESC,            // wTotalLength
    0x00,
    USBD_INTERFACES_COUNT,            // bNumInterfaces: 2, 3 or 4 interfaces
    0x01,                             // bConfigurationValue
    0x00,                             // iConfiguration (String not used)
    0x80 | USBD_SELF_POWERED,         // bmAttributes
    0x4B,                             // MaxPower 150 mA

    // ========================= CANDLELIGHT 1 ==========================

    // ------ Interface descriptor ------
    // length = USB_LEN_CANDLE_DESC:
    USB_LEN_IF_DESC,                  // bLength = 9 byte
    USB_DESC_TYPE_INTERFACE,          // bDescriptorType: Interface
    0,                                // bInterfaceNumber: 0
    0x00,                             // bAlternateSetting
    0x02,                             // bNumEndpoints
    0xFF,                             // bInterfaceClass:    Vendor Specific
    0xFF,                             // bInterfaceSubClass: Vendor Specific
    0xFF,                             // bInterfaceProtocol: Vendor Specific
    CANDLE_INTERFACE_STR_1_IDX,       // iInterface

    // ----- Endpoint IN descriptor ------
    USB_LEN_EP_DESC,                  // bLength = 7 byte
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType: Endpoint
    0x81,                             // bEndpointAddress
    0x02,                             // bmAttributes: bulk
    LOBYTE(EP_DATA_PACKET_SIZE),      // wMaxPacketSize
    HIBYTE(EP_DATA_PACKET_SIZE),
    0x00,                             // bInterval

    // ----- Endpoint OUT descriptor ------
    USB_LEN_EP_DESC,                  // bLength = 7 byte
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType: Endpoint
    0x02,                             // bEndpointAddress
    0x02,                             // bmAttributes: bulk
    LOBYTE(EP_DATA_PACKET_SIZE),      // wMaxPacketSize
    HIBYTE(EP_DATA_PACKET_SIZE),
    0x00,                             // bInterval

    // ======================== FIRMWARE UPDATE =========================

    // ------ DFU Interface descriptor ------
    // length = USB_LEN_FIRMWARE_DESC:
    USB_LEN_IF_DESC,                  // bLength = 9 byte
    USB_DESC_TYPE_INTERFACE,          // bDescriptorType: Interface
    DFU_INTERFACE_NUMBER,             // bInterfaceNumber: 1
    0x00,                             // bAlternateSetting
    0x00,                             // bNumEndpoints
    0xFE,                             // bInterfaceClass: Vendor Specific
    0x01,                             // bInterfaceSubClass
    0x01,                             // bInterfaceProtocol : Runtime mode
    DFU_INTERFACE_STR_IDX,            // iInterface

    // ------ DFU Functional descriptor ------
    USB_LEN_DFU_DESC,                 // bLength = 9 byte
    0x21,                             // bDescriptorType: DFU FUNCTIONAL
    0x0B,                             // bmAttributes: detach, upload, download
    0xFF, 0x00,                       // wDetachTimeOut
    0x00, 0x08,                       // wTransferSize
    0x1a, 0x01,                       // bcdDFUVersion: 1.1a

#if CANDLE_INRERFACE_COUNT > 1

    // ========================= CANDLELIGHT 2 ==========================

    // ------ Interface descriptor ------
    // length = USB_LEN_CANDLE_DESC:
    USB_LEN_IF_DESC,                  // bLength = 9 byte
    USB_DESC_TYPE_INTERFACE,          // bDescriptorType: Interface
    2,                                // bInterfaceNumber: 2
    0x00,                             // bAlternateSetting
    0x02,                             // bNumEndpoints
    0xFF,                             // bInterfaceClass:    Vendor Specific
    0xFF,                             // bInterfaceSubClass: Vendor Specific
    0xFF,                             // bInterfaceProtocol: Vendor Specific
    CANDLE_INTERFACE_STR_2_IDX,       // iInterface

    // ----- Endpoint IN descriptor ------
    USB_LEN_EP_DESC,                  // bLength = 7 byte
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType: Endpoint
    0x83,                             // bEndpointAddress
    0x02,                             // bmAttributes: bulk
    LOBYTE(EP_DATA_PACKET_SIZE),      // wMaxPacketSize
    HIBYTE(EP_DATA_PACKET_SIZE),
    0x00,                             // bInterval

    // ----- Endpoint OUT descriptor ------
    USB_LEN_EP_DESC,                  // bLength = 7 byte
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType: Endpoint
    0x04,                             // bEndpointAddress
    0x02,                             // bmAttributes: bulk
    LOBYTE(EP_DATA_PACKET_SIZE),      // wMaxPacketSize
    HIBYTE(EP_DATA_PACKET_SIZE),
    0x00,                             // bInterval

#endif

#if CANDLE_INRERFACE_COUNT > 2

    // ========================= CANDLELIGHT 3 ==========================

    // ------ Interface descriptor ------
    // length = USB_LEN_CANDLE_DESC:
    USB_LEN_IF_DESC,                  // bLength = 9 byte
    USB_DESC_TYPE_INTERFACE,          // bDescriptorType: Interface
    3,                                // bInterfaceNumber: 3
    0x00,                             // bAlternateSetting
    0x02,                             // bNumEndpoints
    0xFF,                             // bInterfaceClass:    Vendor Specific
    0xFF,                             // bInterfaceSubClass: Vendor Specific
    0xFF,                             // bInterfaceProtocol: Vendor Specific
    CANDLE_INTERFACE_STR_3_IDX,       // iInterface

    // ----- Endpoint IN descriptor ------
    USB_LEN_EP_DESC,                  // bLength = 7 byte
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType: Endpoint
    0x85,                             // bEndpointAddress
    0x02,                             // bmAttributes: bulk
    LOBYTE(EP_DATA_PACKET_SIZE),      // wMaxPacketSize
    HIBYTE(EP_DATA_PACKET_SIZE),
    0x00,                             // bInterval

    // ----- Endpoint OUT descriptor ------
    USB_LEN_EP_DESC,                  // bLength = 7 byte
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType: Endpoint
    0x06,                             // bEndpointAddress
    0x02,                             // bmAttributes: bulk
    LOBYTE(EP_DATA_PACKET_SIZE),      // wMaxPacketSize
    HIBYTE(EP_DATA_PACKET_SIZE),
    0x00,                             // bInterval

#endif
};

//  Microsoft Compatible ID Feature Descriptor
__ALIGN_BEGIN uint8_t USBD_MicrosoftFeatureDescr[] __ALIGN_END =
{
    // length = USB_LEN_FEATURE_DESC:
    LOBYTE(USB_LEN_MS_OS_TOT_DESC),   // length 0
    HIBYTE(USB_LEN_MS_OS_TOT_DESC),   // length 1
    0x00,                             // length 2
    0x00,                             // length 3
    0x00, 0x01,                       // version 1.0
    0x04, 0x00,                       // descr index (0x0004)
    USBD_INTERFACES_COUNT,            // number of sections (2, 3 or 4)
    0x00, 0x00, 0x00, 0x00,           // reserved
    0x00, 0x00, 0x00,
    // ------------------------
    // length = USB_LEN_WINUSB_DESC:
    0x00,                             // interface number: 0 = Candlelight 1
    0x01,                             // reserved - 1 byte
    0x57, 0x49, 0x4E, 0x55,           // compatible ID ("WINUSB\0\0") - 8 bytes
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // sub-compatible ID - 8 bytes
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // reserved - 6 bytes
    0x00, 0x00,
    // ------------------------
    // length = USB_LEN_WINUSB_DESC:
    DFU_INTERFACE_NUMBER,             // interface number: 1 = DFU
    0x01,                             // reserved - 1 byte
    0x57, 0x49, 0x4E, 0x55,           // compatible ID ("WINUSB\0\0") - 8 bytes
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // sub-compatible ID - 8 bytes
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // reserved - 6 bytes
    0x00, 0x00,
#if CANDLE_INRERFACE_COUNT > 1
    // length = USB_LEN_WINUSB_DESC:
    0x02,                             // interface number: 2 = Candlelight 2
    0x01,                             // reserved - 1 byte
    0x57, 0x49, 0x4E, 0x55,           // compatible ID ("WINUSB\0\0") - 8 bytes
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // sub-compatible ID - 8 bytes
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // reserved - 6 bytes
    0x00, 0x00,
#endif
#if CANDLE_INRERFACE_COUNT > 2
    // length = USB_LEN_WINUSB_DESC:
    0x03,                             // interface number: 3 = Candlelight 3
    0x01,                             // reserved - 1 byte
    0x57, 0x49, 0x4E, 0x55,           // compatible ID ("WINUSB\0\0") - 8 bytes
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // sub-compatible ID - 8 bytes
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,           // reserved - 6 bytes
    0x00, 0x00,
#endif
};

// Microsoft Extended Properties Descriptor
__ALIGN_BEGIN uint8_t USBD_MicrosoftExtPropertyDescr[] __ALIGN_END =
{
    0x92, 0x00, 0x00, 0x00, // length
    0x00, 0x01,             // version 1.0
    0x05, 0x00,             // descr index (0x0005)
    0x01, 0x00,             // number of sections
    0x88, 0x00, 0x00, 0x00, // property section size
    0x07, 0x00, 0x00, 0x00, // property data type 7: Unicode REG_MULTI_SZ
    0x2a, 0x00,             // property name length

    0x44, 0x00, 0x65, 0x00, // property name "DeviceInterfaceGUIDs"
    0x76, 0x00, 0x69, 0x00,
    0x63, 0x00, 0x65, 0x00,
    0x49, 0x00, 0x6e, 0x00,
    0x74, 0x00, 0x65, 0x00,
    0x72, 0x00, 0x66, 0x00,
    0x61, 0x00, 0x63, 0x00,
    0x65, 0x00, 0x47, 0x00,
    0x55, 0x00, 0x49, 0x00,
    0x44, 0x00, 0x73, 0x00,
    0x00, 0x00,

    0x50, 0x00, 0x00, 0x00, // property data length

    0x7b, 0x00, 0x63, 0x00, // property name: "{c15b4308-04d3-11e6-b3ea-6057189e6443}\0\0" == Unique Candlelight GUID
    0x31, 0x00, // <----- This '1' at offset 70 will be replaced with a '2' for the DFU interface
    0x35, 0x00,
    0x62, 0x00, 0x34, 0x00,
    0x33, 0x00, 0x30, 0x00,
    0x38, 0x00, 0x2d, 0x00,
    0x30, 0x00, 0x34, 0x00,
    0x64, 0x00, 0x33, 0x00,
    0x2d, 0x00, 0x31, 0x00,
    0x31, 0x00, 0x65, 0x00,
    0x36, 0x00, 0x2d, 0x00,
    0x62, 0x00, 0x33, 0x00,
    0x65, 0x00, 0x61, 0x00,
    0x2d, 0x00, 0x36, 0x00,
    0x30, 0x00, 0x35, 0x00,
    0x37, 0x00, 0x31, 0x00,
    0x38, 0x00, 0x39, 0x00,
    0x65, 0x00, 0x36, 0x00,
    0x34, 0x00, 0x34, 0x00,
    0x33, 0x00, 0x7d, 0x00,
    0x00, 0x00, 0x00, 0x00
};

// =========================================================================================================

// Configue Packet Memory Area (PMA) for all endpoints
// Called from USBD_LL_Init() during initialization
USBD_StatusTypeDef USBD_ConfigureEndpoints(USBD_HandleTypeDef *pdev)
{
    PCD_HandleTypeDef* hpcd = (PCD_HandleTypeDef*)pdev->pData;
    uint32_t addr = MAX_BTABLE_SIZE;

    if (!USBD_LL_ConfigurePMA(hpcd, 0x00, false, &addr, USB_MAX_EP0_SIZE) || // EP 0 OUT
        !USBD_LL_ConfigurePMA(hpcd, 0x80, false, &addr, USB_MAX_EP0_SIZE))   // EP 0 IN
        return USBD_FAIL; // PMA buffer overflow

    for (uint8_t C=0; C<CANDLE_INRERFACE_COUNT; C++)
    {
        if (!USBD_LL_ConfigurePMA(hpcd, EndpointsIN [C], false, &addr, EP_DATA_PACKET_SIZE) || // EP 1,3,5 IN
            !USBD_LL_ConfigurePMA(hpcd, EndpointsOUT[C], true,  &addr, EP_DATA_PACKET_SIZE))   // EP 2,4,6 OUT, double buffered
            return USBD_FAIL; // PMA buffer overflow
    }
    return USBD_OK;
}

// =========================================================================================================

// interrupt callback
uint8_t USBD_GS_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    pdev->pClassData = &GLB_UsbDevice; // dummy, not used here, but required for usb_core.c

    // Initialize the default response to DFU_RequGetStatus request
    DFU_Status.Status    = DfuStatus_OK;     // no error
    DFU_Status.State     = DfuState_AppIdle; // in application mode and idle
    DFU_Status.StringIdx = 0xFF;             // no string descriptor available

    for (uint8_t C=0; C<CANDLE_INRERFACE_COUNT; C++)
    {
        buf_class* inst = buf_get_instance(C);
        inst->TxBusy = false;

        // fill reverse lookup table: endpoint --> channel
        EpToChannel[EndpointsIN [C] & 0xF] = C; // 0x81 --> 0, 0x83 --> 1, 0x85 --> 2
        EpToChannel[EndpointsOUT[C] & 0xF] = C; // 0x02 --> 0, 0x04 --> 1, 0x06 --> 2

        USBD_LL_OpenEP(pdev, EndpointsIN [C], USBD_EP_TYPE_BULK, EP_DATA_PACKET_SIZE);
        USBD_LL_OpenEP(pdev, EndpointsOUT[C], USBD_EP_TYPE_BULK, EP_DATA_PACKET_SIZE);

        // pass the buffer from_host_buf to the HAL to store USB OUT data
        USBD_StatusTypeDef status = USBD_LL_PrepareReceive(pdev, EndpointsOUT[C], inst->from_host_buf, sizeof(inst->from_host_buf));
        if (status != USBD_OK)
            return status;
    }
    return USBD_OK;
}

// interrupt callback
uint8_t USBD_GS_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    for (uint8_t C=0; C<CANDLE_INRERFACE_COUNT; C++)
    {
        USBD_LL_CloseEP(pdev, EndpointsIN [C]);
        USBD_LL_CloseEP(pdev, EndpointsOUT[C]);
    }
    return USBD_OK;
}

// interrupt callback
// A SETUP request has been received
// This callback is called from USBD_StdDevReq() in usb_ctrlreq.c and the return value is ignored.
uint8_t USBD_GS_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    // ATTENTION: USBD_CtlSendData() does not work with a local buffer on the stack --> define as static!
    static uint8_t ifalt = 0;

    switch (req->bRequestType & USB_REQ_TYPE_MASK)
    {
        case USB_REQ_TYPE_CLASS:
        case USB_REQ_TYPE_VENDOR:
            USBD_GS_Vendor_Request(pdev, req);
            break;

        case USB_REQ_TYPE_STANDARD:
            switch (req->bRequest)
            {
                case USB_REQ_GET_INTERFACE:
                    USBD_CtlSendData(pdev, &ifalt, 1);
                    break;
            }
            break;
    }
    return USBD_OK; // ignored
}

// called from inside an interrupt callback
// First stage of vendor SETUP requests
// See "USB Tutorial.chm" in subfolder "Documentation"
void USBD_GS_Vendor_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    // wIndex = interface number
    if (req->wIndex == DFU_INTERFACE_NUMBER)
    {
        if (USBD_GS_DFU_Request(pdev, req))
            return; // success
    }
    else
    {
        if (control_setup_request(pdev, req))
            return; // success
    }
    USBD_CtlError(pdev, 0); // stall endpoint 0
}

// interrupt callback
// Second stage of SETUP requests with OUT data
// This callback is called from USBD_LL_DataOutStage() in usb_core.c and the return value is ignored.
// IMPORTANT: Read comment of control_setup_OUT_data() !!!
uint8_t USBD_GS_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
    control_setup_OUT_data(pdev);
    return USBD_OK; // ignored
}

// called from inside an interrupt callback
// request has destination to interface 1 (firmware update)
bool USBD_GS_DFU_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    if ((req->bRequestType & USB_REQ_RECIPIENT_MASK) != USB_REQ_RECIPIENT_INTERFACE ||
        (req->bRequestType & USB_REQ_TYPE_MASK)      != USB_REQ_TYPE_CLASS)
        return false; // stall endpoint 0

    switch (req->bRequest)
    {
        case DFU_RequDetach:
            // Enter DFU mode with a delay of 300 ms
            // If the pin BOOT0 was disabled the user must reconnect the USB cable to generate a hardware reset.
            // Inform the firmware updater that the device cannot enter DFU mode by returning state DfuSte_AppDetach
            if (dfu_switch_to_bootloader() == FBK_ResetRequired)
                DFU_Status.State = DfuState_AppDetach; // hardware reset required
            return true;

        case DFU_RequGetStatus:
            USBD_CtlSendData(pdev, (uint8_t*)&DFU_Status, sizeof(DFU_Status));
            return true;

        default:
            return false; // stall endpoint 0
    }
}

// interrupt callback
// host data has arrived on the USB OUT endpoint (0x02, 0x04, 0x06)
uint8_t USBD_GS_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    int channel = EpToChannel[epnum & 0xF]; // epnum = 0x02 --> channel 0, 0x04 --> 1, 0x06 --> 2
    buf_class* usb_buf = buf_get_instance(channel);
    buf_class* can_buf = usb_buf;

    // Although the multi channel firmware creates one USB interface for each CAN channel,
    // the legacy protocol routes all traffic of all CAN channels through the first USB interface (EP 81 / 02) for backward compatibility.
    // kHostFrameLegacy.channel tells the host which channel is the origin/destination of the packet.
    if (!GLB_ProtoElmue) // legacy protocol
    {
        // get the destination CAN channel from the legacy packet
        kHostFrameLegacy* pk_Legacy = (kHostFrameLegacy*)usb_buf->from_host_buf;
        channel = pk_Legacy->channel;
        
        // if the host has sent an invalid channel --> store the frame in channel 0.
        // buf_process_can() will set do_not_send == true, the packet is not sent and error_assert(APP_CanTxFail)
        if (channel >= CHANNEL_COUNT) 
            channel = 0;
        
        can_buf = buf_get_instance(channel);
    }

    kHostFrameObject* obj_to_can = buf_get_frame_locked(&can_buf->list_can_pool);
    if (obj_to_can)
    {
        memcpy(&obj_to_can->frame, usb_buf->from_host_buf, sizeof(usb_buf->from_host_buf));
        list_add_tail_locked(&obj_to_can->list, &can_buf->list_to_can);
    }
    else // CAN buffer overflow
    {
        // in case of buffer overflow inform the host immediately, so the host stops sending more packets and displays an error to the user.
        error_assert(channel, APP_CanTxOverflow, true); // Both LED's = ON --> indicate severe error
    }

    // pass the buffer from_host_buf to the HAL for the next frame to receive
    USBD_LL_PrepareReceive(pdev, epnum, usb_buf->from_host_buf, sizeof(usb_buf->from_host_buf));
    return USBD_OK; // ignored
}

// interrupt callback
// get a Unicode string for the given string index that comes from the descriptors
uint8_t* USBD_GetUserStringDescr(USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length)
{
    switch (index)
    {
        case CANDLE_INTERFACE_STR_1_IDX:
        case CANDLE_INTERFACE_STR_2_IDX:
        case CANDLE_INTERFACE_STR_3_IDX:
        {
            // The interface name is important: Windows displays it while the driver is installed.
            // Do not display a stupid abbreviation like "gs_usb" that an ordinary computer user will not understand.
#if CANDLE_INRERFACE_COUNT > 1
            uint8_t* unicode = USBD_GetStringDescr("CAN FD Interface x", length);
            unicode[36] = ('1' - CANDLE_INTERFACE_STR_1_IDX) + index; // replace the 'x' with '1', '2', '3'
            return unicode;
#else
            return USBD_GetStringDescr("CAN FD Interface", length);
#endif
        }
        // This name is important: Windows displays it while the driver is installed.
        case DFU_INTERFACE_STR_IDX:
            return USBD_GetStringDescr("Firmware Update Interface", length);

        case 0xEE: // Microsoft OS String Descriptor Request --> "MSFT100" + Vendor Code
        {
            uint8_t* unicode = USBD_GetStringDescr("MSFT100x", length);
            unicode[16] = USBD_MS_OS_VENDOR_CODE; // replace the 'x' with the vendor code
            return unicode;
        }
        default:
            return NULL;
    }
}

// =========================================================================================================

// Called from interrupt handler PCD_EP_ISR_Handler --> HAL_PCD_SetupStageCallback
// return true if request was handled
bool USBD_SetupStageRequest(PCD_HandleTypeDef *hpcd)
{
    USBD_HandleTypeDef *pdev = (USBD_HandleTypeDef*)hpcd->pData;
    USBD_ParseSetupRequest((USBD_SetupReqTypedef*)&pdev->request, (uint8_t*)hpcd->Setup);

    switch (pdev->request.bRequestType & USB_REQ_RECIPIENT_MASK)
    {
        case USB_REQ_RECIPIENT_DEVICE:    // device request
        case USB_REQ_RECIPIENT_INTERFACE: // interface request
            return USBD_GS_CustomRequest(pdev, &pdev->request);
        default:
            return false;
    }
}

// Called from interrupt handler
// Handle Microsoft OS SETUP requests (required for automatic driver installation on Windows)
// Windows sends an interface request, but for testing with WinUSB it is required that also a device request is answered the same way.
// For details read: https://netcult.ch/elmue/CANable Firmware Update
// return true if request was handled
bool USBD_GS_CustomRequest(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    if (req->bRequest != USBD_MS_OS_VENDOR_CODE || req->wValue >= USBD_INTERFACES_COUNT)
        return false;

    switch (req->wIndex) // wIndex = requested descriptor type
    {
        case 4: // Microsoft OS Feature Request
            USBD_CtlSendData(pdev, USBD_MicrosoftFeatureDescr, MIN(sizeof(USBD_MicrosoftFeatureDescr), req->wLength));
            return true;

        case 5: // Microsoft OS Extended Properties Request
        {
            // IMPORTANT: In the legacy firmware it was wrong to return only a GUID for interface 0.
            // If no GUID is returned for the DFU interface, Windows will not install the WinUSB driver
            // and the Firmware Updater cannot switch the CANable into DFU mode!
            // for Candlelight     interface return GUID "{c15b4308-04d3-11e6-b3ea-6057189e6443}"
            // for Firmware Update interface return GUID "{c25b4308-04d3-11e6-b3ea-6057189e6443}"

            // ATTENTION: USBD_CtlSendData() does not work with a local buffer on the stack --> get 512 byte string buffer at fix address
            uint8_t* buf = USBD_GetStringDescr(NULL, NULL);
            memcpy(buf, USBD_MicrosoftExtPropertyDescr, sizeof(USBD_MicrosoftExtPropertyDescr));
            if (req->wValue == DFU_INTERFACE_NUMBER)
                buf[70] = '2';

            USBD_CtlSendData(pdev, buf, MIN(sizeof(USBD_MicrosoftExtPropertyDescr), req->wLength));
            return true;
        }
    }
    return false;
}

// ==================================== IN Transfer ==========================================

// This function is called from the main loop only after usb_buf->TxBusy == false.
// Send a frame to the host on IN endpoint, either kHostFrameLegacy or kHeader
void USBD_SendFrameToHost(int channel, void* frame)
{
    // Legacy protocol routes all CAN channels through interface 0
    if (!GLB_ProtoElmue)
        channel = 0;
    
    buf_class* usb_buf = buf_get_instance(channel);
    uint16_t len;
    if (GLB_ProtoElmue) // new ElmüSoft protocol
    {
        // Using the optimized new ElmüSoft protocol reduces unnecessary USB overhead as it was sent by the legacy firmware.
        // If a CAN frame has only 2 data bytes, send only 2 data bytes over USB.
        // All ElmüSoft messages use the same header, no matter if CAN packet or an ASCII message.
        len = ((kHeader*)frame)->size;
    }
    else // legacy Geschwister Schneider protocol
    {
        kHostFrameLegacy* pk_Legacy = (kHostFrameLegacy*)frame;
        
        // The legacy protocol is not intelligently designed. The timestamp is behind a fix 64 byte data array.
        // For CAN FD it sends ALWAYS 76 or 80 bytes over USB no matter how many bytes the frame really has.
        len = sizeof(kHostFrameLegacy); // 80 bytes
        if ((pk_Legacy->flags & FRM_FDF) == 0) len -= 56;
        if ((GLB_UserFlags[pk_Legacy->channel] & USR_Timestamp) == 0) len -= 4;
    }

    usb_buf->TxBusy  = true;
    usb_buf->SendZLP = len > 0 && (len % EP_DATA_PACKET_SIZE) == 0;

    // IMPORTANT:
    // USBD_LL_Transmit does not copy the frame data to another buffer.
    // The HAL needs a pointer to a buffer that stays unchanged until all data has been sent.
    // If the data exceeds the USB endpoint maximum packet size (64 byte), it will be sent in multiple USB packets.
    memcpy(usb_buf->to_host_buf, frame, len);

    // always returns HAL_OK
    USBD_LL_Transmit(&GLB_UsbDevice, EndpointsIN[channel], usb_buf->to_host_buf, len);
}

// interrupt callback
// The data from USBD_SendFrameToHost() has been sent to the host on IN endpoint (0x81, 0x83, 0x85)
uint8_t USBD_GS_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
    int channel = EpToChannel[epnum & 0xF]; // epnum = 0x81 --> channel 0, 0x83 --> 1, 0x85 --> 2
    buf_class* usb_buf = buf_get_instance(channel);

    // This important code was missing in the legacy firmware. (fixed by ElmSüoft)
    // After sending exactly 64 bytes a zero length packet (ZLP) must follow.
    // Read "Excellent USB Tutorial.chm" in subfolder "Documentation"
    if (usb_buf->SendZLP)
    {
        usb_buf->SendZLP = false;

        // Send ZLP
        // A ZLP is a USB packet that contains no data payload. It's length is zero.
        // ZLP's are important to signal the end of a data transfer when the last packet sent
        // is exactly the maximum packet size (EP_DATA_PACKET_SIZE).
        // Otherwise, if the last packet in a transfer is exactly wMaxPacketSize, the host could not know if more data will follow.
        // If the ZLP is missing the host will expect another packet to come. To test this send a debug message with 62 characters.
        USBD_LL_Transmit(&GLB_UsbDevice, epnum, NULL, 0);
    }
    else
    {
        usb_buf->TxBusy = false;
    }
    return USBD_OK;
}


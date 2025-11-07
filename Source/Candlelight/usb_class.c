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

#define GSUSB_ENDPOINT_IN           0x81
#define GSUSB_ENDPOINT_OUT          0x02
#define CAN_DATA_MAX_PACKET_SIZE    64   // endpoints 81 + 02
#define CANDLE_INTERFACE_NUMBER     0
#define CANDLE_INTERFACE_STR_INDEX  20
#define DFU_INTERFACE_NUMBER        1
#define DFU_INTERFACE_STR_INDEX     0xE0
#define USB_CAN_CONFIG_DESC_SIZE    50
#define USBD_MS_OS_VENDOR_CODE      0x20

extern USB_BufHandleTypeDef  USB_BufHandle;
extern USBD_HandleTypeDef    USB_Device;
extern uint8_t               USBD_StrDesc[USBD_MAX_STR_DESC_SIZE];
extern eUserFlags            USER_Flags;

kDfuStatus DFU_Status = {0};

static uint8_t  USBD_GS_Init(USBD_HandleTypeDef *pdev,  uint8_t cfgidx);
static uint8_t  USBD_GS_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_GS_Setup(USBD_HandleTypeDef *pdev,  USBD_SetupReqTypedef *req);
static uint8_t  USBD_GS_DataIn(USBD_HandleTypeDef *pdev,  uint8_t epnum);
static uint8_t  USBD_GS_DataOut(USBD_HandleTypeDef *pdev,  uint8_t epnum);
static uint8_t  USBD_GS_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t* USBD_GS_GetFSConfigDesc(uint16_t *length);
static uint8_t* USBD_GS_GetUserStringDescr(USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length);
static void     USBD_GS_Vendor_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static bool     USBD_GS_DFU_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static bool     USBD_GS_CustomRequest(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
/*
// not used for Full speed USB device
static uint8_t  *USBD_GS_GetHSCfgDesc(uint16_t *length);
static uint8_t  *USBD_GS_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t  *USBD_GS_GetDeviceQualifierDescriptor(uint16_t *length);
*/

// WinUSB class callbacks structure
// These functions are all called over usb_core and usb_lowlevel from PCD_EP_ISR_Handler() interrupts
USBD_ClassTypeDef USBD_ClassCallbacks = 
{
    .Init                          = USBD_GS_Init,
    .DeInit                        = USBD_GS_DeInit,
    .Setup                         = USBD_GS_Setup,
    .EP0_RxReady                   = USBD_GS_EP0_RxReady,
    .DataIn                        = USBD_GS_DataIn,
    .DataOut                       = USBD_GS_DataOut,
    .SOF                           = NULL,
    .GetHSConfigDescriptor         = NULL, // not used for FULL speed USB devices
    .GetFSConfigDescriptor         = USBD_GS_GetFSConfigDesc,
    .GetOtherSpeedConfigDescriptor = NULL, // not used for FULL speed USB devices
    .GetDeviceQualifierDescriptor  = NULL, // not used for FULL speed USB devices
    .GetUsrStrDescriptor           = USBD_GS_GetUserStringDescr,
};

// Device descriptor Candlelight
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END =
{
    0x12,                              // bLength 
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
    LOBYTE(FIRMWARE_VERSION_BCD >> 8), // bcdDevice firmware version   see settings.h
    HIBYTE(FIRMWARE_VERSION_BCD >> 8), // bcdDevice firmware version   see settings.h
    USBD_IDX_MFC_STR,                  // Index of manufacturer  string
    USBD_IDX_PRODUCT_STR,              // Index of product string
    USBD_IDX_SERIAL_STR,               // Index of serial number string
    USBD_MAX_NUM_CONFIGURATION         // bNumConfigurations
};

// Configuration Descriptor
__ALIGN_BEGIN uint8_t USBD_ConfigDescr[USB_CAN_CONFIG_DESC_SIZE] __ALIGN_END =
{
    // Configuration Descriptor 
    0x09,                             // bLength 
    USB_DESC_TYPE_CONFIGURATION,      // bDescriptorType 
    USB_CAN_CONFIG_DESC_SIZE,         // wTotalLength 
    0x00,
    0x02,                             // bNumInterfaces 
    0x01,                             // bConfigurationValue 
    USBD_IDX_CONFIG_STR,              // iConfiguration 
    0x80,                             // bmAttributes: bus powered 
    0x4B,                             // MaxPower 150 mA 
    //-----------------------------
    // GS_USB Interface Descriptor 
    0x09,                             // bLength 
    USB_DESC_TYPE_INTERFACE,          // bDescriptorType 
    CANDLE_INTERFACE_NUMBER,          // bInterfaceNumber 
    0x00,                             // bAlternateSetting 
    0x02,                             // bNumEndpoints 
    0xFF,                             // bInterfaceClass:    Vendor Specific
    0xFF,                             // bInterfaceSubClass: Vendor Specific 
    0xFF,                             // bInterfaceProtocol: Vendor Specific 
    CANDLE_INTERFACE_STR_INDEX,       // iInterface 
    //-----------------------------
    // EP1 descriptor 
    0x07,                             // bLength 
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType 
    GSUSB_ENDPOINT_IN,                // bEndpointAddress  0x81
    0x02,                             // bmAttributes: bulk 
    LOBYTE(CAN_DATA_MAX_PACKET_SIZE), // wMaxPacketSize 
    HIBYTE(CAN_DATA_MAX_PACKET_SIZE),
    0x00,                             // bInterval: 
    //-----------------------------
    // EP2 descriptor 
    0x07,                             // bLength 
    USB_DESC_TYPE_ENDPOINT,           // bDescriptorType 
    GSUSB_ENDPOINT_OUT,               // bEndpointAddress  0x02
    0x02,                             // bmAttributes: bulk 
    LOBYTE(CAN_DATA_MAX_PACKET_SIZE), // wMaxPacketSize 
    HIBYTE(CAN_DATA_MAX_PACKET_SIZE),
    0x00,                             // bInterval: 
    //--------------------------
    // DFU Interface Descriptor 
    0x09,                             // bLength 
    USB_DESC_TYPE_INTERFACE,          // bDescriptorType 
    DFU_INTERFACE_NUMBER,             // bInterfaceNumber 
    0x00,                             // bAlternateSetting 
    0x00,                             // bNumEndpoints 
    0xFE,                             // bInterfaceClass: Vendor Specific
    0x01,                             // bInterfaceSubClass 
    0x01,                             // bInterfaceProtocol : Runtime mode 
    DFU_INTERFACE_STR_INDEX,          // iInterface 
    //---------------------------
    // DFU Functional Descriptor 
    0x09,                    // bLength 
    0x21,                    // bDescriptorType: DFU FUNCTIONAL 
    0x0B,                    // bmAttributes: detach, upload, download 
    0xFF, 0x00,              // wDetachTimeOut 
    0x00, 0x08,              // wTransferSize 
    0x1a, 0x01,              // bcdDFUVersion: 1.1a 
};

//  Microsoft Compatible ID Feature Descriptor 
__ALIGN_BEGIN uint8_t USBD_MicrosoftFeatureDescr[] __ALIGN_END = 
{
    0x40, 0x00, 0x00, 0x00, // length 
    0x00, 0x01,             // version 1.0 
    0x04, 0x00,             // descr index (0x0004) 
    0x02,                   // number of sections 
    0x00, 0x00, 0x00, 0x00, // reserved 
    0x00, 0x00, 0x00,
    0x00,                   // interface number Canlelight 
    0x01,                   // reserved - 1 byte 
    0x57, 0x49, 0x4E, 0x55, // compatible ID ("WINUSB\0\0") - 8 bytes 
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, // sub-compatible ID - 8 bytes 
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, // reserved - 6 bytes 
    0x00, 0x00,
    0x01,                   // interface number Firmware Update 
    0x01,                   // reserved - 1 byte 
    0x57, 0x49, 0x4E, 0x55, // compatible ID ("WINUSB\0\0") - 8 bytes 
    0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, // sub-compatible ID - 8 bytes 
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, // reserved - 6 bytes  
    0x00, 0x00
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

// Called from USBD_LL_Init() during initialization
void USBD_ConfigureEndpoints(USBD_HandleTypeDef *pdev)
{
    // Configue Packet Memory Area (PMA) for all endpoints
    HAL_PCDEx_PMAConfig((PCD_HandleTypeDef*)pdev->pData, 0x00, PCD_SNG_BUF, 0x18);       // EP 0 OUT (max packet size = 64 byte)
    HAL_PCDEx_PMAConfig((PCD_HandleTypeDef*)pdev->pData, 0x80, PCD_SNG_BUF, 0x58);       // EP 0 IN  (max packet size = 64 byte)
    HAL_PCDEx_PMAConfig((PCD_HandleTypeDef*)pdev->pData, 0x81, PCD_SNG_BUF, 0xd8);       // EP 1 IN  (max packet size = 64 byte)
    HAL_PCDEx_PMAConfig((PCD_HandleTypeDef*)pdev->pData, 0x02, PCD_DBL_BUF, 0x015801d8); // EP 2 OUT (max packet size = 64 byte, double buffer addr 158 + 1D8)
}

// =========================================================================================================

// interrupt callback
static uint8_t USBD_GS_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    pdev->pClassData = &USB_BufHandle;

    USBD_LL_OpenEP(pdev, GSUSB_ENDPOINT_IN,  USBD_EP_TYPE_BULK, CAN_DATA_MAX_PACKET_SIZE);
    USBD_LL_OpenEP(pdev, GSUSB_ENDPOINT_OUT, USBD_EP_TYPE_BULK, CAN_DATA_MAX_PACKET_SIZE);
    
    USB_BufHandle.TxBusy = false;    

    // Initialize the default response to DFU_RequGetStatus request
    DFU_Status.Status    = DfuStatus_OK;     // no error
    DFU_Status.State     = DfuState_AppIdle; // in application mode and idle
    DFU_Status.StringIdx = 0xFF;             // no string descriptor available
    
    return USBD_LL_PrepareReceive(pdev, GSUSB_ENDPOINT_OUT, USB_BufHandle.from_host_buf, sizeof(USB_BufHandle.from_host_buf));
}

// interrupt callback
static uint8_t USBD_GS_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
    USBD_LL_CloseEP(pdev, GSUSB_ENDPOINT_IN);
    USBD_LL_CloseEP(pdev, GSUSB_ENDPOINT_OUT);
    return USBD_OK;
}

// interrupt callback
// A SETUP request has been received
// This callback is called from USBD_StdDevReq() in usb_ctrlreq.c and the return value is ignored.
static uint8_t USBD_GS_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    static uint8_t ifalt = 0;

    switch (req->bmRequest & USB_REQ_TYPE_MASK) 
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
static void USBD_GS_Vendor_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    if ((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_INTERFACE &&
        (req->bmRequest & USB_REQ_TYPE_MASK)      == USB_REQ_TYPE_CLASS &&
         req->wIndex                              == DFU_INTERFACE_NUMBER)
    {
        if (USBD_GS_DFU_Request(pdev, req))
            return; // success
    }
    
    if (req->wIndex == CANDLE_INTERFACE_NUMBER)
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
static uint8_t USBD_GS_EP0_RxReady(USBD_HandleTypeDef *pdev) 
{
    control_setup_OUT_data(pdev);
    return USBD_OK; // ignored
}

// called from inside an interrupt callback
// Request has destination to interface 1 (firmware update)
static bool USBD_GS_DFU_Request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
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
            return false;
    }
}

// interrupt callback
// Data has arrived on the USB OUT endpoint 02
static uint8_t USBD_GS_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum) 
{
    USB_BufHandleTypeDef *hcan = (USB_BufHandleTypeDef*)pdev->pClassData;

    kHostFrameObject* pool_frame = buf_get_frame_locked(&hcan->list_can_pool);
    if (pool_frame)
    {
        memcpy(&pool_frame->frame, hcan->from_host_buf, sizeof(hcan->from_host_buf));
        list_add_tail_locked(&pool_frame->list, &hcan->list_to_can);
    }
    else // CAN buffer overflow
    {
        // in case of buffer overflow inform the host immediately, so the host stops sending more packets and displays an error to the user.
        error_assert(APP_CanTxOverflow, true);
    }

    // pass the buffer from_host_buf to the HAL for the next frame to receive
    USBD_LL_PrepareReceive(pdev, GSUSB_ENDPOINT_OUT, hcan->from_host_buf, sizeof(hcan->from_host_buf));
    return USBD_OK; // ignored
}

// interrupt callback
static uint8_t *USBD_GS_GetFSConfigDesc(uint16_t *length)
{
    *length = sizeof(USBD_ConfigDescr);
    return USBD_ConfigDescr;
}

// interrupt callback
// get a Unicode string for the given string index that comes from the descriptors
uint8_t* USBD_GS_GetUserStringDescr(USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length)
{
    switch (index) 
    {
        // This name is important: Windows displays it while the driver is installed.
        // Do not display a stupid name like "gs_usb" that an ordinary person will not understand.
        case CANDLE_INTERFACE_STR_INDEX:
            USBD_GetString((uint8_t*)"CAN FD Interface", USBD_StrDesc, length);
            return USBD_StrDesc;
        
        // This name is important: Windows displays it while the driver is installed.
        case DFU_INTERFACE_STR_INDEX:
            USBD_GetString((uint8_t*)"Firmware Update Interface", USBD_StrDesc, length);
            return USBD_StrDesc;
            
        case 0xEE: // Microsoft OS String Descriptor Request --> "MSFT100" + Vendor Code
            USBD_GetString((uint8_t*)"MSFT100x", USBD_StrDesc, length);
            USBD_StrDesc[16] = USBD_MS_OS_VENDOR_CODE; // replace the 'x' with the vendor code
            return USBD_StrDesc;
            
        default:
            *length = 0;
            USBD_CtlError(pdev, 0); // stall endpoint 0
            return 0;
    }
}

// =========================================================================================================

// Called from interrupt handler PCD_EP_ISR_Handler --> HAL_PCD_SetupStageCallback
// return true if request was handled
bool USBD_SetupStageRequest(PCD_HandleTypeDef *hpcd)
{
    USBD_HandleTypeDef *pdev = (USBD_HandleTypeDef*)hpcd->pData;
    USBD_ParseSetupRequest((USBD_SetupReqTypedef*)&pdev->request, (uint8_t*)hpcd->Setup);

    switch (pdev->request.bmRequest & USB_REQ_RECIPIENT_MASK)
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
    if (req->bRequest != USBD_MS_OS_VENDOR_CODE || req->wValue > DFU_INTERFACE_NUMBER) 
        return false;
    
    switch (req->wIndex) // wIndex = requested descriptor type
    {
        case 4: // Microsoft OS Feature Request
            USBD_CtlSendData(pdev, USBD_MicrosoftFeatureDescr, MIN(sizeof(USBD_MicrosoftFeatureDescr), req->wLength));
            return true;

        case 5: // Microsoft OS Extended Properties Request
            // IMPORTANT: In the legacy firmware it is wrong to return only a GUID for interface 0.
            // If no GUID is returned for the DFU interface, Windows will not install the WinUSB driver
            // and the Firmware Updater cannot switch the CANable into DFU mode!
            // for interface 0 (Candlelight)     return GUID "{c15b4308-04d3-11e6-b3ea-6057189e6443}"
            // for interface 1 (Firmware Update) return GUID "{c25b4308-04d3-11e6-b3ea-6057189e6443}"
            memcpy(USBD_StrDesc, USBD_MicrosoftExtPropertyDescr, sizeof(USBD_MicrosoftExtPropertyDescr));            
            if (req->wValue == DFU_INTERFACE_NUMBER) // wValue = 0 --> Candlelight interface, 1 --> DFU interface
                USBD_StrDesc[70] = '2';
            
            USBD_CtlSendData(pdev, USBD_StrDesc, MIN(sizeof(USBD_MicrosoftExtPropertyDescr), req->wLength));
            return true;
    }
    return false;
}

// ==================================== IN Transfer ==========================================

// This function is called from the main loop only after USBD_IsTxBusy() has returned false.
// Send a frame to the host on IN endpoint 81, either kHostFrameLegacy or kHeader
void USBD_SendFrameToHost(void *frame)
{   
    uint16_t len;
    if (USER_Flags & USR_ProtoElmue) // new ElmüSoft protocol
    {
        // Using the optimized new ElmüSoft protocol reduces unnecessary USB overhead as it was sent by the legacy firmware.
        // If a CAN frame has only 2 data bytes, send only 2 data bytes over USB.
        // All ElmüSoft messages use the same header, no matter if CAN packet or an ASCII message.
        len = ((kHeader*)frame)->size;
    }
    else // legacy Geschwister Schneider protocol
    {
        // The legacy protocol is not intelligently designed. The timestamp is behind a fix 64 byte data array.
        // For CAN FD it sends ALWAYS 76 or 80 bytes over USB no matter how many bytes the frame really has.
        len = sizeof(kHostFrameLegacy); // 80 bytes
        if ((((kHostFrameLegacy*)frame)->flags & FRM_FDF) == 0) len -= 56;
        if ((USER_Flags & USR_Timestamp) == 0) len -= 4;
    }
 
    USB_BufHandleTypeDef *hcan = (USB_BufHandleTypeDef*)USB_Device.pClassData; 
    hcan->TxBusy  = true;   
    hcan->SendZLP = len > 0 && (len % CAN_DATA_MAX_PACKET_SIZE) == 0;
   
    // IMPORTANT:
    // USBD_LL_Transmit does not copy the frame data to another buffer.
    // The HAL needs a pointer to a buffer that stays unchanged until all data has been sent.
    // If the data exceeds the USB endpoint maximum packet size (64 byte), it will be sent in multiple USB packets.
    memcpy(hcan->to_host_buf, frame, len);  
      
    // always returns HAL_OK
    USBD_LL_Transmit(&USB_Device, GSUSB_ENDPOINT_IN, hcan->to_host_buf, len);
}

// interrupt callback
// The data from USBD_SendFrameToHost() has been sent to the host on IN endpoint 0x81 (epnum == 0x01)
static uint8_t USBD_GS_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum) 
{
    USB_BufHandleTypeDef *hcan = (USB_BufHandleTypeDef*)pdev->pClassData;
    
    // This important code was missing in the legacy firmware. (fixed by ElmSüoft)
    // After sending exactly 64 bytes a zero length packet (ZLP) must follow.
    // Read "Excellent USB Tutorial.chm" in subfolder "Documentation"
    if (hcan->SendZLP)
    {   
        hcan->SendZLP = false;

        // Send ZLP
        // A ZLP is a USB packet that contains no data payload. It's length is zero.
        // ZLP's are important to signal the end of a data transfer when the last packet sent 
        // is exactly the maximum packet size (CAN_DATA_MAX_PACKET_SIZE).
        // Otherwise, if the last packet in a transfer is exactly wMaxPacketSize, the host cannot know if more data is coming.
        // If the ZLP is missing the host will expect another packet to come. To test this send a debug message with 62 characters.
        USBD_LL_Transmit(&USB_Device, GSUSB_ENDPOINT_IN, NULL, 0);
    }
    else
    {
        hcan->TxBusy = false;
    }
    return USBD_OK;
}

// returns true if a transfer to the host on the IN endpoint 81 is still in progress
bool USBD_IsTxBusy()
{
    USB_BufHandleTypeDef *hcan = (USB_BufHandleTypeDef*)USB_Device.pClassData;
    return hcan->TxBusy;
}


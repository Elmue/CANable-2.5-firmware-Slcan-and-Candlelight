/*
    The MIT License
    Implemenatation of USB GS Class (Geschwister Schneider)
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "can.h"
#include "utils.h"
#include "error.h"
#include "led.h"
#include "dfu.h"
#include "control.h"
#include "usb_ioreq.h"

extern USB_BufHandleTypeDef  USB_BufHandle;
extern eUserFlags            USER_Flags;
// legacy Geschwister Schneider protocol
kCapabilityClassic           GS_CapabilityClassic;
kCapabilityFD                GS_CapabilityFD;
kDeviceVersion               GS_DeviceVersion = {0};
// new ELmüSoft protocol
kBoardInfo                   ELM_BoardInfo    = {0};
eFeedback                    ELM_LastError    = FBK_Success;

void control_init()
{
    // all the other flags must be enabled by the user
    USER_Flags = USR_CandleDefault;

    GS_DeviceVersion.sw_version_bcd = FIRMWARE_VERSION_BCD; // BCD version 0x250814 --> display as "25.08.14" (14th august 2025)
    GS_DeviceVersion.hw_version_bcd = 0x200;                // BCD version 0x200    --> display as  "2.00" (hardware = CANable 2.0)

    // ------------------------------------------------

    GS_CapabilityClassic.feature = GS_DevFlagListenOnly     |
                                   GS_DevFlagLoopback       |
                                   GS_DevFlagOneShot        |
                                   GS_DevFlagTimestamp      |
                                   GS_DevFlagIdentify       |
                                   GS_DevFlagCAN_FD         |
                                   GS_DevFlagBitTimingFD    |
                                   ELM_DevFlagProtocolElmue |
                                   ELM_DevFlagDisableTxEcho;
    if (TERMINATOR_Pin > 0)
        GS_CapabilityClassic.feature |= GS_DevFlagTermination;

    // ------------------------------------------------

    // store the REAL limits of the processor, not totally wrong values as in the legacy firmware from Hubert Denkmair
    bitlimits* limits = utils_get_bit_limits();

    GS_CapabilityClassic.fclk_can      = system_get_can_clock();
    GS_CapabilityClassic.time.seg1_min = 1;
    GS_CapabilityClassic.time.seg1_max = limits->nom_seg1_max;
    GS_CapabilityClassic.time.seg2_min = 1;
    GS_CapabilityClassic.time.seg2_max = limits->nom_seg2_max;
    GS_CapabilityClassic.time.brp_min  = 1;
    GS_CapabilityClassic.time.brp_max  = limits->nom_brp_max;
    GS_CapabilityClassic.time.brp_inc  = 1;
    GS_CapabilityClassic.time.sjw_max  = limits->nom_sjw_max;

    // ------------------------------------------------

    GS_CapabilityFD.fclk_can = GS_CapabilityClassic.fclk_can;
    GS_CapabilityFD.feature  = GS_CapabilityClassic.feature;
    GS_CapabilityFD.time_nom = GS_CapabilityClassic.time;

    GS_CapabilityFD.time_data.seg1_min = 1;
    GS_CapabilityFD.time_data.seg1_max = limits->fd_seg1_max;
    GS_CapabilityFD.time_data.seg2_min = 1;
    GS_CapabilityFD.time_data.seg2_max = limits->fd_seg2_max;
    GS_CapabilityFD.time_data.brp_min  = 1;
    GS_CapabilityFD.time_data.brp_max  = limits->fd_brp_max;
    GS_CapabilityFD.time_data.brp_inc  = 1;
    GS_CapabilityFD.time_data.sjw_max  = limits->fd_sjw_max;

    // -------------- Added by ElmüSoft ----------------

    ELM_BoardInfo.McuDeviceID = (uint16_t)HAL_GetDEVID();
    strcpy(ELM_BoardInfo.McuName,   utils_get_MCU_name()); // "STM32G431"  (from makefile)
    strcpy(ELM_BoardInfo.BoardName, TARGET_BOARD);         // "Multiboard", "OpenlightLabs", "Jhoinrch"  (from makefile)
}

// A SETUP vendor request packet has been received (first stage).
// For IN  data requests (to the host) send the response.
// For OUT data requests (from the host) provide a buffer which will be filled and passed to control_vendor_OUT_data()
// See "USB Tutorial.chm" in subfolder "Documentation"
// This function is called from HAL_PCD_SetupStageCallback() -> USBD_LL_SetupStage() -> USBD_StdDevReq() -> USBD_GS_Setup() -> USBD_GS_Vendor_Request()
// returns false on error and sets ELM_LastError.
// IMPORTNT: Read the comment for USBD_GS_Vendor_Request()
bool control_setup_request(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    USB_BufHandleTypeDef* hcan = (USB_BufHandleTypeDef*) pdev->pClassData;

    if (req->bRequest != ELM_ReqGetLastError)
        ELM_LastError = FBK_Success; // reset error from the last command

    // Flash the blue LED very shortly if bus is closed
    // ATTENTION: If the bus is closed the green LED is on, so this is not the same as blue flashing with bus open.
    if (!can_is_opened())
        led_flash_RX(); // flash 15 ms

    uint8_t  value8;
    uint16_t value16;
    uint32_t value32;
    void*    src = NULL;
    uint16_t len = 0;
    switch (req->bRequest)
    {
        // ------- Host -> Device (error checking in next function) --------
        case GS_ReqSetHostFormat:
            len = sizeof(uint32_t);
            break;
        case GS_ReqIdentify:
            len = sizeof(uint32_t); // the application sends a 32 bit "mode", but the value is ignored here
            break;
        case GS_ReqSetBitTiming:
        case GS_ReqSetBitTimingFD:
            len = sizeof(kBitTiming);
            break;
        case GS_ReqSetDeviceMode:
            len = sizeof(kDeviceMode);
            break;
        case GS_ReqSetTermination:
            len = sizeof(uint32_t);
            break;
        case ELM_ReqSetFilter:
            len = sizeof(kFilter);
            break;
        case ELM_ReqSetBusLoadReport:
            len = sizeof(uint8_t);
            break;
        case ELM_ReqSetPinStatus:
            len = sizeof(kPinStatus);
            break;

        // -------- Device -> Host (error checking here) --------
        case GS_ReqGetCapabilities:
            src = &GS_CapabilityClassic;
            len = sizeof(kCapabilityClassic);
            break;
        case GS_ReqGetCapabilitiesFD:
            src = &GS_CapabilityFD;
            len = sizeof(kCapabilityFD);
            break;
        case GS_ReqGetDeviceVersion:
            src = &GS_DeviceVersion;
            len = sizeof(kDeviceVersion);
            break;
        case GS_ReqGetTimestamp:
            // Bugfix: The legacy firmware used a timestamp created only when a USB SOF packet was received.
            // This is totally stupid, because the timestamp has a precision of 1 µs, but SOF packets are received once every millisecond.
            value32 = system_get_timestamp();
            src = &value32;
            len = sizeof(uint32_t);
            break;
        case GS_ReqGetTermination:
        {
            bool bEnabled;
            if (!can_get_termination(&bEnabled))
            {
                ELM_LastError = FBK_UnsupportedFeature;
                return false; // the board cannot switch on/off the termination resistor
            }
            value32 = bEnabled ? GS_TerminationON : GS_TerminationOFF;
            src = &value32;
            len = sizeof(uint32_t);
            break;
        }
        case ELM_ReqGetBoardInfo:
            src = &ELM_BoardInfo;
            len = sizeof(kBoardInfo);
            break;
        case ELM_ReqGetLastError:
        {
            value8 = ELM_LastError; // error of last SETUP command (eUsbRequest)
            src = &value8;
            len = sizeof(uint8_t);
            break;
        }
        case ELM_ReqGetPinStatus:
        {
            switch (req->wValue) // ePinID must be transmitted in wValue
            {
                case PINID_BOOT0: // currently the only implemented pin (PINST_High is irrelevant here)
                    value16 = system_is_option_enabled(OPT_BOOT0_Enable) ? PINST_Enabled : 0;
                    break;
                default:
                    ELM_LastError = FBK_InvalidParameter;
                    return false;
            }
            src = &value16;
            len = sizeof(uint16_t);
            break;
        }
        default:
            ELM_LastError = FBK_InvalidCommand;
            return false;
    }

    // If the host passes a buffer that is too small for the entire response, this is not an error.
    // All USB devices return a partial response in this case.
    len = MIN(len, req->wLength);

    switch (req->bRequest)
    {
        // -------- Host -> Device (OUT) --------
        case  GS_ReqIdentify:
        case  GS_ReqSetHostFormat:
        case  GS_ReqSetBitTiming:
        case  GS_ReqSetBitTimingFD:
        case  GS_ReqSetDeviceMode:
        case  GS_ReqSetTermination:
        case ELM_ReqSetFilter:
        case ELM_ReqSetBusLoadReport:
        case ELM_ReqSetPinStatus:
            // provide the buffer ep0_buf in which the data from the host is passed to control_setup_OUT_data()
            hcan->last_setup_request = *req;
            USBD_CtlPrepareRx(pdev, hcan->ep0_buf, req->wLength);
            return true;

        // -------- Device -> Host (IN) --------
        case  GS_ReqGetCapabilities:
        case  GS_ReqGetCapabilitiesFD:
        case  GS_ReqGetDeviceVersion:
        case  GS_ReqGetTimestamp:
        case  GS_ReqGetTermination:
        case ELM_ReqGetBoardInfo:
        case ELM_ReqGetLastError:
        case ELM_ReqGetPinStatus:
            // return the requested data
            USBD_CtlSendData(pdev, (uint8_t*)src, len);
            return true;

        default:
            ELM_LastError = FBK_InvalidCommand;
            return false;
    }
}

// Second Stage: The OUT data of a SETUP vendor request from the host has been received in the Endpoint 0 buffer.
// See "USB Tutorial.chm" in subfolder "Documentation"
// This function is called from ISR handler -> HAL_PCD_DataOutStageCallback -> USBD_LL_DataOutStage() -> USBD_GS_EP0_RxReady()
// IMPORTANT:
// The HAL does not allow to stall endpoint 0 in this stage anymore.
// If the user has sent invalid data for BitTiming or for a Filter we have no way to inform the host about this error.
// Calling  USBD_CtlError() in this stage will not stall the endpoint. This is EXTREMLY stupid.
// So the ONLY way to transmit errors of SETUP requests to the host is with the new ElmüSoft protocol and command ELM_ReqGetLastError.
// The host must call ELM_ReqGetLastError after each SETUP request to check for errors!
// See subfolder SampleApplication, this is very easy.
void control_setup_OUT_data(USBD_HandleTypeDef *pdev)
{
    USB_BufHandleTypeDef* hcan = (USB_BufHandleTypeDef*) pdev->pClassData;
    USBD_SetupReqTypedef*  req  = &hcan->last_setup_request;

    switch (req->bRequest)
    {
        case GS_ReqSetHostFormat:
        {
            // The firmware of the original USB2CAN by Geschwister Schneider exchanges all data in host byte order.
            // The application sends the value 0xbeef indicating the desired byte order.
            // The widely used open source CandleLight does not support this and uses always little endian byte order.
            // return an error if the host requests big endian.
            uint32_t* format = (uint32_t*)hcan->ep0_buf;
            if (*format != 0xbeef)
                ELM_LastError = FBK_UnsupportedFeature;
            return;
        }
        case GS_ReqSetBitTiming: // set CAN classic and CAN FD nominal baudrate + samplepoint
        {
            kBitTiming* timing = (kBitTiming*)hcan->ep0_buf;
            ELM_LastError = can_set_nom_bit_timing(timing->brp, timing->prop + timing->seg1, timing->seg2, timing->sjw);
            return;
        }
        case GS_ReqSetBitTimingFD: // set CAN FD data baudrate + samplepoint
        {
            kBitTiming* timing = (kBitTiming*)hcan->ep0_buf;
            ELM_LastError = can_set_data_bit_timing(timing->brp, timing->prop + timing->seg1, timing->seg2, timing->sjw);
            return;
        }
        case GS_ReqSetDeviceMode:
        {
            // ------------------------- 1.) Error Checking --------------------------------
            kDeviceMode* dev_Mode = (kDeviceMode*)hcan->ep0_buf;
            if (dev_Mode->mode != GS_ModeStart && dev_Mode->mode != GS_ModeReset)
            {
                ELM_LastError = FBK_InvalidParameter;
                return;
            }
            if (dev_Mode->mode == GS_ModeStart)
            {
                if (can_is_opened())
                {
                    ELM_LastError = FBK_AdapterMustBeClosed;
                    return;
                }
                // The flag GS_DevFlagCAN_FD is superfluous for this command.
                // CAN FD is enabled automatically as soon as a data bitrate has been set with GS_ReqSetBitTimingFD.
                if ((dev_Mode->flags & GS_DevFlagCAN_FD) > 0 && !can_using_FD())
                {
                    ELM_LastError = FBK_BaudrateNotSet; // CAN FD data bitrate not set --> CAN FD not possible
                    return;
                }
            }
            // ------------------------- 2.) Set Flags -------------------------------------
            USER_Flags = USR_CandleDefault; // reset all falgs to their default
            if (dev_Mode->flags &  GS_DevFlagOneShot)       USER_Flags &= ~USR_Retransmit;
            if (dev_Mode->flags &  GS_DevFlagTimestamp)     USER_Flags |=  USR_Timestamp;
            if (dev_Mode->flags & ELM_DevFlagDisableTxEcho) USER_Flags &= ~USR_ReportTX;
            if (dev_Mode->flags & ELM_DevFlagProtocolElmue) USER_Flags |= (USR_ProtoElmue | USR_DebugReport);

            // ------------------------- 3.) Start / Reset ----------------------------------
            if (dev_Mode->mode == GS_ModeStart)
            {
                uint32_t open_mode = FDCAN_MODE_NORMAL;
                if ((dev_Mode->flags & GS_DevFlagListenOnly) > 0)
                {
                    open_mode = FDCAN_MODE_BUS_MONITORING; // Do not send ACK to the CAN bus

                    if ((dev_Mode->flags & GS_DevFlagLoopback) > 0)
                        open_mode = FDCAN_MODE_INTERNAL_LOOPBACK; // Do not send neither ACK nor packets to the CAN bus, Loopback Tx -> Rx
                }
                else
                {
                    if ((dev_Mode->flags & GS_DevFlagLoopback) > 0)
                        open_mode = FDCAN_MODE_EXTERNAL_LOOPBACK; // Send packets to CAN bus and Loopback Tx -> Rx
                }              
                ELM_LastError = can_open(open_mode);
                return;
            }
            if (dev_Mode->mode == GS_ModeReset)
            {
                can_close(); // no error if already closed
                return;
            }
        }
        case GS_ReqIdentify:
        {
            uint32_t* mode = (uint32_t*)hcan->ep0_buf; // 1 = blink, 0 = stop
            led_blink_identify(*mode); // blink blue / green LEDs alternatingly
            return;
        }
        case GS_ReqSetTermination:
        {
            uint32_t* termination = (uint32_t*)hcan->ep0_buf; // eTermination
            if (!can_set_termination(*termination == GS_TerminationON))
                ELM_LastError = FBK_UnsupportedFeature;
            return;
        }
        case ELM_ReqSetFilter:
        {
            kFilter* filter = (kFilter*)hcan->ep0_buf;
            switch (filter->Operation)
            {
                case FIL_ClearAll:
                    ELM_LastError = can_clear_filters();
                    return;
                case FIL_AcceptMask11bit:
                case FIL_AcceptMask29bit:
                    ELM_LastError = can_set_mask_filter(filter->Operation == FIL_AcceptMask29bit, filter->Filter, filter->Mask);
                    return;
                default:
                    ELM_LastError = FBK_InvalidParameter;
                    return;
            }
        }
        case ELM_ReqSetBusLoadReport:
        {
            uint8_t interval = hcan->ep0_buf[0];
            if ((USER_Flags & USR_ProtoElmue) == 0) // the ElmüSoft protocol must be enabled for busload reports
                ELM_LastError = FBK_InvalidParameter;
            else
                ELM_LastError = can_enable_busload(interval); // interval in 100ms steps
            return;
        }
        case ELM_ReqSetPinStatus:
        {
            kPinStatus* pin_status = (kPinStatus*)hcan->ep0_buf;

            // Enabling the pin needs not to be implemented here.
            // The pin is automatically enabled when entering DFU mode in dfu_switch_to_bootloader()
            if (pin_status->PinID == PINID_BOOT0 && pin_status->Operation == PINOP_Disable)
            {
                ELM_LastError = system_set_option_bytes(OPT_BOOT0_Disable);
                return;
            }
            ELM_LastError = FBK_InvalidParameter;
            return;
        }
    }
}

// ========================= Errors ===========================

// This function is called approx 100 times in one millisecond from the main loop
// if the error state has changed, report it every 100 ms
// if the error state did not change, report the same state only every 3000 ms.
void control_process(uint32_t tick_now)
{
    if (error_is_report_due(tick_now))
        buf_store_error();
    
    // Revover BusOff AFTER printing error BusOff to the Trace output!
    can_recover_bus_off();
}

void control_report_busload(uint8_t busload_percent)
{
    kHostFrameObject* pool_frame = buf_get_frame_locked(&USB_BufHandle.list_host_pool);
    if (!pool_frame)
        return; // buffer overflow! buf_process() will report this error to  the host

    kBusloadElmue* packet = (kBusloadElmue*)&pool_frame->frame;
    packet->header.size     = sizeof(kBusloadElmue);
    packet->header.msg_type = MSG_Busload;
    packet->bus_load        = busload_percent;

    list_add_tail_locked(&pool_frame->list, &USB_BufHandle.list_to_host);
}

// Send a debug message. Maximum length is 78 characters.
// The message may contain "\n" for multi-line output.
// To make sure that you see all debug output the first command that you execute
// should be GS_ReqSetDeviceMode with GS_ModeClose and ELM_DevFlagProtocolElmue.
// This closes the device if still open and enables debug output.
// If the device has a legacy firmware it will ignore any flags that are passed with GS_ModeClose.
// Only the new ElmüSoft firmware allows to set flags when closing the device.
bool control_send_debug_mesg(const char* message)
{
    if ((USER_Flags & USR_DebugReport) == 0)
        return false;

    kHostFrameObject* pool_frame = buf_get_frame_locked(&USB_BufHandle.list_host_pool);
    if (!pool_frame)
        return false; // buffer overflow! buf_process() will report this error to  the host

    // ------------------------------

    int len = strlen(message);
    if (len > sizeof(kHostFrameLegacy) - sizeof(kStringElmue))
    {
        message = "*** Dbg msg too long";
        len = 20;
    }

    kStringElmue* packet = (kStringElmue*)&pool_frame->frame;
    packet->header.size     = sizeof(kStringElmue) + len;
    packet->header.msg_type = MSG_String;
    memcpy(packet->ascii_msg, message, len);

    list_add_tail_locked(&pool_frame->list, &USB_BufHandle.list_to_host);
    return true;
}

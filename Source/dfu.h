/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once
#include "settings.h"

// See "DFU Functional Descriptor 1.1.pdf" in subfolder "Documentation".

// These requests can be sent to the firmware update interface.
// In DFU mode they are all functional, but require the STtube30 driver from ST Microelectronics.
// In APP mode the Candlelight exposes a reduced DFU interface which implements only DFU_RequDetach and DFU_RequGetStatus.
typedef enum 
{
    DFU_RequDetach      = 0, // RequType = 0x21, Tells device to detach and re-enter DFU mode (wValue = Timeout)
    DFU_RequDownload    = 1, // RequType = 0x21, Download firmware data to device (wValue = BlockNumber)
    DFU_RequUpload      = 2, // RequType = 0xA1, Upload firmware data from device
    DFU_RequGetStatus   = 3, // RequType = 0xA1, Get device status and poll timeout (6 byte)
    DFU_RequClearStatus = 4, // RequType = 0x21, Clear current device status
    DFU_RequGetState    = 5, // RequType = 0xA1, Get current device state (1 byte)
    DFU_RequAbort       = 6, // RequType = 0x21, Abort current operation
} eDfuRequest;

// This is sent in byte 0 (bStatus) of kDfuStatus from a DFU_RequGetStatus request
typedef enum 
{
    DfuStatus_OK = 0,         // No error condition is present.
    DfuStatus_ErrTarget,      // File is not targeted for use by this device. 
    DfuStatus_ErrFile,        // File is for this device but fails some vendor-specific verification test. 
    DfuStatus_ErrWrite,       // Device is unable to write memory. 
    DfuStatus_ErrErase,       // Memory erase function failed.
    DfuStatus_ErrCheckErased, // Memory erase check failed.
    DfuStatus_ErrProg,        // Program memory function failed.
    DfuStatus_ErrVerify,      // Programmed memory failed verification. 
    DfuStatus_ErrAddress,     // Cannot program memory due to received address that is out of range. 
    DfuStatus_ErrNotDone,     // Received DFU_DNLOAD with wLength = 0, but device does not think it has all of the data yet. 
    DfuStatus_ErrFirmware,    // Device’s firmware is corrupt.  It cannot return to run-time (non-DFU) operations. 
    DfuStatus_ErrVendor,      // StringIdx indicates a vendor-specific error. 
    DfuStatus_ErrUSBR,        // Device detected unexpected USB reset signaling. 
    DfuStatus_ErrPOR,         // Device detected unexpected power on reset.  
    DfuStatus_ErrUnknown,     // Something went wrong, but the device does not know what it was. 
    DfuStatus_ErrStallEP,     // Device stalled an unexpected request. 
} eDfuStatus;

// This is sent in byte 4 (bState) of kDfuStatus from a DFU_RequGetStatus request
typedef enum 
{
    DfuState_AppIdle = 0,       // Device is running its normal application mode.
    DfuState_AppDetach,         // Device is running its normal application, has received the DFU_DETACH request, and is waiting for a USB reset. 
    DfuState_DfuIdle,           // Device is operating in the DFU mode and is waiting for requests.
    DfuState_DownloadSync,      // Device has received a block and is waiting for the host to solicit the status via DFU_GETSTATUS. 
    DfuState_DownloadBusy,      // Device is programming a control-write block into its nonvolatile memories. 
    DfuState_DownloadIdle,      // Device is processing a download operation, expecting DFU_DNLOAD requests. 
    DfuState_ManifestSync,      // Device has received the final block of firmware and waits for DFU_GETSTATUS to begin Manifestation phase
    DfuState_Manifest,          // Device is in the Manifestation phase.  
    DfuState_ManifestWaitReset, // Device has programmed its memories and is waiting for a USB reset or a power-on reset
    DfuState_UploadIdle,        // Device is processing an upload operation. 
    DfuState_Error              // An error has occurred. Awaiting the DFU_CLRSTATUS request. 
} eDfuState;

// response to DFU_RequGetStatus request (size = 6 byte)
typedef struct
{
    uint8_t Status;          // eDfuStatus
    uint8_t PollTimeout[3];
    uint8_t State;           // eDfuState
    uint8_t StringIdx;       // string index for proprietary vendor error messages (see DfuStatus_ErrVendor)
} __packed __aligned(1) kDfuStatus;


eFeedback dfu_switch_to_bootloader();
void dfu_timer_100ms(uint32_t tick_now);

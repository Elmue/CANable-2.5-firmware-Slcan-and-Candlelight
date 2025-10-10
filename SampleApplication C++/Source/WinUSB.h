
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ====================================== USB SETUP Request =====================================

enum eSetupRecip // Bits 0,1,2,3,4 of bmRequestType
{
    RECIP_Device    = 0x00,
    RECIP_Interface = 0x01,
    RECIP_Endpoint  = 0x02,
    RECIP_Other     = 0x03,
    //   ....   0x1F,
};
    
enum eSetupType // Bits 5,6 of bmRequestType
{
    TYP_Standard = 0x00, // 0 << 5
    TYP_Class    = 0x20, // 1 << 5
    TYP_Vendor   = 0x40, // 2 << 5
};

enum eDirection // Bit 7 of bmRequestType, also used for endpoints
{
    DIR_Out = 0x00,
    DIR_In  = 0x80,
};

#pragma pack(1)

typedef struct _WINUSB_SETUP_PACKET 
{
    UCHAR   RequestType; // eSetupRecip | eSetupType | eDirection
    UCHAR   Request;
    USHORT  Value;
    USHORT  Index;
    USHORT  Length;
} WINUSB_SETUP_PACKET, *PWINUSB_SETUP_PACKET;

#pragma pack()


// ========================================= DFU ==========================================

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

// This is sent in byte 0 of a DFU_RequGetStatus request
typedef enum 
{
    DfuStatus_OK = 0,      // No error condition is present.
    DfuStatus_ErrTarget,   // File is not targeted for use by this device. 
    DfuStatus_ErrFile,     // File is for this device but fails some vendor-specific verification test. 
    DfuStatus_ErrWrite,    // Device is unable to write memory. 
    DfuStatus_ErrErase,    // Memory erase function failed.
    // and more... (not used here)
} eDfuStatus;

// This is sent in byte 4 of a DFU_RequGetStatus request
typedef enum 
{
    DfuState_AppIdle = 0,   // Device is running its normal application mode.
    DfuState_AppDetach,     // Device is running its normal application, has received the DFU_DETACH request, and is waiting for a USB reset. 
    DfuState_DfuIdle,       // Device is operating in the DFU mode and is waiting for requests.
    DfuState_DownloadSync,  // Device has received a block and is waiting for the host to solicit the status via DFU_GETSTATUS. 
    DfuState_DownloadBusy,  // Device is programming a control-write block into its nonvolatile memories. 
    DfuState_DownloadIdle,  // Device is processing a download operation, expecting DFU_DNLOAD requests. 
    // and more... (not used here)
} eDfuState;

#pragma pack(push,1)

// response to DFU_RequGetStatus request (size = 6 byte)
typedef struct
{
    BYTE Status;          // eDfuStatus
    BYTE PollTimeout[3];
    BYTE State;           // eDfuState
    BYTE StringIdx;
} kDfuStatus;

#pragma pack(pop)


// ======================================= WINUSB ==========================================

// Copyright (c) 2001 Microsoft Corporation.  All Rights Reserved.

#if(NTDDI_VERSION >= NTDDI_WINXP)

// Pipe policy types
#define SHORT_PACKET_TERMINATE  0x01
#define AUTO_CLEAR_STALL        0x02
#define PIPE_TRANSFER_TIMEOUT   0x03
#define IGNORE_SHORT_PACKETS    0x04
#define ALLOW_PARTIAL_READS     0x05
#define AUTO_FLUSH              0x06
#define RAW_IO                  0x07
#define MAXIMUM_TRANSFER_SIZE   0x08
#define RESET_PIPE_ON_RESUME    0x09

#define USB_DEVICE_DESCRIPTOR_TYPE                          0x01
#define USB_CONFIGURATION_DESCRIPTOR_TYPE                   0x02
#define USB_STRING_DESCRIPTOR_TYPE                          0x03
#define USB_INTERFACE_DESCRIPTOR_TYPE                       0x04
#define USB_ENDPOINT_DESCRIPTOR_TYPE                        0x05
#define USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE                0x06
#define USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE       0x07
#define USB_INTERFACE_POWER_DESCRIPTOR_TYPE                 0x08
#define USB_OTG_DESCRIPTOR_TYPE                             0x09
#define USB_DEBUG_DESCRIPTOR_TYPE                           0x0A
#define USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE           0x0B
#define USB_BOS_DESCRIPTOR_TYPE                             0x0F
#define USB_DEVICE_CAPABILITY_DESCRIPTOR_TYPE               0x10
#define USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE   0x30


// Power policy types
//
// Add 0x80 for Power policy types in order to prevent overlap with 
// Pipe policy types to prevent "accidentally" setting the wrong value for the 
// wrong type.
//
#define AUTO_SUSPEND            0x81
#define SUSPEND_DELAY           0x83

// Device Information types
#define DEVICE_SPEED            0x01

// Device Speeds
#define LowSpeed                0x01
#define FullSpeed               0x02
#define HighSpeed               0x03 

// {DA812BFF-12C3-46a2-8E2B-DBD3B7834C43}
#include <initguid.h>

typedef LONG USBD_STATUS;

DEFINE_GUID(WinUSB_TestGuid, 0xda812bff, 0x12c3, 0x46a2, 0x8e, 0x2b, 0xdb, 0xd3, 0xb7, 0x83, 0x4c, 0x43);

typedef enum _USBD_PIPE_TYPE {
    UsbdPipeTypeControl,
    UsbdPipeTypeIsochronous,
    UsbdPipeTypeBulk,
    UsbdPipeTypeInterrupt
} USBD_PIPE_TYPE;

#include <pshpack2.h>

// Must be packed with 2 byte alignment!!
typedef struct _WINUSB_PIPE_INFORMATION {
    USBD_PIPE_TYPE  PipeType;
    UCHAR           PipeId;
    USHORT          MaximumPacketSize;
    UCHAR           Interval;
} WINUSB_PIPE_INFORMATION, *PWINUSB_PIPE_INFORMATION;


typedef struct _WINUSB_PIPE_INFORMATION_EX {
    USBD_PIPE_TYPE PipeType;
    UCHAR          PipeId;
    USHORT         MaximumPacketSize;
    UCHAR          Interval;
    ULONG          MaximumBytesPerInterval;
} WINUSB_PIPE_INFORMATION_EX, *PWINUSB_PIPE_INFORMATION_EX;

#include <poppack.h>
#include <pshpack1.h>

//
// USB 1.1: 9.6.1 Device, Table 9-7. Standard Device Descriptor
// USB 2.0: 9.6.1 Device, Table 9-8. Standard Device Descriptor
// USB 3.0: 9.6.1 Device, Table 9-8. Standard Device Descriptor
//
typedef struct _USB_DEVICE_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    USHORT  bcdUSB;
    UCHAR   bDeviceClass;
    UCHAR   bDeviceSubClass;
    UCHAR   bDeviceProtocol;
    UCHAR   bMaxPacketSize0;
    USHORT  idVendor;
    USHORT  idProduct;
    USHORT  bcdDevice;
    UCHAR   iManufacturer;
    UCHAR   iProduct;
    UCHAR   iSerialNumber;
    UCHAR   bNumConfigurations;
} USB_DEVICE_DESCRIPTOR, *PUSB_DEVICE_DESCRIPTOR;

C_ASSERT(sizeof(USB_DEVICE_DESCRIPTOR) == 18);

//
// USB 1.1: 9.6.2 Configuration, Table 9-8. Standard Configuration Descriptor
// USB 2.0: 9.6.3 Configuration, Table 9-10. Standard Configuration Descriptor
// USB 3.0: 9.6.3 Configuration, Table 9-15. Standard Configuration Descriptor
//
typedef struct _USB_CONFIGURATION_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    USHORT  wTotalLength;
    UCHAR   bNumInterfaces;
    UCHAR   bConfigurationValue;
    UCHAR   iConfiguration;
    UCHAR   bmAttributes;
    UCHAR   MaxPower;
} USB_CONFIGURATION_DESCRIPTOR, *PUSB_CONFIGURATION_DESCRIPTOR;

C_ASSERT(sizeof(USB_CONFIGURATION_DESCRIPTOR) == 9);

//
// USB 1.1: 9.6.3 Interface, Table 9-9. Standard Interface Descriptor
// USB 2.0: 9.6.5 Interface, Table 9-12. Standard Interface Descriptor
// USB 3.0: 9.6.5 Interface, Table 9-17. Standard Interface Descriptor
//
typedef struct _USB_INTERFACE_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    UCHAR   bInterfaceNumber;
    UCHAR   bAlternateSetting;
    UCHAR   bNumEndpoints;
    UCHAR   bInterfaceClass;
    UCHAR   bInterfaceSubClass;
    UCHAR   bInterfaceProtocol;
    UCHAR   iInterface;
} USB_INTERFACE_DESCRIPTOR, *PUSB_INTERFACE_DESCRIPTOR;

C_ASSERT(sizeof(USB_INTERFACE_DESCRIPTOR) == 9);

typedef struct _USBD_ISO_PACKET_DESCRIPTOR {
    ULONG Offset;
    ULONG Length;
    USBD_STATUS Status;
} USBD_ISO_PACKET_DESCRIPTOR, *PUSBD_ISO_PACKET_DESCRIPTOR;


typedef PVOID WINUSB_INTERFACE_HANDLE, *PWINUSB_INTERFACE_HANDLE;

typedef PVOID WINUSB_ISOCH_BUFFER_HANDLE, *PWINUSB_ISOCH_BUFFER_HANDLE;

// ---------------------------------------------------------

BOOL __stdcall
WinUsb_Initialize(
    _In_  HANDLE DeviceHandle,
    _Out_ PWINUSB_INTERFACE_HANDLE InterfaceHandle
    );


BOOL __stdcall
WinUsb_Free(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle
    );


BOOL __stdcall
WinUsb_GetAssociatedInterface(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR AssociatedInterfaceIndex,
    _Out_ PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle
    );



BOOL __stdcall
WinUsb_GetDescriptor(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR DescriptorType,
    _In_  UCHAR Index,
    _In_  USHORT LanguageID,
    _Out_ PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_ PULONG LengthTransferred
    );

BOOL __stdcall
WinUsb_QueryInterfaceSettings(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR AlternateInterfaceNumber,
    _Out_ PUSB_INTERFACE_DESCRIPTOR UsbAltInterfaceDescriptor
    );

BOOL __stdcall
WinUsb_QueryDeviceInformation(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  ULONG InformationType,
    _Inout_ PULONG BufferLength,
     PVOID Buffer
    );

BOOL __stdcall
WinUsb_SetCurrentAlternateSetting(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR SettingNumber
    );

BOOL __stdcall
WinUsb_GetCurrentAlternateSetting(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _Out_ PUCHAR SettingNumber
    );

BOOL __stdcall
WinUsb_QueryPipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR AlternateInterfaceNumber,
    _In_  UCHAR PipeIndex,
    _Out_ PWINUSB_PIPE_INFORMATION PipeInformation
    );

BOOL __stdcall
WinUsb_QueryPipeEx(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR AlternateSettingNumber,
    _In_  UCHAR PipeIndex,
    _Out_ PWINUSB_PIPE_INFORMATION_EX PipeInformationEx
    );

BOOL __stdcall
WinUsb_SetPipePolicy(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _In_  ULONG PolicyType,
    _In_  ULONG ValueLength,
    _In_  PVOID Value
    );

BOOL __stdcall
WinUsb_GetPipePolicy(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _In_  ULONG PolicyType,
    _Inout_ PULONG ValueLength,
    _Out_ PVOID Value
    );

BOOL __stdcall
WinUsb_ReadPipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _Out_ PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_ LPOVERLAPPED Overlapped
    );

BOOL __stdcall
WinUsb_WritePipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _In_  PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_ LPOVERLAPPED Overlapped
    );

BOOL __stdcall
WinUsb_ControlTransfer(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  WINUSB_SETUP_PACKET SetupPacket,
    _Out_ PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_  LPOVERLAPPED Overlapped
    );

BOOL __stdcall
WinUsb_ResetPipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID
    );

BOOL __stdcall
WinUsb_AbortPipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID
    );

BOOL __stdcall
WinUsb_FlushPipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID
    );

BOOL __stdcall
WinUsb_SetPowerPolicy(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  ULONG PolicyType,
    _In_  ULONG ValueLength,
    _In_  PVOID Value
    );

BOOL __stdcall
WinUsb_GetPowerPolicy(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  ULONG PolicyType,
    _Inout_ PULONG ValueLength,
    _Out_ PVOID Value
    );

BOOL __stdcall
WinUsb_GetOverlappedResult(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  LPOVERLAPPED lpOverlapped,
    _Out_ LPDWORD lpNumberOfBytesTransferred,
    _In_  BOOL bWait
    );

PUSB_INTERFACE_DESCRIPTOR __stdcall
WinUsb_ParseConfigurationDescriptor(
    _In_  PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    _In_  PVOID StartPosition,
    _In_  LONG InterfaceNumber,
    _In_  LONG AlternateSetting,
    _In_  LONG InterfaceClass,
    _In_  LONG InterfaceSubClass,
    _In_  LONG InterfaceProtocol
    );

typedef struct _USB_COMMON_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
} USB_COMMON_DESCRIPTOR, *PUSB_COMMON_DESCRIPTOR;

PUSB_COMMON_DESCRIPTOR __stdcall
WinUsb_ParseDescriptors(
    _In_  PVOID    DescriptorBuffer,
    _In_  ULONG    TotalLength,
    _In_  PVOID    StartPosition,
    _In_  LONG     DescriptorType
    );

BOOL __stdcall WinUsb_GetCurrentFrameNumber (
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _Out_ PULONG CurrentFrameNumber,
    _Out_ LARGE_INTEGER *TimeStamp
    );

BOOL __stdcall WinUsb_GetAdjustedFrameNumber (
    _Inout_ PULONG CurrentFrameNumber,
    _In_  LARGE_INTEGER TimeStamp
    );

BOOL __stdcall
WinUsb_RegisterIsochBuffer(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _Inout_ PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_ PWINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle
    );

BOOL __stdcall
WinUsb_UnregisterIsochBuffer(
    _In_  WINUSB_ISOCH_BUFFER_HANDLE IsochBufferHandle
    );

BOOL __stdcall WinUsb_WriteIsochPipe (
    _In_  WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_  ULONG Offset,
    _In_  ULONG Length,
    _Inout_ PULONG FrameNumber,
    _In_opt_ LPOVERLAPPED Overlapped
    );

BOOL __stdcall WinUsb_ReadIsochPipe (
    _In_  WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_  ULONG Offset,
    _In_  ULONG Length,
    _Inout_ PULONG FrameNumber,
    _In_  ULONG NumberOfPackets,
    _Out_ PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors,
    _In_opt_ LPOVERLAPPED Overlapped
    );

BOOL __stdcall WinUsb_WriteIsochPipeAsap (
    _In_  WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_  ULONG Offset,
    _In_  ULONG Length,
    _In_  BOOL ContinueStream,
    _In_opt_ LPOVERLAPPED Overlapped
    );

BOOL __stdcall WinUsb_ReadIsochPipeAsap (
    _In_  WINUSB_ISOCH_BUFFER_HANDLE BufferHandle,
    _In_  ULONG Offset,
    _In_  ULONG Length,
    _In_  BOOL ContinueStream,
    _In_  ULONG NumberOfPackets,
    _Out_ PUSBD_ISO_PACKET_DESCRIPTOR IsoPacketDescriptors,
    _In_opt_ LPOVERLAPPED Overlapped
    );


#ifndef __USB_TIME_SYNC_DEFINED
#define __USB_TIME_SYNC_DEFINED


typedef struct _USB_START_TRACKING_FOR_TIME_SYNC_INFORMATION {

    HANDLE          TimeTrackingHandle;
    BOOLEAN         IsStartupDelayTolerable;

} USB_START_TRACKING_FOR_TIME_SYNC_INFORMATION, *PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION;

typedef struct _USB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION {

    HANDLE          TimeTrackingHandle;

} USB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION, *PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION;

typedef struct _USB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION {

    //
    // Input
    //

    HANDLE          TimeTrackingHandle;
    ULONG           InputFrameNumber;
    ULONG           InputMicroFrameNumber;

    //
    // Output
    //

    LARGE_INTEGER   QueryPerformanceCounterAtInputFrameOrMicroFrame;
    LARGE_INTEGER   QueryPerformanceCounterFrequency;
    ULONG           PredictedAccuracyInMicroSeconds;

    ULONG           CurrentGenerationID;
    LARGE_INTEGER   CurrentQueryPerformanceCounter;
    ULONG           CurrentHardwareFrameNumber;         // 11 bits from hardware/MFINDEX
    ULONG           CurrentHardwareMicroFrameNumber;    //  3 bits from hardware/MFINDEX
    ULONG           CurrentUSBFrameNumber;              // 32 bit USB Frame Number

} USB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION, *PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION;

#include <poppack.h>

#endif

BOOL __stdcall WinUsb_StartTrackingForTimeSync (
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION StartTrackingInfo
    );

BOOL __stdcall WinUsb_GetCurrentFrameNumberAndQpc (
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION FrameQpcInfo
    );

BOOL __stdcall WinUsb_StopTrackingForTimeSync (
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION StopTrackingInfo
    );

#endif // (NTDDI_VERSION >= NTDDI_WINXP)

#ifdef __cplusplus
}
#endif





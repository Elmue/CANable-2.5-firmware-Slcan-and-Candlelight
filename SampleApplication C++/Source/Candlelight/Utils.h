
// https://netcult.ch/elmue/CANable%20Firmware%20Update

#pragma once

// includes for Windows and Linux
#include <stdio.h>
#include <assert.h>
#include <cwchar>
#include <cstdarg>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>    // std::vector
#include <algorithm> // std::sort
#include <ctime>     // std::clock

using namespace std;

// ---------------------------------

// definitions to compile Candlelight_def.h on VS / GCC
#define  __aligned(x)  // only valid for ARM compiler (STM32xxx processors)
#define  __packed      // only valid for ARM compiler (STM32xxx processors)

// definitions for Visual Studio
#if defined(_MSC_VER)

    #define  uint8_t    unsigned char
    #define  int16_t    short
    #define  uint16_t   unsigned short
    #define  uint32_t   unsigned long
    #define  int64_t    __int64
    #define  uint64_t   unsigned __int64
    #pragma warning(disable: 4200) // Warning: nonstandard extension used : zero-sized array in struct/union in Candlelight_def.h

    // Visual Studio 2013 and older lack snprintf, but support _snprintf
    #if _MSC_VER < 1900
        #define snprintf _snprintf
        #pragma warning(disable: 4996)
    #endif
    
#elif defined(__linux__)    

    #include <cstdint>
    #include <cstring>
    #include <fstream>
    #include <unistd.h>
    #include <termios.h>
    #include <sys/select.h>
    #include <libusb-1.0/libusb.h>
    
#else
    #error "Unknown compiler"
#endif

#ifndef UNUSED
    #define UNUSED(x) (void)(x)
#endif

// Candlelight_def.h must be identical with the same file that is compiled into the firmware.
#include "Candlelight_def.h"

// ---------------------------------

// must be equal to FIRMW_UPDATE_INTERFACE in usb_class.h in the firmware
#define FIRMW_UPDATE_INTERFACE  1

// Timeout for writing the OUT pipe and for Control Transfer (500 ms is far more than required)
#define PIPE_TIMEOUT            500  

// These error codes are used for Windows and Linux.
// Native Windows API error codes are far below 50000.
// libusb errors are always negative (-1 to -99), although they are returned as uint32_t from the functions in OsLibrary.
#ifndef NO_ERROR
    #define NO_ERROR              0  // Success
#endif
#define ERR_DEVICE_IN_USE     57009  // Access denied. Probably the device is already open elsewhere.
#define ERR_INVALID_DEVICE    57010  // Not a Candlelight device
#define ERR_INVALID_FIRMWARE  57011  // Not CANable 2.5 firmware
#define ERR_CODE_IN_FEEDBACK  57012  // Check me_LastError for an explanation
#define ERR_RX_FIFO_OVERFLOW  57013  // The application is polling ReceiveData() slower than USB IN packets arrive. In the demo app the reason may be the slow Windows console.
#define ERR_CORRUPT_IN_DATA   57014  // Corrupt USB IN packet received from the firmware
#define ERR_UPDATE_FIRMWARE   57015  // The user must update the firmware to the device
#define ERR_TOO_MANY_ERRORS   57016  // Too many errors during WritePipe / ReadPipe
#define ERR_TX_DATA_TOO_LONG  57017  // The Tx data is too long
#define ERR_OPERATION_INVALID 57018  // Invalid operation
#define ERR_PARAM_INVALID     57019  // Invalid parameter
#define ERR_INVALID_RX_DATA   57020  // Invalid Rx data was received from the device
#define ERR_TIMEOUT           57021  // Timeout waiting for data
#define ERR_NO_DRIVER         57022  // The driver is not installed correctly
#define ERR_UPDATE_LIBUSB     57023  // The user must update libusb on Linux

namespace CANable
{
#pragma pack(push,1)

// standard USB device descriptor
struct kDeviceDescriptor
{
    uint8_t   bLength;
    uint8_t   bDescriptorType;
    uint16_t  bcdUSB;
    uint8_t   bDeviceClass;
    uint8_t   bDeviceSubClass;
    uint8_t   bDeviceProtocol;
    uint8_t   bMaxPacketSize0;
    uint16_t  idVendor;
    uint16_t  idProduct;
    uint16_t  bcdDevice;
    uint8_t   iManufacturer;
    uint8_t   iProduct;
    uint8_t   iSerialNumber;
    uint8_t   bNumConfigurations;
};

// standard USB interface descriptor
struct kInterfaceDescriptor
{
    uint8_t   bLength;
    uint8_t   bDescriptorType;
    uint8_t   bInterfaceNumber;
    uint8_t   bAlternateSetting;
    uint8_t   bNumEndpoints;
    uint8_t   bInterfaceClass;
    uint8_t   bInterfaceSubClass;
    uint8_t   bInterfaceProtocol;
    uint8_t   iInterface;
};

// =============== USB SETUP Request ================

enum eSetupRecip // Bits 0,1,2,3,4 of kSetup.bRequestType
{
    RECIP_Device    = 0x00,
    RECIP_Interface = 0x01,
    RECIP_Endpoint  = 0x02,
    RECIP_Other     = 0x03,
    //   ....   0x1F,
};
    
enum eSetupType // Bits 5,6 of kSetup.bRequestType
{
    TYP_Standard = 0x00, // 0 << 5
    TYP_Class    = 0x20, // 1 << 5
    TYP_Vendor   = 0x40, // 2 << 5
};

enum eDirection // Bit 7 of kSetup.bRequestType, also used for endpoints
{
    DIR_Out = 0x00,
    DIR_In  = 0x80,
};

// standard USB Setup request
struct kSetup
{
    uint8_t   bRequestType; // eSetupRecip | eSetupType | eDirection
    uint8_t   bRequest;     // GS_ReqGetCapabilities,... / DFU_RequDetach, DFU_RequGetStatus,...
    uint16_t  wValue;       // CAN Channel / ePinID for ELM_ReqGetPinStatus
    uint16_t  wIndex;       // Interface number (0 = Candlelight, 1 = DFU)
    uint16_t  wLength;      // Byte count
};

// ================= DFU =================

// See "DFU Functional Descriptor 1.1.pdf" in subfolder "Documentation".

// These requests can be sent to the firmware update interface.
// In DFU mode they are all functional, but require the STtube30 driver from ST Microelectronics.
// In APP mode the Candlelight exposes a reduced Firmware Update interface which implements only DFU_RequDetach and DFU_RequGetStatus.
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

// This is sent in byte 0 (Status) of kDfuStatus from a DFU_RequGetStatus request
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

// This is sent in byte 4 (State) of kDfuStatus from a DFU_RequGetStatus request
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
    DfuState_Error,             // An error has occurred. Awaiting the DFU_CLRSTATUS request. 
    // -------------
    DfuState_UploadSync  = 0x91,
    DfuState_UploadBusy  = 0x92,
} eDfuState;

// response to DFU_RequGetStatus request (size = 6 byte)
typedef struct
{
    uint8_t Status;          // eDfuStatus
    uint8_t PollTimeout[3];
    uint8_t State;           // eDfuState
    uint8_t StringIdx;       // string index for proprietary vendor error messages (see DfuStatus_ErrVendor)
} kDfuStatus;

#pragma pack(pop)

// -------------------------------------------------------------

// This struct contains all the details of the connected Candlelight device.
// This struct can be obtained with Candlelight::GetDeviceInfo()
struct kDevInfo
{
    // the following members are set in OsLibrary::Open()
    string                   ms_Vendor;           // from device descriptor
    string                   ms_Product;          // from device descriptor
    string                   ms_Serial;           // from device descriptor
    string                   ms_Interface;        // from interface descriptor
    uint8_t                  mu8_EndpointIN;      // e.g. 0x81
    uint8_t                  mu8_EndpointOUT;     // e.g. 0x02
    uint16_t                 mu16_MaxPackSizeIN;  // max packet size for IN  endpoint (64 bytes for Full Speed USB)
    uint16_t                 mu16_MaxPackSizeOUT; // max packet size for OUT endpoint (64 bytes for Full Speed USB)
    kDeviceDescriptor        mk_DeviceDescr;      // entire device descriptor
    kInterfaceDescriptor     mk_InterfDescr;      // entire interface descriptor
    
    // the following members are set in Candlelight::Open()
    uint8_t                  mu8_Channel;         // CAN channel 0,1,2
    bool                     mb_IsElmueSoft;      // The adapter supports the ElmüSoft protocol
    bool                     mb_SupportsFD;       // The adapter supports CAN FD
    kCapabilityClassic       mk_Capability;       // see Candlelight_def.h
    kCapabilityFD            mk_CapabilityFD;     // see Candlelight_def.h
    kDeviceVersion           mk_DeviceVersion;    // see Candlelight_def.h
    kBoardInfo               mk_BoardInfo;        // see Candlelight_def.h

    void Clear()
    {
        ms_Vendor           = "";
        ms_Product          = "";
        ms_Serial           = "";
        ms_Interface        = "";
        mu8_EndpointIN      = 0;
        mu8_EndpointOUT     = 0;
        mu16_MaxPackSizeIN  = 0;
        mu16_MaxPackSizeOUT = 0;
        mu8_Channel         = 0;        
        mb_IsElmueSoft      = false;
        mb_SupportsFD       = false;
        memset(&mk_DeviceDescr,   0, sizeof(mk_DeviceDescr));
        memset(&mk_InterfDescr,   0, sizeof(mk_InterfDescr));
        memset(&mk_Capability,    0, sizeof(mk_Capability));
        memset(&mk_CapabilityFD,  0, sizeof(mk_CapabilityFD));
        memset(&mk_DeviceVersion, 0, sizeof(mk_DeviceVersion));
        memset(&mk_BoardInfo,     0, sizeof(mk_BoardInfo));
    }
};

// A USB packet that was received on the IN pipe
struct kUsbInPacket
{
    uint8_t   mu8_Buffer[MAX_BLOB_SIZE];
    uint32_t  mu32_BytesRead;
    uint32_t  mu32_Error;
    int64_t   ms64_OsTimestamp;  // Timestamp with 1µs precision from operating system
};

// This struct is filled by OsLibrary::EnumDevices()
// The information is displayed to the user, so he can select one of the connected USB devices.
struct kUsbDevice
{
public:
    string  ms_Product;      // from Device Descriptor
    string  ms_SerialNo;     // from Device Descriptor
    string  ms_Interface;    // from Interface Descriptor
    int     ms32_Interface;  // zero-based interface index
    void*   mpi_LinuxDevice; // on Linux: libusb_device*
    // Windows = "\\?\USB#VID_1D50&PID_606F&MI_00#7&1B930F3C&0&0000#{C15B4308-04D3-11E6-B3EA-6057189E6443}"
    // Linux   = "Bus number: 2, Device address: 3"
    string  ms_DevicePath;   
    
    kUsbDevice()
    {
        ms32_Interface  = 0;
        mpi_LinuxDevice = 0;
    }

    // The Firmware Update Interface (1) has no CAN channels --> return -1
    // The Candlelight interfaces are 0,2,3,... --> display as CAN Channel 1,2,3,...
    int GetCanChannel()
    {
        switch (ms32_Interface)
        {
            case 0:                      return  1; // display one-based channel number           
            case FIRMW_UPDATE_INTERFACE: return -1; // invalid
            default:                     return ms32_Interface;
        }
    }

    // Sort by by Serial Number and then by Channel number
    bool operator<(const kUsbDevice& i_Dev2) const
    {
        if (ms_SerialNo != i_Dev2.ms_SerialNo)
            return ms_SerialNo < i_Dev2.ms_SerialNo;

        return ms32_Interface < i_Dev2.ms32_Interface;
    }
};

// -------------------------------------------------------------

class cUtils
{
public:
    static string   MakeUpper(string s_String);
    static string   TrimRight(string s_String, const char* s_Remove = " \n\r\t");
    static string   Format(const char* c_Format, ...);
    static string   MapLookup(unordered_map<string, string>& i_Map, string& s_Key);
    static string   FormatHexBytes(uint8_t u8_Data[], int s32_DataLen);
    static string   FormatBcdVersion(uint32_t u32_Version);
    static uint64_t GetTickMilli();
};

}; // namesapce

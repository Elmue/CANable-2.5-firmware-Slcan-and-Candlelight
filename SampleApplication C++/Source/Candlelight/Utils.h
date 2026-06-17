
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
#endif

// Candlelight_def.h must be identical with the same file that is compiled into the firmware.
#include "Candlelight_def.h"

// ---------------------------------

// must be equal to FIRMW_UPDATE_INTERFACE in usb_class.h in the firmware
#define FIRMW_UPDATE_INTERFACE  1

// Timeout for writing the OUT pipe and for Control Transfer (500 ms is far more than required)
#define PIPE_TIMEOUT            500  

#define cStringMap              unordered_map<string, string>

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

// standard USB Setup request
struct kSetup
{
    uint8_t   bRequestType; // eSetupRecip | eSetupType | eDirection
    uint8_t   bRequest;     // GS_ReqGetCapabilities,... / DFU_RequDetach, DFU_RequGetStatus,...
    uint16_t  wValue;       // CAN Channel / ePinID for ELM_ReqGetPinStatus
    uint16_t  wIndex;       // Interface number (0 = Candlelight, 1 = DFU)
    uint16_t  wLength;      // Byte count
};

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
    // Linux   = "/sys/class/usb_device/usbdev1.4/device/1-1.2:1.0"
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

    // returns "Candlelight 2.5 - OleksiiDual - CAN FD Interface 2"
    // returns "canable gs_usb" for a legacy device
    string DisplayName()
    {
        // If a legacy Candlelight device does not expose a string in the Candlelight interface, 
        // Windows returns the Product string instead --> both are identical
        if (ms_Product == ms_Interface || ms_Interface.length() == 0)
            return ms_Product;

        return ms_Product + " - " + ms_Interface;
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
    static string   TrimRight(string s_String, char* s_Remove = " \n\r\t");
    static string   Format   (char* c_Format, ...);
    static string   MapLookup(cStringMap& i_Map, string& s_Key);
    static string   FormatHexBytes(uint8_t u8_Data[], int s32_DataLen);
    static string   FormatBcdVersion(uint32_t u32_Version);
    static uint64_t GetTickMilli();
};

}; // namesapce
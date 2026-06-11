
// https://netcult.ch/elmue/CANable%20Firmware%20Update

#pragma once

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

// Stuff to compile Candlelight_def.h on VS
#if defined(_MSC_VER)
    #define  __aligned(x)  
    #define  __packed 
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

#define CStringMap unordered_map<wstring, wstring>

// Windows error codes are far below 50000
// Define proprietary error codes here that are used also for Linux:
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

struct kSetup
{
    uint8_t   RequestType; // eSetupRecip | eSetupType | eDirection
    uint8_t   Request;     // GS_ReqGetCapabilities,... / DFU_RequDetach, DFU_RequGetStatus,...
    uint16_t  Value;       // CAN Channel / ePinID for ELM_ReqGetPinStatus
    uint16_t  Index;       // Interface number (0 = Candlelight, 1 = DFU)
    uint16_t  Length;      // Byte count
};

#pragma pack(pop)

// -------------------------------------------------------------

struct kDevInfo
{
    wchar_t                  mc_Vendor   [128];
    wchar_t                  mc_Product  [128];
    wchar_t                  mc_Serial   [128];
    wchar_t                  mc_Interface[128];
    uint8_t                  mu8_EndpointIN;
    uint8_t                  mu8_EndpointOUT;
    uint8_t                  mu8_Channel;
    uint16_t                 mu16_MaxPackSizeIN;
    uint16_t                 mu16_MaxPackSizeOUT;
    bool                     mb_IsElmueSoft;
    bool                     mb_SupportsFD;
    kDeviceDescriptor        mk_DeviceDescr;
    kInterfaceDescriptor     mk_InterfDescr;
    kCapabilityClassic       mk_Capability;
    kCapabilityFD            mk_CapabilityFD;
    kDeviceVersion           mk_DeviceVersion;
    kBoardInfo               mk_BoardInfo;
};

// A USB frame that was received on th IN pipe and is stored in the Rx FIFO
struct kUsbInPacket
{
    uint8_t   mu8_Buffer[MAX_BLOB_SIZE];
    uint32_t  mu32_BytesRead;
    uint32_t  mu32_Error;
    int64_t   ms64_OsTimestamp;  // Timestamp with 1µs precision from operating system
};

struct kUsbDevice
{
public:
    wstring  ms_Product;   // from Device Descriptor
    wstring  ms_SerialNo;  // from Device Descriptor
    wstring  ms_Interface; // from Interface Descriptor
    wstring  ms_DevPath;   // Operating System path to open the device
    int      ms32_Channel; // one-based channel (always 1 for devices that have only one CAN channel)

    kUsbDevice()
    {
        ms32_Channel = 0; // invalid
    }

    // returns "Candlelight 2.5 - OleksiiDual - CAN FD Interface 2"
    // returns "canable gs_usb" for a legacy device
    wstring DisplayName()
    {
        // If a legacy Candlelight device does not expose a string in the Candlelight interface, 
        // Windows returns the Product string instead --> both are identical
        if (ms_Product == ms_Interface)
            return ms_Product;

        return ms_Product + L" - " + ms_Interface;
    }

    // Sort by by Serial Number and then by Channel number
    bool operator<(const kUsbDevice& i_Dev2) const
    {
        if (ms_SerialNo != i_Dev2.ms_SerialNo)
            return ms_SerialNo < i_Dev2.ms_SerialNo;

        return ms32_Channel < i_Dev2.ms32_Channel;
    }
};

// -------------------------------------------------------------

class cUtils
{
public:
    static wstring  MakeUpper(wstring s_String);
    static wstring  TrimRight(wstring s_String, wchar_t* s_Remove = L" \n\r\t");
    static wstring  Format   (wchar_t* c_Format, ...);
    static wstring  MapLookup(CStringMap& i_Map, wstring& s_Key);
    static wstring  FormatHexBytes(uint8_t u8_Data[], int s32_DataLen);
    static wstring  FormatBcdVersion(uint32_t u32_Version);
    static uint64_t GetTickMilli();
};

}; // namesapce
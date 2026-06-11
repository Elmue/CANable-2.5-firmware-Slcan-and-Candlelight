
// https://netcult.ch/elmue/CANable%20Firmware%20Update

/*
NAMING CONVENTIONS which allow to see the type of a variable immediately without having to jump to the variable declaration:
 
     cName  for class    definitions
     tName  for type     definitions
     eName  for enum     definitions
     kName  for "konstruct" (struct) definitions (letter 's' already used for string)
   delName  for delegate definitions

    b_Name  for bool
    c_Name  for Char, also Color
    d_Name  for double
    e_Name  for enum variables
    f_Name  for function delegates, also float
    i_Name  for instances of classes
    k_Name  for "konstructs" (struct) (letter 's' already used for string)
	r_Name  for Rectangle
    s_Name  for strings
    o_Name  for objects

   s8_Name  for   signed  8 Bit (sbyte)
  s16_Name  for   signed 16 Bit (short)
  s32_Name  for   signed 32 Bit (int)
  s64_Name  for   signed 64 Bit (long)
   u8_Name  for unsigned  8 Bit (byte)
  u16_Name  for unsigned 16 bit (ushort)
  u32_Name  for unsigned 32 Bit (uint)
  u64_Name  for unsigned 64 Bit (ulong)

An additional "m" is prefixed for all member variables (e.g. ms_String)
*/

// =======================================================================================================
//
//  This class contains code for Windows.
//  Someone must re-write it for Linux
//  The WinUSB library must be replaced with the libusb library
//  The thread in this class can probably be removed when using lubusb
//  NOTE: This class uses WinUSB by purpose: 
//  The WinUSB driver is part of the orepating system and installed 100% automatically 
//  when connecting the device for the first time.
//  On the other hand when using libusb on Windows the user would be forced to download 
//  and install a driver manually that has no advantage over WinUSB.
//
// =======================================================================================================

#include "WinLibrary.h"
#include <setupapi.h>
#include <initguid.h> // DEVPKEY_Device_BusReportedDeviceDesc
#include <devpkey.h>  // DEVPKEY_Device_BusReportedDeviceDesc

#pragma comment(lib, "SetupApi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "WinUsb.lib")

using namespace CANable;

#define  LANGUAGE_ENGLISH_USA   0x409

// Interface 0 "{c15b4308-04d3-11e6-b3ea-6057189e6443}"
GUID GUID_CANDLELIGHT = { 0xc15b4308, 0x04d3, 0x11e6, { 0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43 }};

// Interface 1 "{c25b4308-04d3-11e6-b3ea-6057189e6443}"
// This GUID can be used to switch the device into DFU mode. Requires the CANable 2.5 firmware from ElmüSoft.
GUID GUID_CANDLE_DFU  = { 0xc25b4308, 0x04d3, 0x11e6, { 0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43 }};

HANDLE gh_ConsoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
HANDLE gh_ConsoleIn  = GetStdHandle(STD_INPUT_HANDLE);

// Constructor
OsLibrary::OsLibrary()
{
    mh_Device       = NULL;
    mh_WinUsb       = NULL;
    mh_ReceiveEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    mh_ThreadEvent  = CreateEvent(NULL, FALSE, FALSE, NULL);
    mb_ThreadRuns   = false;
    InitializeCriticalSection(&mk_Critical);
}

// Destructor
OsLibrary::~OsLibrary()
{
    CloseHandle(mh_ReceiveEvent);
    CloseHandle(mh_ThreadEvent);
}

// Called from Candlelight::Open()
uint32_t OsLibrary::Open(wstring s_DevicePath)
{
    if (mh_Device)
        return ERR_OPERATION_INVALID; // Already open

    ms64_PerfTimeStart  = 0;
    mu32_RxPipeErrors   = 0;
    mu32_TxPipeErrors   = 0;
    ms32_FifoCount      = 0;
    ms32_FifoReadIdx    = 0;
    mb_FifoOverflow     = false;
    mb_AbortThread      = false;
    memset(&mk_Info, 0, sizeof(mk_Info));

    // IMPORTANT:
    // Do NOT set FILE_SHARE_READ or FILE_SHARE_WRITE here!
    // This assures that any other application that tries to open the device at the same time will get ERROR_ACCESS_DENIED.
    // NOTE:
    // Here we enable Overlapped mode although we do not use a OVERLAPPED structure. This is unusual.
    // But it works here because we set a timeout with WinUsb_SetPipePolicy(PIPE_TRANSFER_TIMEOUT)
    mh_Device = CreateFileW(s_DevicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 
                            0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

    if (mh_Device == INVALID_HANDLE_VALUE)
    {
        uint32_t u32_Error = GetLastError();
        if (u32_Error == ERROR_ACCESS_DENIED)
            u32_Error =  ERR_DEVICE_IN_USE; // show a more intelligent error message than "Access denied"
        return u32_Error;
    }

    if (!WinUsb_Initialize(mh_Device, &mh_WinUsb))
    {
        // ERROR_NOT_ENOUGH_MEMORY: The device does not have a WinUSB driver installed
        uint32_t u32_Error = GetLastError();
        if (u32_Error == ERROR_NOT_ENOUGH_MEMORY)
            u32_Error =  ERR_NO_DRIVER;
        return u32_Error;
    }

    // Set timeout for control pipe (500 ms is far more than enough)
    uint32_t u32_Timeout = 500;
    if (!WinUsb_SetPipePolicy(mh_WinUsb, 0, PIPE_TRANSFER_TIMEOUT, sizeof(u32_Timeout), &u32_Timeout))
        return GetLastError();

    // Get Device Descriptor
    uint32_t u32_Read;
    if (!WinUsb_GetDescriptor(mh_WinUsb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, (uint8_t*)&mk_Info.mk_DeviceDescr, sizeof(kDeviceDescriptor), &u32_Read))
        return GetLastError();

    // Get Interface Descriptor
    // Windows uses a unique s_DevicePath for each interface. There is no need to specify an interface number here.
    // The device path defines which interface is opened with CreateFileW().
    // "{c15b4308-04d3-11e6-b3ea-6057189e6443}" opens interface 0
    // "{c25b4308-04d3-11e6-b3ea-6057189e6443}" opens interface 1
    if (!WinUsb_QueryInterfaceSettings(mh_WinUsb, 0, (USB_INTERFACE_DESCRIPTOR*)&mk_Info.mk_InterfDescr))
        return GetLastError();

    // Microsoft manipulates iProduct in the device descriptor to point to the string for the interface name.
    // In the vast majority of USB devices we find: iManufacturer = 1, iProduct = 2, iSerialNumber = 3.
    // WinUSB sets iProduct = iInterface which is in case of the Candlelight the string for interface 0.
    // We try to fix this here to get the string of the device descriptor instead of the interface descriptor.
    if (mk_Info.mk_DeviceDescr.iManufacturer == 1 && mk_Info.mk_DeviceDescr.iSerialNumber == 3)
        mk_Info.mk_DeviceDescr.iProduct = 2;

    ReadStringDescriptor(mk_Info.mk_DeviceDescr.iManufacturer, LANGUAGE_ENGLISH_USA, mk_Info.mc_Vendor);
    ReadStringDescriptor(mk_Info.mk_DeviceDescr.iProduct,      LANGUAGE_ENGLISH_USA, mk_Info.mc_Product);
    ReadStringDescriptor(mk_Info.mk_DeviceDescr.iSerialNumber, LANGUAGE_ENGLISH_USA, mk_Info.mc_Serial);
    ReadStringDescriptor(mk_Info.mk_InterfDescr.iInterface,    LANGUAGE_ENGLISH_USA, mk_Info.mc_Interface);

    // Get the 2 pipes of the Candlelight interface (the DFU interface has bNumEndpoints == 0)
    for (uint8_t P=0; P<mk_Info.mk_InterfDescr.bNumEndpoints; P++)
    {
        WINUSB_PIPE_INFORMATION k_PipeInfo;
        if (!WinUsb_QueryPipe(mh_WinUsb, 0, P, &k_PipeInfo))
            return GetLastError();

        if (k_PipeInfo.PipeType != UsbdPipeTypeBulk)
            return ERR_INVALID_DEVICE;

        if (k_PipeInfo.PipeId & DIR_In)
        {
            mk_Info.mu8_EndpointIN     = k_PipeInfo.PipeId;
            mk_Info.mu16_MaxPackSizeIN = k_PipeInfo.MaximumPacketSize;
        }
        else // OUT
        {
            mk_Info.mu8_EndpointOUT     = k_PipeInfo.PipeId;
            mk_Info.mu16_MaxPackSizeOUT = k_PipeInfo.MaximumPacketSize;
        }
    }
    return NO_ERROR;
}

// This is not called for the DFU interface which has no endpoints
uint32_t OsLibrary::StartPipes()
{
    uint8_t u8_True = 1;
    if (!WinUsb_SetPipePolicy(mh_WinUsb, mk_Info.mu8_EndpointIN, RAW_IO, sizeof(u8_True), &u8_True))
        return GetLastError();

    // Set timeout for OUT pipe (500 ms is far more than enough)
    // This timeout assures that pipe operations are not blocking eternally as an OVERLAPPED structure is not used.
    uint32_t u32_Timeout = 500;
    if (!WinUsb_SetPipePolicy(mh_WinUsb, mk_Info.mu8_EndpointOUT, PIPE_TRANSFER_TIMEOUT, sizeof(u32_Timeout), &u32_Timeout))
        return GetLastError();

    /*
    // The maximum TX size of the OUT pipe is 0x40000 = 256 kB
    uint32_t u32_MaxTransfer = 0;
    uint32_t u32_ValLength   = sizeof(u32_MaxTransfer);
    if (!WinUsb_GetPipePolicy(mh_WinUsb, mk_Info.mu8_EndpointOUT, MAXIMUM_TRANSFER_SIZE, &u32_ValLength, &u32_MaxTransfer))
        return GetLastError();
    */

    uint32_t u32_ThreadID;
    HANDLE h_Thread = CreateThread(0, 0, &PipeThreadStatic, this, 0, &u32_ThreadID);
    if (!h_Thread)
        return GetLastError();

    CloseHandle(h_Thread);

    return NO_ERROR;
}

// Called from Candlelight::Close()
void OsLibrary::Close()
{
    // abort PipeThread and wait until it has exited. Timeout is 1 second.
    for (int i=0; mb_ThreadRuns && i<100; i++)
    {
        mb_AbortThread = true;
        SetEvent(mh_ThreadEvent);
        Sleep(10);
    }

    if (mh_WinUsb)
    {
        WinUsb_Free(mh_WinUsb);
        mh_WinUsb = NULL;
    }

    if (mh_Device)
    {
        CloseHandle(mh_Device);
        mh_Device = NULL;
    }
}

// --------------------------------------------------------------------

// Read a string descriptor
uint32_t OsLibrary::ReadStringDescriptor(uint8_t u8_Index, uint16_t u16_LanguageID, wchar_t s_String[128])
{
    s_String[0] = 0;

    // If the descriptor does not define a string, the index is zero. This is not an error.
    if (u8_Index == 0)
        return NO_ERROR;

    // 256 bytes = 2 byte header + 127 Unicode chars
    uint8_t  u8_Buffer[256]; 
    uint32_t u32_Read;
    if (!WinUsb_GetDescriptor(mh_WinUsb, USB_STRING_DESCRIPTOR_TYPE, u8_Index, u16_LanguageID, u8_Buffer, sizeof(u8_Buffer), &u32_Read))
        return GetLastError();

    uint8_t u8_Length = u8_Buffer[0];
    uint8_t u8_Descr  = u8_Buffer[1];
    if (u8_Descr != USB_STRING_DESCRIPTOR_TYPE || u32_Read < 2 || u8_Length != u32_Read || (u32_Read & 1) > 0)
    {
        wcscpy_s(s_String, 128, L"*** CRIPPLED STRING ***");
        return ERR_INVALID_RX_DATA;
    }

    memcpy(s_String, u8_Buffer + 2, u32_Read - 2);
    return NO_ERROR;
}

// ===================================== CTRL Pipe =====================================

// ATTENTION: returns ERROR_NOACCESS if u8_Buffer is not in RAM !
uint32_t OsLibrary::ControlTransfer(kSetup* pk_Setup, uint8_t* u8_Buffer, uint32_t u32_BufLen, uint32_t* pu32_Transferred)
{
    if (!WinUsb_ControlTransfer(mh_WinUsb, *(WINUSB_SETUP_PACKET*)pk_Setup, u8_Buffer, u32_BufLen, pu32_Transferred, NULL))
        return GetLastError();

    return NO_ERROR;
}

// ===================================== OUT Pipe ======================================

uint32_t OsLibrary::WritePipeOut(uint8_t* u8_Transmit, uint32_t u32_TxLen)
{
    uint32_t u32_Transferred;
    if (!WinUsb_WritePipe(mh_WinUsb, mk_Info.mu8_EndpointOUT, u8_Transmit, u32_TxLen, &u32_Transferred, NULL))
    {
        mu32_TxPipeErrors ++;
        return GetLastError();
    }
    
    mu32_TxPipeErrors = 0;
    return NO_ERROR;
}

// ====================================== IN Pipe =======================================

// ------------------------------------------------------------------------------------------------------------------------------------
// IMPORTANT:
// WinUSB is different from other Windows API's.
// An overlapped read operation with WinUsb_ReadPipe() is totally different from the usual overlapped read operation on Windows.
// This extremely important detail is not documented by Microsoft, nor does Microsoft give us any useful sample code.
// Therefore you find this implemented totally wrong in Cangaroo and in Candle.NET on Github.
// You cannot use the typical scheme ReadPipe() --> ERROR_IO_PENDING --> WaitForSingleObject(Timeout) --> GetOverlappedResult().
// If you do this with a short timeout (50 ms) you will receive NOTHING !!!
// If you do it with a longer timeout (500 ms) it will work mostly, but some USB IN packets will be lost.
// To not lose USB packets the timeout for WaitForSingleObject() *MUST* be INIFINTE.
// The reason is that WinUSB starts polling the USB IN endpoint when you call WinUsb_ReadPipe().
// But when this operation is aborted by an elapsed timeout, any USB IN packet that was about to arrive will be dropped.
// WinUSB does NOT have an internal buffer to store packets that arrive between calls to WinUsb_ReadPipe().
// So the unusual is here that we use an overlapped read operation with an INFINITE timeout.
// This requires to run in a thread and the overlapped event is required to abort the thread.
// ------------------------------------------------------------------------------------------------------------------------------------

uint32_t OsLibrary::PipeThreadStatic(void* p_This)
{
    ((OsLibrary*)p_This)->PipeThreadMember();
    return 0;
}
void OsLibrary::PipeThreadMember()
{
    mb_AbortThread = false;
    mb_ThreadRuns  = true;
    ResetEvent(mh_ReceiveEvent);

    OVERLAPPED k_Overlapped = {0};
    k_Overlapped.hEvent = mh_ThreadEvent;

    // This thread is time critical
    // If Rx Events are not polled fast enough USB packets may get lost because WinUSB does not have an internal Rx buffer.
    // WinUsb_ReadPipe() must be called as fast as possible again after a USB packet was received.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (!mb_AbortThread)
    {
        EnterCriticalSection(&mk_Critical);
            if (ms32_FifoCount >= RX_FIFO_MAX_COUNT)
                mb_FifoOverflow = true;
        LeaveCriticalSection(&mk_Critical);

        // if an overflow occurred, stop reading USB packets and inform the caller that it is polling too slowly.
        if (mb_FifoOverflow)
        {
            Sleep(50);
            continue;
        }

        EnterCriticalSection(&mk_Critical);
            int s32_FifoWriteIdx  = (ms32_FifoReadIdx + ms32_FifoCount) % RX_FIFO_MAX_COUNT;
            kUsbInPacket* pk_FifoWrite = &mk_RxFifo[s32_FifoWriteIdx];
        LeaveCriticalSection(&mk_Critical);

        uint32_t u32_Read  = 0;
        uint32_t u32_Error = NO_ERROR;
        if (!WinUsb_ReadPipe(mh_WinUsb, mk_Info.mu8_EndpointIN, pk_FifoWrite->mu8_Buffer, sizeof(pk_FifoWrite->mu8_Buffer), NULL, &k_Overlapped))
        {
            u32_Error = GetLastError();
            if (u32_Error == ERROR_IO_PENDING)
            {
                u32_Error = NO_ERROR;

                // mh_ThreadEvent = k_Overlapped.hEvent is set when a USB IN packet was received and in Close() to abort the thread
                uint32_t u32_Result = WaitForSingleObject(mh_ThreadEvent, INFINITE);
                if (mb_AbortThread)
                    break;

                switch (u32_Result)
                {
                    case WAIT_TIMEOUT: // This should never happen with timeout = INFINITE
                        u32_Error = ERR_TIMEOUT;
                        break;

                    case WAIT_OBJECT_0:
                        if (WinUsb_GetOverlappedResult(mh_WinUsb, &k_Overlapped, &u32_Read, FALSE))
                            mu32_RxPipeErrors = 0;
                        else
                            u32_Error = GetLastError();
                        break;

                    default: // WAIT_FAILED (I have never seen this error, but just in case...)
                        u32_Error = GetLastError();
                        break;
                }
            }
            else assert(FALSE); // this should never happen
        }
        else assert(FALSE); // this should never happen

        pk_FifoWrite->mu32_BytesRead   = u32_Read;
        pk_FifoWrite->mu32_Error       = u32_Error;
        pk_FifoWrite->ms64_OsTimestamp = GetTimestamp();

        // Increment write index for the next ReadPipe, leave read index unchanged
        EnterCriticalSection(&mk_Critical);
            ms32_FifoCount ++;
            SetEvent(mh_ReceiveEvent);
        LeaveCriticalSection(&mk_Critical);

        if (u32_Error)
        {
            // If the CANable has been disconnected an error ERROR_BAD_COMMAND or ERROR_GEN_FAILURE will be reported in each loop.
            // This high priority thread must be slowed down to avoid that it consumes
            // a lot of CPU power running in an endless loop and to avoid that the FIFO overflows with errors.
            mu32_RxPipeErrors ++;
            Sleep(50);
        }
    } // while
    mb_ThreadRuns = false;
}

// Get the next frame from the Rx FIFO and copy it to pk_UsbInPacket.
uint32_t OsLibrary::ReadPipeIn(uint32_t u32_Timeout, kUsbInPacket* pk_UsbInPacket)
{
    EnterCriticalSection(&mk_Critical);
        kUsbInPacket* pk_FifoRead = &mk_RxFifo[ms32_FifoReadIdx];
        int s32_Available = ms32_FifoCount;
        if (s32_Available > 0)
            ResetEvent(mh_ReceiveEvent);
    LeaveCriticalSection(&mk_Critical);

    if (s32_Available == 0) // nothing received
    {
        // After all messages in the FIFO have been returned inform once about the FIFO overflow.
        if (mb_FifoOverflow)
        {
            EnterCriticalSection(&mk_Critical);
                mb_FifoOverflow = false;
            LeaveCriticalSection(&mk_Critical);
            return ERR_RX_FIFO_OVERFLOW;
        }

        uint32_t u32_Result = WaitForSingleObject(mh_ReceiveEvent, u32_Timeout);
        if (u32_Result == WAIT_TIMEOUT)
            return ERR_TIMEOUT;

        EnterCriticalSection(&mk_Critical);
            s32_Available = ms32_FifoCount;
        LeaveCriticalSection(&mk_Critical);

        if (s32_Available == 0)
            return ERR_TIMEOUT;
    }

    uint32_t u32_Error = pk_FifoRead->mu32_Error;
    
    if (u32_Error == NO_ERROR)
        memcpy(pk_UsbInPacket, pk_FifoRead, sizeof(kUsbInPacket));

    EnterCriticalSection(&mk_Critical);
        ms32_FifoReadIdx = (ms32_FifoReadIdx + 1) % RX_FIFO_MAX_COUNT;
        ms32_FifoCount --;
    LeaveCriticalSection(&mk_Critical);

    return u32_Error;
}

// =================================== Enumerate USB Devices ==================================

// Returns device name, serial and path like "\\?\USB#VID_1D50&PID_606F&MI_00#7&20E43BBC&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
// This function can also enumerate the devices in DFU mode using GUID_CANDLE_DFU, but only if the device has the ElmüSoft firmware.
// All legacy fimrware versions were buggy and unable to send the two Microsoft OS descriptors correctly, so the driver is not installed.
uint32_t OsLibrary::EnumDevices(bool b_Candlelight, vector<kUsbDevice>* pi_Devices)
{
    CStringMap i_Serials;
    EnumSerialNumbers(i_Serials); // ignore error

    GUID* pk_Guid = b_Candlelight ? &GUID_CANDLELIGHT : &GUID_CANDLE_DFU;

    // Enumerate all USB devices with the given GUID that are currently connected
    HDEVINFO h_DevInfo = SetupDiGetClassDevs(pk_Guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h_DevInfo == INVALID_HANDLE_VALUE) 
        return GetLastError();

    HDEVINFO h_ParentInfo = SetupDiCreateDeviceInfoList(NULL, NULL);
    if (h_ParentInfo == INVALID_HANDLE_VALUE) 
    {
        SetupDiDestroyDeviceInfoList(h_DevInfo);
        return GetLastError();
    }

    uint32_t u32_Error = NO_ERROR;
    SP_DEVICE_INTERFACE_DATA k_InterfaceData;
    k_InterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    SP_DEVINFO_DATA k_DevicInfo;
    k_DevicInfo.cbSize = sizeof(SP_DEVINFO_DATA);

    uint8_t u8_DetailBuf[2000];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W* pk_DetailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)u8_DetailBuf;
    pk_DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    DEVPROPTYPE u32_PropType;
    uint32_t    u32_RequSize;

    wchar_t c_Interface[128]; // USB Interface string                  (max 127 unicode chars)
    wchar_t c_Product  [128]; // USB device descriptor product string  (max 127 unicode chars)
    wchar_t c_Container[50];
    wchar_t c_Parent   [256];

    for (int Idx=0; true; Idx++)
    {
        if (!SetupDiEnumDeviceInterfaces(h_DevInfo, NULL, pk_Guid, Idx, &k_InterfaceData)) 
        {
            u32_Error = GetLastError();
            if (u32_Error == ERROR_NO_MORE_ITEMS)
                u32_Error =  NO_ERROR; // All existing devices have been enumerated. This is not an error.
            break;
        }

        // Get the NT path of the device that will be passed to CreateFile()
        if (!SetupDiGetDeviceInterfaceDetailW(h_DevInfo, &k_InterfaceData, pk_DetailData, sizeof(u8_DetailBuf), 
                                              &u32_RequSize, &k_DevicInfo)) 
        {
            u32_Error = GetLastError();
            continue;
        }

        // Get the 'ContainerID' GUID string (since Windows 7) which is identical for all interfaces of the same device
        if (!SetupDiGetDeviceRegistryPropertyW(h_DevInfo, &k_DevicInfo, SPDRP_BASE_CONTAINERID, NULL, 
                                               (uint8_t*)c_Container, sizeof(c_Container), NULL))
        {
            u32_Error = GetLastError();
            continue;
        }

        // Get the Interface string from Interface Descriptor (max USB string descriptor length = 127 Unicode chars)
        // If a legacy interface descriptor has iInterface == 0 (no string available) this will return the product string instead.
        if (!SetupDiGetDevicePropertyW(h_DevInfo, &k_DevicInfo, &DEVPKEY_Device_BusReportedDeviceDesc, &u32_PropType, 
                                      (uint8_t*)c_Interface, sizeof(c_Interface), &u32_RequSize, 0))
        {
            u32_Error = GetLastError();
            continue;
        }

        // Go one level up from USB interface to USB device --> c_Parent = "USB\VID_1D50&PID_606F\208A347D4B4550142"
        if (!SetupDiGetDevicePropertyW(h_DevInfo, &k_DevicInfo, &DEVPKEY_Device_Parent, &u32_PropType, 
                                       (PBYTE)c_Parent, sizeof(c_Parent), &u32_RequSize, 0))
        {
            u32_Error = GetLastError();
            continue;
        }

        if (!SetupDiOpenDeviceInfoW(h_ParentInfo, c_Parent, NULL, 0, &k_DevicInfo))
        {
            u32_Error = GetLastError();
            continue;
        }

        // Get the Product string from Device Descriptor (max USB string descriptor length = 127 Unicode chars)
        if (!SetupDiGetDevicePropertyW(h_ParentInfo, &k_DevicInfo, &DEVPKEY_Device_BusReportedDeviceDesc, &u32_PropType, 
                                      (uint8_t*)c_Product, sizeof(c_Product), &u32_RequSize, 0))
        {
            u32_Error = GetLastError();
            continue;
        }

        // ---------------------

        kUsbDevice k_UsbDev;
        k_UsbDev.ms_Product   = c_Product;   // "Candlelight 2.5 - OleksiiDual"
        k_UsbDev.ms_Interface = c_Interface; // "CAN FD Interface 2"
        k_UsbDev.ms_DevPath   = cUtils::MakeUpper(pk_DetailData->DevicePath); // "\\?\usb#vid_1d50&pid_606f&mi_00#7&1b930f3c&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
        wstring  s_Container  = cUtils::MakeUpper(c_Container); // "{2c7d6257-7635-5dc8-ad4f-f4d3ad209925}"
        k_UsbDev.ms_SerialNo  = cUtils::MapLookup(i_Serials, s_Container);

        // Append interface number for multi-interface (MI) adapters
        int s32_Pos = (int)k_UsbDev.ms_DevPath.find(L"&MI_0");
        if (s32_Pos > 0)
        {
            // MI_00 --> Candlelight 1
            // MI_01 --> DFU
            // MI_02 --> Candlelight 2
            // MI_03 --> Candlelight 3
            k_UsbDev.ms32_Channel = _wtoi(k_UsbDev.ms_DevPath.substr(s32_Pos + 5, 1).c_str());
            if (k_UsbDev.ms32_Channel == 0)
                k_UsbDev.ms32_Channel = 1;  // display one-based interface number           
        }

        pi_Devices->push_back(k_UsbDev);
    }

    // Sort by serial number and then by interface number
    sort(pi_Devices->begin(), pi_Devices->end());

    SetupDiDestroyDeviceInfoList(h_DevInfo);    // free memory
    SetupDiDestroyDeviceInfoList(h_ParentInfo); // free memory
    return u32_Error;
}

// Get the serial numbers of all Candlelight devices
// "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\USB\VID_1D50&PID_606F\2066349E39455006"
// The last part is the serial number: "2066349E39455006"
// return a Map with ContainerID --> Serial Number
uint32_t OsLibrary::EnumSerialNumbers(CStringMap& i_Serials)
{
    wstring s_RootPath = L"System\\CurrentControlSet\\Enum\\USB\\VID_1D50&PID_606F";

    HKEY  h_RootKey = 0;
    uint32_t u32_Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, s_RootPath.c_str(), 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &h_RootKey);
    if (u32_Error != NO_ERROR) 
        return u32_Error;

    wchar_t c_Serial[100];
    for (uint32_t i=0; TRUE; i++)
    {
        c_Serial[0] = 0;
        u32_Error = RegEnumKeyW(h_RootKey, i, c_Serial, sizeof(c_Serial) / 2);
        if (u32_Error == ERROR_NO_MORE_ITEMS)
            break;

        if (u32_Error != NO_ERROR || !c_Serial[0])
        {
            assert(false);
            continue;
        }
        
        // All interfaces of a multi-interface device have the same ContainerID
        wstring s_Container;
        u32_Error = RegReadString(HKEY_LOCAL_MACHINE, (s_RootPath + L"\\" + c_Serial).c_str(), L"ContainerID", &s_Container);
        if (u32_Error != NO_ERROR)
        {
            assert(false);
            continue;
        }

        s_Container = cUtils::MakeUpper(s_Container);
        i_Serials[s_Container] = c_Serial;
    }

    RegCloseKey(h_RootKey);
    return 0;
}

// read a string from the registry (max 1000 chars)
uint32_t OsLibrary::RegReadString(HKEY h_Class, const wchar_t* u16_Path, const wchar_t* u16_Entry, wstring* ps_Value)
{
    *ps_Value = L"";

    HKEY h_Key; 
    uint32_t u32_Error = RegOpenKeyExW(h_Class, u16_Path, 0, KEY_QUERY_VALUE, &h_Key);
    if (u32_Error)
        return u32_Error;

    wchar_t  u16_Buffer[1000];
    uint32_t u32_Type;                      // OUT
    uint32_t u32_Size = sizeof(u16_Buffer); // IN = 2000 --> OUT = count of bytes read
    u32_Error = RegQueryValueExW(h_Key, u16_Entry, 0, &u32_Type, (uint8_t*)u16_Buffer, &u32_Size);

    RegCloseKey(h_Key);

    u16_Buffer[u32_Size / 2] = 0;
    *ps_Value = u16_Buffer;
    return u32_Error;
}

// ===================================== Console =====================================

// Set console title, buffer size and window size
void OsLibrary::SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, wstring s_Title)
{
    SetConsoleTitle(s_Title.c_str());

    COORD k_Size = {s16_BufWidth, s16_BufHeight}; 
    SetConsoleScreenBufferSize(gh_ConsoleOut, k_Size);

    COORD k_Max = GetLargestConsoleWindowSize(gh_ConsoleOut);

    SMALL_RECT k_Wnd = {0}; 
    k_Wnd.Right  = min(k_Max.X, s16_WndWidth)  - 4;
    k_Wnd.Bottom = min(k_Max.Y, s16_WndHeight) - 4;
    SetConsoleWindowInfo(gh_ConsoleOut, TRUE, &k_Wnd);
}

// Print coloured console output (max 2000 chars!)
void OsLibrary::PrintConsole(uint16_t u16_Color, wstring s_Format, ...)
{
    SetConsoleTextAttribute(gh_ConsoleOut, u16_Color); 
    va_list  args;
    va_start(args, s_Format);

    WCHAR u16_Buffer[2000];
    uint32_t u32_Len = vswprintf(u16_Buffer, 2000, s_Format.c_str(), args);

    // WriteConsole() is significantly faster than wprinf() or vwprintf(), which need 10 ms per line!
    uint32_t u32_Written;
    WriteConsoleW(gh_ConsoleOut, u16_Buffer, u32_Len, &u32_Written, NULL);
}

// Check if the user has pressed the ENTER key in the console (non-blocking function)
bool OsLibrary::CheckConsoleEnterPressed()
{
    INPUT_RECORD k_Buffer;
    uint32_t     u32_Events;
    if (!PeekConsoleInput(gh_ConsoleIn, &k_Buffer, 1, &u32_Events))
        return false;
    
    if (u32_Events == 0)
        return false;
    
    // The event must be removed from the input buffer, otherwise it is reported eternally.
    ReadConsoleInput(gh_ConsoleIn, &k_Buffer, 1, &u32_Events);  

    return (k_Buffer.EventType == KEY_EVENT  && 
            k_Buffer.Event.KeyEvent.bKeyDown &&
            k_Buffer.Event.KeyEvent.wVirtualKeyCode == VK_RETURN);
}

// Blocks until the user hits a key, returns the ASCII code
int OsLibrary::WaitConsoleChar()
{
    return _getch();
}

// ===================================== Helpers =====================================

// Create a timestamp with 1 µs precision.
// The returned timestamp starts at zero when the device is opened.
// It is recommended to turn off transmssion of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
// Then this function is used as a replacement to generate a timestamp on reception of a packet and when sending a packet.
int64_t OsLibrary::GetTimestamp()
{
    static int64_t s64_Frequency = 0; 

    // The performance counter runs inside the CPU and the frequency is identical over all CPU cores and never changes.
    // The performance counter frequency depends on the CPU and the operating system, mostly above 3 MHz
    if (s64_Frequency == 0 || ms64_PerfTimeStart == 0)
    {
	    QueryPerformanceFrequency((LARGE_INTEGER*)&s64_Frequency);
        QueryPerformanceCounter  ((LARGE_INTEGER*)&ms64_PerfTimeStart);
    }

	int64_t s64_Counter;
	QueryPerformanceCounter((LARGE_INTEGER*)&s64_Counter);
	return (s64_Counter - ms64_PerfTimeStart) * 1000000 / s64_Frequency;
}

// Format Windows API error
// u32_Error = ERROR_ACCESS_DENIED --> returns "Access is denied" in the language of the operating system.
wstring OsLibrary::GetErrorMessage(uint32_t u32_Error)
{
    const uint32_t FLAGS = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    wchar_t c_Buffer[1000];
    FormatMessageW(FLAGS, 0, u32_Error, 0, c_Buffer, 1000, 0);
    return cUtils::TrimRight(c_Buffer);
}

// Strings longer than 1000 characters are not supported. This will never happen.
wstring OsLibrary::Utf8ToUnicode(const char* s8_UTF8, int s32_StrLen) // s32_StrLen = - 1
{
    if (s32_StrLen < 0)
        s32_StrLen = strlen(s8_UTF8);

    wchar_t c_Unicode[1000];
    int s32_Written = MultiByteToWideChar(CP_UTF8, 0, s8_UTF8, s32_StrLen, c_Unicode, sizeof(c_Unicode) / 2);
    c_Unicode[s32_Written] = 0;
    return c_Unicode;
}
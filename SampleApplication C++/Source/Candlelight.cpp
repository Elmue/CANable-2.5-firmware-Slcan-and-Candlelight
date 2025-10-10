
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


#include "stdafx.h"
#include "Candlelight.h"
#include <assert.h>
#include <setupapi.h>
#pragma comment(lib, "SetupApi.lib")
#pragma comment(lib, "WinUsb.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// "{c15b4308-04d3-11e6-b3ea-6057189e6443}"
GUID GUID_CANDLELIGHT = { 0xc15b4308, 0x04d3, 0x11e6, { 0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43 }};

// "{c25b4308-04d3-11e6-b3ea-6057189e6443}"
// This GUID can be used to switch the device into DFU mode. Requires the CANable 2.5 firmware from ElmüSoft.
GUID GUID_CANDLE_DFU  = { 0xc25b4308, 0x04d3, 0x11e6, { 0xb3, 0xea, 0x60, 0x57, 0x18, 0x9e, 0x64, 0x43 }};

// This class implements the new CANable 2.5 ElmüSoft protocol.
Candlelight::Candlelight()
{
    mh_Device       = NULL;
    mh_WinUsb       = NULL;
    mh_ThreadEvent  = NULL;
    mh_ReceiveEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    ms32_FifoCount  = 0;
    mb_FifoOverflow = false;
    mb_InitDone     = false;
    mb_AbortThread  = false;
}

Candlelight::~Candlelight()
{
    Close();
    CloseHandle(mh_ReceiveEvent);
}

void Candlelight::Close()
{
    // abort ReadPipeThread and wait until it has exited (mh_ThreadEvent == NULL). Timeout is 1 second.
    for (int i=0; mh_ThreadEvent && i<100; i++)
    {
        mb_AbortThread = true;
        SetEvent(mh_ThreadEvent);
        Sleep(10);
    }

    if (mh_WinUsb)
    {
        Reset(); // stop the CAN interface and reset all variables in the firmware
        WinUsb_Free(mh_WinUsb);
        mh_WinUsb = NULL;
    }
    if (mh_Device)
    {
        CloseHandle(mh_Device);
        mh_Device = NULL;
    }
    mb_InitDone = false; 
}

// --------------------------------------------------------------------

// STEP 1)
// Returns device name and device path like "\\?\USB#VID_1D50&PID_606F&MI_00#7&20E43BBC&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
// This function can also enumerate the devices in DFU mode using GUID_CANDLE_DFU, but only if the device has the ElmüSoft firmware.
// All legacy fimrware versions were buggy and unable to send the two Microsoft OS descriptors correctly, so the driver is not installed.
DWORD Candlelight::EnumDevices(BYTE u8_Interface, CStringArray* pi_DispNames, CStringArray* pi_DevicePaths)
{
    GUID* pk_Guid;
    switch (u8_Interface)
    {
        case 0: pk_Guid = &GUID_CANDLELIGHT; break; // Interface 0 = Candlelight
        case 1: pk_Guid = &GUID_CANDLE_DFU;  break; // Interface 1 = Firmware Update
        default: return ERROR_INVALID_PARAMETER;
    }
    mu8_Interface = u8_Interface;

    // Enumerate all USB devices with the given GUID that are currently connected
    HDEVINFO h_DevInfo = SetupDiGetClassDevs(pk_Guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h_DevInfo == INVALID_HANDLE_VALUE) 
        return GetLastError();

    DWORD u32_Error = ERROR_SUCCESS;
    SP_DEVICE_INTERFACE_DATA k_InterfaceData;
    k_InterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    SP_DEVINFO_DATA k_DevicInfo;
    k_DevicInfo.cbSize = sizeof(SP_DEVINFO_DATA);

    BYTE u8_DetailBuf[2000];
    SP_DEVICE_INTERFACE_DETAIL_DATA_W* pk_DetailData = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)u8_DetailBuf;
    pk_DetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

    BYTE u8_NameBuf[256];

    for (int Idx=0; true; Idx++)
    {
        if (!SetupDiEnumDeviceInterfaces(h_DevInfo, NULL, pk_Guid, Idx, &k_InterfaceData)) 
        {
            u32_Error = GetLastError();
            if (u32_Error == ERROR_NO_MORE_ITEMS)
                u32_Error =  ERROR_SUCCESS; // All existing devices have been enumerated. This is not an error.
            break;
        }

        // Get the NT path of the device that is required for CreateFile()
        DWORD u32_RequSize;
        if (!SetupDiGetDeviceInterfaceDetailW(h_DevInfo, &k_InterfaceData, pk_DetailData, sizeof(u8_DetailBuf), &u32_RequSize, &k_DevicInfo)) 
        {
            u32_Error = GetLastError();
            break;
        }

        // Get the display name
        if (!SetupDiGetDeviceRegistryPropertyW(h_DevInfo, &k_DevicInfo, SPDRP_DEVICEDESC, NULL, u8_NameBuf, sizeof(u8_NameBuf), NULL))
        {
            u32_Error = GetLastError();
            break;
        }

        pi_DispNames  ->Add((const WCHAR*)u8_NameBuf);
        pi_DevicePaths->Add(pk_DetailData->DevicePath);
    }

    SetupDiDestroyDeviceInfoList(h_DevInfo); // free memory
    return u32_Error;
}

// --------------------------------------------------------------------

// STEP 3)
// Initialize WinUSB and get the Candlelight structures with board info, capabilities, etc from the firmware
DWORD Candlelight::Open(CString s_DevicePath)
{
    if (mh_Device)
        return ERROR_INVALID_OPERATION; // Already open

    mu8_EndpointIN      = 0;
    mu8_EndpointOUT     = 0;
    mu16_MaxPackSizeIN  = 0;
    mu16_MaxPackSizeOUT = 0;
    mu8_EchoMarker      = 0;
    mu32_McuTimeStart   = 0;
    ms32_FifoCount      = 0;
    ms32_FifoReadIdx    = 0;
    mh_ThreadEvent      = NULL;
    mb_FifoOverflow     = false;
    mb_SupportsFD       = false;
    mb_BaudFDSet        = false;
    mb_TxOverflow       = false;
    mb_InitDone         = false;
    mb_Started          = false;
    me_LastError        = FBK_Success;
    memset(&mk_DeviceDescr,   0, sizeof(mk_DeviceDescr));
    memset(&mk_Capability,    0, sizeof(mk_Capability));
    memset(&mk_CapabilityFD,  0, sizeof(mk_CapabilityFD));
    memset(&mk_DeviceVersion, 0, sizeof(mk_DeviceVersion));
    memset(&mk_BoardInfo,     0, sizeof(mk_BoardInfo));
    memset(&mk_EchoFrames,    0, sizeof(mk_EchoFrames));
    QueryPerformanceCounter((LARGE_INTEGER*)&ms64_WinTimeStart);

    SYSTEMTIME k_Now;
    GetLocalTime(&k_Now);
    ms64_ClockOffset = (((((__int64)k_Now.wHour * 60) + k_Now.wMinute) * 60 + k_Now.wSecond) * 1000 + k_Now.wMilliseconds) * 100;

    // IMPORTANT:
    // Do NOT set FILE_SHARE_READ or FILE_SHARE_WRITE here!
    // This assures that any other application that tries to open the device at the same time will get ERROR_ACCESS_DENIED.
    // NOTE:
    // Here we enable Overlapped mode although we do not use a OVERLAPPED structure. This is unusual.
    // But it works here because we set a timeout with WinUsb_SetPipePolicy(PIPE_TRANSFER_TIMEOUT)
    mh_Device = CreateFileW(s_DevicePath, GENERIC_READ | GENERIC_WRITE, 
                            0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

    if (mh_Device == INVALID_HANDLE_VALUE)
        return GetLastError();

    // ERROR_NOT_ENOUGH_MEMORY: The device does not have a WinUSB driver installed
    if (!WinUsb_Initialize(mh_Device, &mh_WinUsb))
        return GetLastError();

    // Set timeout for control pipe (500 ms is far more than enough)
    DWORD u32_Timeout = 500;
    if (!WinUsb_SetPipePolicy(mh_WinUsb, 0, PIPE_TRANSFER_TIMEOUT, sizeof(u32_Timeout), &u32_Timeout))
        return GetLastError();

    DWORD u32_Read;
    if (!WinUsb_GetDescriptor(mh_WinUsb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, (BYTE*)&mk_DeviceDescr, sizeof(mk_DeviceDescr), &u32_Read))
        return GetLastError();

    // WinUSB manipulates iProduct in the device descriptor to point to the string for the interface name.
    // In the vast majority of USB devices we find: iManufacturer = 1, iProduct = 2, iSerialNumber = 3.
    // WinUSB sets iProduct = iInterface (20) which is in case of the Candlelight the string for interface 0.
    // We try to fix this here.
    if (mk_DeviceDescr.iManufacturer == 1 && mk_DeviceDescr.iSerialNumber == 3)
        mk_DeviceDescr.iProduct = 2;

    // Language ID 0x409 = english
    // Ignore errors as these strings are not important
    ReadStringDescriptor(mk_DeviceDescr.iManufacturer, 0x409, &ms_Vendor);
    ReadStringDescriptor(mk_DeviceDescr.iProduct,      0x409, &ms_Product);
    ReadStringDescriptor(mk_DeviceDescr.iSerialNumber, 0x409, &ms_Serial);

    // The DFU interface has no interface IN / OUT endpoints. It supports only SETUP requests.
    if (mu8_Interface == 1)
    {
        mb_InitDone = true;
        return ERROR_SUCCESS;
    }

    // --------------------------------------------------------------------

    // Get interface descriptor
    // Windows uses a unique s_DevicePath for each interface. There is no need to specify an interface number here.
    // The device path defines which interface is opened with CreateFileW().
    // "{c15b4308-04d3-11e6-b3ea-6057189e6443}" opens interface 0
    // "{c25b4308-04d3-11e6-b3ea-6057189e6443}" opens interface 1
    USB_INTERFACE_DESCRIPTOR k_InterfDescr;
    if (!WinUsb_QueryInterfaceSettings(mh_WinUsb, 0, &k_InterfDescr))
        return GetLastError();

    // There must be exactly 2 endpoints: IN and OUT
    if (k_InterfDescr.bNumEndpoints != 2)
        return ERROR_INVALID_DEVICE;

    // iterate the two pipes
    for (BYTE P=0; P<2; P++)
    {
        WINUSB_PIPE_INFORMATION k_PipeInfo;
        if (!WinUsb_QueryPipe(mh_WinUsb, 0, P, &k_PipeInfo))
            return GetLastError();

        if (k_PipeInfo.PipeType != UsbdPipeTypeBulk)
            return ERROR_INVALID_DEVICE;

        if (k_PipeInfo.PipeId & DIR_In)
        {
            mu8_EndpointIN     = k_PipeInfo.PipeId;
            mu16_MaxPackSizeIN = k_PipeInfo.MaximumPacketSize;
        }
        else // OUT
        {
            mu8_EndpointOUT     = k_PipeInfo.PipeId;
            mu16_MaxPackSizeOUT = k_PipeInfo.MaximumPacketSize;
        }
    }

    BYTE u8_True = 1;
    if (!WinUsb_SetPipePolicy(mh_WinUsb, mu8_EndpointIN, RAW_IO, sizeof(u8_True), &u8_True))
        return GetLastError();

    // Set timeout for OUT pipe (500 ms is far more than enough)
    // This timeout assures that pipe operations are not blocking eternally as an OVERLAPPED structure is not used.
    u32_Timeout = 500;
    if (!WinUsb_SetPipePolicy(mh_WinUsb, mu8_EndpointOUT, PIPE_TRANSFER_TIMEOUT, sizeof(u32_Timeout), &u32_Timeout))
        return GetLastError();

    // --------------------------------------------------------------------

    // Reset() should always be the first command.
    // The device may still be open --> close it.
    // And the CANable 2.5 firmware allows to set ELM_DevFlagProtocolElmue which enables debug messages at the very beginnning.
    DWORD u32_Error = Reset();
    if (u32_Error)
        return u32_Error;

    // GS_ReqGetCapabilities is a legacy commmand supported by all Candlelight's
    u32_Error = CtrlTransfer(DIR_In, GS_ReqGetCapabilities, 0, &mk_Capability, sizeof(mk_Capability));
    if (u32_Error)
        return u32_Error;

    mb_LegacyFirmware = (mk_Capability.feature & ELM_DevFlagProtocolElmue) == 0;

    mb_SupportsFD = ((mk_Capability.feature & GS_DevFlagCAN_FD) && 
                     (mk_Capability.feature & GS_DevFlagBitTimingFD));

    if (mb_SupportsFD)
    {
        // GS_ReqGetCapabilitiesFD is a legacy commmand supported by all Candlelight's
        u32_Error = CtrlTransfer(DIR_In, GS_ReqGetCapabilitiesFD, 0, &mk_CapabilityFD, sizeof(mk_CapabilityFD));
        if (u32_Error)
            return u32_Error;
    }

    // GS_ReqGetDeviceVersion is a legacy commmand supported by all Candlelight's
    u32_Error = CtrlTransfer(DIR_In, GS_ReqGetDeviceVersion, 0, &mk_DeviceVersion, sizeof(mk_DeviceVersion));
    if (u32_Error)
        return u32_Error;

    if (mb_LegacyFirmware)
        return ERROR_INVALID_FIRMWARE;

    // ELM_ReqGetBoardInfo requires ElmüSoft firmware
    u32_Error = CtrlTransfer(DIR_In, ELM_ReqGetBoardInfo, 0, &mk_BoardInfo, sizeof(mk_BoardInfo));
    if (u32_Error)
        return u32_Error;

    DWORD u32_ThreadID;
    HANDLE h_Thread = CreateThread(0, 0, &ReadPipeThreadStatic, this, 0, &u32_ThreadID);
    if (!h_Thread)
        return GetLastError();

    CloseHandle(h_Thread);

    mb_InitDone = true;
    return ERROR_SUCCESS;
}

// Read a string descriptor (private)
DWORD Candlelight::ReadStringDescriptor(BYTE u8_Index, WORD u16_LanguageID, CString* ps_String)
{
    WCHAR u16_Buffer[128]; // 128 WCHARs = 256 bytes
    DWORD u32_Read;
    if (!WinUsb_GetDescriptor(mh_WinUsb, USB_STRING_DESCRIPTOR_TYPE, u8_Index, u16_LanguageID, (BYTE*)u16_Buffer, 256, &u32_Read))
        return GetLastError();

    // Byte 0 = length of the entire descriptor in bytes
    // Byte 1 = descriptor type = string (always 3)
    BYTE u8_ByteLen = (BYTE)u16_Buffer[0];

    // Add zero termination
    u16_Buffer[u8_ByteLen / 2] = 0;
    *ps_String = u16_Buffer + 1; // skip first 2 bytes
    return ERROR_SUCCESS;
}

// --------------------------------------------------------------------

// STEP 4)
// Please read "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder Documentation
// returns the formatted baudrate and samplepoint in s_Display
DWORD Candlelight::SetBitrate(bool b_FD, int s32_BRP, int s32_Seg1, int s32_Seg2, CString* ps_Display)
{
    if (!mb_InitDone)
        return ERROR_INVALID_OPERATION;

    if (b_FD && !mb_SupportsFD)
        return ERROR_INVALID_OPERATION; // CAN FD not supported
    
    // NOTE:
    // It is not necessary to check if BRP, Seg1, Seg2 are in the allowed range defined in kTimeMinMax in the Capabilities.
    // If an inalid value is sent the firmware will return an error.
    // The values in kTimeMinMax are only required if you write an alorithm that calculates BRP, Seg1, Seg2
    // automatically from a given baudrate and samplepoint.

    kBitTiming k_Timing;
    k_Timing.brp  = s32_BRP;  // bitrate prescaler
    k_Timing.prop = 0;        // Propagation segment, not used, this is already included in Segment 1
    k_Timing.seg1 = s32_Seg1; // Time Segment 1 (Time quantums before samplepoint)
    k_Timing.seg2 = s32_Seg2; // Time Segment 2 (Time quantums after  samplepoint)
    k_Timing.sjw  = min(s32_Seg1, s32_Seg2); // Synchronization Jump Width (see "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder "Documentation")

    eUsbRequest e_Requ = b_FD ? GS_ReqSetBitTimingFD : GS_ReqSetBitTiming;
    DWORD u32_Error = CtrlTransfer(DIR_Out, e_Requ, 0, &k_Timing, sizeof(k_Timing));
    if (u32_Error)
        return u32_Error;

    int s32_TotTQ  = 1 + s32_Seg1 + s32_Seg2;
    int s32_Baud   = mk_Capability.fclk_can / s32_BRP / s32_TotTQ;
    int s32_Sample = 1000 * (1 + s32_Seg1)  / s32_TotTQ;

    // Do not display 83333 baud as "83k"
    WCHAR* s_Unit = L"";
         if (s32_Baud >= 1000000 && (s32_Baud % 1000000) == 0) { s32_Baud /= 1000000; s_Unit = L"M"; }
    else if (s32_Baud >= 1000    && (s32_Baud % 1000)    == 0) { s32_Baud /= 1000;    s_Unit = L"k"; }

    WCHAR* s_Type = b_FD ? L"Data" : L"Nominal";
    ps_Display->Format(L"%s Baudrate: %u%s, Samplepoint: %u.%u%%", s_Type, s32_Baud, s_Unit, s32_Sample / 10, s32_Sample % 10);

    if (b_FD) mb_BaudFDSet = true;
    return ERROR_SUCCESS;
}

// STEP 5)  (optional)
// Add one to eight filters
// ATTENTION: If you set only an 11 bit filter, no 29 bit ID's will pass and vice versa.
DWORD Candlelight::AddMaskFilter(bool b_29bit, DWORD u32_Filter, DWORD u32_Mask)
{
    kFilter k_Filter;
    k_Filter.Filter    = u32_Filter;
    k_Filter.Mask      = u32_Mask;
    k_Filter.Operation = b_29bit ? FIL_AcceptMask29bit : FIL_AcceptMask11bit;

    return CtrlTransfer(DIR_Out, ELM_ReqSetFilter, 0, &k_Filter, sizeof(k_Filter));
}

// --------------------------------------------------------------------

// STEP 6)
// Connect to CAN bus, turn off the green LED
DWORD Candlelight::Start(eDeviceFlags e_Flags)
{
    if (!mb_InitDone)
        return ERROR_INVALID_OPERATION;

    mb_SendTimestamp = (e_Flags & GS_DevFlagTimestamp) > 0;

    kDeviceMode k_Mode;
    k_Mode.flags = e_Flags | ELM_DevFlagProtocolElmue; // required for this demo!
    k_Mode.mode  = GS_ModeStart;
    DWORD u32_Error = CtrlTransfer(DIR_Out, GS_ReqSetDeviceMode, 0, &k_Mode, sizeof(k_Mode)); // turn off green LED
    if (u32_Error)
        return u32_Error;

    mb_Started = true;

    // Get the current timestamp of the processor as start time
    // ATTENTION:
    // This must be done AFTER GS_ReqSetDeviceMode which initializes the timestamp counter to 1µs / 10 µs intervals.
    if (mb_SendTimestamp)
        u32_Error = CtrlTransfer(DIR_In, GS_ReqGetTimestamp, 0, &mu32_McuTimeStart, sizeof(mu32_McuTimeStart));

    return u32_Error;
}

// Stop CAN bus and reset all variables and user settings in the adapter, turn on green LED
DWORD Candlelight::Reset()
{
    mb_Started = false;

    // IMPORTANT: Set flag ELM_DevFlagProtocolElmue always to make sure that the device can send debug messages.
    // Should there be a legacy device connected, it will ignore all flags sent with GS_ModeReset
    kDeviceMode k_Mode;
    k_Mode.flags = ELM_DevFlagProtocolElmue;
    k_Mode.mode  = GS_ModeReset;
    return CtrlTransfer(DIR_Out, GS_ReqSetDeviceMode, 0, &k_Mode, sizeof(k_Mode));
}

// ======================================= Send ========================================

// CAN FD packets (b_FDF) can only be sent if a data baudrate has been set
DWORD Candlelight::SendPacket(DWORD u32_ID, bool b_29bit, bool b_FDF, bool b_BRS, bool b_RTR, BYTE u8_Data[], int s32_DataLen,
                              CString* ps_Packet, __int64* ps64_WinTimestamp)
{
    *ps64_WinTimestamp = -1;

    if (!mb_InitDone || !mb_Started)
        return ERROR_INVALID_OPERATION;

    int s32_MaxData = mb_BaudFDSet ? 64 : 8;
    if (s32_DataLen > s32_MaxData)
        return ERROR_INVALID_PARAMETER;

    if (!mb_BaudFDSet && (b_FDF || b_BRS))
        return ERROR_INVALID_PARAMETER;

    if (mb_TxOverflow)
    {
        // 3 + 64 messages have been sent to the firmware which were not acknowledged. The adapter is blocked.
        me_LastError = FBK_TxBufferFull;
        return ERROR_CODE_IN_FEEDBACK;
    }

    DWORD u32_MaxID = b_29bit ? CAN_MASK_29 : CAN_MASK_11;
    if (u32_ID > u32_MaxID)
        return ERROR_INVALID_PARAMETER;

    if (b_29bit) u32_ID |= CAN_ID_29Bit;  // 29 bit CAN ID
    if (b_RTR)   u32_ID |= CAN_ID_RTR;    // Remote Transmission Request

    int s32_PadLen = s32_DataLen;
         if (s32_DataLen > 48) s32_PadLen = 64;
    else if (s32_DataLen > 32) s32_PadLen = 48;
    else if (s32_DataLen > 24) s32_PadLen = 32;
    else if (s32_DataLen > 20) s32_PadLen = 24;
    else if (s32_DataLen > 16) s32_PadLen = 20;
    else if (s32_DataLen > 12) s32_PadLen = 16;
    else if (s32_DataLen >  8) s32_PadLen = 12;

    // The STM32G431 supports to store a unique 8 bit marker for each sent frame which is returned when the frame has been acknowledged.
    // The firmware sends the marker back in kTxEchoElmue and we get the sent frame from mk_EchoFrames to display it to the user.
    // 256 markers are far more than enough because the processor has a Tx FIFO for 3 CAN packtes and the firmware can store
    // additionally 64 waiting frames in the queue. When a Tx buffer overflow is reported any further SendPacket() is blocked.
    BYTE* u8_TxMemory = mk_EchoFrames[mu8_EchoMarker].u8_TxMemory;
    memset(u8_TxMemory, 0, sizeof(kEchoFrame)); // zero out any padding bytes

    kTxFrameElmue* pk_TxFrame   = (kTxFrameElmue*)u8_TxMemory;
    pk_TxFrame->header.size     = sizeof(kTxFrameElmue) + s32_PadLen;
    pk_TxFrame->header.msg_type = MSG_TxFrame;
    pk_TxFrame->can_id          = u32_ID;
    pk_TxFrame->flags           = 0;
    pk_TxFrame->marker          = mu8_EchoMarker;
    if (b_FDF) pk_TxFrame->flags |= FRM_FDF;
    if (b_BRS) pk_TxFrame->flags |= FRM_BRS;

    memcpy(u8_TxMemory + sizeof(kTxFrameElmue), u8_Data, s32_DataLen);

    // Get timestamp immediately before sending the packet
    *ps64_WinTimestamp = GetWinTimestamp();

    DWORD u32_Transferred;
    if (!WinUsb_WritePipe(mh_WinUsb, mu8_EndpointOUT, u8_TxMemory, pk_TxFrame->header.size, &u32_Transferred, NULL))
        return GetLastError();

    mu8_EchoMarker ++;

    CString s_Data = FormatHexBytes(u8_TxMemory + sizeof(kTxFrameElmue), s32_PadLen);
    *ps_Packet = FormatFrame(pk_TxFrame->can_id, pk_TxFrame->flags, s_Data);
    return ERROR_SUCCESS;
}

// ====================================== Receive Pipe =======================================

// ------------------------------------------------------------------------------------------------------------------------------------
// IMPORTANT:
// WinUSB is different from other Windows API's.
// An overlapped read operation with WinUsb_ReadPipe() is totally different from the usual overlapped read operation on Windows.
// This extremely important detail is not documented by Microsoft, nor does Microsoft give us any useful sample code.
// Therefore you find this implemented totally wrong in Cangaroo and in Candle.NET.
// You cannot use the typical scheme ReadPipe() --> ERROR_IO_PENDING --> WaitForSingleObject(Timeout) --> GetOverlappedResult().
// If you do this with a short timeout (50 ms) you will receive NOTHING !!!
// If you do it with a longer timeout (500 ms) it will work mostly, but some USB IN packets will be lost.
// To not lose USB packets the timeout for WaitForSingleObject() *MUST* be INIFINTE.
// The reason is that WinUSB starts polling the USB IN endpoint when you call WinUsb_ReadPipe().
// But when this operation is aborted by an elapsed timeout, any USB IN packet that were about to arrive will be dropped.
// WinUSB does NOT have an internal buffer to store packets that arrive between calls to WinUsb_ReadPipe().
// So the unusual is here that we use an overlapped read operation with an INFINITE timeout.
// This requires to run in a thread and the overlapped event is required to abort the thread.
// ------------------------------------------------------------------------------------------------------------------------------------

DWORD Candlelight::ReadPipeThreadStatic(void* p_This)
{
    ((Candlelight*)p_This)->ReadPipeThreadMember();
    return 0;
}
void Candlelight::ReadPipeThreadMember()
{
    mb_AbortThread = false;
    mh_ThreadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    ResetEvent(mh_ReceiveEvent);

    OVERLAPPED k_Overlapped = {0};
    k_Overlapped.hEvent = mh_ThreadEvent;

    // This thread is time critical
    // If Rx Events are not polled fast enough USB packets may get lost because WinUSB does not have an internal Rx buffer.
    // WinUsb_ReadPipe() must be called as fast as possible again after a USB packet was received.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    while (!mb_AbortThread)
    {
        mi_Critical.Lock();
            if (ms32_FifoCount >= RX_FIFO_MAX_COUNT)
                mb_FifoOverflow = true;
        mi_Critical.Unlock();

        // if an overflow occurred, stop reading USB packets and inform the caller that it is polling too slowly.
        if (mb_FifoOverflow)
        {
            Sleep(50);
            continue;
        }

        mi_Critical.Lock();
            int s32_FifoWriteIdx  = (ms32_FifoReadIdx + ms32_FifoCount) % RX_FIFO_MAX_COUNT;
            kRxFifo* pk_FifoWrite = &mk_RxFifo[s32_FifoWriteIdx];
        mi_Critical.Unlock();

        DWORD u32_Read  = 0;
        DWORD u32_Error = ERROR_SUCCESS;
        if (!WinUsb_ReadPipe(mh_WinUsb, mu8_EndpointIN, pk_FifoWrite->mu8_Buffer, RX_FIFO_BUF_SIZE, NULL, &k_Overlapped))
        {
            u32_Error = GetLastError();
            if (u32_Error == ERROR_IO_PENDING)
            {
                u32_Error = ERROR_SUCCESS;

                // mh_ThreadEvent is set when a USB IN packet was received and in Close() to abort the thread
                DWORD u32_Result = WaitForSingleObject(mh_ThreadEvent, INFINITE);
                if (mb_AbortThread)
                    break;

                switch (u32_Result)
                {
                    case WAIT_TIMEOUT: // This should never happen with timeout = INFINITE
                        u32_Error = ERROR_TIMEOUT;
                        break;

                    case WAIT_OBJECT_0:
                        if (!WinUsb_GetOverlappedResult(mh_WinUsb, &k_Overlapped, &u32_Read, FALSE))
                            u32_Error = GetLastError();
                        break;

                    default: // WAIT_FAILED (I have never seen this error, but just in case...)
                        u32_Error = GetLastError();
                        break;
                }
            }
        }

        pk_FifoWrite->mu32_BytesRead    = u32_Read;
        pk_FifoWrite->mu32_Error        = u32_Error;
        pk_FifoWrite->ms64_WinTimestamp = GetWinTimestamp();

        // Increment write index for the next ReadPipe, leave read index unchanged
        mi_Critical.Lock();
            ms32_FifoCount ++;
            SetEvent(mh_ReceiveEvent);
        mi_Critical.Unlock();

        if (u32_Error)
        {
            // If the CANable has been disconnected an error ERROR_BAD_COMMAND or ERROR_GEN_FAILURE will be reported in each loop.
            // This high priority thread must be slowed down to avoid that it consumes
            // a lot of CPU power running in an endless loop and to avoid that the FIFO overflows with errors.
            Sleep(50);
        }
    } // while

    CloseHandle(mh_ThreadEvent);
    mh_ThreadEvent = NULL; // Setting this to NULL signals that the thread has finished.
}

// Receive a Rx packet, a Tx echo packet, an error frame, a debug message, a busload packet, or .......
// pk_Header and ps64_WinTimestamp are only valid if the function does not return an error.
DWORD Candlelight::ReceiveData(DWORD u32_Timeout, kHeader* pk_Header, DWORD u32_BufSize, __int64* ps64_WinTimestamp)
{
    // This timestamp is only in case that an error is returned
    *ps64_WinTimestamp = GetWinTimestamp();

    if (!mb_InitDone || !mb_Started)
        return ERROR_INVALID_OPERATION;

    if (u32_BufSize < RX_FIFO_BUF_SIZE)
        return ERROR_INSUFFICIENT_BUFFER;

    mi_Critical.Lock();
        kRxFifo* pk_FifoRead = &mk_RxFifo[ms32_FifoReadIdx];
        int s32_Available = ms32_FifoCount;
        if (s32_Available > 0)
            ResetEvent(mh_ReceiveEvent);
    mi_Critical.Unlock();

    if (s32_Available == 0) // nothing received
    {
        // After all messages in the FIFO have been returned inform about the FIFO overflow.
        if (mb_FifoOverflow)
        {
            mi_Critical.Lock();
                mb_FifoOverflow = false;
            mi_Critical.Unlock();
            return ERROR_RX_FIFO_OVERFLOW;
        }

        DWORD u32_Result = WaitForSingleObject(mh_ReceiveEvent, u32_Timeout);
        if (u32_Result == WAIT_TIMEOUT)
            return ERROR_TIMEOUT;

        mi_Critical.Lock();
            s32_Available = ms32_FifoCount;
        mi_Critical.Unlock();

        if (s32_Available == 0)
            return ERROR_TIMEOUT;
    }

    // store the timestamp when the thread has received the packet
    *ps64_WinTimestamp = pk_FifoRead->ms64_WinTimestamp;
    DWORD u32_Error    = pk_FifoRead->mu32_Error;
    
    if (u32_Error == ERROR_SUCCESS)
    {
        memcpy(pk_Header, pk_FifoRead->mu8_Buffer, pk_FifoRead->mu32_BytesRead);
        if (pk_FifoRead->mu32_BytesRead < pk_Header->size)
            u32_Error = ERROR_CORRUPT_IN_DATA;
    }

    mi_Critical.Lock();
        ms32_FifoReadIdx = (ms32_FifoReadIdx + 1) % RX_FIFO_MAX_COUNT;
        ms32_FifoCount --;
    mi_Critical.Unlock();

    return u32_Error;
}

// ==========================================================================================

// Flashes the green / blue LEDs on the board
DWORD Candlelight::Indentify(bool b_Blink)
{
    if (!mb_InitDone)
        return ERROR_INVALID_OPERATION;

    DWORD u32_Mode = b_Blink; 
    return CtrlTransfer(DIR_Out, GS_ReqIdentify, 0, &u32_Mode, sizeof(u32_Mode));
}

// Interval = 7 --> report busload in percent every 700 ms.
// NOTE: The firmware does not report the busload if bus load is permanently 0%.
DWORD Candlelight::EnableBusLoadReport(BYTE u8_Interval)
{
    if (!mb_InitDone)
        return ERROR_INVALID_OPERATION;

    return CtrlTransfer(DIR_Out, ELM_ReqSetBusLoadReport, 0, &u8_Interval, sizeof(u8_Interval));
}

// Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
DWORD Candlelight::DisableBootPin()
{
    if (!mb_InitDone)
        return ERROR_INVALID_OPERATION;

    kPinStatus k_PinStatus = {0};
    k_PinStatus.Operation  = PINOP_Disable;
    k_PinStatus.PinID      = PINID_BOOT0;
    return CtrlTransfer(DIR_Out, ELM_ReqSetPinStatus, 0, &k_PinStatus, sizeof(k_PinStatus));
}

// Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
DWORD Candlelight::IsBootPinEnabled(bool* pb_Enabled)
{
    if (!mb_InitDone)
        return ERROR_INVALID_OPERATION;

    // The requested pin ID must be transmitted in SETUP.wValue because a USB IN request cannot otherwise transmit parameters to the firmware.
    WORD u16_PinStatus;
    DWORD u32_Error = CtrlTransfer(DIR_In, ELM_ReqGetPinStatus, PINID_BOOT0, &u16_PinStatus, sizeof(u16_PinStatus));
    if (u32_Error)
        return u32_Error;

    *pb_Enabled = (u16_PinStatus & PINST_Enabled) > 0;
    return 0;
}

// --------------------------------------------------------------------

// Send a SETUP request to the firmware
// u32_DataSize must be the expected byte count to be received from the firmware or to be sent to the firmware.
// u8_Request must be eUsbRequest for interface 0 and eDfuRequest for interface 1.
DWORD Candlelight::CtrlTransfer(eDirection e_Dir, BYTE u8_Request, WORD u16_Value, void* p_Data, DWORD u32_DataSize)
{
    // The Candlelight interface implements Vendor requests while the DFU interface implements Class requests.
    eSetupType e_Type = (mu8_Interface == 0) ? TYP_Vendor : TYP_Class;

    WINUSB_SETUP_PACKET k_Setup;
    k_Setup.RequestType = RECIP_Interface | e_Type | e_Dir;
    k_Setup.Request     = u8_Request;
    k_Setup.Value       = u16_Value;     // only used for ELM_ReqGetPinStatus
    k_Setup.Index       = mu8_Interface; // destination interface (0 = Candlelight, 1 = DFU Firmware Update)
    k_Setup.Length      = 0;             // set by WinUSB to u32_DataSize

    // -------- Execute Request ------------

    DWORD u32_CmdErr = ERROR_SUCCESS;
    DWORD u32_CmdBytes;
    if (!WinUsb_ControlTransfer(mh_WinUsb, k_Setup, (BYTE*)p_Data, u32_DataSize, &u32_CmdBytes, NULL))
        u32_CmdErr = GetLastError();

    // The DFU interface has no feedback
    if (mu8_Interface == 0)
    {
        // ---------- Get Feedback -------------

        // ALWAYS get the feedback, even if the previous command execution did NOT return an error!
        // In second stage of the SETUP request the firmware can NOT stall the endpoint which is the only way to alert an USB error.

        k_Setup.RequestType = RECIP_Interface | TYP_Vendor | DIR_In;
        k_Setup.Request     = ELM_ReqGetLastError;

        BYTE  u8_Feedback;
        DWORD u32_FbkErr = ERROR_SUCCESS;
        DWORD u32_FbkBytes;
        if (!WinUsb_ControlTransfer(mh_WinUsb, k_Setup, &u8_Feedback, sizeof(u8_Feedback), &u32_FbkBytes, NULL))
            u32_FbkErr = GetLastError();

        me_LastError = (eFeedback)u8_Feedback;

        // --------- Process Errors ------------

        // me_LastError is only valid if u32_FbkErr == ERROR_SUCCESS
        // if a legacy board is connected it will not understand request ELM_ReqGetLastError --> Endpoint stalled --> u32_FbkErr = ERROR_GEN_FAILURE
        if (u32_FbkErr == ERROR_SUCCESS && me_LastError != FBK_Success)
            return ERROR_CODE_IN_FEEDBACK;
    }

    if (u32_CmdErr)
        return u32_CmdErr;

    if (u32_CmdBytes < u32_DataSize)
        return ERROR_INVALID_DATA; 

    return ERROR_SUCCESS;
}

// =======================================================================================================================

// Create a timestamp with 10 µs precision.
// It is recommended to turn off transmssion of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
// Then this function is used as a replacement to generate a timestamp on reception of a packet and when sending a packet.
// When Open() is called the timestamp is reset to start at zero.
// ATTENTION:
// The legacy Candlelight firmware has sent hardware timestamps with 1 µs precision.
// This is a big nonsense, because nobody needs to know with the precision of 1 microsecond when a CAN packet has arrived.
// The disadvantage is that a 32 bit timstamp with 1 µs precision will roll over within one hour.
// Therefore the new firmware switches to send 10 µs timestamps as soon as ELM_DevFlagProtocolElmue is set.
// 10 µs precicion is far more than enough and the timestamp will roll over after 10 hours.
__int64 Candlelight::GetWinTimestamp()
{
    static __int64 s64_Frequency = 0; 

    // The performance counter runs inside the CPU and the frequency identical over all CPU cores and never changes.
    // The performance counter frequency depends on the CPU and the operating system, mostly above 3 MHz
    if (s64_Frequency == 0)
	    QueryPerformanceFrequency((LARGE_INTEGER*)&s64_Frequency);

	__int64 s64_Counter;
	QueryPerformanceCounter((LARGE_INTEGER*)&s64_Counter);

	return (s64_Counter - ms64_WinTimeStart) * 100000 / s64_Frequency;
}

// Formats a timestamp with 10 µs precision
// returns "HH:MM:SS.mmm.µµµ"
// pk_Header may contain a timestamp if GS_DevFlagTimestamp is set --> mb_SendTimestamp = true
// otherwise use s64_WinTimestamp which comes from GetWinTimestamp() at packet reception
CString Candlelight::FormatTimestamp(kHeader* pk_Header, __int64 s64_WinTimestamp)
{
    __int64 s64_Stamp = s64_WinTimestamp;
    if (mb_SendTimestamp)
    {
        s64_Stamp = -1;
        if (pk_Header != NULL)
        {
            switch (pk_Header->msg_type)
            {
                // These 3 messages send firmware timestamps:
                case MSG_TxEcho:  s64_Stamp = (DWORD)(((kTxEchoElmue*) pk_Header)->timestamp - mu32_McuTimeStart); break;
                case MSG_RxFrame: s64_Stamp = (DWORD)(((kRxFrameElmue*)pk_Header)->timestamp - mu32_McuTimeStart); break;
                case MSG_Error:   s64_Stamp = (DWORD)(((kErrorElmue*)  pk_Header)->timestamp - mu32_McuTimeStart); break;
            }
        }
    }

    if (s64_Stamp < 0)
        return L"No Timestamp    ";

    s64_Stamp += ms64_ClockOffset;

    s64_Stamp *= 10; // 10 µs --> 1 µs
    DWORD u32_Micro = s64_Stamp % 1000;
    s64_Stamp /= 1000;
    DWORD u32_Milli = s64_Stamp % 1000;
    s64_Stamp /= 1000;
    DWORD u32_Sec = s64_Stamp % 60;
    s64_Stamp /= 60;
    DWORD u32_Min = s64_Stamp % 60;
    s64_Stamp /= 60;
    DWORD u32_Hour = s64_Stamp % 24;

    CString s_Time;
    s_Time.Format(L"%02u:%02u:%02u.%03u.%03u", u32_Hour, u32_Min, u32_Sec, u32_Milli, u32_Micro);
    return s_Time;
}

// returns "02 67 5E C7 FF"
CString Candlelight::FormatHexBytes(BYTE u8_Data[], int s32_DataLen)
{
    CString s_Hex;
    for (int i=0; i<s32_DataLen; i++)
    {
        WCHAR c_Buf[5];
        swprintf_s(c_Buf, L"%02X ", u8_Data[i]);
        s_Hex += c_Buf;
    }
    return s_Hex;
}

// returns "5" or "2.00" or "25.09.14"
CString Candlelight::FormatBcdVersion(DWORD u32_Version)
{
    if (u32_Version == 0)
        return L"0";

    CString s_Version;
    for (int i=0; i<4; i++)
    {
        WCHAR c_Buf[5];
        swprintf_s(c_Buf, L".%02X", u32_Version & 0xFF);
        s_Version.Insert(0, c_Buf);

        u32_Version >>= 8;
        if (u32_Version == 0)
            break;
    }
    return s_Version.TrimLeft(L".0");
}

CString Candlelight::FormatRxPacket(kRxFrameElmue* pk_RxFrame)
{
    int s32_PackSize = sizeof(kRxFrameElmue);
    if (!mb_SendTimestamp) s32_PackSize -= 4;

    BYTE*  u8_DataStart = (BYTE*)pk_RxFrame + s32_PackSize;
    CString s_Data = FormatHexBytes(u8_DataStart, pk_RxFrame->header.size - s32_PackSize);
    return FormatFrame(pk_RxFrame->can_id, pk_RxFrame->flags, s_Data);
}

CString Candlelight::FormatTxEcho(kTxEchoElmue* pk_Echo)
{
    BYTE* u8_TxMemory = mk_EchoFrames[pk_Echo->marker].u8_TxMemory;
    kTxFrameElmue* pk_TxFrame = (kTxFrameElmue*)u8_TxMemory;

    CString s_Data = FormatHexBytes(u8_TxMemory + sizeof(kTxFrameElmue), pk_TxFrame->header.size - sizeof(kTxFrameElmue));
    return FormatFrame(pk_TxFrame->can_id, pk_TxFrame->flags, s_Data);
}

CString Candlelight::FormatFrame(DWORD u32_ID, BYTE u8_Flags, CString s_Data)
{
    const WCHAR* s_Format = (u32_ID & CAN_ID_29Bit) ? L"%08X: %s" : L"%03X: %s";

    CString s_Frame;
    s_Frame.Format(s_Format, u32_ID & CAN_MASK_29, s_Data);

    if ((u32_ID & CAN_ID_RTR) > 0) s_Frame += L" RTR"; // Remote Transmission Request
    if ((u8_Flags  & FRM_FDF) > 0) s_Frame += L" FDF"; // Flexible Datarate Frame
    if ((u8_Flags  & FRM_BRS) > 0) s_Frame += L" BRS"; // Bitrate Switch
    if ((u8_Flags  & FRM_ESI) > 0) s_Frame += L" ESI"; // Error Indicator
    return s_Frame;
}

// From the multiple flags that have been defined by previous programmers we check only those which the CANable 2.5 firmware sets.
// pe_BusStatus returns the current bus status (active, warning, passive, off)
// pe_Level return the error level (low, ledium, high)
CString Candlelight::FormatCanErrors(kErrorElmue* pk_Error, eErrorBusStatus* pe_BusStatus, eErrorLevel* pe_Level)
{
    eErrFlagsCanID e_ID    = (eErrFlagsCanID)pk_Error->err_id;
    eErrFlagsByte1 e_Byte1 = (eErrFlagsByte1)pk_Error->err_data[1];
    eErrFlagsByte1 e_Byte2 = (eErrFlagsByte1)pk_Error->err_data[2];
    eErrorAppFlags e_App   = (eErrorAppFlags)pk_Error->err_data[5];

    mb_TxOverflow = (e_App & APP_CanTxOverflow) > 0;
    *pe_BusStatus = BUS_StatusActive;
    *pe_Level     = LEVEL_Low;

    CString s_Mesg;
    if (e_ID & ERID_Bus_is_off) 
    {
        *pe_BusStatus = BUS_StatusOff;
        *pe_Level     = LEVEL_High;
        s_Mesg += L"Bus Off, ";
    }
    else if (e_Byte1 & (ER1_Rx_Passive_status_reached  | ER1_Tx_Passive_status_reached))
    {
        *pe_BusStatus = BUS_StatusPassive;
        *pe_Level     = LEVEL_High;
        s_Mesg += L"Bus Passive, ";
    }
    else if (e_Byte1 & (ER1_Rx_Errors_at_warning_level | ER1_Tx_Errors_at_warning_level))
    {
        *pe_BusStatus = BUS_StatusWarning;
        *pe_Level     = LEVEL_Medium;
        s_Mesg += L"Bus Warning, ";
    }
    else // Active
    {
        if (e_Byte1 & ER1_Bus_is_back_active) s_Mesg += L"Back to Active, ";
        else                                  s_Mesg += L"Bus Active, ";
    }

    // all errors generated by the firmware are bigger problems (Level High)
    if (e_App > 0) *pe_Level = LEVEL_High;
    if (e_App & APP_CanRxFail)      s_Mesg += L"Rx Failed, ";
    if (e_App & APP_CanTxFail)      s_Mesg += L"Tx Failed, ";
    if (e_App & APP_CanTxTimeout)   s_Mesg += L"Tx Timeout, ";
    if (e_App & APP_CanTxOverflow)  s_Mesg += L"CAN Tx Overflow, ";
    if (e_App & APP_UsbInOverflow)  s_Mesg += L"USB IN Overflow, ";

    // Error cause
    if (e_ID    & ERID_No_ACK_received)             s_Mesg += L"No ACK received, ";
    if (e_ID    & ERID_CRC_Error)                   s_Mesg += L"CRC Error, ";
    if (e_Byte2 & ER2_Bit_stuffing_error)           s_Mesg += L"Bit Stuffing Error, ";
    if (e_Byte2 & ER2_Frame_format_error)           s_Mesg += L"Frame Format Error, ";    // e.g. CAN FD frame received in classic mode
    if (e_Byte2 & ER2_Unable_to_send_dominant_bit)  s_Mesg += L"Dominant Bit Error, ";
    if (e_Byte2 & ER2_Unable_to_send_recessive_bit) s_Mesg += L"Recessive Bit Error, ";

    WCHAR c_Buf[50];
    if (pk_Error->err_data[6] > 0) 
    {
        swprintf_s(c_Buf, L"Tx Errors: %u, ", pk_Error->err_data[6]);
        s_Mesg += c_Buf;
    }
    if (pk_Error->err_data[7] > 0) 
    {
        swprintf_s(c_Buf, L"Rx Errors: %u, ", pk_Error->err_data[7]);
        s_Mesg += c_Buf;
    }
    return s_Mesg.TrimRight(L", ");
}

CString Candlelight::FormatLastError(DWORD u32_Error)
{
    switch (u32_Error)
    {
        case ERROR_ACCESS_DENIED: // from CreateFile()
            return L"Access denied. Probably the device is already used in another application.";
        case ERROR_INVALID_DEVICE:   
            return L"The device is not a Candlelight adapter.";
        case ERROR_INVALID_FIRMWARE: 
            return L"This demo supports only devices that have the CANable 2.5 firmware from ElmüSoft.";
        case ERROR_RX_FIFO_OVERFLOW:
            return L"USB Rx FIFO overflow. Polling is too slow.";
        case ERROR_CORRUPT_IN_DATA:
            return L"Corrupt USB IN data received.";
        case ERROR_CODE_IN_FEEDBACK:
        {
            switch (me_LastError)
            {
                case FBK_InvalidCommand:      return L"The command is invalid.";
                case FBK_InvalidParameter:    return L"One of the parameters is invalid.";
                case FBK_AdapterMustBeOpen:   return L"This command cannot be executed before opening the adapter.";
                case FBK_AdapterMustBeClosed: return L"This command cannot be executed after  opening the adapter.";
                case FBK_ErrorFromHAL:        return L"The HAL from ST Microelectronics has reported an error.";
                case FBK_UnsupportedFeature:  return L"The feature is not implemented or not supported by the board.";
                case FBK_TxBufferFull:        return L"Sending is not possible because the Tx buffer is full.";
                case FBK_BusIsOff:            return L"Sending is not possible because the processor is blocked in BusOff state.";
                case FBK_NoTxInSilentMode:    return L"Sending is not possible because the adapter is in bus monitoring mode.";
                case FBK_BaudrateNotSet:      return L"The baudrate has not been set.";
                case FBK_OptBytesProgrFailed: return L"Programming the Option Bytes failed.";
                case FBK_ResetRequired:       return L"Please reconnect the USB cable.";
                default:                      return L"Unknown feedback received from the device.";
            }
        }
        default:
        {
            // Format Windows API error
            const DWORD FLAGS = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
            CString s_Error;
            FormatMessageW(FLAGS, 0, u32_Error, 0, s_Error.GetBuffer(1000), 1000, 0);
            s_Error.ReleaseBuffer();
            return s_Error.TrimRight();
        }
    }
}

// ================================== DFU ========================================

// Switch the Candlelight into firmware update mode.
// This function requires that you have called EnumDevices(Interface = 1) before to get access to interface 1.
// IMPORTANT:
// This will ONLY work if the Candlelight has the new CANable 2.5 firmware from ElmüSoft.
// ALL legacy Candlelights have a sloppy firmware that does not respond to the Microsoft OS descriptor request for interface 1.
// The consequence is that Windows cannot install the WinUSB driver for the DFU interface and EnumDevices() will not find the device.
// ATTENTION:
// This works only if the device is in Candlelight mode. If the device is already in DFU mode it will fail.
// If the device is already in DFU mode you cannot use the WinUSB driver, you need the STtube30 driver from ST Microelectronics.
// If you want to update the firmware use HUD ECU Hacker which comes with a very comfortable STM32 Firmware Updater.
DWORD Candlelight::EnterDfuMode()
{
    if (!mb_InitDone || mu8_Interface != 1)
        return ERROR_INVALID_OPERATION;

    // The legacy firmware would have entered immediately in DFU mode and CtrlTransfer() would have returned error 31 here.
    // But the CANable 2.5 firmware responds correctly to all SETUP requests because it makes a delay of 300 ms before entering DFU mode.
    DWORD u32_Error = CtrlTransfer(DIR_Out, DFU_RequDetach, 0, NULL, 0);
    if (u32_Error)
        return u32_Error;

    kDfuStatus k_Status;
    // returned Error must be ignored here because legacy devices enter boot mode immediately and CtrlTransfer will return error 31.
    if (CtrlTransfer(DIR_In, DFU_RequGetStatus, 0, &k_Status, sizeof(k_Status)) == ERROR_SUCCESS)
    {
        // returning AppDetach has been added by ElmüSoft to the firmware and means that the user must reconnect the USB cable.
        // This happens only if the pin BOOT0 was disabled before calling EnterDfuMode()
        // k_Status.State is either DfuSte_AppIdle or DfuSte_AppDetach.
        if (k_Status.State == DfuState_AppDetach)
        {
            // The user must reconnect the USB cable now.
            // This happens only if the pin BOOT0 was disabled before calling this function.
            me_LastError = FBK_ResetRequired;
            return ERROR_CODE_IN_FEEDBACK;
        }
    }

    // The device will enter DFU mode in 300 ms --> the WinUSB handle is not valid anymore.
    Close();
    return ERROR_SUCCESS;
}


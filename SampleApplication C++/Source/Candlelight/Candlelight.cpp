
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

#include "Candlelight.h"

using namespace CANable;

// Adapt this to the latest available CANable 2.5 firmware version.
// It shows an error to upload the latest firmware to the adapter.
// The version number is BCD encoded (0x251218 = 18.dec.2025)
#define MIN_FIRMWARE      0x260606
// must be equal to DFU_INTERFACE_NUMBER in usb_class.h in the firmware
#define DFU_INTERFACE     1
// must be equal to CAN_QUEUE_SIZE in buffer.h in the firmware
#define CAN_QUEUE_SIZE    64

// This class implements the new CANable 2.5 ElmüSoft protocol.
Candlelight::Candlelight()
{
    mb_InitDone = false;

    assert(sizeof(kDeviceDescriptor)    == 18);
    assert(sizeof(kInterfaceDescriptor) ==  9);
    assert(sizeof(kSetup)               ==  8);
}

Candlelight::~Candlelight()
{
    Close();
}

void Candlelight::Close()
{
    if (mi_OsLibrary.IsOpen())
        Reset(); // stop the CAN interface and reset all variables in the firmware

    mi_OsLibrary.Close();
    mb_InitDone = false; 
}

// --------------------------------------------------------------------

// STEP 1)
// Initialize WinUSB and get the Candlelight structures with board info, capabilities, etc from the firmware
uint32_t Candlelight::Open(string s_DevicePath)
{
    if (mi_OsLibrary.IsOpen())
        return ERR_OPERATION_INVALID; // Already open
    
    mu8_EchoMarker    = 1; // counter 1...255
    ms64_McuRollOver  = 0;
    ms64_LastMcuStamp = 0;
    mu64_TxOverflow   = 0;    
    mu32_BlobOffset   = 0;
    ms32_BlobFrames   = 0;
    mb_BaudFDSet      = false;
    mb_InitDone       = false;
    mb_Started        = false;
    mb_EnableTxEcho   = true;
    me_LastError      = FBK_Success;
    
    mi_Details.clear();
    memset(&mk_EchoPackets, 0, sizeof(mk_EchoPackets));
    
    uint32_t u32_Error = mi_OsLibrary.Open(s_DevicePath);
    if (u32_Error)
        return u32_Error;   
    
    mpk_Info      = mi_OsLibrary.DevInfo();
    mu8_Interface = mpk_Info->mk_InterfDescr.bInterfaceNumber;   

    mi_Details.push_back(kDetail("USB Vendor",         cUtils::Format("\"%s\"", mpk_Info->ms_Vendor   .c_str())));   
    mi_Details.push_back(kDetail("USB Product",        cUtils::Format("\"%s\"", mpk_Info->ms_Product  .c_str())));  
    mi_Details.push_back(kDetail("USB Serial  Nº",     cUtils::Format("\"%s\"", mpk_Info->ms_Serial   .c_str())));   
    mi_Details.push_back(kDetail("USB Interface Name", cUtils::Format("\"%s\"", mpk_Info->ms_Interface.c_str())));
    mi_Details.push_back(kDetail("USB Vendor  ID",     cUtils::Format("%04X",   mpk_Info->mk_DeviceDescr .idVendor)));
    mi_Details.push_back(kDetail("USB Product ID",     cUtils::Format("%04X",   mpk_Info->mk_DeviceDescr .idProduct)));
    mi_Details.push_back(kDetail("USB Device Version", cUtils::FormatBcdVersion (mpk_Info->mk_DeviceDescr.bcdDevice)));  

    // -------------------------- DFU --------------------------------

    if (mu8_Interface == DFU_INTERFACE)
    {
        // The DFU interface has no IN / OUT endpoints. It supports only SETUP requests.
        if (mpk_Info->mk_InterfDescr.bNumEndpoints != 0)
            return ERR_INVALID_DEVICE;

        mb_InitDone = true;
        return NO_ERROR;
    }

    // ------------------------ Candlelight --------------------------

    // There must be exactly 2 endpoints: IN (81) and OUT (02)
    if (mpk_Info->mk_InterfDescr.bNumEndpoints != 2)
        return ERR_INVALID_DEVICE;

    // Interface 0 -> Channel 0
    // Interface 1 -> DFU
    // Interface 2 -> Channel 1
    // Interface 3 -> Channel 2
    mu8_Channel = mu8_Interface; 
    if (mu8_Channel > 0)
        mu8_Channel --;

    mpk_Info->mu8_Channel = mu8_Channel;
    
    mi_Details.push_back(kDetail("USB Endpoint CTRL", cUtils::Format(  "00,  max packet size: %u byte", mpk_Info->mk_DeviceDescr.bMaxPacketSize0)));
    mi_Details.push_back(kDetail("USB Endpoint IN",   cUtils::Format("%02X,  max packet size: %u byte", mpk_Info->mu8_EndpointIN,  mpk_Info->mu16_MaxPackSizeIN)));
    mi_Details.push_back(kDetail("USB Endpoint OUT",  cUtils::Format("%02X,  max packet size: %u byte", mpk_Info->mu8_EndpointOUT, mpk_Info->mu16_MaxPackSizeOUT)));  

    // --------------------------------------------------------------------

    // Reset() should always be the first command.
    // The device may still be open --> close it, which resets all variables in the firmware.
    // And the CANable 2.5 firmware allows to set ELM_DevFlagProtocolElmue which enables debug messages at the very beginnning.
    if (u32_Error = Reset())
        return u32_Error;

    // GS_ReqGetCapabilities is a legacy commmand supported by all Candlelight's
    if (u32_Error = CtrlTransfer(DIR_In, GS_ReqGetCapabilities, mu8_Channel, &mpk_Info->mk_Capability, sizeof(kCapabilityClassic)))
        return u32_Error;

    mpk_Info->mb_IsElmueSoft =  (mpk_Info->mk_Capability.feature & ELM_DevFlagProtocolElmue) > 0;
    mpk_Info->mb_SupportsFD  = ((mpk_Info->mk_Capability.feature & GS_DevFlagCAN_FD) && 
                                (mpk_Info->mk_Capability.feature & GS_DevFlagBitTimingFD));

    if (mpk_Info->mb_SupportsFD)
    {
        // GS_ReqGetCapabilitiesFD is a legacy commmand supported by all Candlelight's
        u32_Error = CtrlTransfer(DIR_In, GS_ReqGetCapabilitiesFD, mu8_Channel, &mpk_Info->mk_CapabilityFD, sizeof(kCapabilityFD));
        if (u32_Error)
            return u32_Error;
    }

    // GS_ReqGetDeviceVersion is a legacy commmand supported by all Candlelight's
    if (u32_Error = CtrlTransfer(DIR_In, GS_ReqGetDeviceVersion, mu8_Channel, &mpk_Info->mk_DeviceVersion, sizeof(kDeviceVersion)))
        return u32_Error;

    mi_Details.push_back(kDetail("Hardware Version", cUtils::FormatBcdVersion(mpk_Info->mk_DeviceVersion.hw_version_bcd)));  
    mi_Details.push_back(kDetail("Firmware Version", cUtils::FormatBcdVersion(mpk_Info->mk_DeviceVersion.sw_version_bcd)));  

    if (mpk_Info->mb_IsElmueSoft) // not BCD encoded
        mi_Details.push_back(kDetail("HAL Version", cUtils::Format("%u.%u.%u", mpk_Info->mk_DeviceVersion.hal_ver_high,
                                                                                 mpk_Info->mk_DeviceVersion.hal_ver_mid,
                                                                                 mpk_Info->mk_DeviceVersion.hal_ver_low)));

    mi_Details.push_back(kDetail("Firmware Type",   mpk_Info->mb_IsElmueSoft ? "CANable 2.5" : "Legacy"));        
    mi_Details.push_back(kDetail("Supports CAN FD", mpk_Info->mb_SupportsFD  ? "Yes"         : "No"));            

    if (!mpk_Info->mb_IsElmueSoft)
    {
        mi_Details.push_back(kDetail("CAN Clock", cUtils::Format("%u MHz", mpk_Info->mk_Capability.fclk_can / 1000000)));  
        return ERR_INVALID_FIRMWARE; // this class requires the new ElmüSoft firmware
    }

    // --------------- Here comes only ElmüSoft firmware ---------------

    // ELM_ReqGetBoardInfo requires ElmüSoft firmware
    if (u32_Error = CtrlTransfer(DIR_In, ELM_ReqGetBoardInfo, mu8_Channel, &mpk_Info->mk_BoardInfo, sizeof(kBoardInfo)))
        return u32_Error;

    // IsBootPinEnabled() cannot be called here because mb_InitDone must be set at the end of this function.
    uint16_t u16_PinStatus;
    if (u32_Error = CtrlTransfer(DIR_In, ELM_ReqGetPinStatus, PINID_BOOT0, &u16_PinStatus, sizeof(u16_PinStatus)))
        return u32_Error;

    mi_Details.push_back(kDetail("Target Board", mpk_Info->mk_BoardInfo.BoardName));
    mi_Details.push_back(kDetail("Processor",    cUtils::Format("%s, CAN Clock: %u MHz, MCU DeviceID: 0x%X",
                                                                mpk_Info->mk_BoardInfo.McuName,
                                                                mpk_Info->mk_Capability.fclk_can / 1000000,
                                                                mpk_Info->mk_BoardInfo.McuDeviceID)));
                                                                 
    bool b_UseQuartz = (mpk_Info->mk_BoardInfo.BoardFlags & BRD_Quartz_In_Use) > 0;                                                                 
    mi_Details.push_back(kDetail("Quartz in use", b_UseQuartz ? "Yes": "No"));
    mi_Details.push_back(kDetail("CAN Channel",   cUtils::Format("%d of %d", mu8_Channel + 1, mpk_Info->mk_DeviceVersion.icount + 1)));
    mi_Details.push_back(kDetail("Pin BOOT0",     (u16_PinStatus & PINST_Enabled) ? "Enabled" : "Disabled"));

    if (mpk_Info->mk_DeviceVersion.sw_version_bcd < MIN_FIRMWARE)
        return ERR_UPDATE_FIRMWARE;

    // Update MIN_FIRMWARE to the latest firmware version! Implement new features if available in the new firmware!
    assert(mpk_Info->mk_DeviceVersion.sw_version_bcd == MIN_FIRMWARE);

    if (u32_Error = mi_OsLibrary.StartPipes())
        return u32_Error;

    mb_InitDone = true;
    return NO_ERROR;
}

// --------------------------------------------------------------------

// STEP 2)  (optional)
// Define if you want to receive Tx Echo Markers
void Candlelight::EnableTxEcho(bool b_Enable)
{
    mb_EnableTxEcho = b_Enable;
}

// --------------------------------------------------------------------

// STEP 3)
// Please read "CiA - Recommendations for CAN Bit Timing.pdf" in subfolder Documentation
// returns the formatted baudrate and samplepoint in s_Display
uint32_t Candlelight::SetBitrate(bool b_FD, int s32_BRP, int s32_Seg1, int s32_Seg2, string* ps_Display)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    if (b_FD && !mpk_Info->mb_SupportsFD)
        return ERR_OPERATION_INVALID; // CAN FD not supported
    
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
    uint32_t u32_Error = CtrlTransfer(DIR_Out, e_Requ, mu8_Channel, &k_Timing, sizeof(k_Timing));
    if (u32_Error)
        return u32_Error;

    int s32_TotTQ  = 1 + s32_Seg1 + s32_Seg2;
    int s32_Baud   = mpk_Info->mk_Capability.fclk_can / s32_BRP / s32_TotTQ;
    int s32_Sample = 1000 * (1 + s32_Seg1)  / s32_TotTQ;

    // Do not display 83333 baud as "83k"
    char* c_Unit = "";
         if (s32_Baud >= 1000000 && (s32_Baud % 1000000) == 0) { s32_Baud /= 1000000; c_Unit = "M"; }
    else if (s32_Baud >= 1000    && (s32_Baud % 1000)    == 0) { s32_Baud /= 1000;    c_Unit = "k"; }

    if (ps_Display)
    {
        char* c_Type = b_FD ? "Data   " : "Nominal";
        *ps_Display = cUtils::Format("%s Baudrate: %u%s, Samplepoint: %u.%u%%", c_Type, s32_Baud, c_Unit, s32_Sample / 10, s32_Sample % 10);
    }

    if (b_FD) mb_BaudFDSet = true;
    return NO_ERROR;
}

// STEP 4)  (optional)
// Add one to eight host filters
// ATTENTION: If you set only an 11 bit filter, no 29 bit ID's will pass and vice versa.
uint32_t Candlelight::AddHostFilter(bool b_29bit, uint32_t u32_Filter, uint32_t u32_Mask)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    kFilter k_Filter = {0};
    k_Filter.Operation = b_29bit ? FIL_HostPass_29 : FIL_HostPass_11;
    k_Filter.Filter    = u32_Filter;
    k_Filter.Mask      = u32_Mask;

    return CtrlTransfer(DIR_Out, ELM_ReqSetFilter, mu8_Channel, &k_Filter, sizeof(k_Filter));
}

// STEP 5)  (optional)
// set / clear one of 20 bridge filters
// b_Enable = false and Index == 0x13  --> clear only bridge filter Nº 0x13
// b_Enable = false and Index == 0xFF  --> clear all bridge filters
// b_Enable = true and b_Block = true  --> set block filter
// b_Enable = true and b_Block = false --> set pass filter
uint32_t Candlelight::SetBridgeFilter(uint8_t u8_FilterIndex, uint8_t u8_DestChannel, bool b_Enable, bool b_Block, bool b_29bit, uint32_t u32_Filter, uint32_t u32_Mask)
{
    kFilter k_Filter = {0};
    k_Filter.Operation   = FIL_BridgeClear;
    k_Filter.Filter      = u32_Filter;
    k_Filter.Mask        = u32_Mask;
    k_Filter.DestChannel = u8_DestChannel;
    k_Filter.Index       = u8_FilterIndex;

    if (b_Enable)
    {
        if (b_Block)
        {
            if (b_29bit) k_Filter.Operation = FIL_BridgeBlock_29;
            else         k_Filter.Operation = FIL_BridgeBlock_11;
        }
        else
        {
            if (b_29bit) k_Filter.Operation = FIL_BridgePass_29;
            else         k_Filter.Operation = FIL_BridgePass_11;
        }
    }

    return CtrlTransfer(DIR_Out, ELM_ReqSetFilter, mu8_Channel, &k_Filter, sizeof(k_Filter));
}

// --------------------------------------------------------------------

// STEP 6)
// Connect to CAN bus, turn off the Tx LED
uint32_t Candlelight::Start(eDeviceFlags e_Flags)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    kDeviceMode k_Mode;
    k_Mode.flags = e_Flags;
    k_Mode.mode  = GS_ModeStart;

    k_Mode.flags |= ELM_DevFlagProtocolElmue; // required for this demo!
    if (mpk_Info->mk_Capability.feature & ELM_DevFlagSendUsbBlobs)
        k_Mode.flags |= ELM_DevFlagSendUsbBlobs;

    uint32_t u32_Error = CtrlTransfer(DIR_Out, GS_ReqSetDeviceMode, mu8_Channel, &k_Mode, sizeof(k_Mode)); // turn off Tx LED
    if (u32_Error)
        return u32_Error;

    mb_McuTimestamp = (e_Flags & GS_DevFlagTimestamp) > 0;
    mb_Started      = true;
    return u32_Error;
}

// Stop CAN bus and reset all variables and user settings in the adapter, turn on Tx LED
uint32_t Candlelight::Reset()
{
    mb_Started = false;

    // IMPORTANT: Set flag ELM_DevFlagProtocolElmue always to make sure that the device can send debug messages.
    // Should there be a legacy device connected, it will ignore all flags sent with GS_ModeReset
    kDeviceMode k_Mode;
    k_Mode.flags = ELM_DevFlagProtocolElmue;
    k_Mode.mode  = GS_ModeReset;
    return CtrlTransfer(DIR_Out, GS_ReqSetDeviceMode, mu8_Channel, &k_Mode, sizeof(k_Mode));
}

// ======================================= Send ========================================

// Send s32_Count CAN packets in one blob over USB to the firmware.
// This optimizes the USB speed to the maximum.
uint32_t Candlelight::SendPacketBlob(kCanPacket* pk_Packets, int s32_Count, int64_t* ps64_OsTimestamp)
{
    *ps64_OsTimestamp = -1;

    if (!mb_InitDone || !mb_Started)
        return ERR_OPERATION_INVALID;

    if ((mpk_Info->mk_Capability.feature & ELM_DevFlagSendUsbBlobs) == 0)
    {
        me_LastError = FBK_UnsupportedFeature;
        return ERR_CODE_IN_FEEDBACK;
    }

    // the firmware has a FIFO for max 64 packets
    if (s32_Count > CAN_QUEUE_SIZE)
        return ERR_TX_DATA_TOO_LONG;

    uint8_t u8_Transmit[MAX_BLOB_SIZE];
    kBlob* pk_Blob = (kBlob*)u8_Transmit;
    pk_Blob->frame_count = s32_Count;
    pk_Blob->msg_type    = MSG_TxBlob;

    int s32_Offset = sizeof(kBlob);
    for (int P=0; P<s32_Count; P++)
    {
        uint32_t u32_Error = TxPacketToTxBytes(&pk_Packets[P], u8_Transmit, sizeof(u8_Transmit), &s32_Offset);
        if (u32_Error)
            return u32_Error;
    }

    // Get timestamp immediately before sending the packet
    *ps64_OsTimestamp = GetOsTimestamp();

    return mi_OsLibrary.WritePipeOut(u8_Transmit, s32_Offset);
}

// CAN FD packets (b_FDF) can only be sent if a data baudrate has been set before.
// Remote frames (b_RTR = true): s32_DataLen = 0 --> DLC = 0 will be sent, or s32_DataLen = 1 and u8_Data[0] contains the DLC to send.
uint32_t Candlelight::SendPacket(kCanPacket* pk_Packet, int64_t* ps64_OsTimestamp)
{
    *ps64_OsTimestamp = -1;

    if (!mb_InitDone || !mb_Started)
        return ERR_OPERATION_INVALID;

    uint8_t u8_Transmit[256];

    int s32_Offset = 0;
    uint32_t u32_Error = TxPacketToTxBytes(pk_Packet, u8_Transmit, sizeof(u8_Transmit), &s32_Offset);
    if (u32_Error)
        return u32_Error;

    // Get timestamp immediately before sending the packet
    *ps64_OsTimestamp = GetOsTimestamp();

    return mi_OsLibrary.WritePipeOut(u8_Transmit, s32_Offset);
}

// If the packet has insufficient bytes to match one of the CAN FD DLC values, it will be padded with PAD_BYTE.
uint32_t Candlelight::TxPacketToTxBytes(kCanPacket* pk_Packet, uint8_t* u8_TxBuf, int s32_BufSize, int* ps32_Offset)
{
    // Pad missing bytes with zeroe's
    const uint8_t PAD_BYTE = 0;

    if (mi_OsLibrary.HasPipeErrors())
        return ERR_TOO_MANY_ERRORS;

    int s32_MaxData = mb_BaudFDSet ? 64 : 8;
    if (pk_Packet->mu8_DataLen > s32_MaxData)
        return ERR_PARAM_INVALID;

    // Remote frames do not exist in CAN FD
    if (mb_BaudFDSet && pk_Packet->mb_RTR)
        return ERR_PARAM_INVALID;

    // FDF and BRS flags require CAN FD
    if (!mb_BaudFDSet && (pk_Packet->mb_FDF || pk_Packet->mb_BRS))
        return ERR_PARAM_INVALID;

    // 3 + 64 messages have been sent to the firmware which were not acknowledged. 
    // The adapter is blocked --> report error once only.
    // If no errors were reported in the last 3 seconds the buffer is not full anymore
    if (mu64_TxOverflow > 0 && (cUtils::GetTickMilli() - mu64_TxOverflow) < 4000)
    {
        mu64_TxOverflow = 0;
        me_LastError    = FBK_TxBufferFull;
        return ERR_CODE_IN_FEEDBACK;
    }

    uint32_t u32_ID    = pk_Packet->mu32_ID;
    uint32_t u32_MaxID = pk_Packet->mb_29bit ? CAN_MASK_29 : CAN_MASK_11;
    if (u32_ID > u32_MaxID)
        return ERR_PARAM_INVALID;

    if (pk_Packet->mb_29bit) u32_ID |= CAN_ID_29Bit; // 29 bit CAN ID
    if (pk_Packet->mb_RTR)
    {
        u32_ID |= CAN_ID_RTR;  // Remote Transmission Request

        // Remote frames contain no data or one byte that defines the DLC value.
        if (pk_Packet->mu8_DataLen > 1)
            return ERR_PARAM_INVALID;
    }

    // set padding bytes to zero
    for (int i=pk_Packet->mu8_DataLen; i<64; i++)
    {
        pk_Packet->mu8_Data[i] = PAD_BYTE;
    }

    // Pad to match one of the CAN FD DLC values
         if (pk_Packet->mu8_DataLen > 48) pk_Packet->mu8_DataLen = 64;
    else if (pk_Packet->mu8_DataLen > 32) pk_Packet->mu8_DataLen = 48;
    else if (pk_Packet->mu8_DataLen > 24) pk_Packet->mu8_DataLen = 32;
    else if (pk_Packet->mu8_DataLen > 20) pk_Packet->mu8_DataLen = 24;
    else if (pk_Packet->mu8_DataLen > 16) pk_Packet->mu8_DataLen = 20;
    else if (pk_Packet->mu8_DataLen > 12) pk_Packet->mu8_DataLen = 16;
    else if (pk_Packet->mu8_DataLen >  8) pk_Packet->mu8_DataLen = 12;

    kTxFrameElmue k_TxFrame   = {0};
    k_TxFrame.header.size     = sizeof(kTxFrameElmue) + pk_Packet->mu8_DataLen;
    k_TxFrame.header.msg_type = MSG_TxFrame;
    k_TxFrame.can_id          = u32_ID;
    k_TxFrame.flags           = 0;
    if (pk_Packet->mb_FDF) k_TxFrame.flags |= FRM_FDF;
    if (pk_Packet->mb_BRS) k_TxFrame.flags |= FRM_BRS;

    if (*ps32_Offset + k_TxFrame.header.size >= s32_BufSize)
        return ERR_TX_DATA_TOO_LONG;

    // The STM32G431 supports to store a unique 8 bit marker for each sent frame which is returned when the frame has been acknowledged.
    // The firmware sends the marker back in kTxEchoElmue and we get the sent frame from mk_EchoFrames to display it to the user.
    // 255 markers are far more than enough because the processor has a Tx FIFO for 3 CAN packtes and the firmware can store
    // additionally 64 waiting frames in the queue. When a Tx buffer overflow is reported any further SendPacket() is blocked.
    if (mb_EnableTxEcho)
    {
        mu8_EchoMarker ++;
        if (mu8_EchoMarker == 0) 
            mu8_EchoMarker = 1;  // a marker value of zero does not send an echo
        k_TxFrame.marker = mu8_EchoMarker;
    }

    memcpy(&mk_EchoPackets[k_TxFrame.marker], pk_Packet, sizeof(kCanPacket));

    memcpy(u8_TxBuf + *ps32_Offset, &k_TxFrame, sizeof(kTxFrameElmue));
    *ps32_Offset += sizeof(kTxFrameElmue);

    memcpy(u8_TxBuf + *ps32_Offset, pk_Packet->mu8_Data, pk_Packet->mu8_DataLen);
    *ps32_Offset += pk_Packet->mu8_DataLen;

    return NO_ERROR;
}


// Receive a Rx packet, a Tx echo packet, an error frame, a debug message, a busload packet, or .......
// pk_Header and pb_RxBlob are only valid if the function does not return an error.
uint32_t Candlelight::ReceiveData(uint32_t u32_Timeout, kHeader** ppk_Header, int64_t* ps64_RxTimestamp, bool* pb_RxBlob)
{
    // This timestamp is only used in case that an error is returned
    *ps64_RxTimestamp = GetOsTimestamp();
    *ppk_Header = 0;
    if (pb_RxBlob) *pb_RxBlob = false;

    if (!mb_InitDone || !mb_Started)
        return ERR_OPERATION_INVALID;

    if (mi_OsLibrary.HasPipeErrors())
        return ERR_TOO_MANY_ERRORS;

    // Get frames form the IN pipe if there is no pending data in mk_UsbInPacket
    if (ms32_BlobFrames <= 0)
    {
        ms32_BlobFrames = 0;
        mu32_BlobOffset = 0;

        uint32_t u32_Error = mi_OsLibrary.ReadPipeIn(u32_Timeout, &mk_UsbInPacket); // loads mk_UsbInPacket
        if (u32_Error)
            return u32_Error;

        kBlob* pk_Blob = (kBlob*)mk_UsbInPacket.mu8_Buffer;
        if (pk_Blob->msg_type == MSG_RxBlob)
        {
            ms32_BlobFrames = pk_Blob->frame_count;
            mu32_BlobOffset = sizeof(kBlob);
        }
    }

    kHeader* pk_Header = (kHeader*)(mk_UsbInPacket.mu8_Buffer + mu32_BlobOffset);

    if (mu32_BlobOffset + pk_Header->size > mk_UsbInPacket.mu32_BytesRead)
    {
        ms32_BlobFrames = 0;
        return ERR_CORRUPT_IN_DATA;
    }

    if (pb_RxBlob) *pb_RxBlob = ms32_BlobFrames > 0; // FIRST

    ms32_BlobFrames --;                              // AFTER
    mu32_BlobOffset += pk_Header->size;

    *ppk_Header       = pk_Header;
    *ps64_RxTimestamp = mk_UsbInPacket.ms64_OsTimestamp;
    return NO_ERROR;    
}

kCanPacket Candlelight::RxFrameToCanPacket(kRxFrameElmue* pk_Frame)
{
    kCanPacket k_Packet = {0};
    k_Packet.mu32_ID    = (pk_Frame->can_id & CAN_MASK_29);
    k_Packet.mb_29bit   = (pk_Frame->can_id & CAN_ID_29Bit) != 0;
    k_Packet.mb_RTR     = (pk_Frame->can_id & CAN_ID_RTR)   != 0;
    k_Packet.mb_FDF     = (pk_Frame->flags  & FRM_FDF)      != 0;
    k_Packet.mb_BRS     = k_Packet.mb_FDF && (pk_Frame->flags & FRM_BRS) != 0;
    k_Packet.mb_ESI     = k_Packet.mb_FDF && (pk_Frame->flags & FRM_ESI) != 0;

    uint8_t* u8_StructStart = (uint8_t*) pk_Frame;
    uint8_t* u8_DataStart   = (uint8_t*)&pk_Frame->timestamp;
    if (mb_McuTimestamp) u8_DataStart += 4;

    k_Packet.mu8_DataLen = pk_Frame->header.size - (u8_DataStart - u8_StructStart);
    memcpy(k_Packet.mu8_Data, u8_DataStart, k_Packet.mu8_DataLen);
    return k_Packet;
}

kCanPacket Candlelight::GetTxEchoPacket(kTxEchoElmue* pk_TxEcho)
{
    return mk_EchoPackets[pk_TxEcho->marker];
}

// Get the content of a debug message from the adapter
string Candlelight::ConvertStringFrame(kStringElmue* pk_String)
{
    int s32_StrLen = pk_String->header.size - sizeof(kHeader);
    return string(pk_String->ascii_msg, s32_StrLen);
}

// ==========================================================================================

// Flashes the Rx + Tx LEDs on the board
uint32_t Candlelight::Identify(bool b_Blink)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    uint32_t u32_Mode = b_Blink ? 1 : 0; 
    return CtrlTransfer(DIR_Out, GS_ReqIdentify, mu8_Channel, &u32_Mode, sizeof(u32_Mode));
}

// Interval = 7 --> report busload in percent every 700 ms.
// NOTE: The firmware does not report the busload if bus load is permanently 0%.
uint32_t Candlelight::EnableBusLoadReport(uint8_t u8_Interval)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    return CtrlTransfer(DIR_Out, ELM_ReqSetBusLoadReport, mu8_Channel, &u8_Interval, sizeof(u8_Interval));
}

// Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
// Enabling the pin needs not to be implemented here.
// The pin is automatically enabled when entering DFU mode with EnterDfuMode()
uint32_t Candlelight::DisableBootPin()
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    kPinStatus k_PinStatus = {0};
    k_PinStatus.Operation  = PINOP_Disable;
    k_PinStatus.PinID      = PINID_BOOT0;
    return CtrlTransfer(DIR_Out, ELM_ReqSetPinStatus, mu8_Channel, &k_PinStatus, sizeof(k_PinStatus));
}

// Read the detailed documentation about pin BOOT0 on https://netcult.ch/elmue/CANable%20Firmware%20Update
uint32_t Candlelight::IsBootPinEnabled(bool* pb_Enabled)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    // The requested pin ID must be transmitted in SETUP.wValue because a USB IN request cannot otherwise transmit parameters to the firmware.
    uint16_t u16_PinStatus;
    uint32_t u32_Error = CtrlTransfer(DIR_In, ELM_ReqGetPinStatus, PINID_BOOT0, &u16_PinStatus, sizeof(u16_PinStatus));
    if (u32_Error)
        return u32_Error;

    *pb_Enabled = (u16_PinStatus & PINST_Enabled) > 0;
    return NO_ERROR;
}

// Write user data to flash memory. The firmware also stores the length of the data and returns the same data in ReadFlash()
// A segment of the STM32G431 has 2 kB. Segment 0 is the last segment in the flash memory.
// ATTENTION: u8_Buffer must point to RAM memory, otherwise ERROR_NOACCESS.
uint32_t Candlelight::WriteFlash(uint8_t u8_Segment, uint8_t* u8_Buffer, uint32_t u32_DataLen)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    return CtrlTransfer(DIR_Out, ELM_ReqWriteFlash, u8_Segment, u8_Buffer, u32_DataLen);
}

// Read user data from the flash memory that was written before with WriteFlash()
// A segment of the STM32G431 has 2 kB. Segment 0 is the last segment in the flash memory.
// *pu32_DataRead returns the count of bytes that was written into u8_Buffer.
uint32_t Candlelight::ReadFlash(uint8_t u8_Segment, uint8_t* u8_Buffer, uint32_t u32_BufSize, uint32_t* pu32_DataRead)
{
    if (!mb_InitDone || mu8_Interface == DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    return CtrlTransfer(DIR_In, ELM_ReqReadFlash, u8_Segment, u8_Buffer, u32_BufSize, pu32_DataRead);
}

// --------------------------------------------------------------------

// Send a SETUP request to the firmware
// u32_DataSize must be the expected byte count to be received from the firmware or to be sent to the firmware.
// u8_Request must be eUsbRequest for interface 0 and eDfuRequest for interface 1.
// This function can obtain the feedback from the ElmüSoft firmware, but works also with legacy firmware.
// ATTENTION: p_Data must point to RAM memory, otherwise ERROR_NOACCESS.
uint32_t Candlelight::CtrlTransfer(eDirection e_Dir, uint8_t u8_Request, uint16_t u16_Value, 
                                   void* p_Data, uint32_t u32_DataSize, 
                                   uint32_t* pu32_DataRead) // = NULL
{
    if (pu32_DataRead) *pu32_DataRead = 0;

    // A Control Transfer must not exceed 4 kB.
    if (u32_DataSize > 4096)
        return ERR_TX_DATA_TOO_LONG;

    // The Candlelight interface implements Vendor requests while the DFU interface implements Class requests.
    eSetupType e_Type = (mu8_Interface == DFU_INTERFACE) ? TYP_Class : TYP_Vendor;

    kSetup k_Setup;
    k_Setup.RequestType = RECIP_Interface | e_Type | e_Dir;
    k_Setup.Request     = u8_Request;
    k_Setup.Value       = u16_Value;     // Channel / PinID for ELM_ReqGetPinStatus
    k_Setup.Index       = mu8_Interface; // destination interface (0 = Candlelight, 1 = DFU Firmware Update)
    k_Setup.Length      = 0;             // set by WinUSB to u32_DataSize

    // -------- Execute Request ------------

    uint32_t u32_CmdBytes;
    // ATTENTION: returns ERROR_NOACCESS if p_Data is not in RAM !
    uint32_t u32_CmdErr = mi_OsLibrary.ControlTransfer(&k_Setup, (uint8_t*)p_Data, u32_DataSize, &u32_CmdBytes);

    // The DFU interface sends no feedback
    if (mu8_Interface != DFU_INTERFACE)
    {
        // ---------- Get Feedback -------------

        // ALWAYS get the feedback, even if the previous command execution did NOT return an error!
        // In second stage of the SETUP request the firmware can NOT stall the endpoint which is the only way to alert an USB error.

        k_Setup.RequestType = RECIP_Interface | TYP_Vendor | DIR_In;
        k_Setup.Request     = ELM_ReqGetLastError;

        uint8_t  u8_Feedback; // feedback is a one byte response
        uint32_t u32_FbkBytes;
        uint32_t u32_FbkErr = mi_OsLibrary.ControlTransfer(&k_Setup, &u8_Feedback, sizeof(u8_Feedback), &u32_FbkBytes);

        me_LastError = (eFeedback)u8_Feedback;

        // --------- Process Errors ------------

        // me_LastError is only valid if u32_FbkErr == NO_ERROR
        // if a legacy board is connected it will not understand request ELM_ReqGetLastError --> Endpoint stalled --> u32_FbkErr = ERROR_GEN_FAILURE
        if (u32_FbkErr == NO_ERROR && me_LastError != FBK_Success)
            return ERR_CODE_IN_FEEDBACK;
    }

    if (u32_CmdErr)
        return u32_CmdErr;

    if (e_Dir == DIR_In)
    {
        // When reading flash memory the firmware will return less bytes than requested, this is not an error.
        if (u8_Request != ELM_ReqReadFlash)
        {
            if (u32_CmdBytes < u32_DataSize) 
                return ERR_INVALID_RX_DATA; 
        }
    }

    if (pu32_DataRead) *pu32_DataRead = u32_CmdBytes;
    return NO_ERROR;
}

// =======================================================================================================================

// Formats a timestamp with 1 µs precision
// returns "HH:MM:SS.mmm.µµµ"
// pk_Header may contain a timestamp if GS_DevFlagTimestamp is set --> mb_McuTimestamp = true
// otherwise use s64_OsTimestamp which comes from GetOsTimestamp() at packet reception
string Candlelight::FormatTimestamp(kHeader* pk_Header, int64_t s64_OsTimestamp)
{
    if (!mb_Started) // the variable mb_McuTimestamp is not yet valid
        return "Not Initialized ";

    int64_t s64_Stamp = -1;
    if (mb_McuTimestamp)
    {
        if (pk_Header != NULL)
        {
            switch (pk_Header->msg_type)
            {
                // These 3 messages send firmware timestamps
                case MSG_TxEcho:  s64_Stamp = ((kTxEchoElmue*) pk_Header)->timestamp; break;
                case MSG_RxFrame: s64_Stamp = ((kRxFrameElmue*)pk_Header)->timestamp; break;
                case MSG_Error:   s64_Stamp = ((kErrorElmue*)  pk_Header)->timestamp; break;
            }
        }

        if (s64_Stamp >= 0)
        {
            // The 32 bit firmware timestamp will roll over after 1 hour, this must be detected here.
            // ATTENTION: The MCU may send an Rx packet with a lower timestamp than the previous Rx packet.
            // This may happen --> ignore small jumps back in time and detect only big jumps.
            if (s64_Stamp         <  0x10000000 &&
                ms64_LastMcuStamp >  0xF0000000)
                ms64_McuRollOver += 0x100000000;
            
            ms64_LastMcuStamp = s64_Stamp;

            // roll-over compensated 64 bit timestamp
            s64_Stamp += ms64_McuRollOver;
        }
    }
    else // Operating System performance counter timestamps are used
    {
        s64_Stamp = s64_OsTimestamp;
    }

    if (s64_Stamp < 0)
        return "No Timestamp    ";

    uint32_t u32_Micro = s64_Stamp % 1000;
    s64_Stamp /= 1000;
    uint32_t u32_Milli = s64_Stamp % 1000;
    s64_Stamp /= 1000;
    uint32_t u32_Sec   = s64_Stamp % 60;
    s64_Stamp /= 60;
    uint32_t u32_Min   = s64_Stamp % 60;
    s64_Stamp /= 60;
    uint32_t u32_Hour  = s64_Stamp % 24;

    return cUtils::Format("%02u:%02u:%02u.%03u.%03u", u32_Hour, u32_Min, u32_Sec, u32_Milli, u32_Micro);
}


string Candlelight::FormatCanPacket(kCanPacket* pk_Packet)
{
    string s_Frame;
    if (pk_Packet->mb_29bit) s_Frame = cUtils::Format("%08X: ", pk_Packet->mu32_ID & CAN_MASK_29);
    else                     s_Frame = cUtils::Format("%03X: ", pk_Packet->mu32_ID & CAN_MASK_11);

    // For remote frames the DLC (0...8) may be transmitted in the first data byte.
    // The display of "7E8: RTR [5]" means that a remote request with DLC = 5 has been sent/received
    if (pk_Packet->mb_RTR)
    {
        s_Frame += "RTR ["; // Remote Transmission Request
        if (pk_Packet->mu8_DataLen > 0) s_Frame += (char)(pk_Packet->mu8_Data[0] + '0');
        else                            s_Frame += "0";
        s_Frame += "]";
    }
    else
    {
        s_Frame += cUtils::FormatHexBytes(pk_Packet->mu8_Data, pk_Packet->mu8_DataLen);

        if (pk_Packet->mb_FDF || pk_Packet->mb_BRS || pk_Packet->mb_ESI) s_Frame += "-";

        if (pk_Packet->mb_FDF) s_Frame += " FDF"; // Flexible Datarate Frame
        if (pk_Packet->mb_BRS) s_Frame += " BRS"; // Bitrate Switch
        if (pk_Packet->mb_ESI) s_Frame += " ESI"; // Error Indicator
    }
    return s_Frame;
}


// From the multiple flags that have been defined by previous programmers we check only those which the CANable 2.5 firmware sets.
// pe_BusStatus returns the current bus status (active, warning, passive, off)
// pe_Level return the error level (low, ledium, high)
string Candlelight::FormatCanErrors(kErrorElmue* pk_Error, eErrorBusStatus* pe_BusStatus, eErrorLevel* pe_Level)
{
    eErrFlagsCanID e_ID    = (eErrFlagsCanID)pk_Error->err_id;
    eErrFlagsByte1 e_Byte1 = (eErrFlagsByte1)pk_Error->err_data[1];
    eErrFlagsByte2 e_Byte2 = (eErrFlagsByte2)pk_Error->err_data[2];
    eErrorAppFlags e_App   = (eErrorAppFlags)pk_Error->err_data[5];

    if (e_App & APP_CanTxOverflow) mu64_TxOverflow = cUtils::GetTickMilli(); // block sending further packets
    else                           mu64_TxOverflow = 0;    
    
    *pe_BusStatus = BUS_StatusActive;
    *pe_Level     = LEVEL_Low;

    string s_Mesg;
    if (e_ID & ERID_Bus_is_off) 
    {
        *pe_BusStatus = BUS_StatusOff;
        *pe_Level     = LEVEL_High;
        s_Mesg += "Bus Off, ";
    }
    else if (e_Byte1 & (ER1_Rx_Passive_status_reached  | ER1_Tx_Passive_status_reached))
    {
        *pe_BusStatus = BUS_StatusPassive;
        *pe_Level     = LEVEL_High;
        s_Mesg += "Bus Passive, ";
    }
    else if (e_Byte1 & (ER1_Rx_Errors_at_warning_level | ER1_Tx_Errors_at_warning_level))
    {
        *pe_BusStatus = BUS_StatusWarning;
        *pe_Level     = LEVEL_Medium;
        s_Mesg += "Bus Warning, ";
    }
    else // Active
    {
        if (e_Byte1 & ER1_Bus_is_back_active) s_Mesg += "Back to Active, ";
        else                                  s_Mesg += "Bus Active, ";
    }

    // all errors generated by the firmware are bigger problems (Level High)
    if (e_App > 0) *pe_Level = LEVEL_High;
    if (e_App & APP_CanRxFail)      s_Mesg += "Rx Failed, ";
    if (e_App & APP_CanTxFail)      s_Mesg += "Tx Failed, ";
    if (e_App & APP_CanTxTimeout)   s_Mesg += "Tx Timeout, ";
    if (e_App & APP_CanTxOverflow)  s_Mesg += "CAN Tx Overflow, ";
    if (e_App & APP_UsbInOverflow)  s_Mesg += "USB IN Overflow, ";

    // Error cause
    if (e_ID    & ERID_No_ACK_received)             s_Mesg += "No ACK received, ";
    if (e_ID    & ERID_CRC_Error)                   s_Mesg += "CRC Error, ";
    if (e_Byte2 & ER2_Bit_stuffing_error)           s_Mesg += "Bit Stuffing Error, ";
    if (e_Byte2 & ER2_Frame_format_error)           s_Mesg += "Frame Format Error, ";    // e.g. CAN FD frame received in classic mode
    if (e_Byte2 & ER2_Unable_to_send_dominant_bit)  s_Mesg += "Dominant Bit Error, ";
    if (e_Byte2 & ER2_Unable_to_send_recessive_bit) s_Mesg += "Recessive Bit Error, ";

    char c_Buf[50];
    if (pk_Error->err_data[6] > 0) 
    {
        sprintf_s(c_Buf, "Tx Errors: %u, ", pk_Error->err_data[6]);
        s_Mesg += c_Buf;
    }
    if (pk_Error->err_data[7] > 0) 
    {
        sprintf_s(c_Buf, "Rx Errors: %u, ", pk_Error->err_data[7]);
        s_Mesg += c_Buf;
    }
    return cUtils::TrimRight(s_Mesg, ", ");
}

string Candlelight::FormatLastError(uint32_t u32_Error)
{
    assert(u32_Error != NO_ERROR); // calling this function without an error code make no sense

    switch (u32_Error)
    {
        case ERR_DEVICE_IN_USE:     return "Access denied. Probably the device is already open elsewhere.";
        case ERR_INVALID_DEVICE:    return "The device is not a Candlelight adapter.";
        case ERR_INVALID_FIRMWARE:  return "This demo supports only devices that have the CANable 2.5 firmware from ElmüSoft.";
        case ERR_RX_FIFO_OVERFLOW:  return "USB Rx FIFO overflow. Polling is too slow."; // in the demo app the reason is the slow Windows console.
        case ERR_CORRUPT_IN_DATA:   return "Corrupt USB IN data received.";
        case ERR_UPDATE_FIRMWARE:   return "Please upload the latest firmware to the device.";
        case ERR_TOO_MANY_ERRORS:   return "Too many errors. The CANable has a problem or was disconnected.";
        case ERR_TX_DATA_TOO_LONG:  return "The Tx data is too long.";
        case ERR_OPERATION_INVALID: return "Invalid operation.";
        case ERR_PARAM_INVALID:     return "Invalid parameter.";
        case ERR_INVALID_RX_DATA:   return "Invalid Rx data was received from the device";
        case ERR_TIMEOUT:           return "Timeout waiting for data";
        case ERR_NO_DRIVER:         return "The driver is not installed correctly";

        case ERR_CODE_IN_FEEDBACK:
        {
            switch (me_LastError)
            {
                case FBK_InvalidCommand:      return "The command is invalid.";
                case FBK_InvalidParameter:    return "One of the parameters is invalid.";
                case FBK_AdapterMustBeOpen:   return "This command cannot be executed before opening the adapter.";
                case FBK_AdapterMustBeClosed: return "This command cannot be executed after  opening the adapter.";
                case FBK_ErrorFromHAL:        return "The HAL from ST Microelectronics has reported an error.";
                case FBK_UnsupportedFeature:  return "The feature is not implemented or not supported by the board.";
                case FBK_TxBufferFull:        return "Sending is not possible because the Tx buffer is full.";
                case FBK_BusIsOff:            return "Sending is not possible because the processor is blocked in BusOff state.";
                case FBK_NoTxInSilentMode:    return "Sending is not possible because the adapter is in bus monitoring mode.";
                case FBK_BaudrateNotSet:      return "The baudrate has not been set.";
                case FBK_OptBytesProgrFailed: return "Programming the Option Bytes failed.";
                case FBK_ResetRequired:       return "Please reconnect the USB cable.";
                case FBK_ParamOutOfRange:     return "A paramter is outside the valid range.";
                default:       return cUtils::Format("Unknown feedback code %d received from the device.", me_LastError);
            }
        }
    }
    return OsLibrary::GetErrorMessage(u32_Error);
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
// If you want to update the firmware use HUD ECU Hacker which comes with a very comfortable STM32 Firmware Programmer.
uint32_t Candlelight::EnterDfuMode()
{
    if (!mb_InitDone || mu8_Interface != DFU_INTERFACE)
        return ERR_OPERATION_INVALID;

    // The legacy firmware would have entered immediately in DFU mode and CtrlTransfer() would have returned error 31 here.
    // But the CANable 2.5 firmware responds correctly to all SETUP requests because it makes a delay of 300 ms before entering DFU mode.
    uint32_t u32_Error = CtrlTransfer(DIR_Out, DFU_RequDetach, 0, NULL, 0);
    if (u32_Error)
        return u32_Error;

    kDfuStatus k_Status;
    // returned Error must be ignored here because legacy devices enter boot mode immediately and CtrlTransfer will return error 31.
    if (CtrlTransfer(DIR_In, DFU_RequGetStatus, 0, &k_Status, sizeof(k_Status)) == NO_ERROR)
    {
        // Here k_Status.State is either DfuSte_AppIdle or DfuSte_AppDetach or DfuSte_Error.

        // returning AppDetach has been added by ElmüSoft to the firmware and means that the user must reconnect the USB cable.
        // This happens only if the pin BOOT0 was disabled before calling EnterDfuMode()
        if (k_Status.State == DfuState_AppDetach)
        {
            me_LastError = FBK_ResetRequired; // The user must reconnect the USB cable now.
            return ERR_CODE_IN_FEEDBACK;
        }

        // Since firmware 17.May.2026 the feedback code is transferred in StringIdx.
        // Feedback = UnsupportedFeature, AdapterMustBeClosed, OptBytesProgrFailed
        if (k_Status.State == DfuState_Error && k_Status.StringIdx > 0 && k_Status.StringIdx < 255)
        {
            me_LastError = (eFeedback)k_Status.StringIdx;
            return ERR_CODE_IN_FEEDBACK;
        }
    }

    // The device will enter DFU mode in 300 ms --> the WinUSB handle is not valid anymore.
    Close();
    return NO_ERROR;
}


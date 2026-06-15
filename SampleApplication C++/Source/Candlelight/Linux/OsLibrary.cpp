
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
//  Study the class "Windows/OsLibrary.cpp" and re-write it for Linux, then send it to elmue@gmx.de
//  The WinUSB library must be replaced with the libusb library.
//  The thread and the Rx FIFO in the Windows class can probably be removed when using libusb.
//
// =======================================================================================================

#include "OsLibrary.h"

#define  LANGUAGE_ENGLISH_USA   0x409

using namespace CANable;

// Constructor
OsLibrary::OsLibrary()
{
    ..... TODO    
}

// Destructor
OsLibrary::~OsLibrary()
{
    ..... TODO    
}

// Called from Candlelight::Open()
uint32_t OsLibrary::Open(string s_DevicePath)
{
    mk_Info.Clear();
    
    .... TODO
    
    return NO_ERROR;    
}

// This is not called for the DFU interface which has no endpoints
uint32_t OsLibrary::StartPipes()
{
    .... TODO
    
    return NO_ERROR;
}

// Called from Candlelight::Close()
void OsLibrary::Close()
{
}

// ===================================== CTRL Pipe =====================================

// Send SETUP packet and optionally additional data bytes as IN or OUT transfer
// Timeout has been set to 500 ms in Open()
uint32_t OsLibrary::ControlTransfer(kSetup* pk_Setup, uint8_t* u8_Buffer, uint32_t u32_BufLen, uint32_t* pu32_Transferred)
{
    ..... TODO
    
    libusb_control_transfer()
     
    return NO_ERROR;
}

// ===================================== OUT Pipe ======================================

// Timeout has been set to 500 ms in StartPipes()
uint32_t OsLibrary::WritePipeOut(uint8_t* u8_Transmit, uint32_t u32_TxLen)
{
    ..... TODO
    
    libusb_bulk_transfer()
    
    return NO_ERROR;
}

// ====================================== IN Pipe =======================================

// Get the next frame from USB and copy it to pk_UsbInPacket.
// If no data received during timeout return ERR_TIMEOUT.
uint32_t OsLibrary::ReadPipeIn(uint32_t u32_Timeout, kUsbInPacket* pk_UsbInPacket)
{
    ..... TODO
    
    libusb_bulk_transfer()
    
    return NO_ERROR;
}

// =================================== Enumerate USB Devices ==================================

// Returns device name, serial number and device path
// b_Candlelight = false -> this function enumerates the DFU interfaces.
uint32_t OsLibrary::EnumDevices(bool b_Candlelight, vector<kUsbDevice>* pi_Devices)
{
    ..... TODO
    
    libusb_get_device_list()
    libusb_get_device_descriptor()   
    
    return NO_ERROR;
}

// ===================================== Console =====================================

// Set console title, buffer size and window size
void OsLibrary::SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, string s_Title)
{
    ..... TODO    
}

// Print coloured console output (max 2000 chars!)
void OsLibrary::PrintConsole(uint16_t u16_Color, string s_Format, ...)
{
    ..... TODO
}

// Check if the user has pressed the ENTER key in the console (non-blocking function)
bool OsLibrary::CheckConsoleEnterPressed()
{
    ..... TODO
}

// Blocks until the user hits a key, returns the ASCII code
int OsLibrary::WaitConsoleChar()
{
    ..... TODO
}

// ===================================== Helpers =====================================

// Create a timestamp with 1 µs precision.
// The returned timestamp starts at zero when the device is opened.
// It is recommended to turn off transmssion of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
// Then this function is used as a replacement to generate a timestamp on reception of a USB packet and when sending a packet.
int64_t OsLibrary::GetTimestamp()
{
    ..... TODO    
}

// Convert API error code to text message
string OsLibrary::GetErrorMessage(uint32_t u32_Error)
{
    ..... TODO
}


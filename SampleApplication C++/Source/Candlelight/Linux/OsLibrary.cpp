
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
//  I'am a Windows developer and I'm not interested in Linux.
//  However, I wrote this class for the Linux community with the help of Gemini.
//  This class has never been compiled and never been tested.
//  Finish and test this class on Linux, then send it to elmue@gmx.de
//
// =======================================================================================================

// see also includes in Utils.h
#include <iomanip>
#include <fstream>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <filesystem> // Requires C++17
#include <libusb-1.0/libusb.h>
#include "OsLibrary.h"

using namespace CANable;

// Constructor
OsLibrary::OsLibrary()
{
    mpi_UsbContext     = nullptr;
    mppi_UsbDeviceList = nullptr;
    mpi_DevHandle      = nullptr;
    mb_IsOpen          = false;
}

// Destructor
OsLibrary::~OsLibrary()
{
    Close();

    if (mppi_UsbDeviceList)
        libusb_free_device_list(mppi_UsbDeviceList, 1);

    if (mpi_UsbContext)
        libusb_exit(mpi_UsbContext);
}

// Called from Candlelight::Open() only if the device is not already open
// pk_Device comes from OsLibrary::EnumDevices()
uint32_t OsLibrary::Open(kUsbDevice* pk_Device)
{
    mu32_RxPipeErrors   = 0;
    mu32_TxPipeErrors   = 0;
    ms64_TimestampStart = 0;
    mk_Info.Clear();

    libusb_device* pi_UsbDevice = (libusb_device*)pk_Device->mpi_LinuxDevice;

    int s32_Error = libusb_get_device_descriptor(pi_UsbDevice, (libusb_device_descriptor*)&mk_Info.mk_DeviceDescr);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    s32_Error = libusb_open(pi_UsbDevice, &mpi_DevHandle);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    // ------------------------

    char s8_Buffer[256];
    int s32_Written = libusb_get_string_descriptor_ascii(mpi_DevHandle, mk_Info.mk_DeviceDescr.iManufacturer, (uint8_t*)s8_Buffer, sizeof(s8_Buffer));
    if (s32_Written < 0)
        return (uint32_t)s32_Written;

    mk_Info.ms_Vendor = s8_Buffer;

    int s32_Written = libusb_get_string_descriptor_ascii(mpi_DevHandle, mk_Info.mk_DeviceDescr.iProduct,      (uint8_t*)s8_Buffer, sizeof(s8_Buffer));
    if (s32_Written < 0)
        return (uint32_t)s32_Written;

    mk_Info.ms_Product = s8_Buffer;

    int s32_Written = libusb_get_string_descriptor_ascii(mpi_DevHandle, mk_Info.mk_DeviceDescr.iSerialNumber, (uint8_t*)s8_Buffer, sizeof(s8_Buffer));
    if (s32_Written < 0)
        return (uint32_t)s32_Written;

    mk_Info.ms_Serial = s8_Buffer;

    // ------------------------

    libusb_config_descriptor* pk_ConfigDesc;
    s32_Error = libusb_get_active_config_descriptor(pi_UsbDevice, &pk_ConfigDesc);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    const libusb_interface*            pk_Interface  = &pk_ConfigDesc->interface[pk_Device->ms32_Interface];
    const libusb_interface_descriptor* pk_InterfDesc = &pk_Interface->altsetting[0];

    // copy the first 9 bytes of libusb_interface_descriptor
    memcpy(&mk_Info.mk_InterfDescr, pk_InterfDesc, sizeof(kInterfaceDescriptor));

    int s32_Written = libusb_get_string_descriptor_ascii(mpi_DevHandle, pk_InterfDesc->iInterface, (uint8_t*)s8_Buffer, sizeof(s8_Buffer));
    if (s32_Written < 0)
    {
        libusb_free_config_descriptor(pk_ConfigDesc);
        return (uint32_t)s32_Written;
    }

    mk_Info.ms_Interface = s8_Buffer;

    // ------------------------

    // Get the 2 endpoints of the Candlelight interface (the Firmware Update interface has bNumEndpoints == 0)
    for (uint8_t P=0; P<pk_InterfDesc->bNumEndpoints; P++)
    {
        libusb_endpoint_descriptor* pk_Endpoint = pk_InterfDesc->endpoint[P];
        if ((pk_Endpoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
        {
            libusb_free_config_descriptor(pk_ConfigDesc);
            return ERR_INVALID_DEVICE;
        }

        if (pk_Endpoint->bEndpointAddress & 0x80) // IN
        {
            mk_Info.mu8_EndpointIN     = pk_Endpoint->bEndpointAddress;
            mk_Info.mu16_MaxPackSizeIN = pk_Endpoint->wMaxPacketSize;
        }
        else // OUT
        {
            mk_Info.mu8_EndpointOUT     = pk_Endpoint->bEndpointAddress;
            mk_Info.mu16_MaxPackSizeOUT = pk_Endpoint->wMaxPacketSize;
        }
    }

    libusb_free_config_descriptor(pk_ConfigDesc);

    mb_IsOpen = true;
    return NO_ERROR;
}

// This is not called for the Firmware Update interface which has no endpoints
uint32_t OsLibrary::StartPipes()
{
    // this function is not needed for libusb
    return NO_ERROR;
}

// Called from Candlelight::Close()
void OsLibrary::Close()
{
    if (mpi_DevHandle)
    {
        libusb_close(mpi_DevHandle);
        mpi_DevHandle = nullptr;
    }
    mb_IsOpen = false;
}

// ===================================== CTRL Pipe =====================================

// Send SETUP packet and optionally additional data bytes as IN or OUT transfer
// Timeout has been set to 500 ms in Open()
uint32_t OsLibrary::ControlTransfer(kSetup* pk_Setup, uint8_t* u8_Buffer, uint32_t* pu32_Transferred)
{
    int s32_Transferred = libusb_control_transfer(mpi_DevHandle, pk_Setup->bRequestType, pk_Setup->bRequest,
                                                  pk_Setup->wValue, pk_Setup->wIndex, u8_Buffer, pk_Setup->wLength, PIPE_TIMEOUT);
    if (s32_Transferred < 0)
        return (uint32_t)s32_Transferred; // error

    *pu32_Transferred = (uint32_t)s32_Transferred;
    return NO_ERROR;
}

// ===================================== OUT Pipe ======================================

// Timeout has been set to 500 ms in StartPipes()
uint32_t OsLibrary::WritePipeOut(uint8_t* u8_TxData, uint32_t u32_TxLen)
{
    int s32_Transferred;
    int s32_Error = libusb_bulk_transfer(mpi_DevHandle, mk_Info.mu8_EndpointOUT, u8_TxData,
                                        (int)u32_TxLen, &s32_Transferred, PIPE_TIMEOUT);
    if (s32_Error < 0)
    {
        mu32_TxPipeErrors ++;
        return (uint32_t)s32_Error;
    }
    mu32_TxPipeErrors = 0;
    return NO_ERROR;
}

// ====================================== IN Pipe =======================================

// Get the next frame from USB into pk_UsbInPacket.
// If no data received during timeout return ERR_TIMEOUT.
uint32_t OsLibrary::ReadPipeIn(uint32_t u32_Timeout, kUsbInPacket* pk_UsbInPacket)
{
    int s32_Transferred;
    int s32_Error = libusb_bulk_transfer(mpi_DevHandle, mk_Info.mu8_EndpointIN, pk_UsbInPacket->mu8_Buffer,
                                         MAX_BLOB_SIZE, &s32_Transferred, u32_Timeout);

    pk_UsbInPacket->mu32_BytesRead   = (uint32_t)s32_Transferred;
    pk_UsbInPacket->mu32_Error       = (uint32_t)s32_Error;
    pk_UsbInPacket->ms64_OsTimestamp = GetTimestamp();

    if (s32_Error < 0)
    {
        if (s32_Error == LIBUSB_ERROR_TIMEOUT)
            return ERR_TIMEOUT;

        mu32_RxPipeErrors ++;
        return (uint32_t)s32_Error;
    }
    mu32_RxPipeErrors = 0;
    return NO_ERROR;
}

// =================================== Enumerate USB Devices ==================================

// Returns device name, serial number and libusb_device of all connected Candlelight devices.
// b_GetCandlelight = false -> this function enumerates the Firmware Update interfaces.
// Calling EnumDevices() again will invalidate any previously returned device list
uint32_t OsLibrary::EnumDevices(bool b_GetCandlelight, vector<kUsbDevice>* pi_Devices)
{
    int s32_Error;
    if (!mpi_UsbContext) // init once only
    {
        s32_Error = libusb_init(&mpi_UsbContext);
        if (s32_Error < 0)
            return (uint32_t)s32_Error;
    }

    if (mppi_UsbDeviceList) // free any previous list
    {
        libusb_free_device_list(mppi_UsbDeviceList, 1)
        mppi_UsbDeviceList = nullptr;
    }

    ssize_t s32_DevCount = libusb_get_device_list(mpi_UsbContext, &mppi_UsbDeviceList);
    if (s32_DevCount < 0)
        return (uint32_t)s32_DevCount; // s32_DevCount is error code

    // enumerate USB devices
    for (ssize_t D = 0; D < s32_DevCount; D++)
    {
        libusb_device* pi_UsbDevice = mppi_UsbDeviceList[D];

        libusb_device_descriptor k_DevDescr;
        s32_Error = libusb_get_device_descriptor(pi_UsbDevice, &k_DevDescr);
        if (s32_Error < 0)
            return (uint32_t)s32_Error;

        if (k_DevDescr.idVendor  != 0x1D50 || // OpenMoko Inc.
            k_DevDescr.idProduct != 0x606F)
            continue; // not a Candlelight device

        libusb_config_descriptor* pk_ConfigDesc;
        s32_Error = libusb_get_active_config_descriptor(pi_UsbDevice, &pk_ConfigDesc);
        if (s32_Error < 0)
            return (uint32_t)s32_Error;

        // If there are not at least 2 interfaces, it is not a valid Candlelight device
        if (pk_ConfigDesc->bNumInterfaces >= 2)
        {
            // get string descriptors without opening the device
            char s8_Product[256];
            s32_Error = libusb_get_device_string(pi_UsbDevice, LIBUSB_DEVICE_STRING_PRODUCT, s8_Product, sizeof(s8_Product));
            if (s32_Error < 0)
                return (uint32_t)s32_Error;
            
            char s8_Serial[256];            
            s32_Error = libusb_get_device_string(pi_UsbDevice, LIBUSB_DEVICE_STRING_SERIAL_NUMBER, s8_Serial, sizeof(s8_Serial));
            if (s32_Error < 0)
                return (uint32_t)s32_Error;
            
            // ---------------------------------
           
            // Now we build the Linux device path in multiple steps:
            // DevicePath = "/sys/class/usb_device/usbdev1.4/device/1-1.2:1.0"
            // where        "............................N.D/....../N-R.H:C.I"
            // means: N= BusNumber, D= DeviceAddress, R= RootPort, H= HubPort, C= ConfigValue, I= Interface Number

            uint8_t u8_BusNumber  = libusb_get_bus_number    (pi_UsbDevice);
            uint8_t u8_DeviceAddr = libusb_get_device_address(pi_UsbDevice);
            string s_BasePath = cUtils::Format("/sys/class/usb_device/usbdev%u.%u/device/%u-",
                                               u8_BusNumber, u8_DeviceAddr, u8_BusNumber);   

            uint8_t u8_Ports[7]; // Linux supports up to 7 tiers in topology
            int s32_PortCount = libusb_get_port_numbers(pi_UsbDevice, u8_Ports, sizeof(u8_Ports));
            if (s32_PortCount < 0)
                return (uint32_t)s32_PortCount;
            
            for (int P = 0; P < s32_PortCount; P++) 
            {
                if (P > 0) s_BasePath += ".";
                s_BasePath += cUtils::Format("%u", u8_Ports[P]);
            }
            
            s_BasePath += cUtils::Format(":%u.", pk_ConfigDesc->bConfigurationValue);
            
            // enumerate interfaces
            for (uint8_t I = 0; I < pk_ConfigDesc->bNumInterfaces; I++)
            {
                // The Firmware Update interface is always the second interface (I == 1)
                // The others are Candlelight interfaces: (I == 0, 2, 3,...)
                bool b_IsCandle = (I != FIRMW_UPDATE_INTERFACE);
                if  (b_IsCandle != b_GetCandlelight)
                    continue; // not the requested interface type

                const libusb_interface* pk_Interface = &pk_ConfigDesc->interface[I];

                if (pk_Interface->num_altsetting != 1)
                    continue; // not a valid Candlelight device

                const libusb_interface_descriptor* pk_InterfDesc = &pk_Interface->altsetting[0];
                
                kUsbDevice k_UsbDev;
                k_UsbDev.mpi_LinuxDevice = pi_UsbDevice;
                k_UsbDev.ms32_Interface  = I;
                k_UsbDev.ms_Product      = s8_Product;
                k_UsbDev.ms_SerialNo     = s8_Serial;
                k_UsbDev.ms_DevicePath   = s_BasePath + cUtils::Format("%u", pk_InterfDesc->bInterfaceNumber);
                k_UsbDev.ms_Interface    = ReadSysfsString(k_UsbDev.ms_DevicePath + "/interface");

                pi_Devices->push_back(k_UsbDev);
            }
        }

        libusb_free_config_descriptor(pk_ConfigDesc);
    }
    return NO_ERROR;
}

// Read strings that are cached in the kernel.
// This avoids sending USB requests to each USB device which would be slow or even hang if a device does not respond.
string ReadSysfsString(string s_Path)
{
    if (!fs::exists(s_Path))
    {
        assert(false);
        return "[Invalid Path]";
    }

    ifstream i_File(s_Path);
    if (!i_File.is_open())
    {
        assert(false);
        return "[Device not open]"; // e.g. device disconnected / suspended
    }

    string s_Line;
    if (getline(i_File, s_Line))
        return cUtils::TrimRight(s_Line); // remove all whitespace at the end
    else
        return ""; // no string available
}

// ===================================== Console =====================================

// Set console title, buffer size and window size
void OsLibrary::SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, string s_Title)
{
    // Linux uses cryptic Escape sequences!
    cout << "\033]2;" << s_Title.c_str() << "\007" << std::flush;

    // TODO: Set console window size and buffer size
}

// Print coloured console output (max 2000 chars!)
void OsLibrary::PrintConsole(uint16_t u16_Color, string s_Format, ...)
{
    // Linux uses cryptic Escape sequences!
    switch (u16_Color)
    {
        case GREEN:   cout <<  "\033[32m"; break; // dark LIME
        case BROWN:   cout <<  "\033[33m"; break; // dark YELLOW
        case GREY:    cout <<  "\033[37m"; break; // dark WHITE
        case RED:     cout <<  "\033[91m"; break; // bright
        case LIME:    cout <<  "\033[92m"; break; // bright
        case YELLOW:  cout <<  "\033[93m"; break; // bright
        case BLUE:    cout <<  "\033[94m"; break; // bright
        case MAGENTA: cout <<  "\033[95m"; break; // bright
        case CYAN:    cout <<  "\033[96m"; break; // bright
        case WHITE:   cout <<  "\033[97m"; break; // bright
    }

    va_list args;
    va_start(args, s_Format);

    char s_Buffer[2000];
    int s32_Len = vsnprintf_s(s_Buffer, sizeof(s_Buffer), s_Format.c_str(), args);
    va_end(args);

    if (s32_Len < 0)
    {
        assert(false); // Buffer too small
        return;
    }
    s_Buffer[s32_Len] = 0;

    cout << s_Buffer;
}

// Check if the user has pressed the ENTER key in the console (non-blocking function)
bool OsLibrary::CheckConsoleEnterPressed()
{
    return GetKeyboardInput(false) == '\n';
}

// Wait until the user hits a key, returns the ASCII code (blocking function)
int OsLibrary::WaitConsoleChar()
{
    return GetKeyboardInput(true);
}

// b_Blocking = true  --> waits indefinitely until a key is pressed.
// b_Blocking = false --> checks instantly and returns immediately.
// returns the ASCII code of the key pressed, or -1 if no key was pressed (in non-blocking mode).
int OsLibrary::GetKeyboardInput(bool b_Blocking)
{
    // 1. Setup raw terminal flags (disable line buffering and echo)
    // ICANON turns off buffered line editing (canonical mode)
    // ECHO   turns off printing characters back to the screen
    struct termios k_OldTerm, k_NewTerm;
    tcgetattr(STDIN_FILENO, &k_OldTerm);
    k_NewTerm = k_OldTerm;
    k_NewTerm.c_lflag &= ~(ICANON | ECHO);

    // Set up standard blocking parameters for read()
    k_NewTerm.c_cc[VMIN]  = b_Blocking ? 1 : 0;
    k_NewTerm.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &k_NewTerm);

    // 2. Handle non-blocking specific check
    if (!b_Blocking)
    {
        struct timeval k_Time = {0, 0}; // 0s, 0µs timeout (instant snapshot)
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        // If no data is waiting in the buffer, skip reading entirely
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &k_Time) <= 0)
        {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &k_OldTerm); // Restore settings
            return -1;
        }
    }

    // 3. Read the character (Blocks natively if b_Blocking is true)
    int s32_Char;
    if (read(STDIN_FILENO, &s32_Char, 1) < 0)
        s32_Char = -1;

    // 4. Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &k_OldTerm);
    return s32_Char;
}

// ===================================== Helpers =====================================

// Create a timestamp with 1 µs precision.
// The returned timestamp starts at zero when the device is opened.
// It is recommended to turn off transmission of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
// Then this function is used as a replacement to generate a timestamp on reception of a USB packet and when sending a packet.
int64_t OsLibrary::GetTimestamp()
{
    struct timespec k_Time;
    if (clock_gettime(CLOCK_REALTIME, &k_Time) != 0)
        return 0; // Return 0 if the system call fails

    // Convert seconds to microseconds and add the nanosecond fractional part converted to microseconds
    int64_t s64_Timestamp = (int64_t)k_Time.tv_sec  * 1000000ULL +
                            (int64_t)k_Time.tv_nsec / 1000ULL;

    // ms64_TimestampStart is set to zero when the device is opened
    if (ms64_TimestampStart == 0)
        ms64_TimestampStart = s64_Timestamp;

    return s64_Timestamp - ms64_TimestampStart;
}

// Convert libusb error code into a text message
string OsLibrary::GetErrorMessage(uint32_t u32_Error)
{
    // convert u32_Error back to a negative value
    return libusb_strerror((int)u32_Error);
}


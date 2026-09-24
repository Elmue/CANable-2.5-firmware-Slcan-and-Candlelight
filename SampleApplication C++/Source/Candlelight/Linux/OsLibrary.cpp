
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
//  This class is for Linux. It has been tested on Fedora 44.
//
// =======================================================================================================

#include "OsLibrary.h"

using namespace CANable;
using namespace std;

// init static members
termios OsLibrary::mk_OldTermSettg = {};
bool    OsLibrary::mb_OldTermValid = false;

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
    mu32_RxPipeErrors = 0;
    mu32_TxPipeErrors = 0;
    mk_Info.Clear();

    // in case the last call to Open() failed with an exception and mpi_DevHandle is still open
    Close();

    libusb_device* pi_UsbDevice = (libusb_device*)pk_Device->mpi_LinuxDevice;

    int s32_Error = libusb_get_device_descriptor(pi_UsbDevice, (libusb_device_descriptor*)&mk_Info.mk_DeviceDescr);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    s32_Error = libusb_open(pi_UsbDevice, &mpi_DevHandle);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    // ------------------------

    s32_Error = ReadStringDescriptor(mk_Info.mk_DeviceDescr.iManufacturer, &mk_Info.ms_Vendor);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    s32_Error = ReadStringDescriptor(mk_Info.mk_DeviceDescr.iProduct,      &mk_Info.ms_Product);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    s32_Error = ReadStringDescriptor(mk_Info.mk_DeviceDescr.iSerialNumber, &mk_Info.ms_Serial);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    // ------------------------

    libusb_config_descriptor* pk_ConfigDesc;
    s32_Error = libusb_get_active_config_descriptor(pi_UsbDevice, &pk_ConfigDesc);
    if (s32_Error < 0)
        return (uint32_t)s32_Error;

    const libusb_interface*            pk_Interface  = &pk_ConfigDesc->interface[pk_Device->ms32_Interface];
    const libusb_interface_descriptor* pk_InterfDesc = &pk_Interface->altsetting[0];

    // copy the first 9 bytes of libusb_interface_descriptor
    memcpy(&mk_Info.mk_InterfDescr, pk_InterfDesc, sizeof(kInterfaceDescriptor));
    
    s32_Error = ReadStringDescriptor(pk_InterfDesc->iInterface, &mk_Info.ms_Interface);
    if (s32_Error < 0)
    {
        libusb_free_config_descriptor(pk_ConfigDesc);
        return (uint32_t)s32_Error;
    }

    // ------------------------

    // Get the 2 endpoints of the Candlelight interface (the Firmware Update interface has bNumEndpoints == 0)
    for (uint8_t P=0; P<pk_InterfDesc->bNumEndpoints; P++)
    {
        const libusb_endpoint_descriptor k_Endpoint = pk_InterfDesc->endpoint[P];
        if ((k_Endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK)
        {
            libusb_free_config_descriptor(pk_ConfigDesc);
            return ERR_INVALID_DEVICE;
        }

        if (k_Endpoint.bEndpointAddress & 0x80) // IN
        {
            mk_Info.mu8_EndpointIN     = k_Endpoint.bEndpointAddress;
            mk_Info.mu16_MaxPackSizeIN = k_Endpoint.wMaxPacketSize;
        }
        else // OUT
        {
            mk_Info.mu8_EndpointOUT     = k_Endpoint.bEndpointAddress;
            mk_Info.mu16_MaxPackSizeOUT = k_Endpoint.wMaxPacketSize;
        }
    }

    // ------------------------

    // If the gs_usb kernel driver is attached --> detach it and claim the interface for libusb.
    libusb_set_auto_detach_kernel_driver(mpi_DevHandle, 1);

    s32_Error = libusb_claim_interface(mpi_DevHandle, mk_Info.mk_InterfDescr.bInterfaceNumber);
    if (s32_Error < 0)
    {
        libusb_free_config_descriptor(pk_ConfigDesc);
        return s32_Error;
    }

    // ------------------------

    libusb_free_config_descriptor(pk_ConfigDesc);

    mb_IsOpen = true;
    return NO_ERROR;
}

// This is not called for the Firmware Update interface which has no endpoints
uint32_t OsLibrary::StartPipes()
{
    // this function is only needed for WinUSB
    return NO_ERROR;
}

// Called from Candlelight::Close()
void OsLibrary::Close()
{
    if (mpi_DevHandle)
    {
        libusb_release_interface(mpi_DevHandle, mk_Info.mk_InterfDescr.bInterfaceNumber);
        libusb_close(mpi_DevHandle);
        mpi_DevHandle = nullptr;
    }
    mb_IsOpen = false;
}

// ===================================== CTRL Pipe =====================================

// Send SETUP packet and optionally additional data bytes as IN or OUT transfer
uint32_t OsLibrary::ControlTransfer(kSetup* pk_Setup, void* p_Data, uint32_t* pu32_Transferred)
{
    int s32_Transferred = libusb_control_transfer(mpi_DevHandle, pk_Setup->bRequestType, pk_Setup->bRequest,
                                                  pk_Setup->wValue, pk_Setup->wIndex, (uint8_t*)p_Data, pk_Setup->wLength, PIPE_TIMEOUT);
    if (s32_Transferred < 0)
        return (uint32_t)s32_Transferred; // error

    *pu32_Transferred = (uint32_t)s32_Transferred;
    return NO_ERROR;
}

// ===================================== OUT Pipe ======================================

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
    pk_UsbInPacket->ms64_OsTimestamp = GetOsTimestamp();

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
// Calling EnumDevices() again will invalidate any previously returned device handles in mpi_LinuxDevice
uint32_t OsLibrary::EnumDevices(bool b_GetCandlelight, vector<kUsbDevice>* pi_Devices)
{
    const libusb_version* pk_Version = libusb_get_version();

    // Mandatory: libusb 1.0.30 required for libusb_get_device_string()
    if (pk_Version->major < 1 || pk_Version->micro < 30)
        return ERR_UPDATE_LIBUSB;

    // Optional: libusb 1.0.31 required for libusb_get_interface_string()
    if (pk_Version->major < 1 || pk_Version->micro < 31)
        PrintConsole(YELLOW, "Update to libusb 1.0.31\n");

    int s32_Error;
    if (!mpi_UsbContext) // init once only
    {
        s32_Error = libusb_init(&mpi_UsbContext);
        if (s32_Error < 0)
            return (uint32_t)s32_Error;
    }

    if (mppi_UsbDeviceList) // free any previous list
    {
        libusb_free_device_list(mppi_UsbDeviceList, 1);
        mppi_UsbDeviceList = nullptr;
    }

    ssize_t s32_DevCount = libusb_get_device_list(mpi_UsbContext, &mppi_UsbDeviceList);
    if (s32_DevCount < 0)
        return (uint32_t)s32_DevCount; // s32_DevCount is error code

    // enumerate USB devices
    for (ssize_t Dev = 0; Dev < s32_DevCount; Dev++)
    {
        libusb_device* pi_UsbDevice = mppi_UsbDeviceList[Dev];

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
            // get string descriptor from the kernel without opening the device
            // libusb_get_device_string() requires libusb version 1.0.30
            char s8_Product[256];
            s32_Error = libusb_get_device_string(pi_UsbDevice, LIBUSB_DEVICE_STRING_PRODUCT, s8_Product, sizeof(s8_Product));
            if (s32_Error < 0)
            {
                libusb_free_config_descriptor(pk_ConfigDesc);
                return (uint32_t)s32_Error;
            }

			// get string descriptor from the kernel without opening the device
            // libusb_get_device_string() requires libusb version 1.0.30
            char s8_Serial[256];
            s32_Error = libusb_get_device_string(pi_UsbDevice, LIBUSB_DEVICE_STRING_SERIAL_NUMBER, s8_Serial, sizeof(s8_Serial));
            if (s32_Error < 0)
            {
                libusb_free_config_descriptor(pk_ConfigDesc);
                return (uint32_t)s32_Error;
            }

            // Add each interface as a separate device to pi_Devices
            for (uint8_t Idx = 0; Idx < pk_ConfigDesc->bNumInterfaces; Idx++)
            {
                // The Firmware Update interface is always the second interface (Idx == 1)
                // The others are Candlelight interfaces: (Idx == 0, 2, 3,...)
                bool b_IsCandle = (Idx != FIRMW_UPDATE_INTERFACE);
                if  (b_IsCandle != b_GetCandlelight)
                    continue; // not the requested interface type

                const libusb_interface* pk_Interface = &pk_ConfigDesc->interface[Idx];

                if (pk_Interface->num_altsetting != 1)
                    break; // not a valid Candlelight device

                const libusb_interface_descriptor* pk_InterfDesc = &pk_Interface->altsetting[0];

                kUsbDevice k_UsbDev;
                k_UsbDev.mpi_LinuxDevice = pi_UsbDevice;
                k_UsbDev.ms32_Interface  = Idx;
                k_UsbDev.ms_Product      = s8_Product;
                k_UsbDev.ms_SerialNo     = s8_Serial;

                // On Windows ms_DevicePath is the real Windows NT device path used by the kernel.
                // But libusb does not offer an API that returns the Linux device path although it is stored internally in priv->sysfs_dir.
                // We build a string here that gives a little information about the USB device location on the USB bus.
                k_UsbDev.ms_DevicePath = cUtils::Format("Bus number: %u, Device address: %u",
                                                        libusb_get_bus_number    (pi_UsbDevice),
                                                        libusb_get_device_address(pi_UsbDevice));

                // libusb uses totaly stupid version numbers: 0x0100010D = 1.0.31 !!
                #if LIBUSB_API_VERSION >= 0x0100010D
                    // Get the interface name from the kernel without opening the device.
                    // libusb_get_interface_string() has been added in version 1.0.31 (https://github.com/libusb/libusb/pull/1860)
                    char s8_Interface[256];
                    s32_Error = libusb_get_interface_string(pi_UsbDevice, pk_ConfigDesc->bConfigurationValue,
                                                            pk_InterfDesc->bInterfaceNumber, pk_InterfDesc->bAlternateSetting,
                                                            s8_Interface, sizeof(s8_Interface));
                    if (s32_Error < 0)
                        k_UsbDev.ms_Interface = cUtils::Format("[libusb error %s]", libusb_strerror(s32_Error));
                    else
                        k_UsbDev.ms_Interface = s8_Interface;
                #else
                    UNUSED(pk_InterfDesc);
                    k_UsbDev.ms_Interface = "[libusb is too old]";
                #endif

                pi_Devices->push_back(k_UsbDev);
            } // for (Idx)

        } // if (bNumInterfaces >= 2)
        libusb_free_config_descriptor(pk_ConfigDesc);

    } // for (Dev)
        
    return NO_ERROR;
}

// ===================================== Console OUT =====================================

// Set console title, buffer size and window size
void OsLibrary::SetUpConsole(int16_t s16_BufWidth, int16_t s16_BufHeight, int16_t s16_WndWidth, int16_t s16_WndHeight, string s_Title)
{
    UNUSED(s16_BufWidth);
    UNUSED(s16_BufHeight);
    UNUSED(s16_WndWidth);
    UNUSED(s16_WndHeight);

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
        case GREEN:   cout <<  "\033[1;38;2;0;180;0m";     break; // dark Lime
        case BROWN:   cout <<  "\033[1;38;2;180;100;0m";   break; // dark Yellow
        case GREY:    cout <<  "\033[1;38;2;160;160;160m"; break; // dark White
        case RED:     cout <<  "\033[1;38;2;255;30;30m";   break; // bright
        case LIME:    cout <<  "\033[1;38;2;0;255;0m";     break; // bright
        case YELLOW:  cout <<  "\033[1;38;2;255;235;0m";   break; // bright
        case BLUE:    cout <<  "\033[1;38;2;30;130;255m";  break; // bright
        case MAGENTA: cout <<  "\033[1;38;2;255;0;255m";   break; // bright
        case CYAN:    cout <<  "\033[1;38;2;0;235;255m";   break; // bright
        case WHITE:   cout <<  "\033[1;38;2;255;255;255m"; break; // bright       
    }

    va_list args;
    va_start(args, s_Format);

    char s_Buffer[2000];
    int s32_Len = vsnprintf(s_Buffer, sizeof(s_Buffer), s_Format.c_str(), args);
    va_end(args);

    if (s32_Len < 0)
    {
        assert(false); // Buffer too small
        return;
    }
    s_Buffer[s32_Len] = 0;

    cout << s_Buffer;
}

// ===================================== Console IN =====================================

// static
// Check if the user has pressed the ENTER key in the console (non-blocking function)
bool OsLibrary::CheckConsoleEnterPressed()
{
    int s32_Ascii = GetKeyboardInput();
    return s32_Ascii == 10 || s32_Ascii == 13;
}

// static
// Wait until the user hits a key, returns the ASCII code (blocking function)
int OsLibrary::WaitConsoleChar()
{
    while (true)
    {
        int s32_Ascii = GetKeyboardInput();
        if (s32_Ascii > -1)
            return s32_Ascii;

        usleep(50 * 1000);  // 50 ms
    }
}

// static
// returns the ASCII code of the key pressed, or -1 if no key was pressed
int OsLibrary::GetKeyboardInput()
{
    struct timeval k_Time = {0, 0}; // 0s, 0µs timeout (instant snapshot)
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    // If no data is waiting in the buffer, skip reading entirely
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &k_Time) <= 0)
        return -1;

    uint8_t u8_Char;
    if (read(STDIN_FILENO, &u8_Char, 1) < 0)
        return -1;

    return u8_Char;
}

// -------------------

// static
void OsLibrary::SwitchTerminalToNonCanonical()
{
    if (mb_OldTermValid || !isatty(STDIN_FILENO))
        return;

    // Save original settings
    if (tcgetattr(STDIN_FILENO, &mk_OldTermSettg) == 0)
    {
        mb_OldTermValid = true;

        // Copy original settings to modify
        termios k_NewTermSettg = mk_OldTermSettg;

        // Disable line buffering and echo and set non-blocking read parameters
        k_NewTermSettg.c_lflag &= ~(ICANON | ECHO);
        k_NewTermSettg.c_cc[VMIN]  = 0;
        k_NewTermSettg.c_cc[VTIME] = 0;

        // Apply settings without flushing pending input
        tcsetattr(STDIN_FILENO, TCSADRAIN, &k_NewTermSettg);

        // Register emergency restoration on process termination
        atexit(OsLibrary::RestoreTerminal);
    }
}

// static
void OsLibrary::RestoreTerminal()
{
    if (mb_OldTermValid && isatty(STDIN_FILENO))
    {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &mk_OldTermSettg);
        mb_OldTermValid = false;
    }
}

// ===================================== Helpers =====================================

// Create a timestamp with 1 µs precision.
// It is recommended to turn off transmission of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
// Then this function is used as a replacement to generate a timestamp on reception of a USB packet and when sending a packet.
int64_t OsLibrary::GetOsTimestamp()
{
    struct timespec k_Time;
    if (clock_gettime(CLOCK_REALTIME, &k_Time) != 0)
        return 0; // Return 0 if the system call fails

    // Convert seconds to microseconds and add the nanosecond fractional part converted to microseconds
    return (int64_t)k_Time.tv_sec  * 1000000ULL +
           (int64_t)k_Time.tv_nsec / 1000ULL;
}

// Convert libusb error code into a text message
string OsLibrary::GetErrorMessage(uint32_t u32_Error)
{
    // convert u32_Error back to a negative value
    return libusb_strerror((int)u32_Error);
}

int OsLibrary::ReadStringDescriptor(uint8_t u8_StrIndex, string* ps_String)
{
    // If the descriptor does not define a string, the index is zero. This is not an error.
    if (u8_StrIndex == 0)
    {
        *ps_String = "";
        return NO_ERROR;
    }

    char s8_Buffer[512];
    int s32_Written = libusb_get_string_descriptor_ascii(mpi_DevHandle, u8_StrIndex, (uint8_t*)s8_Buffer, sizeof(s8_Buffer));
    if (s32_Written < 0)
        return s32_Written;

    *ps_String = s8_Buffer;
    return NO_ERROR;
}


// https://netcult.ch/elmue/CANable Firmware Update

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

using System;
using System.Text;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.ComponentModel;
using Microsoft.Win32;

using eApiError           = CANable.Utils.eApiError;

namespace CANable
{
public class SetupApi
{
    #region structs, classes

    public class cUsbDevice : IComparable
    {
        public String ms_Product;
        public String ms_Interface;
        public String ms_DevPath;
        public String ms_SerialNo;
        public int    ms32_Channel;

        public String DisplayName
        {
            get
            {
                // If a legacy Candlelight device does not expose a string in the Candlelight interface, 
                // Windows returns the Product string instead --> both are identical
                if (ms_Product == ms_Interface)
                    return ms_Product;

                return ms_Product + " - " + ms_Interface;
            }
        }

        /// <summary>
        /// Compare by Serial Number and then by Channel number for sorting
        /// </summary>
        int IComparable.CompareTo(Object o_Comp)
        {
            cUsbDevice i_Dev2 = (cUsbDevice)o_Comp;

            int s32_Diff = ms_SerialNo.CompareTo(i_Dev2.ms_SerialNo);

            if (s32_Diff == 0)
                s32_Diff = ms32_Channel.CompareTo(i_Dev2.ms32_Channel);
            
            return s32_Diff;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVICE_INTERFACE_DATA
    {
        public int    cbSize;
        public Guid   InterfaceClassGuid;
        public int    Flags;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVINFO_DATA
    {
        public int    cbSize;
        public Guid   ClassGuid;
        public int    DevInst;
        public IntPtr Reserved;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct SP_DEVICE_INTERFACE_DETAIL_DATA
    {
        public int  cbSize;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 256)]
        public Char[] chrDevicePath;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DEVPROPKEY
    {
        public Guid fmtid;
        public int  pid;

        public DEVPROPKEY(Guid guid, int id)
        {
            fmtid = guid;
            pid   = id;
        }
    }

    #endregion

    #region Dll Imports SetupApi

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr SetupDiGetClassDevsW(ref Guid ClassGuid, string Enumerator, IntPtr hwndParent, int Flags);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr h_DevInfo, IntPtr devInfo, ref Guid interfaceClassGuid, int memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceInterfaceDetailW(IntPtr h_DevInfo, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, ref SP_DEVICE_INTERFACE_DETAIL_DATA k_DetailData, int deviceInterfaceDetailDataSize, out int s32_ReqSize, ref SP_DEVINFO_DATA deviceInfoData);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr h_DevInfo, ref SP_DEVINFO_DATA deviceInfoData, int property, out int s32_RegDataType, StringBuilder propertyBuffer, int propertyBufferSize, out int s32_ReqSize);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiGetDevicePropertyW(IntPtr h_DevInfo, ref SP_DEVINFO_DATA deviceInfoData, ref DEVPROPKEY PropertyKey, out int PropertyType, StringBuilder propertyBuffer, int propertyBufferSize, out int s32_ReqSize, int flags);

    [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern bool SetupDiOpenDeviceInfoW(IntPtr h_DevInfo, String DeviceInstanceId, IntPtr h_WndParent, int OpenFlags, out SP_DEVINFO_DATA deviceInfoData);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr k_GuidDummy, IntPtr h_Wnd);

    [DllImport("setupapi.dll", SetLastError = true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr h_DevInfo);

    #endregion

    // Interface 0 = Candlelight
    const String GUID_CANDLE = "{c15b4308-04d3-11e6-b3ea-6057189e6443}";
    // Interface 1 = DFU
    const String GUID_DFU    = "{c25b4308-04d3-11e6-b3ea-6057189e6443}";

    const int DIGCF_PRESENT          = 0x02;
    const int DIGCF_DEVICEINTERFACE  = 0x10;
    const int SPDRP_DEVICEDESC       = 0x00;
    const int SPDRP_FRIENDLYNAME     = 0x0C;
    const int SPDRP_BASE_CONTAINERID = 0x24;

    static DEVPROPKEY DEVPKEY_Device_BusReportedDeviceDesc = new DEVPROPKEY(new Guid("540b947e-8b40-45bc-a8a2-6a0b894cbda2"), 4);
    static DEVPROPKEY DEVPKEY_Device_Parent                = new DEVPROPKEY(new Guid("4340a6c5-93fa-4706-972c-7b648008a5a7"), 8);

    /// <summary>
    /// Enumerate all Candlelight devices that are currently connected
    /// b_Candlelight = true  --> get only Candlelight interface
    /// b_Candlelight = false --> get only DFU interface
    /// returns devices name and NT paths "\\?\USB#VID_1D50&PID_606F&MI_00#7&20E43BBC&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
    /// </summary>
    public static List<cUsbDevice> EnumerateUsbDevices(bool b_Candlelight)
    {
        Dictionary<String, String> i_Serials = EnumSerialNumbers();

        Guid k_Guid = new Guid(b_Candlelight ? GUID_CANDLE : GUID_DFU);

        IntPtr h_DevInfo = SetupDiGetClassDevsW(ref k_Guid, null, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (h_DevInfo == Utils.INVALID_HANDLE_VALUE)
            Utils.ThrowApiError(Marshal.GetLastWin32Error(), "Error {0} enumerating USB devices: {1}");

        IntPtr h_ParentInfo = SetupDiCreateDeviceInfoList(IntPtr.Zero, IntPtr.Zero);
        if (h_ParentInfo == Utils.INVALID_HANDLE_VALUE)
        {
            SetupDiDestroyDeviceInfoList(h_DevInfo);
            Utils.ThrowApiError(Marshal.GetLastWin32Error(), "Error {0} enumerating USB devices: {1}");
        }

        SP_DEVICE_INTERFACE_DATA k_InterfaceData = new SP_DEVICE_INTERFACE_DATA();
        k_InterfaceData.cbSize = Marshal.SizeOf(k_InterfaceData);

        SP_DEVINFO_DATA k_DeviceInfo = new SP_DEVINFO_DATA();
        k_DeviceInfo.cbSize = Marshal.SizeOf(k_DeviceInfo);

        SP_DEVICE_INTERFACE_DETAIL_DATA k_DetailData = new SP_DEVICE_INTERFACE_DETAIL_DATA();
        k_DetailData.cbSize = ((IntPtr.Size == 8) ? 8 : 4) + sizeof(Char); // alignment

        StringBuilder    s_ProductBuf   = new StringBuilder(128);
        StringBuilder    s_InterfaceBuf = new StringBuilder(128);
        StringBuilder    s_ContainerBuf = new StringBuilder(50);
        StringBuilder    s_ParentBuf    = new StringBuilder(256);
        List<cUsbDevice> i_DeviceList   = new List<cUsbDevice>();

        int s32_Error = 0;
        int s32_ReqSize;
        int s32_RegDataType;
        int s32_PropType;

        for (int s32_Idx = 0; true; s32_Idx++)
        {
            if (!SetupDiEnumDeviceInterfaces(h_DevInfo, IntPtr.Zero, ref k_Guid, s32_Idx, ref k_InterfaceData))
            {
                s32_Error = Marshal.GetLastWin32Error();
                if (s32_Error == (int)eApiError.NO_MORE_ITEMS)
                    s32_Error = 0;
                break;
            }

            // Get the NT path of the device that will be passed to CreateFile()
            if (!SetupDiGetDeviceInterfaceDetailW(h_DevInfo, ref k_InterfaceData, ref k_DetailData, 2000, 
                                                    out s32_ReqSize, ref k_DeviceInfo))
            {
                s32_Error = Marshal.GetLastWin32Error();
                continue;
            }

            // Get the 'ContainerID' GUID string (since Windows 7) which is identical for all interfaces of the same device
            if (!SetupDiGetDeviceRegistryPropertyW(h_DevInfo, ref k_DeviceInfo, SPDRP_BASE_CONTAINERID, out s32_RegDataType, 
                                                   s_ContainerBuf, s_ContainerBuf.Capacity * 2, out s32_ReqSize))
            {
                s32_Error = Marshal.GetLastWin32Error();
                continue;
            }

            // Get the Interface string from Interface Descriptor (max USB string descriptor length = 127 Unicode chars)
            // If a legacy interface descriptor has iInterface == 0 (no string available) this will return the product string instead.
            if (!SetupDiGetDevicePropertyW(h_DevInfo, ref k_DeviceInfo, ref DEVPKEY_Device_BusReportedDeviceDesc, out s32_PropType, 
                                          s_InterfaceBuf, s_InterfaceBuf.Capacity * 2, out s32_ReqSize, 0))
            {
                s32_Error = Marshal.GetLastWin32Error();
                continue;
            }

            // Go one level up from USB interface to USB device --> c_Parent = "USB\VID_1D50&PID_606F\208A347D4B4550142"
            if (!SetupDiGetDevicePropertyW(h_DevInfo, ref k_DeviceInfo, ref DEVPKEY_Device_Parent, out s32_PropType, 
                                           s_ParentBuf, s_ParentBuf.Capacity * 2, out s32_ReqSize, 0))
            {
                s32_Error = Marshal.GetLastWin32Error();
                continue;
            }

            if (!SetupDiOpenDeviceInfoW(h_ParentInfo, s_ParentBuf.ToString(), IntPtr.Zero, 0, out k_DeviceInfo))
            {
                s32_Error = Marshal.GetLastWin32Error();
                continue;
            }

            // Get the Product string from Device Descriptor (max USB string descriptor length = 127 Unicode chars)
            if (!SetupDiGetDevicePropertyW(h_ParentInfo, ref k_DeviceInfo, ref DEVPKEY_Device_BusReportedDeviceDesc, out s32_PropType, 
                                           s_ProductBuf, s_ProductBuf.Capacity * 2, out s32_ReqSize, 0))
            {
                s32_Error = Marshal.GetLastWin32Error();
                continue;
            }

            // --------------------

            cUsbDevice i_UsbDev   = new cUsbDevice();
            i_UsbDev.ms_DevPath   = new String(k_DetailData.chrDevicePath).TrimEnd('\0').ToUpper();
            i_UsbDev.ms_Product   = s_ProductBuf  .ToString();
            i_UsbDev.ms_Interface = s_InterfaceBuf.ToString();
            i_Serials.TryGetValue(s_ContainerBuf.ToString(), out i_UsbDev.ms_SerialNo);

            // Append interface number for multi-interface adapters
            int Pos = i_UsbDev.ms_DevPath.IndexOf("&MI_0");
            if (Pos > 0)
            {
                if (int.TryParse(i_UsbDev.ms_DevPath.Substring(Pos + 5, 1), out i_UsbDev.ms32_Channel))
                {
                    // MI_00 --> Candlelight 1
                    // MI_01 --> DFU
                    // MI_02 --> Candlelight 2
                    // MI_03 --> Candlelight 3
                    if (i_UsbDev.ms32_Channel == 0)
                        i_UsbDev.ms32_Channel = 1;  // display one-based interface number
                }
            }

            i_DeviceList.Add(i_UsbDev);
        }

        SetupDiDestroyDeviceInfoList(h_DevInfo);
        SetupDiDestroyDeviceInfoList(h_ParentInfo);

        if (s32_Error != 0)
            Utils.ThrowApiError(s32_Error, "Error {0} enumerating USB devices: {1}");

        i_DeviceList.Sort();
        return i_DeviceList;
    }

    // Get the serial numbers of all Candlelight devices
    // "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\USB\VID_1D50&PID_606F\2066349E39455006"
    // The last part is the serial number: "2066349E39455006"
    // return a Dictionary with ContainerID --> Serial Number
    static Dictionary<String, String> EnumSerialNumbers()
    {
        String s_RootPath = "System\\CurrentControlSet\\Enum\\USB\\VID_1D50&PID_606F";

        using (RegistryKey i_RootKey = Registry.LocalMachine.OpenSubKey(s_RootPath))
        {
            Dictionary<String, String> i_Serials = new Dictionary<String, String>(StringComparer.OrdinalIgnoreCase);

            foreach (String s_Serial in i_RootKey.GetSubKeyNames())
            {
                String s_RegPath = "HKEY_LOCAL_MACHINE\\" + s_RootPath + "\\" + s_Serial;

                // All interfaces of a multi-interface device have the same ContainerID
                Object o_Container = Registry.GetValue(s_RegPath, "ContainerID", null);
    
                if (o_Container is String)
                    i_Serials[(String)o_Container] = s_Serial;
            }

            return i_Serials;
        }
    }
} // class
} // namespace

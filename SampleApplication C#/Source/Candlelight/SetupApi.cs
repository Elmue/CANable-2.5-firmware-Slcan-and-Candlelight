
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

using eApiError           = CANable.Utils.eApiError;

namespace CANable
{
    public class SetupApi
    {
        #region structs

        public struct kUsbDevice
        {
            public String Name;
            public String NtPath;

            public kUsbDevice(String s_Name, String s_NtPath)
            {
                Name   = s_Name;
                NtPath = s_NtPath;
            }

            public override string ToString()
            {
                return Name;
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

        #endregion

        #region Dll Imports SetupApi

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr SetupDiGetClassDevsW(ref Guid ClassGuid, string Enumerator, IntPtr hwndParent, int Flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        static extern bool SetupDiEnumDeviceInterfaces(IntPtr h_DevInfo, IntPtr devInfo, ref Guid interfaceClassGuid, int memberIndex, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool SetupDiGetDeviceInterfaceDetailW(IntPtr h_DevInfo, ref SP_DEVICE_INTERFACE_DATA deviceInterfaceData, ref SP_DEVICE_INTERFACE_DETAIL_DATA k_DetailData, int deviceInterfaceDetailDataSize, out int s32_RequiredSize, ref SP_DEVINFO_DATA deviceInfoData);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr h_DevInfo, ref SP_DEVINFO_DATA deviceInfoData, uint property, out uint u32_RegDataType, StringBuilder propertyBuffer, int propertyBufferSize, out int s32_RequiredSize);

        [DllImport("setupapi.dll", SetLastError = true)]
        static extern bool SetupDiDestroyDeviceInfoList(IntPtr h_DevInfo);

        #endregion

        // Interface 0 = Candlelight
        const String GUID_CANDLE = "{c15b4308-04d3-11e6-b3ea-6057189e6443}";
        // Interface 1 = DFU
        const String GUID_DFU    = "{c25b4308-04d3-11e6-b3ea-6057189e6443}";

        const int  DIGCF_PRESENT         = 0x02;
        const int  DIGCF_DEVICEINTERFACE = 0x10;
        const uint SPDRP_DEVICEDESC      = 0;

        /// <summary>
        /// Enumerate all Candlelight devices that are currently connected
        /// u8_Interface = 0 --> get only Candlelight interface
        /// u8_Interface = 1 --> get only DFU interface
        /// returns devices name and NT paths "\\?\USB#VID_1D50&PID_606F&MI_00#7&20E43BBC&0&0000#{c15b4308-04d3-11e6-b3ea-6057189e6443}"
        /// </summary>
        public static List<kUsbDevice> EnumerateUsbDevices(byte u8_Interface)
        {
            Guid k_Guid;
            switch (u8_Interface)
            {
                case 0: k_Guid = new Guid(GUID_CANDLE); break;
                case 1: k_Guid = new Guid(GUID_DFU);    break;
                default: throw new Exception("Invalid interface");
            }

            IntPtr h_DevInfo = SetupDiGetClassDevsW(ref k_Guid, null, IntPtr.Zero, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (h_DevInfo == Utils.INVALID_HANDLE_VALUE)
                Utils.ThrowApiError(Marshal.GetLastWin32Error(), "Error {0} enumerating USB devices: {1}");

            SP_DEVICE_INTERFACE_DATA k_InterfaceData = new SP_DEVICE_INTERFACE_DATA();
            k_InterfaceData.cbSize = Marshal.SizeOf(k_InterfaceData);

            SP_DEVINFO_DATA k_DevInfoData = new SP_DEVINFO_DATA();
            k_DevInfoData.cbSize = Marshal.SizeOf(k_DevInfoData);

            SP_DEVICE_INTERFACE_DETAIL_DATA k_DetailData = new SP_DEVICE_INTERFACE_DETAIL_DATA();
            k_DetailData.cbSize = ((IntPtr.Size == 8) ? 8 : 4) + sizeof(Char); // alignment

            StringBuilder    s_NameBuf    = new StringBuilder(256);
            List<kUsbDevice> i_DeviceList = new List<kUsbDevice>();

            int s32_Error = 0;
            for (int s32_Idx = 0; true; s32_Idx++)
            {
                if (!SetupDiEnumDeviceInterfaces(h_DevInfo, IntPtr.Zero, ref k_Guid, s32_Idx, ref k_InterfaceData))
                {
                    s32_Error = Marshal.GetLastWin32Error();
                    if (s32_Error == (int)eApiError.NO_MORE_ITEMS)
                        s32_Error = 0;
                    break;
                }

                // Get the NT path of the device that is required for CreateFile()
                int s32_RequiredSize = 0;
                if (!SetupDiGetDeviceInterfaceDetailW(h_DevInfo, ref k_InterfaceData, ref k_DetailData, 2000, 
                                                      out s32_RequiredSize, ref k_DevInfoData))
                {
                    s32_Error = Marshal.GetLastWin32Error();
                    break;
                }

                String s_DevicePath = new String(k_DetailData.chrDevicePath);

                // Get display name
                uint u32_RegDataType;
                if (!SetupDiGetDeviceRegistryPropertyW(h_DevInfo, ref k_DevInfoData, SPDRP_DEVICEDESC, out u32_RegDataType, 
                                                       s_NameBuf, s_NameBuf.Capacity, out s32_RequiredSize))
                {
                    s32_Error = Marshal.GetLastWin32Error();
                    break;
                }

                i_DeviceList.Add(new kUsbDevice(s_NameBuf.ToString(), s_DevicePath.TrimEnd('\0')));
            }

            SetupDiDestroyDeviceInfoList(h_DevInfo);

            if (s32_Error != 0)
                Utils.ThrowApiError(s32_Error, "Error {0} enumerating USB devices: {1}");

            return i_DeviceList;
        }
    }
}

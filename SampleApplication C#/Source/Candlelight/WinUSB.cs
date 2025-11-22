
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

#if DEBUG
    // Trace size of data packets sent and received
//    #define TRACE_PIPES
#endif

using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Threading;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.ComponentModel;
using Microsoft.Win32.SafeHandles;

using eApiError           = CANable.Utils.eApiError;
using eWaitObject         = CANable.Utils.eWaitObject;
using eFileAccess         = CANable.Utils.eFileAccess;
using eFileShare          = CANable.Utils.eFileShare;
using eFileCreate         = CANable.Utils.eFileCreate;
using eFileFlags          = CANable.Utils.eFileFlags;

namespace CANable
{
    /// <summary>
    /// ATTENTION:
    /// This class must not be reused. Always create a new instance!
    /// This class must always be disposed. This is done in the Candlelight class.
    /// </summary>
    public class WinUSB : IDisposable
    {
        #region enums

        // Names are displayed in error messages!
        public enum eDescriptor : byte
        {
            Device               = 0x01,
            Configuration        = 0x02,
            String               = 0x03,
            Interface            = 0x04,
            Endpoint             = 0x05,
            DeviceQualifier      = 0x06, // Only high speed
            OtherSpeed           = 0x07, // Only high speed
            InterfacePower       = 0x08, // Obsolete
            OnTheGo              = 0x09, // The OTG descriptor is returned with DESCR_CONFIGURATION
            Debug                = 0x0A, // Debug Descriptor
            InterfaceAssociation = 0x0B, // Interface Association          (USB 2.0)
            BOS                  = 0x0F, // Binary Device Object Store     (USB 2.1)
            DeviceCapability     = 0x10, // Device Capability              (USB 3.0)
            HID                  = 0x21, // HID and DFU
            Report               = 0x22, // HID
            Physical             = 0x23, // HID
            CS_Interface         = 0x24, // Audio & CDC https://discussions.apple.com/thread/6474393
            CS_Endpoint          = 0x25, // Audio & CDC http://www.usb.org/developers/docs/devclass_docs/  (see CDC 1.2 docs)
            HUB                  = 0x29, // HUB
            SuperHUB             = 0x2A, // Super Speed HUB                (USB 3.0)
            EndpointCompanion    = 0x30, // Super Speed Endpoint Companion (USB 3.0)
        }

        public enum eDeviceClass : byte
        {
            Undefined        = 0x00, // Class defined in interface descriptor
            Audio            = 0x01,
            CdcControl       = 0x02, // COM
            HID              = 0x03,
            Physical         = 0x05,
            StillImaging     = 0x06, // PTP / Image
            Printer          = 0x07,
            MassStorage      = 0x08,
            HUB              = 0x09,
            CdcData          = 0x0A, // Data
            Smartcard        = 0x0B,
            Security         = 0x0D,
            Video            = 0x0E,
            HealthCare       = 0x0F,
            DiagnosticDevice = 0xDC, // Diagnostic Device
            Bluetooth        = 0xE0, // Wireless
            Miscellaneous    = 0xEF,
            FwUpgrade        = 0xFE, // Application Specific
            Vendor           = 0xFF, // Vendor Specific
        }

        public enum ePipeType : int
        {
            Control,
            Isochronous,
            Bulk,
            Interrupt,
        }

        public enum ePipePolicy : int
        {
            ShortPacketTerminate = 1, // bool value (1 byte)
            AutoClearStall,           // bool value (1 byte)
            PipeTransferTimeout,      // uint value (4 bytes)
            IgnoreShortPackets,       // bool value (1 byte)
            AllowPartialReads,        // bool value (1 byte)
            AutoFlush,                // bool value (1 byte)
            RawIO,                    // bool value (1 byte)
            MaximumTransferSize,
            ResetPipeOnResume,
        }

        // ---------------------------------------------

        enum eSetupRequest : byte
        {
            GetStatus        =  0,
            ClearFeature     =  1,
            SetFeature       =  3,
            GetMsDescriptor  =  4, // Microsoft OS Descriptor for automatic driver installation        
            SetAddress       =  5,
            GetDescriptor    =  6,
            SetDescriptor    =  7,
            GetConfiguration =  8,
            SetConfiguration =  9,
            GetInterface     = 10,
            SetInterface     = 11,
            SynchFrame       = 12,
        };

        public enum eSetupRecip : byte // Bits 0,1,2,3,4 of u8_RequestType
        {
            Device    = 0x00,
            Interface = 0x01,
            Endpoint  = 0x02,
            Other     = 0x03,
            //   ....   0x1F,
        };
    
        public enum eSetupType : byte  // Bits 5,6 of u8_RequestType
        {
            Standard = 0x00, // 0 << 5
            Class    = 0x20, // 1 << 5
            Vendor   = 0x40, // 2 << 5
        };

        public enum eDirection : byte     // Bit 7 of u8_RequestType, also used for endpoints
        {
            Out = 0x00,
            In  = 0x80,
        };

        // ---------------------------------------------

        /// <summary>
        /// Only SETUP, not for pipes!
        /// wIndex = Interface
        /// </summary>
        public enum eDfuRequest : byte
        {
            Detach      = 0, // RequType = 0x21, Tells device to detach and re-enter DFU mode (wValue = Timeout)
            Download    = 1, // RequType = 0x21, Download firmware data to device (wValue = BlockNumber)
            Upload      = 2, // RequType = 0xA1, Upload firmware data from device
            GetStatus   = 3, // RequType = 0xA1, Get device status and poll timeout (6 byte)
            ClearStatus = 4, // RequType = 0x21, Clear current device status
            GetState    = 5, // RequType = 0xA1, Get current device state (1 byte)
            Abort       = 6, // RequType = 0x21, Abort current operation
        }

        [FlagsAttribute]
        public enum eDfuAttribs : byte
        {
            CanDownload           = 0x01,
            CanUpload             = 0x02,
            ManifestationTolerant = 0x04,
            WillDetach            = 0x08,
        }

        #endregion

        #region structs

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class kDescriptorHead
        {
            public byte         bLength;
            public eDescriptor  eDescrType;      // Byte
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class kDeviceDescriptor : kDescriptorHead
        {
            public ushort       bcdUSB;          // USB version
            public eDeviceClass bDeviceClass;    // Byte
            public byte         bDeviceSubClass;
            public byte         bDeviceProtocol;
            public byte         bMaxPacketSize0;
            public ushort       idVendor;
            public ushort       idProduct;
            public ushort       bcdDevice;       // Device release number
            public byte         iManufacturer;
            public byte         iProduct;
            public byte         iSerialNumber;
            public byte         bNumConfigurations;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        protected class kConfigDescriptor : kDescriptorHead
        {
            public ushort      wTotalLength;
            public byte        bNumInterfaces;
            public byte        bConfigurationValue;
            public byte        iConfiguration;
            public byte        bmAttributes;
            public byte        MaxPower;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class kInterfaceDescriptor : kDescriptorHead
        {
            public byte         bInterfaceNumber;
            public byte         bAlternateSetting;
            public byte         bNumEndpoints;
            public eDeviceClass bInterfaceClass;   // Byte
            public byte         bInterfaceSubClass;
            public byte         bInterfaceProtocol;
            public byte         iInterface;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class kEndpointDescriptor : kDescriptorHead
        {
            public Byte        u8_EndpointAddress; // Endpoint address. Bit 7 indicates direction (0=OUT, 1=IN).
            public Byte        u8_Attributes;      // Endpoint transfer type.
            public UInt16      u16_MaxPacketSize;  // Maximum packet size.
            public Byte        u8_Interval;        // Polling interval in frames.
        }

        /// <summary>
        /// HID and DFU descriptors have the same Type = 0x21, 
        /// but belong to an interface with Class = HID (3) versus App Specific (FE)
        /// </summary>
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class kHidDescriptor : kDescriptorHead
        {
            public Byte        u8_VersionLo;         // HID class specification release (BCD)
            public Byte        u8_VersionHi;         // HID class specification release (BCD)
            public Byte        u8_CountryCode;       // Country code
            public Byte        u8_NumDescriptors;    // Number of additional class specific descriptors
            public Byte        u8_DescrType;         // Type of class descriptor (Report = 34)
            public UInt16      u16_DescriptorLength; // Total size of the Report descriptor
        }

        /// <summary>
        /// HID and DFU descriptors have the same Type = 0x21, 
        /// but belong to an interface with Class = HID (3) versus App Specific (FE)
        /// </summary>
        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        public class kDfuDescriptor : kDescriptorHead
        {
            public eDfuAttribs e_Attributes;
            public UInt16      u16_DetachTimeout;
            public UInt16      u16_TransferSize;
            public Byte        u8_DfuVersionLo;
            public Byte        u8_DfuVersionHi;
        }

        // ---------------------------------------------------------------

        [StructLayout(LayoutKind.Sequential, Pack = 1)]
        struct kSetup
        {
            public byte   mu8_RequType; // eSetupRecip | eSetupType | eSetupDir
            public byte   mu8_Request;  // eSetupRequest or eUsbRequest or eDfuRequest
            public ushort mu16_Value;
            public ushort mu16_Index;
            public ushort mu16_Length; // ATTENTION: WinUSB ignores this value and sets s32_BufSize passed to WinUsb_ControlTransfer()

            public override string ToString()
            {
                Byte[] u8_Data = Utils.StructureToBytesFix(this);
                return Utils.BytesToHex(u8_Data);
            }
        }
        
        // NOT Pack = 1 here !
        [StructLayout(LayoutKind.Sequential, Pack = 2)]
        public struct kPipeInformation
        {
            public ePipeType me_PipeType;
            public Byte      mu8_PipeId;   // Endpoint
            public ushort    mu16_MaxPacketSize;
            public Byte      mu8_Interval; // for interrupt endpoints

            public eDirection Direction
            {
                get { return (eDirection)(mu8_PipeId & 0x80); }
            }
        }

        #endregion

        #region DLL Imports WinUSB

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_Initialize(SafeFileHandle DeviceHandle, out IntPtr InterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_Free(IntPtr InterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_GetAssociatedInterface(IntPtr h_WinUSB, Byte u8_InterfaceIndex, out IntPtr h_AssociatedInterfaceHandle);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_GetDescriptor(IntPtr h_WinUSB, eDescriptor DescriptorType, Byte Index, UInt16 LanguageID, [MarshalAs(UnmanagedType.LPArray)] Byte[] u8_Data, int BufferLength, out int LengthTransfered);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_QueryInterfaceSettings(IntPtr h_Interface, Byte u8_AlternateInterfaceNumber, [MarshalAs(UnmanagedType.LPArray)] Byte[] u8_Data);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_QueryPipe(IntPtr h_Interface, Byte u8_AlternateInterfaceNumber, Byte u8_PipeIndex, out kPipeInformation k_PipeInformation);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_ControlTransfer(IntPtr h_Interface, kSetup k_Setup, Byte[] Buffer, int s32_BufSize, out int s32_Transferred, IntPtr pk_Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_SetPipePolicy(IntPtr h_Interface, Byte u8_PipeID, ePipePolicy PolicyType, int ValueLength, ref Byte u8_BoolValue);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_SetPipePolicy(IntPtr h_Interface, Byte u8_PipeID, ePipePolicy PolicyType, int ValueLength, ref int s32_IntValue);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_AbortPipe(IntPtr h_Interface, Byte u8_PipeID);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_WritePipe(IntPtr h_Interface, Byte u8_PipeID, Byte[] u8_Data, int s32_DataLength, out int s32_Transferred, IntPtr pk_Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_ReadPipe(IntPtr h_Interface, Byte u8_PipeID, IntPtr h_RxBuffer, int s32_BufSize, IntPtr p_Transferred, ref NativeOverlapped k_Overlapped);

        [DllImport("winusb.dll", SetLastError = true)]
        static extern bool WinUsb_GetOverlappedResult(IntPtr h_Interface, ref NativeOverlapped k_Overlapped, out int s32_Transferred, bool b_Wait);

        #endregion

        #region cInterface

        public class cInterface : IDisposable
        {
            private WinUSB               mi_WinUSB;
            private IntPtr               mh_Handle;
            private kInterfaceDescriptor mk_Descriptor;
            private String               ms_InterfaceStr;
            private List<cPipeIn>        mi_InPipes  = new List<cPipeIn>();
            private List<cPipeOut>       mi_OutPipes = new List<cPipeOut>();

            public cInterface(WinUSB i_WinUSB, IntPtr h_Handle)
            {
                mi_WinUSB = i_WinUSB;
                mh_Handle = h_Handle;

                Byte[] u8_Descr = new Byte[Marshal.SizeOf(typeof(kInterfaceDescriptor))];
                if (!WinUsb_QueryInterfaceSettings(h_Handle, 0, u8_Descr))
                    i_WinUSB.ThrowLastError("Error reading interface descriptor");

                mk_Descriptor = Utils.BytesToStructureFix<kInterfaceDescriptor>(u8_Descr);

                for (Byte u8_EP=0; u8_EP < mk_Descriptor.bNumEndpoints; u8_EP++)
                {
                    kPipeInformation k_Pipe;
                    if (!WinUsb_QueryPipe(h_Handle, 0, u8_EP, out k_Pipe))
                        i_WinUSB.ThrowLastError("Error getting pipe information");

                    if (k_Pipe.Direction == eDirection.In)  mi_InPipes .Add(new cPipeIn (i_WinUSB, h_Handle, this, k_Pipe));
                    if (k_Pipe.Direction == eDirection.Out) mi_OutPipes.Add(new cPipeOut(i_WinUSB, h_Handle, this, k_Pipe));
                }

                ms_InterfaceStr = mi_WinUSB.ReadStringDescriptor(mk_Descriptor.iInterface, LANGUAGE_ENGLISH_USA);
            }

            public override string ToString()
            {
                return ms_InterfaceStr;
            }

            public void Dispose()
            {
                foreach (cPipeIn i_PipeIn in mi_InPipes)
                {
                    i_PipeIn.Dispose();
                }

                // The first interface is the main interface returned by WinUsb_Initialize()
                WinUsb_Free(mh_Handle);
                mh_Handle = IntPtr.Zero;
            }

            /// <summary>
            /// Interface 0 = Candlelight
            /// Interface 1 = DFU
            /// </summary>
            public Byte Number
            {
                get { return mk_Descriptor.bInterfaceNumber; }
            }
            public String String
            {
                get { return ms_InterfaceStr; }
            }
            public Byte EndpointCount
            {
                get { return mk_Descriptor.bNumEndpoints; }
            }

            public cPipeOut GetPipeOut(ePipeType e_Type)
            {
                foreach (cPipeOut i_Pipe in mi_OutPipes)
                {
                    if (i_Pipe.PipeType == e_Type)
                        return i_Pipe;
                }
                return null;
            }

            public cPipeIn GetPipeIn(ePipeType e_Type)
            {
                foreach (cPipeIn i_Pipe in mi_InPipes)
                {
                    if (i_Pipe.PipeType == e_Type)
                        return i_Pipe;
                }
                return null;
            }
        }

        #endregion

        #region cPipe

        public class cPipe
        {
            protected WinUSB           mi_WinUSB;
            protected IntPtr           mh_Handle;
            protected cInterface       mi_Interface;
            protected kPipeInformation mk_PipeInfo;
            protected int              ms32_PipeErrors;

            public ePipeType PipeType
            {
                get { return mk_PipeInfo.me_PipeType; }
            }

            public override string ToString()
            {
                return String.Format("EP {0:X2}", Endpoint);
            }

            public Byte Endpoint
            {
                get { return mk_PipeInfo.mu8_PipeId; }
            }
            public UInt16 MaxPacketSize
            {
                get { return mk_PipeInfo.mu16_MaxPacketSize; }
            }
            public int PipeErrors
            {
                get { return ms32_PipeErrors; }
            }

            public cPipe(WinUSB i_WinUSB, IntPtr h_Handle, cInterface i_Interface, kPipeInformation k_PipeInfo)
            {
                mi_WinUSB    = i_WinUSB;
                mh_Handle    = h_Handle;
                mi_Interface = i_Interface;
                mk_PipeInfo  = k_PipeInfo;
            }

            public void SetPolicy(ePipePolicy e_Policy, bool b_Value)
            {
                Debug.Assert(e_Policy != ePipePolicy.PipeTransferTimeout, "Invalid parameter");

                Byte u8_Bool = b_Value ? (Byte)1 : (Byte)0;
                if (!WinUsb_SetPipePolicy(mh_Handle, Endpoint, e_Policy, 1, ref u8_Bool))
                    mi_WinUSB.ThrowLastError("Error setting pipe policy");
            }

            /// <summary>
            /// ERROR_SEM_TIMEOUT in Read()
            /// </summary>
            public void SetTransferTimeout(int s32_Timeout)
            {
                if (!WinUsb_SetPipePolicy(mh_Handle, Endpoint, ePipePolicy.PipeTransferTimeout, 4, ref s32_Timeout))
                    mi_WinUSB.ThrowLastError("Error setting pipe timeout");
            }

            /// <summary>
            /// Does not throw
            /// </summary>
            public bool Abort()
            {
                return WinUsb_AbortPipe(mh_Handle, Endpoint);
            }
        }

        #endregion

        #region cPipeOut

        public class cPipeOut : cPipe
        {
            public cPipeOut(WinUSB i_WinUSB, IntPtr h_Handle, cInterface i_Interface, kPipeInformation k_Pipe) 
                : base(i_WinUSB, h_Handle, i_Interface, k_Pipe)
            {
            }

            /// <summary>
            /// Timeout must be set with SetTransferTimeout() !
            /// </summary>
            public void Send(Byte[] u8_TxData)
            {
                #if TRACE_PIPES
                    Debug.Print(String.Format("Tx: [{0}] {1}", u8_TxData.Length, Utils.BytesToHex(u8_TxData)));
                #endif

                int s32_Transferred = 0;
                if (!WinUsb_WritePipe(mh_Handle, Endpoint, u8_TxData, u8_TxData.Length, out s32_Transferred, IntPtr.Zero))
                {
                    ms32_PipeErrors ++;
                    mi_WinUSB.ThrowLastError("Error writing to pipe");
                }

                if (s32_Transferred != u8_TxData.Length)
                    throw new Exception("Error sending data to pipe.");

                ms32_PipeErrors = 0;
            }
        }

        #endregion

        #region cPipeIn

        public class cPipeIn : cPipe, IDisposable
        {
            class cRxFifo
            {
                public Byte[]  mu8_Buffer;
                public int     ms32_BytesRead;
                public int     ms32_Error;
                public Int64   ms64_WinTimestamp;
            };

            cRxFifo[]   mi_RxFifo = new cRxFifo[30]; // must only be accessed in a lock()
            int         ms32_FifoReadIdx;            // must only be accessed in a lock()
            int         ms32_FifoCount;              // must only be accessed in a lock()
            bool        mb_AbortThread;
            bool        mb_FifoOverflow;
            IntPtr      mh_ThreadEvent;
            IntPtr      mh_ReceiveEvent;

            public cPipeIn(WinUSB i_WinUSB, IntPtr h_Handle, cInterface i_Interface, kPipeInformation k_Pipe) 
                : base(i_WinUSB, h_Handle, i_Interface, k_Pipe)
            {
                mh_ReceiveEvent = Utils.CreateEventW(IntPtr.Zero, false, false, null);

                for (int i=0; i<mi_RxFifo.Length; i++)
                {
                    mi_RxFifo[i] = new cRxFifo();
                }
            }

            public void Dispose()
            {
                // abort ReadPipeThread and wait until it has exited (mh_ThreadEvent == NULL). Timeout is 1 second.
                for (int i=0; mh_ThreadEvent != IntPtr.Zero && i < 100; i++)
                {
                    mb_AbortThread = true;
                    Utils.SetEvent(mh_ThreadEvent);
                    Thread.Sleep(10);
                }
                Utils.CloseHandle(mh_ReceiveEvent);
            }

            public void StartThread()
            {
                Thread i_Thread = new Thread(new ThreadStart(ReadPipeThread));
                i_Thread.IsBackground = true;
                i_Thread.Priority     = ThreadPriority.Highest;
                i_Thread.Name         = "WinUSB Pipe Thread";
                i_Thread.Start();
            }

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
            private void ReadPipeThread()
            {
                mb_AbortThread = false;
                mh_ThreadEvent = Utils.CreateEventW(IntPtr.Zero, false, false, null);

                NativeOverlapped k_Overlapped = new NativeOverlapped();
                k_Overlapped.EventHandle = mh_ThreadEvent;

                // The buffer should be a multiple of the endpoint's max packet size.
                // The buffer must not be moved by the garbage collector between WinUsb_ReadPipe() and WinUsb_GetOverlappedResult()
                Byte[]   u8_RxBuffer = new Byte[128];
                GCHandle i_GcHandle  = GCHandle.Alloc(u8_RxBuffer, GCHandleType.Pinned);
                IntPtr   h_RxBuffer  = i_GcHandle.AddrOfPinnedObject();

                while (!mb_AbortThread)
                {
                    lock (mi_RxFifo) 
                    {
                        if (ms32_FifoCount >= mi_RxFifo.Length)
                            mb_FifoOverflow = true;
                    }

                    // if an overflow occurred, stop reading USB packets and inform the caller that it is polling too slowly.
                    if (mb_FifoOverflow)
                    {
                        Thread.Sleep(50);
                        continue;
                    }

                    int s32_FifoWriteIdx;
                    cRxFifo i_FifoWrite;
                    lock (mi_RxFifo)
                    {
                        s32_FifoWriteIdx = (ms32_FifoReadIdx + ms32_FifoCount) % mi_RxFifo.Length;
                        i_FifoWrite      = mi_RxFifo[s32_FifoWriteIdx];
                    }

                    int s32_Read  = 0;
                    int s32_Error = 0;
                    if (!WinUsb_ReadPipe(mh_Handle, Endpoint, h_RxBuffer, u8_RxBuffer.Length, IntPtr.Zero, ref k_Overlapped))
                    {
                        s32_Error = Marshal.GetLastWin32Error();
                        if (s32_Error == (int)eApiError.ERROR_IO_PENDING)
                        {
                            s32_Error = 0;

                            // mh_ThreadEvent is set when a USB IN packet was received and in Close() to abort the thread
                            eWaitObject e_Result = Utils.WaitForSingleObject(mh_ThreadEvent, Timeout.Infinite);
                            if (mb_AbortThread)
                                break;

                            switch (e_Result)
                            {
                                case eWaitObject.Timeout: // This should never happen with timeout = INFINITE
                                    s32_Error = (int)eApiError.ERROR_TIMEOUT;
                                    break;

                                case eWaitObject.Object0:
                                    if (WinUsb_GetOverlappedResult(mh_Handle, ref k_Overlapped, out s32_Read, false))
                                        ms32_PipeErrors = 0;
                                    else
                                        s32_Error = Marshal.GetLastWin32Error();                                   
                                    break;

                                default: // WAIT_FAILED (I have never seen this error, but just in case...)
                                    s32_Error = Marshal.GetLastWin32Error();
                                    break;
                            }
                        }
                    }

                    i_FifoWrite.ms64_WinTimestamp = Utils.GetWinTimestamp();
                    i_FifoWrite.ms32_BytesRead    = s32_Read;
                    i_FifoWrite.ms32_Error        = s32_Error;
                    i_FifoWrite.mu8_Buffer        = null;
                    if (s32_Error == 0)
                        i_FifoWrite.mu8_Buffer = Utils.ExtractByteArr(u8_RxBuffer, 0, s32_Read);

                    #if TRACE_PIPES
                        Debug.Print(String.Format("Rx: [{0}] {1}", s32_Read, Utils.BytesToHex(i_FifoWrite.mu8_Buffer)));
                    #endif

                    // Increment write index for the next ReadPipe, leave read index unchanged
                    lock (mi_RxFifo) 
                    {
                        ms32_FifoCount ++;               // FIRST !
                        Utils.SetEvent(mh_ReceiveEvent); // AFTER !
                    }

                    if (s32_Error > 0)
                    {
                        // If the CANable has been disconnected an error ERROR_BAD_COMMAND or ERROR_GEN_FAILURE will be reported in each loop.
                        // This high priority thread must be slowed down to avoid that it consumes
                        // a lot of CPU power running in an endless loop and to avoid a FIFO overflow.
                        Thread.Sleep(50);
                        ms32_PipeErrors ++;
                    }
                } // while

                i_GcHandle.Free();
                Utils.CloseHandle(mh_ThreadEvent);
                mh_ThreadEvent = IntPtr.Zero; // Setting this to zero signals that the thread has finished.
            }

            /// <summary>
            /// returns null if timeout
            /// </summary>
            public Byte[] Receive(int s32_Timeout, out Int64 s64_RxTimestamp)
            {
                s64_RxTimestamp = 0;

                int   s32_Available;
                cRxFifo i_FifoRead;
                lock (mi_RxFifo)
                {
                    i_FifoRead    = mi_RxFifo[ms32_FifoReadIdx];
                    s32_Available = ms32_FifoCount;
                    if (s32_Available > 0)
                        Utils.ResetEvent(mh_ReceiveEvent);
                }

                // No data to read --> return null
                if (s32_Available == 0)
                {
                    // After all messages in the FIFO have been returned inform about the FIFO overflow.
                    if (mb_FifoOverflow)
                    {
                        lock (mi_RxFifo)
                        {
                            mb_FifoOverflow = false;
                        }
                        throw new Exception("Rx FIFO overflow");
                    }

                    eWaitObject e_Result = Utils.WaitForSingleObject(mh_ReceiveEvent, s32_Timeout);
                    if (e_Result == eWaitObject.Timeout)
                        return null; // Timeout

                    lock (mi_RxFifo)
                    {
                        s32_Available = ms32_FifoCount;
                    }

                    if (s32_Available == 0)
                        return null; // Timeout
                }

                // s32_Timestamp = i_FifoRead.ms32_Timestamp;
                Byte[] u8_RxData = i_FifoRead.mu8_Buffer;
                int   s32_Error  = i_FifoRead.ms32_Error;
                s64_RxTimestamp  = i_FifoRead.ms64_WinTimestamp;

                lock (mi_RxFifo)
                {
                    ms32_FifoReadIdx = (ms32_FifoReadIdx + 1) % mi_RxFifo.Length;
                    ms32_FifoCount --;
                }

                if (s32_Error != 0)
                    Utils.ThrowApiError(s32_Error, "Error {0} reading WinUSB pipe: {1}");

                return u8_RxData;
            }
        }

        #endregion

        const UInt16 LANGUAGE_ENGLISH_USA = 0x409;

        SafeFileHandle        mi_FileHandle;
        IntPtr                mh_WinUSB;
        kDeviceDescriptor     mi_DeviceDescr;
        List<kDescriptorHead> mi_AllDescriptors = new List<kDescriptorHead>();
        cInterface            mi_Interface;
        String                ms_Vendor;
        String                ms_Product;
        String                ms_SerialNo;
        bool                  mb_IsOpen;

        public String Vendor
        {
            get { return ms_Vendor; }
        }
        public String Product
        {
            get { return ms_Product; }
        }
        public String SerialNo
        {
            get { return ms_SerialNo; }
        }
        public kDeviceDescriptor DeviceDescriptor
        {
            get { return mi_DeviceDescr; }
        }
        public kDescriptorHead[] AllDescriptors
        {
            get { return mi_AllDescriptors.ToArray(); }
        }
        public cInterface Interface
        {
            get { return mi_Interface; }
        }

        public override string ToString()
        {
            return String.Format("{0} - {1} [{2}]", ms_Vendor, ms_Product, ms_SerialNo);
        }

        public void Dispose()
        {
            if (mi_Interface != null)
            {
                mi_Interface.Dispose();
                mi_Interface = null;
            }

            if (mi_FileHandle != null)
            {
                mi_FileHandle.Dispose();
                mi_FileHandle = null;
            }
        }

        // -------------------------------------------------------------------

        /// <summary>
        /// s_NtPath = return value from SetupApi.EnumerateUsbDevices()
        /// s32_ControlTimeout = timeout in ms for Control piupe (endpoint 00)
        /// </summary>
        public void Open(String s_NtPath, int s32_ControlTimeout)
        {
            Debug.Assert(!mb_IsOpen, "Never reuse this class. Always create a new instance.");

            // IMPORTANT: Do NOT set FileShare.Read or FileShare.Write here.
            // When the device is used here, any other software that tries to open it will get an Access Denied error.
            mi_FileHandle = Utils.CreateFileW(s_NtPath, eFileAccess.GenericRead | eFileAccess.GenericWrite, 
                                              eFileShare.None, IntPtr.Zero, eFileCreate.OpenExisting, 
                                              eFileFlags.AttributeNormal | eFileFlags.FlagOverlapped, IntPtr.Zero);

            // Also invalid if device is disconnected
            if (mi_FileHandle.IsInvalid)
            {
                int s32_Error = Marshal.GetLastWin32Error();
                if (s32_Error == (int)eApiError.ACCESS_DENIED)
                    throw new Exception("Error opening the WinUSB device. It is probably already open elsewhere.");
                else
                    ThrowLastError("Error opening the WinUSB device", s32_Error);
            }

            // ERROR_NOT_ENOUGH_MEMORY if the wrong interface in s_NtPath. (MI_00 interface for Candlelight, MI_01 for DFU)
            // ERROR_INVALID_HANDLE if file was not opened with FlagOverlapped
            if (!WinUsb_Initialize(mi_FileHandle, out mh_WinUSB))
                ThrowLastError("Error initializing the WinUSB device");

            // Set timeout for control pipe (Endpoint 00)
            if (!WinUsb_SetPipePolicy(mh_WinUSB, 0, ePipePolicy.PipeTransferTimeout, 4, ref s32_ControlTimeout))
                ThrowLastError("Error setting control pipe timeout");

            ReadDescriptors(eDescriptor.Device);
            ReadDescriptors(eDescriptor.Configuration);

            // Microsoft manipulates iProduct in the device descriptor to point to the string for the interface name.
            // In the vast majority of USB devices we find: iManufacturer = 1, iProduct = 2, iSerialNumber = 3.
            // WinUSB sets iProduct = iInterface which is in case of the Candlelight the string for interface 0.
            // We try to fix this here to get the string of the device descriptor instead of the interface descriptor.
            if (mi_DeviceDescr.iManufacturer == 1 && mi_DeviceDescr.iSerialNumber == 3)
                mi_DeviceDescr.iProduct = 2;

            ms_Vendor   = ReadStringDescriptor(mi_DeviceDescr.iManufacturer, LANGUAGE_ENGLISH_USA);
            ms_Product  = ReadStringDescriptor(mi_DeviceDescr.iProduct,      LANGUAGE_ENGLISH_USA);
            ms_SerialNo = ReadStringDescriptor(mi_DeviceDescr.iSerialNumber, LANGUAGE_ENGLISH_USA);   

            // Theoretically here you can call WinUsb_GetAssociatedInterface() to add more interfaces.
            // But Candlelight has only one.
            mi_Interface = new cInterface(this, mh_WinUSB);

            mb_IsOpen = true;
        }

        // -------------------------------------------------------------------

        /// <summary>
        /// The configuration descriptor comes appended with all it's interface descriptors and functional descriptors.
        /// Writes all descriptors into mi_AllDescriptors.
        /// ATTENTION:
        /// Microsoft manipulates the descriptors so they only contain the data for the interface for which WinUSB has been opened.
        /// Microsoft removes all interfaces from the descriptors that are not in use.
        /// If you Open() for interface 0 you get only the Candlelight descriptos
        /// If you Open() for interface 1 you get only the DFU descriptos
        /// </summary>
        void ReadDescriptors(eDescriptor e_Descriptor)
        {
            Byte[] u8_Data = new Byte[255];
            int s32_Read;
            if (!WinUsb_GetDescriptor(mh_WinUSB, e_Descriptor, 0, 0, u8_Data, u8_Data.Length, out s32_Read))
                ThrowLastError("Error reading the "+e_Descriptor+" descriptor");

            kInterfaceDescriptor i_LastInterface = new kInterfaceDescriptor();

            int s32_Offset = 0;
            while (s32_Offset < s32_Read)
            {
                // read only the first 2 bytes
                kDescriptorHead i_Head = Utils.BytesToStructureVar<kDescriptorHead>(u8_Data, s32_Offset, Marshal.SizeOf(typeof(kDescriptorHead)));

                if (s32_Offset + i_Head.bLength > s32_Read)
                    throw new Exception("Device has sent incomplete data for the "+e_Descriptor+" descriptor.");

                kDescriptorHead i_Descr = null;
                switch (i_Head.eDescrType)
                {
                    case eDescriptor.Device:  
                        mi_DeviceDescr = Utils.BytesToStructureVar<kDeviceDescriptor>(u8_Data, s32_Offset, i_Head.bLength); 
                        i_Descr = mi_DeviceDescr;
                        break;
                    case eDescriptor.Configuration:  
                        i_Descr = Utils.BytesToStructureVar<kConfigDescriptor>(u8_Data, s32_Offset, i_Head.bLength); 
                        break;
                    case eDescriptor.Endpoint:  
                        i_Descr = Utils.BytesToStructureVar<kEndpointDescriptor>(u8_Data, s32_Offset, i_Head.bLength); 
                        break;
                    case eDescriptor.Interface: 
                        i_LastInterface = Utils.BytesToStructureVar<kInterfaceDescriptor>(u8_Data, s32_Offset, i_Head.bLength);
                        i_Descr = i_LastInterface;
                        break;
                    case eDescriptor.HID: // HID and DFU have the same descriptor type (0x21)!
                        if (i_LastInterface.bInterfaceClass == eDeviceClass.HID)
                            i_Descr = Utils.BytesToStructureVar<kHidDescriptor>(u8_Data, s32_Offset, i_Head.bLength);
                        if (i_LastInterface.bInterfaceClass == eDeviceClass.FwUpgrade)
                            i_Descr = Utils.BytesToStructureVar<kDfuDescriptor>(u8_Data, s32_Offset, i_Head.bLength);
                        break;
                    default:
                        // Descriptor not implemented
                        break;
                }

                s32_Offset += i_Descr.bLength;

                if (i_Descr != null)
                    mi_AllDescriptors.Add(i_Descr);
            }
        }

        // Read a string descriptor (private)
        String ReadStringDescriptor(byte u8_Index, ushort u16_LanguageID)
        {
            // If the descriptor does not define a string, the index is zero. This is not an error.
            if (u8_Index == 0)
                return "";

            Byte[] u8_Buffer = new Byte[256];
            int s32_Read;
            if (!WinUsb_GetDescriptor(mh_WinUSB, eDescriptor.String, u8_Index, u16_LanguageID, u8_Buffer, u8_Buffer.Length, out s32_Read))
                ThrowLastError("Error reading string descriptor " + u8_Index);

            Byte      u8_Length = u8_Buffer[0];
            eDescriptor e_Descr = (eDescriptor)u8_Buffer[1];

            if (e_Descr != eDescriptor.String || u8_Length != s32_Read || (s32_Read & 1) > 0)
                throw new Exception("The device returned crippled data for string descriptor " + u8_Index + ".");

            return Encoding.Unicode.GetString(u8_Buffer, 2, s32_Read - 2);
        }

        // -------------------------------------------------------------------

        /// <summary>
        /// u8_Request = eSetupRequest or eUsbRequest or eDfuRequest
        /// </summary>
        public int CtrlTansfer(eSetupRecip e_Recip, eSetupType e_Type, eDirection e_Dir, Byte u8_Request, 
                               UInt16 u16_Value, UInt16 u16_Index, ref Byte[] u8_Buffer)
        {
            kSetup k_Setup;
            k_Setup.mu8_RequType = (Byte)((int)e_Recip | (int)e_Type | (int)e_Dir);
            k_Setup.mu8_Request  = u8_Request;
            k_Setup.mu16_Value   = u16_Value;
            k_Setup.mu16_Index   = u16_Index;
            k_Setup.mu16_Length  = 0; // ignored, set by WinUSB

            int s32_Transferred;
            if (!WinUsb_ControlTransfer(mh_WinUSB, k_Setup, u8_Buffer, u8_Buffer.Length, out s32_Transferred, IntPtr.Zero))
                return Marshal.GetLastWin32Error();

            if (e_Dir == eDirection.In && s32_Transferred < u8_Buffer.Length)
                u8_Buffer = Utils.ExtractByteArr(u8_Buffer, 0, s32_Transferred);

            return 0;
        }

        /// <summary>
        /// s_Mesg = "Error setting control pipe timeout."
        /// </summary>
        void ThrowLastError(String s_Mesg, int s32_Error = 0)
        {
            if (s32_Error <= 0) 
                s32_Error = Marshal.GetLastWin32Error();

            // Replace stupid message "A device attached to the system is not functioning." when an endpoint has been stalled.
            if (mb_IsOpen && s32_Error == (int)eApiError.GEN_FAILURE)
                throw new Exception(s_Mesg + ".  No response from the WinUSB device");
            
            // Let Windows translate the error code
            Win32Exception i_WinEx = new Win32Exception(s32_Error);
            s_Mesg += ".  Error " + s32_Error + ": " + i_WinEx.Message;

            // All errors before mb_IsOpen == true are WinUSB errors
            // Error 121 "The semaphore has timed out" while reading string descriptor 0 after firmware has crashed.
            if (!mb_IsOpen) s_Mesg += "\nThe reason may be that the device has crashed or is defective. Try to reconnect the USB cable.";

            throw new Exception(s_Mesg);
        }
    }
}

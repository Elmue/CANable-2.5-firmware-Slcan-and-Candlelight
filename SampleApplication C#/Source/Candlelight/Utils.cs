
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
using System.IO;
using System.Text;
using System.Diagnostics;
using System.ComponentModel;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace CANable
{
    /// <summary>
    /// Geenral Windows API stuff and helper functions
    /// </summary>
    public class Utils
    {
        #region enums Kernel32

        public  static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

        public enum eApiError : uint
        {
            ACCESS_DENIED        =          5,
            GEN_FAILURE          =         31, // A device attached to the system is not functioning.
            INVALID_PARAMETER    =         87,
            SEM_TIMEOUT          =        121,
            NO_MORE_ITEMS        =        259,
            NO_SUCH_DEVICE       =        433,
            OPERATION_ABORTED    =        995,
            ERROR_IO_INCOMPLETE  =        996,
            ERROR_IO_PENDING     =        997, 
            ERROR_TIMEOUT        =       1460,
            WSAEHOSTUNREACH      =      10065,
            ReflectionTypeLoadEx = 0x80131602,
        }

        public enum eWaitObject : int
        {
            Failed    = -1,  // call GetLastError()
            Object0   = 0,
            Object1   = 1,
            Object2   = 2,
            Object3   = 3,
            Abandoned = 0x80, // only used for Mutex
            Timeout   = 0x102,
        }

        [FlagsAttribute]
        public enum eFileAccess : uint
        {
            GenericRead  = 0x80000000,
            GenericWrite = 0x40000000,
        }

        [FlagsAttribute]
        public enum eFileShare
        {
            None  = 0,
            Read  = 1,
            Write = 2,
        }

        public enum eFileCreate
        {
            OpenExisting = 3,
        }

        [FlagsAttribute]
        public enum eFileFlags
        {
            AttributeNormal = 0x00000080,
            FlagOverlapped  = 0x40000000,
        }

        #endregion

        #region DLL Imports

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern SafeFileHandle CreateFileW(String s_FileName, eFileAccess e_DesiredAccess, eFileShare e_ShareMode, IntPtr p_SecurityAttributes, eFileCreate e_CreationDisposition, eFileFlags e_FlagsAndAttributes, IntPtr h_TemplateFile);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CancelIo(IntPtr h_File);

        [DllImport("kernel32.dll", SetLastError = true, CharSet=CharSet.Unicode)]
        public static extern IntPtr CreateEventW(IntPtr lpEventAttributes, bool bManualReset, bool bInitialState, String lpName);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool SetEvent(IntPtr hHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool ResetEvent(IntPtr hHandle);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern eWaitObject WaitForSingleObject(IntPtr hHandle, int dwMilliseconds);

        [DllImport("msvcrt.dll", CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr memset(IntPtr dest, int c, IntPtr count);

        #endregion

        #region Console Stuff

        public delegate bool ConsoleCtrlDelegate(int ctrlType);

        [StructLayout(LayoutKind.Sequential)]
        public struct INPUT_KEY_RECORD
        {
            public UInt16 EventType;
            public bool   bKeyDown;
            public UInt16 wRepeatCount;
            public UInt16 wVirtualKeyCode;
            public UInt16 wVirtualScanCode;
            public UInt16 UnicodeChar;
            public UInt32 dwControlKeyState;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr GetStdHandle(int nStdHandle);

        [DllImport("Kernel32")]
        public static extern bool SetConsoleCtrlHandler(ConsoleCtrlDelegate handler, bool add);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool PeekConsoleInput(IntPtr hConsoleInput, out INPUT_KEY_RECORD lpBuffer, int nLength, out int lpNumberOfEventsRead);

        [DllImport("kernel32.dll", SetLastError = true, CharSet=CharSet.Unicode)]
        public static extern bool ReadConsoleInputW(IntPtr hConsoleInput, out INPUT_KEY_RECORD lpBuffer, int nLength, out int lpNumberOfEventsRead);

        #endregion

        private static Stopwatch mi_Timestamp = Stopwatch.StartNew();

        // ------------------------------------------------

        // Create a timestamp with 1 µs precision.
        // It is recommended to turn off transmssion of timestamps (not set GS_DevFlagTimestamp) to reduce USB traffic.
        // Then this function is used as a replacement to generate a timestamp on reception of a packet and when sending a packet.
        public static Int64 GetWinTimestamp()
        {
            double d_Time = mi_Timestamp.ElapsedTicks;
            d_Time *= 1000000.0; // µs per second

            // The performance counter runs inside the CPU and the frequency is identical over all CPU cores and never changes.
            // The performance counter frequency depends on the CPU and the operating system, mostly above 3 MHz
            d_Time /= Stopwatch.Frequency;
            return (Int64)d_Time;
        }

        // ------------------------------------------------

        /// <summary>
        /// s_Format = "Error {0} enumerating USB devices: {1}"
        /// </summary>
        public static void ThrowApiError(int s32_Error, String s_Format)
        {
            // Let Windows translate the error code
            Win32Exception i_WinEx = new Win32Exception(s32_Error);
            throw new Exception(String.Format(s_Format, s32_Error, i_WinEx.Message));
        }

        // ------------------------------------------------

        public static Byte[] ExtractByteArr(Byte[] u8_Data, int s32_Start, int s32_Count = -1)
        {
            if (s32_Count < 0)
                s32_Count = u8_Data.Length - s32_Start;

            if (s32_Start < 0 || s32_Count < 0 || s32_Start + s32_Count > u8_Data.Length)
                throw new Exception("Invalid parameters");
            
            Byte[] u8_Copy = new Byte[s32_Count];
            Array.Copy(u8_Data, s32_Start, u8_Copy, 0, s32_Count);
            return u8_Copy;
        }

        public static Byte[] ByteArrayConcat(params Byte[][] u8_Arrays)
        {
            List<Byte> i_List = new List<Byte>();
            foreach (Byte[] u8_Array in u8_Arrays)
            {
                i_List.AddRange(u8_Array);
            }
            return i_List.ToArray();
        }

        // ------------------------------------------------

        /// <summary>
        /// This function expects a byte array with the excatly same length as the structure
        /// </summary>
        public static T BytesToStructureFix<T>(Byte[] u8_Bytes)
        {
            int s32_Size = Marshal.SizeOf(typeof(T));
            if (s32_Size != u8_Bytes.Length)
                throw new Exception("Invalid data for structure " + typeof(T).Name);

            return BytesToStructureVar<T>(u8_Bytes, 0, s32_Size);
        }

        /// <summary>
        /// If s32_ByteCount < sizeof(structure) --> only s32_ByteCount bytes are copied and the rest of the structure stays empty
        /// If s32_ByteCount > sizeof(structure) --> the remaining bytes in u8_Bytes are igonred
        /// </summary>
        public static T BytesToStructureVar<T>(Byte[] u8_Bytes, int s32_Offset, int s32_ByteCount = int.MaxValue)
        {
            int s32_Size = Marshal.SizeOf(typeof(T));
            IntPtr p_Mem = Marshal.AllocHGlobal(s32_Size);
            memset(p_Mem, 0, (IntPtr)s32_Size);

            s32_ByteCount = Math.Min(s32_ByteCount, s32_Size);
            s32_ByteCount = Math.Min(s32_ByteCount, u8_Bytes.Length - s32_Offset);
            try
            {
                Marshal.Copy(u8_Bytes, s32_Offset, p_Mem, s32_ByteCount);
                return (T)Marshal.PtrToStructure(p_Mem, typeof(T));
            }
            finally
            {
                Marshal.FreeHGlobal(p_Mem);
            }
        }

        // ------------------------------------------------

        /// <summary>
        /// Converts the entire structure into a byte array
        /// </summary>
        public static Byte[] StructureToBytesFix(Object o_Structure)
        {
            if (o_Structure is Byte[]) // only used for eDfuRequest.Detach
                return (Byte[])o_Structure;

            return StructureToBytesVar(o_Structure, Marshal.SizeOf(o_Structure.GetType()));
        }

        /// <summary>
        /// Converts only the first s32_ByteCount of the structure into a byte array
        /// </summary>
        public static Byte[] StructureToBytesVar(Object o_Structure, int s32_ByteCount)
        {
            int s32_Size = Marshal.SizeOf(o_Structure.GetType());
            if (s32_ByteCount > s32_Size)
                throw new Exception("Invalid size for struct conversion");

            IntPtr p_Mem = Marshal.AllocHGlobal(s32_Size);
            try
            {
                Marshal.StructureToPtr(o_Structure, p_Mem, false);
                Byte[] u8_Bytes = new Byte[s32_ByteCount];
                Marshal.Copy(p_Mem, u8_Bytes, 0, s32_ByteCount);
                return u8_Bytes;
            }
            finally
            {
                Marshal.FreeHGlobal(p_Mem);
            }
        }

        // ------------------------------------------------

        /// <summary>
        /// s_Delimiter = " " --> returns "82 11 F1 21 01 CS"
        /// </summary>
        public static String BytesToHex(Byte[] u8_Data, int s32_First=0, int s32_MaxCount=0, String s_Delimiter=" ")
        {
            Debug.Assert(u8_Data != null); // This should never happen

            int s32_Last = u8_Data.Length;
            if (s32_MaxCount > 0)
                s32_Last = Math.Min(s32_Last, s32_First + s32_MaxCount);

            StringBuilder i_Hex = new StringBuilder();
            for (int i=s32_First; i<s32_Last; i++)
            {
                if (i > s32_First) i_Hex.Append(s_Delimiter);
                i_Hex.Append(u8_Data[i].ToString("X2"));
            }
            return i_Hex.ToString();
        }

        // 0x0        --> "0"
        // 0x11       --> "11"
        // 0x1122     --> "11.22"
        // 0x11223344 --> "11.22.33.44"
        public static String FormatBcdVersion(UInt32 u32_Version)
        {
            if (u32_Version == 0)
                return "0";

            String s_Version = "";
            for (int i=0; i<4; i++)
            {
                if (i>0) s_Version = "." + s_Version;

                s_Version = (u32_Version & 0xFF).ToString("X2") + s_Version;
                u32_Version >>= 8;
                if (u32_Version == 0)
                    break;
            }
            return s_Version.TrimStart('0');
        }
    }
}


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

#include "Utils.h"

using namespace CANable;

// ====================== Workarounds for stupid std =============================

// The std library is primitive. It has no replacement for Microsoft's CString.ToUpper()
string cUtils::MakeUpper(string s_String)
{
    for (size_t i = 0; i < s_String.length(); i++)
    {
        s_String[i] = toupper(s_String[i]);
    }
    return s_String;
}

// The std library is primitive. It has no replacement for Microsoft's CString.TrimRight()
string cUtils::TrimRight(string s_String, char* s_Remove) // s_Remove = " \n\r\t"
{
    int s32_Len = (int)s_String.length();
    for (int S = s32_Len - 1; S >= 0; S--)
    {
        bool b_Trim = false;
        for (int R=0; s_Remove[R] > 0; R++)
        {
            if (s_String[S] == s_Remove[R])
            {
                s32_Len --;
                b_Trim = true;
                break;
            }
        }
        if (!b_Trim)
            break;
    }
    return s_String.substr(0, s32_Len);
}

// The std library is primitive. It has no replacement for Microsoft's CString.Format()
// This function supports max 5000 characters which will never be exceeded by this class.
string cUtils::Format(char* c_Format, ...)
{
    va_list args;
    va_start(args, c_Format);

    char s_Buffer[5000];
    int s32_Len = vsnprintf_s(s_Buffer, sizeof(s_Buffer), c_Format, args);
    va_end(args);

    if (s32_Len < 0)
    {
        assert(false); // Buffer too small
        return "";
    }

    s_Buffer[s32_Len] = 0;
    return s_Buffer;
}

// The std library is primitive. It has no replacement for Microsoft's CMapStringToString.Lookup()
string cUtils::MapLookup(cStringMap& i_Map, string& s_Key)
{
    auto it = i_Map.find(s_Key);
    if (it == i_Map.end())
        return "";

    return it->second;
}

// ------------------------------------------------

// replacement for Windows GetTickCount() --> return time counter in milliseconds
uint64_t cUtils::GetTickMilli()
{
    return (uint64_t)clock() * 1000 / CLOCKS_PER_SEC;
}

// returns "02 67 5E C7 FF "
string cUtils::FormatHexBytes(uint8_t u8_Data[], int s32_DataLen)
{
    string s_Hex;
    for (int i=0; i<s32_DataLen; i++)
    {
        char c_Buf[5];
        sprintf_s(c_Buf, "%02X ", u8_Data[i]);
        s_Hex += c_Buf;
    }
    return s_Hex;
}

// 0x00000000 --> "0"
// 0x00000011 --> "11"
// 0x00000105 --> "1.5"
// 0x11223344 --> "11.22.33.44"
// 0x00YYMMDD --> "Day.Month.Year"
string cUtils::FormatBcdVersion(uint32_t u32_Version)
{
    if (u32_Version == 0)
        return "0";

    // BCD encoded 0x00YYMMDD
    if (u32_Version > 0x250101 && u32_Version < 0x991231)
    {
        uint8_t u8_Day   = (uint8_t)(u32_Version);
        uint8_t u8_Month = (uint8_t)(u32_Version >> 8);
        uint8_t u8_Year  = (uint8_t)(u32_Version >> 16);

        char* c_MonthName = NULL;
        switch (u8_Month)
        {
            case 0x01: c_MonthName = "Jan"; break;
            case 0x02: c_MonthName = "Feb"; break;
            case 0x03: c_MonthName = "Mar"; break;
            case 0x04: c_MonthName = "Apr"; break;
            case 0x05: c_MonthName = "May"; break;
            case 0x06: c_MonthName = "Jun"; break;
            case 0x07: c_MonthName = "Jul"; break;
            case 0x08: c_MonthName = "Aug"; break;
            case 0x09: c_MonthName = "Sep"; break;
            case 0x10: c_MonthName = "Oct"; break;
            case 0x11: c_MonthName = "Nov"; break;
            case 0x12: c_MonthName = "Dec"; break;
        }

        if (c_MonthName && u8_Day >= 1 && u8_Day <= 31)
            return cUtils::Format("%X.%s.%02X", u8_Day, c_MonthName, u8_Year);
    }

    char c_Buf[10];
    string s_Version;
    for (int s32_Shift = 24; s32_Shift >= 0; s32_Shift -= 8)
    {
        uint8_t u8_Part = (uint8_t)(u32_Version >> s32_Shift);
        if (s_Version.length())
        {
            sprintf_s(c_Buf, ".%X", u8_Part);
            s_Version += c_Buf;
        }
        else if (u8_Part > 0)
        {
            sprintf_s(c_Buf, "%X", u8_Part);
            s_Version += c_Buf;
        }
    }
    return s_Version;
}


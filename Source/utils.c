/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "utils.h"

bitlimits limits;
char      MCU_name[30]; // name is never longer than 24 characters

void utils_init()
{
    // Stupidly the files from ST do not declare any constants for the CAN bitrate ranges.
    // They only give us the macros IS_FDCAN_NOMINAL_PRESCALER(), IS_FDCAN_NOMINAL_TSEG1(), ...
    // But the host application needs these limits to calculate the bitrates (commands 's' and 'y')
    
    for (limits.nom_brp_max  = 2; IS_FDCAN_NOMINAL_PRESCALER(limits.nom_brp_max  +1); limits.nom_brp_max ++) { }
    for (limits.nom_seg1_max = 2; IS_FDCAN_NOMINAL_TSEG1    (limits.nom_seg1_max +1); limits.nom_seg1_max++) { }
    for (limits.nom_seg2_max = 2; IS_FDCAN_NOMINAL_TSEG2    (limits.nom_seg2_max +1); limits.nom_seg2_max++) { }
    for (limits.nom_sjw_max  = 2; IS_FDCAN_NOMINAL_SJW      (limits.nom_sjw_max  +1); limits.nom_sjw_max ++) { }
    
    for (limits.fd_brp_max   = 2; IS_FDCAN_DATA_PRESCALER   (limits.fd_brp_max   +1); limits.fd_brp_max  ++) { }
    for (limits.fd_seg1_max  = 2; IS_FDCAN_DATA_TSEG1       (limits.fd_seg1_max  +1); limits.fd_seg1_max ++) { }
    for (limits.fd_seg2_max  = 2; IS_FDCAN_DATA_TSEG2       (limits.fd_seg2_max  +1); limits.fd_seg2_max ++) { }
    for (limits.fd_sjw_max   = 2; IS_FDCAN_DATA_SJW         (limits.fd_sjw_max   +1); limits.fd_sjw_max  ++) { }
    
    // remove trailing "x" in "STM32G431xx" from makefile
    strcpy(MCU_name, TARGET_MCU);
    int len = strlen(MCU_name);
    while (MCU_name[--len] == 'x')
    {
        MCU_name[len] = 0;
    }
}

bitlimits* utils_get_bit_limits()
{
    return &limits;
}

// returns "STM32G431" 
const char* utils_get_MCU_name()
{
    return MCU_name;
}

// Check if a memory area has only zero bytes
bool utils_mem_is_empty(void* ptr, int size)
{
    uint8_t* pmem = (uint8_t*)ptr;
    for (int i=0; i<size; i++)
    {
        if (pmem[i] > 0)
            return false;
    }
    return true;
}

// Convert a FDCAN DLC into the number of bytes in a message
int8_t utils_dlc_to_byte_count(uint32_t dlc_code)
{
    if (dlc_code <= 8)
        return dlc_code;
    
    switch (dlc_code)
    {
        case 0x9: return 12;
        case 0xA: return 16;
        case 0xB: return 20;
        case 0xC: return 24;
        case 0xD: return 32;
        case 0xE: return 48;
        case 0xF: return 64;
        default:  return -1;
    }
}

int8_t utils_byte_count_to_dlc(uint32_t byte_count)
{
         if (byte_count > 48) return 15;
    else if (byte_count > 32) return 14;
    else if (byte_count > 24) return 13;
    else if (byte_count > 20) return 12;
    else if (byte_count > 16) return 11;
    else if (byte_count > 12) return 10;
    else if (byte_count >  8) return  9;
    return byte_count;    
}

// Formats a string containing the baudrate and samplepoint.
// Although this is very simple many application and firmware developers do this calculation wrong. (see sourcecode of Cangaroo and Nakakiyo)
// It is very important to verify that baudrate and samplepoint have been calculated correctly.
// Therefore this firmware prints them to the debug output.
void utils_format_bitrate(char buf[], char* prefix, can_bitrate_cfg* bit_rate)
{
    if (bit_rate->Brp == 0)
    {
        sprintf(buf, "%s: No baudrate set", prefix);
        return;
    }

    uint32_t smpl = can_calc_sample(bit_rate);
    uint32_t baud = can_calc_baud  (bit_rate);
    
    // Do not display 83333 baud as "83k"
    char* unit = "";
         if (baud >= 1000000 && (baud % 1000000) == 0) { baud /= 1000000; unit = "M"; }
    else if (baud >= 1000    && (baud % 1000)    == 0) { baud /= 1000;    unit = "k"; }
    sprintf(buf, "%s: %lu%s baud, %lu.%lu%%", prefix, baud, unit, smpl/10, smpl%10);
}

// reads a decimal number from the buffer until a separator is found.
// the string must be zero terminated (see control_parse_str())
bool utils_parse_next_decimal(char buf[], int* pos, char separator, uint32_t* value)
{
    *value = 0;
    for (int i=*pos; true; i++)
    {
        char uChar = buf[i];
        if (uChar < '0' || uChar > '9')
        {
            if (uChar != separator)
                return false;
            
            *pos = i + 1;
            return true;
        }        
        *value *= 10;
        *value += uChar - '0';
    }    
}

// Reads a hexadecimal number from the buffer until a separator, an invalid char or the end of the string is found.
// The string must be zero terminated (see control_parse_str())
bool utils_parse_hex_delimiter(char buf[], int* pos, char separator, int* digits, uint32_t* value)
{
    *digits = 0;
    *value  = 0;
    for (int i=*pos; true; i++)
    {
        if (!utils_to_hex_value(buf, i))
        {
            if (buf[i] != separator)
            {
                *pos = i; // set pos to invalid character or zero termination
                return false;
            }
            *pos = i + 1; // set pos to character after separator
            return true;
        }        
        *digits += 1;        
        *value <<= 4;
        *value |= buf[i];
    }    
}

// reads 'digits' hex digits from 'buf' at 'pos' and stores the binary value in 'value'.
bool utils_parse_hex_value(char buf[], int* pos, int digits, uint32_t* value)
{
    *value = 0;
    for (int i=0; i<digits; i++)
    {
        if (!utils_to_hex_value(buf, *pos))
            return false;
        
        *value <<= 4;
        *value |= buf[*pos];
        (*pos)++;        
    } 
    return true;
}

// converts hex ASCII into numeric value in the same buffer location
// returns false on invalid character or end of string 
// (the string has been zero terminated in control_parse_str(), so reading behind the end is not possible)
bool utils_to_hex_value(char buf[], int pos)
{
    char u8_Char = buf[pos];
    
         if ('0' <= u8_Char && u8_Char <= '9') u8_Char -= '0';
    else if ('a' <= u8_Char && u8_Char <= 'f') u8_Char -= 'a' - 10;
    else if ('A' <= u8_Char && u8_Char <= 'F') u8_Char -= 'A' - 10;
    else return false;
    
    buf[pos] = u8_Char;
    return true;
}

uint8_t utils_nibble_to_ascii(uint8_t nibble)
{
    if (nibble < 10) return nibble + '0';
    else             return nibble + 'A' - 10;
}

/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Two helper macros because the precompiler is not able to conacatenate a string with a constant
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// ============================================================================================
// The following enums are used for Slcan and Candlelight

typedef enum 
{
    false = 0,
    true  = 1,
} bool;

// If command feedback is enabled these error codes are sent to the host.
// This enum is used for Slcan and for Candlelight.
// Slcan sends errors as "#1\r" which means FBK_InvalidCommand.
// Candlelight sends errors with command ELM_ReqGetLastError.
typedef enum // sent as 8 bit
{
    FBK_RetString = 1,            // The reponse has already been sent over USB --> no additional feedback. This is used only internally.
    FBK_Success   = 2,            // Command successfully executed
    // --------------------------    
    FBK_InvalidCommand    = '1',  // The command is invalid
    FBK_InvalidParameter,         // One of the parameters is invalid
    FBK_AdapterMustBeOpen,        // The command cannot be executed before opening the adapter
    FBK_AdapterMustBeClosed,      // The command cannot be executed after  opening the adapter
    FBK_ErrorFromHAL,             // The HAL from ST Microelectronics has reported an error
    FBK_UnsupportedFeature,       // The feature is not implemented or not supported by the board
    FBK_TxBufferFull,             // Sending is not possible because the buffer is full (only Slcan)
    FBK_BusIsOff,                 // Sending is not possible because the processor is blocked in the BusOff state
    FBK_NoTxInSilentMode,         // Sending is not possible because the adapter is in Bus Monitoring mode
    FBK_BaudrateNotSet,           // Opening the adapter is not possible if no baudrate has been set
    FBK_OptBytesProgrFailed,      // Programming the Option Bytes failed
    FBK_ResetRequired,            // The user must disconnect and reconnect the USB cable to enter boot mode
} eFeedback;

// If bus status is BUS_OFF both LED's (green + blue) are permanently ON
// This status is controlled only by hardware
// Slcan sends this in the error report "EXXXXXXXX\r"
typedef enum // sent as 4 bit
{
    BUS_StatusActive     = 0x00, // operational  (must be zero because this is not an error)
    BUS_StatusWarning    = 0x10, // set in can.c (>  96 errors)
    BUS_StatusPassive    = 0x20, // set in can.c (> 128 errors)
    BUS_StatusOff        = 0x30, // set in can.c (> 248 errors)
} eErrorBusStatus;

// If any of these flags is set, both LED's (green + blue) are permanently ON
// These flags are reset after sending them once to the host
// They are set again if the error is still present
// Slcan sends this in the error report "EXXXXXXXX\r"
// Candlelight sends this in a special error packet with a flag (legacy: CAN_ID_Error, ElmüSoft: MSG_Error)
typedef enum // sent as 8 bit 
{
    APP_CanRxFail       = 0x01, // the HAL reports an error receiving a CAN packet.
    APP_CanTxFail       = 0x02, // trying to send while in silent mode, while bus off or adaper not open or invalid Tx packet or HAL error
    APP_CanTxOverflow   = 0x04, // a CAN packet could not be sent because the Tx FIFO + buffer are full (mostly because bus is passive).
    APP_UsbInOverflow   = 0x08, // a USB IN packet could not be sent because CAN traffic is faster than USB transfer.
    APP_CanTxTimeout    = 0x10, // A packet in the transmit FIFO was not acknowledged during 500 ms --> abort Tx and clear Tx buffer.
} eErrorAppFlags;

// ============================================================================================
// TARGET_MCU is defined in the Makefile

#if defined(STM32G431xx)
    #include "stm32g4xx.h"
    #include "stm32g4xx_hal.h"
#elif defined(STM32G473xx)
    #include "stm32g4xx.h"
    #include "stm32g4xx_hal.h"
#else
    #error "TARGET_MCU not defined in makefile"
#endif

// ============================================================================================
// TARGET_BOARD is defined in Makefile

#if defined(Multiboard)
    // MKS Makerbase + Walfront + DSD Tech use default settings
#elif defined(Jhoinrch)    
    // The Jhoinrch board has a 25 MHz quartz crystal. The make file sets: QUARTZ_FREQU = 25000000
#elif defined(OpenlightLabs)
    // OpenlightLabs has the green LED at pin B11
    #define LED_TX_Pin          GPIO_PIN_11  
    #define LED_TX_Port         GPIOB
#else
    #error "TARGET_BOARD not defined in makefile"
#endif

// ============================================================================================
// Load default settings if no board-specific settings are defined

// green Tx LED is at pin A0
#ifndef LED_TX_Pin
    #define LED_TX_Pin          GPIO_PIN_0   
    #define LED_TX_Port         GPIOA
#endif

// blue Rx Led is at pin A15
#ifndef LED_RX_Pin
    #define LED_RX_Pin          GPIO_PIN_15
    #define LED_RX_Port         GPIOA      
#endif

// PP = Push/Pull, OD = Open Drain
#ifndef LED_Mode
    #define LED_Mode            GPIO_MODE_OUTPUT_PP
#endif

// Some boards use inverted voltage (Low = ON)
#ifndef LED_ON
    #define LED_ON              GPIO_PIN_RESET
    #define LED_OFF             GPIO_PIN_SET
#endif

// The CAN interface (some processors have 3 CAN interfaces)
#ifndef CAN_INTERFACE
    #define CAN_INTERFACE       FDCAN1
#endif

// Some boards have a 120 Ohm termination resistor that can be enabled by a GPIO pin.
// If the board does not support this --> set TERMINATOR_Pin = -1
#ifndef TERMINATOR_Port
    #define TERMINATOR_Port     GPIOB
    #define TERMINATOR_Pin      -1 // GPIO_PIN_3
    #define TERMINATOR_Mode     GPIO_MODE_OUTPUT_PP
    #define TERMINATOR_ON       GPIO_PIN_SET    // turn on termination resistor
    #define TERMINATOR_OFF      GPIO_PIN_RESET
#endif

// On some boards the power supply of the isolator chip can be disabled when not using CAN bus.
// If the board has no isolation power switch transistor set ISOLATOR_PWR_Pin = -1
#ifndef ISOLATOR_PWR_Port
	#define ISOLATOR_PWR_Port   GPIOC
	#define ISOLATOR_PWR_Pin    -1 // GPIO_PIN_13  
    #define ISOLATOR_ON         GPIO_PIN_SET    // turn on power supply of isolator chip
    #define ISOLATOR_OFF        GPIO_PIN_RESET
#endif

// ============================================================================================

// Define the firmware version in BCD format.
// Version 0x250914 is displayed as "25.09.14" and means 14th september 2025
// The year and month are stored in the device descriptor.
// The entire version is returned by Slcan command "V" and by Candlelight command GS_ReqGetDeviceVersion
// Do not use totally meaningless version numbers like "b158aa7" in legacy firmware on Github.
#define FIRMWARE_VERSION_BCD   0x260113

// ATTENTION: 
// This version defines which Slcan commands are available.
// Whenever you add new Slcan commands, don't forget to increment the version number and write a documentation for them.
// So the controlling application knows with which firmware it is dealing.
// (Candlelight does not need a version number because it returns the supported features as bit flags)
#define SLCAN_VERSION          100




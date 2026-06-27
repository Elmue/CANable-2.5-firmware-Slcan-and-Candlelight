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
#include <stdbool.h>

// Two helper macros because the precompiler is not able to conacatenate a string with a constant
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

// ============================================================================================
// The following enums are used for Slcan and Candlelight

// If command feedback is enabled these error codes are sent to the host.
// This enum is used for Slcan and for Candlelight.
// Slcan sends errors as "#1\r" which means FBK_InvalidCommand.
// Candlelight sends errors with command ELM_ReqGetLastError.
typedef enum // sent as 8 bit
{
    FBK_RetString = 1,            // The reponse has already been sent over USB --> no additional feedback. This is used only internally.
    FBK_Success   = 2,            // Command successfully executed
    // --------------------------
    FBK_InvalidCommand    = '1',  // "#1" = The command is invalid
    FBK_InvalidParameter,         // "#2" = One of the parameters is invalid
    FBK_AdapterMustBeOpen,        // "#3" = The command cannot be executed before opening the adapter
    FBK_AdapterMustBeClosed,      // "#4" = The command cannot be executed after  opening the adapter
    FBK_ErrorFromHAL,             // "#5" = The HAL from ST Microelectronics has reported an error
    FBK_UnsupportedFeature,       // "#6" = The feature is not implemented or not supported by the board
    FBK_TxBufferFull,             // "#7" = Sending is not possible because the buffer is full (only Slcan)
    FBK_BusIsOff,                 // "#8" = Sending is not possible because the processor is blocked in the BusOff state
    FBK_NoTxInSilentMode,         // "#9" = Sending is not possible because the adapter is in Bus Monitoring mode
    FBK_BaudrateNotSet,           // "#:" = Opening the adapter is not possible if no baudrate has been set
    FBK_OptBytesProgrFailed,      // "#;" = Programming the Option Bytes failed
    FBK_ResetRequired,            // "#<" = The user must disconnect and reconnect the USB cable to enter boot mode
    FBK_ParamOutOfRange,          // "#=" = A paramter is outside the valid range
} eFeedback;

// If bus status is BUS_OFF both LED's (Rx + Tx) are permanently ON
// This status is controlled only by hardware
// Slcan sends this in the error report "EXXXXXXXX\r"
typedef enum // sent as 4 bit
{
    BUS_StatusActive     = 0x00, // operational  (must be zero because this is not an error)
    BUS_StatusWarning    = 0x10, // set in can.c (>  96 errors)
    BUS_StatusPassive    = 0x20, // set in can.c (> 128 errors)
    BUS_StatusOff        = 0x30, // set in can.c (> 248 errors)
} eErrorBusStatus;

// If any of these flags is set, both LED's (Rx + Tx) are permanently ON
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
    APP_CanTxTimeout    = 0x10, // a packet in the transmit FIFO was not acknowledged during 500 ms --> abort Tx and clear Tx buffer.
} eErrorAppFlags;

// ============================================================================================
// MCU_SERIE is defined in the Makefile

#if defined(STM32G4xx)
    #include "stm32g4xx.h"
    #include "stm32g4xx_hal.h"
#elif defined(STM32G0xx)
    #include "stm32g0xx.h"
    #include "stm32g0xx_hal.h"
#else
    #error "MCU_SERIE not implemented"
#endif

// ============================================================================================
// TARGET_BOARD is defined in Makefile

#if defined(Multiboard)

    // MKS Makerbase + Walfront + DSD Tech + Jhoinrch before 2026 use default settings and there is no quartz.
    #define MAX_CAN_BAUDRATE    10 // CAN transceiver chip limits to 10 Mbaud
    // -------------------    
    #define ALLOW_DISABLE_BOOT0 1  // allow to disable pin BOOT0
    
#elif defined(Jhoinrch)

    // Jhoinrch puts a 25 MHz quartz on all their boards since 2026
    #define MAX_CAN_BAUDRATE    10 // CAN transceiver chip limits to 10 Mbaud
    // -------------------    
    #define ALLOW_DISABLE_BOOT0 1  // allow to disable pin BOOT0
    
#elif defined(OpenlightLabs)

    // OpenlightLabs has the Tx LED at pin B11
    #define LED_TX_PINS         GPIO_PIN_11
    #define LED_TX_PORTS        GPIOB
    // -------------------
    #define MAX_CAN_BAUDRATE    5 // CAN transceiver chip limits to 5 Mbaud
    // -------------------
    #define ALLOW_DISABLE_BOOT0 1 // allow to disable pin BOOT0    
    
#elif defined(OleksiiSolo)

    // Oleksii puts a 8 MHz quartz on the single channel board
    #define LED_TX_PINS         GPIO_PIN_5
    #define LED_TX_PORTS        GPIOA
    #define LED_RX_PINS         GPIO_PIN_6
    #define LED_RX_PORTS        GPIOA
    // -------------------
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_SET             // The LED's cathode is connected to ground
    #define LED_OFF             GPIO_PIN_RESET
    // -------------------
    #define MAX_CAN_BAUDRATE    8 // CAN transceiver chip limits to 8 Mbaud
    // -------------------
    #define ALLOW_DISABLE_BOOT0 1 // allow to disable pin BOOT0 (indispensable for correct operation)
    
#elif defined(OleksiiDual)

    // Oleksii puts a 8 MHz quartz on the dual channel board
    // The board has 2 CAN connectors and creates 2 Candlelight USB interfaces.
    #define CHANNEL_COUNT       2
    // -------------------      Channel 1:               Channel 2:
    #define CAN_INTERFACES      FDCAN1,                  FDCAN2
    #define CAN_PINS            GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_5 | GPIO_PIN_6 // CANFD Tx, Rx pins
    #define CAN_PORTS           GPIOB,                   GPIOB                   // CANFD Port
    #define CAN_ALTERNATES      GPIO_AF9_FDCAN1,         GPIO_AF9_FDCAN2  // switch pin multiplexer to CAN module
    // -------------------
    #define LED_TX_PINS         GPIO_PIN_5,              GPIO_PIN_3 
    #define LED_TX_PORTS        GPIOA,                   GPIOA
    #define LED_RX_PINS         GPIO_PIN_6,              GPIO_PIN_4 
    #define LED_RX_PORTS        GPIOA,                   GPIOA
    // -------------------
    #define TERMINATOR_PINS     -1,                      -1  // termination resistor is switched by a manual jumper
    #define TERMINATOR_PORTS    GPIOB,                   GPIOB    
    // ---------------------------------------------------------
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_SET             // The LED's cathode is connected to ground
    #define LED_OFF             GPIO_PIN_RESET
    // -------------------
    #define MAX_CAN_BAUDRATE    8 // CAN transceiver chip limits to 8 Mbaud
    // -------------------
    #define ALLOW_DISABLE_BOOT0 1 // allow disable pin BOOT0 (indispensable for correct operation)
    
#elif defined(WeActStudioV1)

    // The WeActStudio USB2CANFD v1 has a STM32G0B1 processor and a 16 MHz quartz
    #define CHANNEL_COUNT       1
    // -------------------
    #define CAN_INTERFACES      FDCAN1
    #define CAN_PINS            GPIO_PIN_8 | GPIO_PIN_9  // Rx = PB8, Tx = PB9
    #define CAN_PORTS           GPIOB                    // Port B
    #define CAN_ALTERNATES      GPIO_AF3_FDCAN1          // switch pin 8,9 multiplexer to CAN module
    // -------------------
    #define LED_RX_PINS         GPIO_PIN_0 // green
    #define LED_RX_PORTS        GPIOA
    #define LED_TX_PINS         GPIO_PIN_1 // blue
    #define LED_TX_PORTS        GPIOA    
    // The third LED is used by the WeAct firmware to show that the device is in firmware update mode.
    // But WeActStudio uses their own proprietary firmware updater which is CRAP and is erased when uploading this firmware.
    // The ElmueSoft firmware uses the third LED as Power LED to show that the firmware is running (Off in DFU mode)
    #define LED_PWR_PIN         GPIO_PIN_2 // red
    #define LED_PWR_PORT        GPIOA    
    // -------------------
    #define MAX_CAN_BAUDRATE    5 // CAN transceiver chip limits to 5 Mbaud
    // -------------------
    #define ALLOW_DISABLE_BOOT0 0 // do not allow to disable pin BOOT0 (not required for this processor) 
    
#elif defined(WeActStudioV2)    

    // The WeActStudio USB2CANFD v2 has a STM32G431 processor and a 16 MHz quartz
    #define LED_RX_PINS         GPIO_PIN_0
    #define LED_RX_PORTS        GPIOA
    #define LED_TX_PINS         GPIO_PIN_1
    #define LED_TX_PORTS        GPIOA    
    #define LED_PWR_PIN         GPIO_PIN_2 // see comment of WeActStudioV1
    #define LED_PWR_PORT        GPIOA    
    // -------------------
    // This board has a more intelligent hardware design than most boards for the STM32G431.
    // The shared processor pin CAN RXD + BOOT0 is held low by hardware to avoid entering BOOT mode when USB power is connected.
    // The firmware must enable the CAN transceiver chip by switching processor pin PB7 to low.
    #define CAN_TRX_ENABLE_PIN  GPIO_PIN_7
    #define CAN_TRX_ENABLE_PORT GPIOB
    #define CAN_TRX_ENABLE_ON   GPIO_PIN_RESET
    // -------------------
    #define MAX_CAN_BAUDRATE    5 // CAN transceiver chip limits to 5 Mbaud
    // -------------------
    #define ALLOW_DISABLE_BOOT0 0 // do not allow to disable pin BOOT0 (not required due to correct hardware design)
    
#else
    
    #error "TARGET_BOARD not implemented"
    
#endif


// ============================================================================================
// Load default settings if no board-specific settings are defined

// Define single channel default: Tx LED is at pin A0
#ifndef LED_TX_PINS
    #define LED_TX_PINS         GPIO_PIN_0  // MKS Makerbase: green, Jhoinrch: blue
    #define LED_TX_PORTS        GPIOA
#endif

// Define single channel default: Rx LED is at pin A15
#ifndef LED_RX_PINS
    #define LED_RX_PINS         GPIO_PIN_15 // MKS Makerbase: blue, Jhoinrch: magenta
    #define LED_RX_PORTS        GPIOA
#endif

// PP = Push/Pull, OD = Open Drain
// Most boards use inverted voltage (Low = ON): The LED's anode is connected to +3.3V
#ifndef LED_MODE
    #define LED_MODE            GPIO_MODE_OUTPUT_PP
    #define LED_ON              GPIO_PIN_RESET
    #define LED_OFF             GPIO_PIN_SET
#endif

// Define single channel default: no terminator pin available
// Some boards have a 120 Ohm termination resistor that can be enabled by a GPIO pin.
// If the board does not support this --> set TERMINATOR_Pin = -1
#ifndef TERMINATOR_PINS
    #define TERMINATOR_PINS     -1
    #define TERMINATOR_PORTS    GPIOB
#endif    
#ifndef TERMINATOR_MODE
    #define TERMINATOR_MODE     GPIO_MODE_OUTPUT_PP
    #define TERMINATOR_ON       GPIO_PIN_SET        // turn on termination resistor
    #define TERMINATOR_OFF      GPIO_PIN_RESET
#endif

// Define single channel default: Use CAN interface 1 at PB8 and PB9
// (STUPID STM32G431 design where FDCAN1 and BOOT0 use the same pin)
#ifndef CHANNEL_COUNT
    #define CHANNEL_COUNT       1
    #define CAN_INTERFACES      FDCAN1
    #define CAN_PINS            GPIO_PIN_8 | GPIO_PIN_9 // Rx = PB8, Tx = PB9
    #define CAN_PORTS           GPIOB                   // Port B
    #define CAN_ALTERNATES      GPIO_AF9_FDCAN1         // switch pin 8,9 multiplexer to CAN module
#endif

// 0x00 = adapter power comes over USB cable
// 0x40 = adapter has own power supply (flag 'Self Powered' in bmAttributes in Configuration descriptor)
// 0xXX = any other value is invalid!
#ifndef USBD_SELF_POWERED
    #define USBD_SELF_POWERED   0x00
#endif





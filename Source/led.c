/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "led.h"
#include "can.h"
#include "error.h"

// Duration in ms of a short flash when a CAN packet was received / sent
// The LEDs are very bright. If the ON time is too long it seems as if it does not go off.
#define FLASH_ON_DURATION     15
#define FLASH_OFF_DURATION    40
// Duration in ms of the power-on blink sequence
#define POWER_ON_DURATION     75
// Duration in ms of the device identification blink sequence
#define IDENTIFY_DURATION     80
// turn the LEDs 4 times on and off
#define POWER_ON_COUNT         8

#define LED_TX LED_TX_Port, LED_TX_Pin
#define LED_RX LED_RX_Port, LED_RX_Pin

// Private variables
static volatile uint32_t led_RX_laston   = 0;
static volatile uint32_t led_TX_laston   = 0;
static uint32_t          led_RX_lastoff  = 0;
static uint32_t          led_TX_lastoff  = 0;
static uint8_t           led_error_was_indicating = 0;
static uint32_t          led_next_blink  = 0;
static uint32_t          led_blink_count = 0;
static bool              led_identify    = false;

// Initialize LED GPIOs
void led_init()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin       = LED_RX_Pin; // see settings.h
    GPIO_InitStruct.Mode      = LED_Mode;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(LED_RX_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = LED_TX_Pin;
    GPIO_InitStruct.Mode      = LED_Mode;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;
    HAL_GPIO_Init(LED_TX_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(LED_RX, LED_ON);
    HAL_GPIO_WritePin(LED_TX, LED_ON);
}

// when the operating system goes into sleep mode --> turn off both LED's
void led_sleep()
{
    HAL_GPIO_WritePin(LED_RX, LED_OFF);
    HAL_GPIO_WritePin(LED_TX, LED_OFF);
}

// Blink green / blue alternatingly on power on.
// This is a blocking function by purpose.
void led_blink_power_on()
{
    uint8_t i;
    for (i = 0; i < POWER_ON_COUNT; i++)
    {
        HAL_GPIO_WritePin(LED_RX, LED_ON);
        HAL_GPIO_WritePin(LED_TX, LED_OFF);
        HAL_Delay(POWER_ON_DURATION);
        HAL_GPIO_WritePin(LED_RX, LED_OFF);
        HAL_GPIO_WritePin(LED_TX, LED_ON);    
        HAL_Delay(POWER_ON_DURATION);
    }
}

// Blink green / blue alternatingly to identify a device if multiple devices are connected at the same time.
// This is a non-blocking function executed by USB command.
void led_blink_identify(bool blink_on)
{
    led_next_blink = HAL_GetTick() + IDENTIFY_DURATION;
    led_identify   = blink_on;
    
    GPIO_PinState state = blink_on ? LED_ON : LED_OFF;
    HAL_GPIO_WritePin(LED_RX, state);
    HAL_GPIO_WritePin(LED_TX, state);
}

// Turn green LED on/off
void led_turn_TX(uint8_t state)
{
    if (led_identify)
        return;
    
    HAL_GPIO_WritePin(LED_TX, state);
}

// Turn green LED on for a short duration
// Called when CAN frame has been transmitted
void led_flash_TX()
{
    if (led_identify)
        return;
    
    // Make sure the LED has been off for at least FLASH_OFF_DURATION before turning on again
    // This prevents a solid status LED on a busy canbus
    if (led_TX_laston == 0 && HAL_GetTick() - led_TX_lastoff > FLASH_OFF_DURATION)
    {
        HAL_GPIO_WritePin(LED_TX, LED_ON);
        led_TX_laston = HAL_GetTick();
    }
}

// Turn blue LED on for a short duration
// Called when CAN frame was received
void led_flash_RX()
{
    if (led_identify)
        return;
    
    // Make sure the LED has been off for at least FLASH_OFF_DURATION before turning on again
    // This prevents a solid status LED on a busy canbus
    if (led_RX_laston == 0 && HAL_GetTick() - led_RX_lastoff > FLASH_OFF_DURATION)
    {
        HAL_GPIO_WritePin(LED_RX, LED_ON);
        led_RX_laston = HAL_GetTick();
    }
}

// called approx 100 times per millisecond from main.c
void led_process(uint32_t tick_now)
{
    if (led_identify) // highest priority
    {
        // Blink pattern: Both off, Rx ON, Both off, Tx ON, ...
        if (tick_now >= led_next_blink)
        {
            led_blink_count ++;
            uint8_t status_tx = ((led_blink_count & 3) == 1) ? LED_ON : LED_OFF;            
            uint8_t status_rx = ((led_blink_count & 3) == 3) ? LED_ON : LED_OFF;
            HAL_GPIO_WritePin(LED_TX, status_tx);            
            HAL_GPIO_WritePin(LED_RX, status_rx);
            led_next_blink += IDENTIFY_DURATION;            
        }
        return;
    }
    
    // If an error occurred, turn blue + green LEDs on (second highest priority)
    // Severe errors displayed by LED are: Bus Off, Rx failed, Tx failed, Buffer Overflow.
    // Bus Passive is NOT a severe error to be displayed by both LED's turned on.
    if (error_get_state()->bus_status == BUS_StatusOff || error_get_state()->app_flags)
    {
        HAL_GPIO_WritePin(LED_RX, LED_ON);
        HAL_GPIO_WritePin(LED_TX, LED_ON);
        led_error_was_indicating = 1;
        return;
    }
    
    // error state has finished --> return to LEDs off
    if (led_error_was_indicating)
    {
        HAL_GPIO_WritePin(LED_RX, LED_OFF);
        HAL_GPIO_WritePin(LED_TX, LED_OFF);
        led_error_was_indicating = 0;
    }

    // If LED has been on for long enough, turn it off
    if (led_RX_laston > 0 && tick_now - led_RX_laston > FLASH_ON_DURATION)
    {
        HAL_GPIO_WritePin(LED_RX, LED_OFF);
        led_RX_laston  = 0;
        led_RX_lastoff = tick_now;
    }

    // If LED has been on for long enough, turn it off
    if (led_TX_laston > 0 && tick_now - led_TX_laston > FLASH_ON_DURATION)
    {
        // Invert LED
        HAL_GPIO_WritePin(LED_TX, LED_OFF);
        led_TX_laston  = 0;
        led_TX_lastoff = tick_now;
    }
    
    // Green LED on while bus is closed
    if (!can_is_opened())
        led_turn_TX(LED_ON); // green on   
}

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

// ---- Settings  (from settings.h)
GPIO_TypeDef* SET_LedTxPorts[CHANNEL_COUNT] = { LED_TX_PORTS };
int           SET_LedTxPins [CHANNEL_COUNT] = { LED_TX_PINS  };
GPIO_TypeDef* SET_LedRxPorts[CHANNEL_COUNT] = { LED_RX_PORTS };
int           SET_LedRxPins [CHANNEL_COUNT] = { LED_RX_PINS  };

// ----- Class Instance
led_class  led_inst[CHANNEL_COUNT] = {0};

// ----- Private Methods
void SetRxLed(int channel, bool status);
void SetTxLed(int channel, bool status);

// Initialize LED GPIOs
void led_init()
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;    
    GPIO_InitStruct.Mode      = LED_MODE;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = 0;
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        if (SET_LedRxPins[C] >= 0)
        {
            GPIO_InitStruct.Pin = SET_LedRxPins[C];
            HAL_GPIO_Init(SET_LedRxPorts[C], &GPIO_InitStruct);
        }
        if (SET_LedTxPins[C] >= 0)
        {
            GPIO_InitStruct.Pin = SET_LedTxPins[C];
            HAL_GPIO_Init(SET_LedTxPorts[C], &GPIO_InitStruct);
        }
        // In case of a crash during initialization --> all LEDs are ON which shows a severe error.
        SetRxLed(C, true);
        SetTxLed(C, true);
    }
}

// when the operating system goes into sleep mode --> turn off all LED's
void led_sleep()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        SetRxLed(C, false);
        SetTxLed(C, false);
    }
}

// Blink LEDs of all channels alternatingly on power on.
// This is a blocking function by purpose.
void led_blink_power_on()
{
    uint8_t i;
    for (i = 0; i < POWER_ON_COUNT; i++)
    {
        for (int C=0; C<CHANNEL_COUNT; C++)
        {
            SetRxLed(C, true);
            SetTxLed(C, false);
        }
        HAL_Delay(POWER_ON_DURATION);
        
        for (int C=0; C<CHANNEL_COUNT; C++)
        {
            SetRxLed(C, false);
            SetTxLed(C, true);    
        }
        HAL_Delay(POWER_ON_DURATION);
    }
}

// -----------------------------------

// Blink green / blue alternatingly to identify a device if multiple devices are connected at the same time.
// This is a non-blocking function. Blinking is enabled by USB command.
void led_blink_identify(int channel, bool blink_on)
{
    led_class* inst = &led_inst[channel];

    if (inst->identify == blink_on)
        return;

    inst->identify   = blink_on;
    inst->next_blink = HAL_GetTick() + IDENTIFY_DURATION;
    
    SetRxLed(channel, blink_on);
    SetTxLed(channel, blink_on);
}

// Turn green LED on/off
void led_turn_TX(int channel, bool state)
{
    led_class* inst = &led_inst[channel];
    if (inst->identify)
        return;
    
    SetTxLed(channel, state);
}

// Turn green LED on for a short duration
// Called when CAN frame has been transmitted
void led_flash_TX(int channel)
{
    led_class* inst = &led_inst[channel];
    if (inst->identify)
        return;
    
    // Make sure the LED has been off for at least FLASH_OFF_DURATION before turning on again
    // This prevents a solid status LED on a busy canbus
    if (inst->TX_laston == 0 && HAL_GetTick() - inst->TX_lastoff > FLASH_OFF_DURATION)
    {
        SetTxLed(channel, true);
        inst->TX_laston = HAL_GetTick();
    }
}

// Turn blue LED on for a short duration
// Called when CAN frame was received
void led_flash_RX(int channel)
{
    led_class* inst = &led_inst[channel];
    if (inst->identify)
        return;
    
    // Make sure the LED has been off for at least FLASH_OFF_DURATION before turning on again
    // This prevents a solid status LED on a busy canbus
    if (inst->RX_laston == 0 && HAL_GetTick() - inst->RX_lastoff > FLASH_OFF_DURATION)
    {
        SetRxLed(channel, true);
        inst->RX_laston = HAL_GetTick();
    }
}

// called approx 100 times per millisecond from main.c
void led_process(int channel, uint32_t tick_now)
{
    led_class* inst = &led_inst[channel];
    
    if (inst->identify) // highest priority
    {
        // Blink pattern: Both off, Rx ON, Both off, Tx ON, ...
        if (tick_now >= inst->next_blink)
        {
            inst->blink_count ++;
            SetTxLed(channel, (inst->blink_count & 3) == 1);            
            SetRxLed(channel, (inst->blink_count & 3) == 3);
            inst->next_blink += IDENTIFY_DURATION;            
        }
        return;
    }
    
    // If an error occurred, turn blue + green LEDs on (second highest priority)
    // Severe errors displayed by LED are: Bus Off, Rx failed, Tx failed, Buffer Overflow.
    // Bus Passive is NOT a severe error to be displayed by both LED's turned on.
    if (error_get_state(channel)->bus_status == BUS_StatusOff || 
        error_get_state(channel)->app_flags)
    {
        SetRxLed(channel, true);
        SetTxLed(channel, true);
        inst->error_was_indicating = 1;
        return;
    }
    
    // error state has finished --> return to LEDs off
    if (inst->error_was_indicating)
    {
        SetRxLed(channel, false);
        SetTxLed(channel, false);
        inst->error_was_indicating = 0;
    }

    // If LED has been flashing for long enough, turn it off
    if (inst->RX_laston > 0 && tick_now - inst->RX_laston > FLASH_ON_DURATION)
    {
        SetRxLed(channel, false);
        inst->RX_laston  = 0;
        inst->RX_lastoff = tick_now;
    }

    // If LED has been flashing for long enough, turn it off
    if (inst->TX_laston > 0 && tick_now - inst->TX_laston > FLASH_ON_DURATION)
    {
        // Invert LED
        SetTxLed(channel, false);
        inst->TX_laston  = 0;
        inst->TX_lastoff = tick_now;
    }
    
    // Green LED on while bus is closed
    if (!can_is_open(channel))
        led_turn_TX(channel, true); // green on   
}

// blue
void SetRxLed(int channel, bool status)
{
    HAL_GPIO_WritePin(SET_LedRxPorts[channel], SET_LedRxPins[channel], status ? LED_ON : LED_OFF);
}
// green
void SetTxLed(int channel, bool status)
{
    HAL_GPIO_WritePin(SET_LedTxPorts[channel], SET_LedTxPins[channel], status ? LED_ON : LED_OFF);
}

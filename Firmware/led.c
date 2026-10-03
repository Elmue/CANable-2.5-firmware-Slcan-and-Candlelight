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
// turn the LEDs 4 times on and 4 times off
#define POWER_ON_COUNT         8

// ---- Settings  (from settings.h)
GPIO_TypeDef* SET_LedTxPorts[CHANNEL_COUNT] = { LED_TX_PORTS };
int           SET_LedTxPins [CHANNEL_COUNT] = { LED_TX_PINS  };
GPIO_TypeDef* SET_LedRxPorts[CHANNEL_COUNT] = { LED_RX_PORTS };
int           SET_LedRxPins [CHANNEL_COUNT] = { LED_RX_PINS  };
bool          SET_CommonPins[CHANNEL_COUNT] = {0};

// ----- Class Instance
led_class  led_inst[CHANNEL_COUNT] = {0};

// ----- Private Methods
void led_set_Rx(uint8_t channel, bool status);
void led_set_Tx(uint8_t channel, bool status);
void led_set_both(uint8_t channel, bool status);
void led_set_alternate(uint8_t channel, bool status);

// Initialize LED GPIOs
bool led_init()
{
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
            
            // Set SET_CommonPins = true if a board has only one LED for Rx and Tx
            SET_CommonPins[C] = SET_LedRxPins [C] == SET_LedTxPins [C] && 
                                SET_LedRxPorts[C] == SET_LedTxPorts[C];
        }
        // In case of a crash during the following initialization --> all LEDs are ON which shows a severe error.
        led_set_both(C, true);
    }
    
#ifdef LED_PWR_PIN   
    // WeActStudio v1 adapter:
    // Turn ON the red Power LED. It is OFF in DFU mode.
    // -------------------------------------------------
    // BigTreeTech U2C v2 adapter:
    // Turn OFF the blue Status LED. 
    // It is ON in DFU mode, because by default PA13 is internally pulled up by the STM32G0B1 processor.
    GPIO_InitStruct.Pin = LED_PWR_PIN;
    HAL_GPIO_Init(LED_PWR_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(LED_PWR_PORT, LED_PWR_PIN, LED_ON);
#endif
    return true;
}

// -------------------------------------------------------------------

// when the operating system goes into sleep mode --> USB suspended --> turn off all LED's
void led_power_down()
{
    for (int C=0; C<CHANNEL_COUNT; C++)
    {
        led_set_both(C, false);
    }
}

// Blink Rx + Tx LEDs of all channels alternatingly on power on.
// This is a blocking function by purpose.
void led_blink_power_on()
{   
    uint8_t i;
    for (i = 0; i < POWER_ON_COUNT; i++)
    {
        for (int C=0; C<CHANNEL_COUNT; C++)
        {
            led_set_alternate(C, true);
        }       
        HAL_Delay(POWER_ON_DURATION);        
        
        for (int C=0; C<CHANNEL_COUNT; C++)
        {
            led_set_alternate(C, false);
        }
        HAL_Delay(POWER_ON_DURATION);
    }
}

// Blink Rx + Tx of one channel alternatingly to identify a channel and device if multiple devices are connected at the same time.
// This is a non-blocking function. Blinking is enabled by USB command.
// When a CAN channel is opened the blinking will be stopped.
void led_blink_identify(uint8_t channel, bool blink_on)
{
    led_class* inst = &led_inst[channel];

    if (inst->identify == blink_on)
        return;

    // blinking is done in led_process()
    inst->identify   = blink_on;
    inst->next_blink = HAL_GetTick();

    if (!blink_on)
        led_set_both(channel, false);
}

// -------------------------------------------------------------------

// Turn Tx LED on for a short duration
// Called when CAN frame has been sent
void led_flash_TX(uint8_t channel)
{
    // If the board has only one common LED for Rx and Tx --> use only the pair of RX_laston and RX_lastoff variables
    // otherwise the LEDs will be permanently ON at high CAN traffic.
    if (SET_CommonPins[channel])
    {
        led_flash_RX(channel);
        return;
    }
    
    led_class* inst = &led_inst[channel];
    if (inst->identify)
        return;
    
    // Make sure the LED has been off for at least FLASH_OFF_DURATION before turning on again
    // This prevents a solid status LED on a busy CAN bus
    if (inst->TX_laston == 0 && HAL_GetTick() - inst->TX_lastoff > FLASH_OFF_DURATION)
    {
        led_set_Tx(channel, true);
        inst->TX_laston = HAL_GetTick();
    }
}

// Turn Rx LED on for a short duration
// Called when CAN frame has been received
void led_flash_RX(uint8_t channel)
{
    led_class* inst = &led_inst[channel];
    if (inst->identify)
        return;
    
    // Make sure the LED has been off for at least FLASH_OFF_DURATION before turning on again
    // This prevents a solid status LED on a busy CAN bus
    if (inst->RX_laston == 0 && HAL_GetTick() - inst->RX_lastoff > FLASH_OFF_DURATION)
    {
        led_set_Rx(channel, true);
        inst->RX_laston = HAL_GetTick();
    }
}

// -------------------------------------------------------------------

// called approx 100 times per millisecond from main.c
void led_process(uint8_t channel, uint32_t tick_now)
{
    led_class* inst = &led_inst[channel];
    
    // --- 1.) user blink (highest priority)
    
    if (inst->identify)
    {
        if (tick_now >= inst->next_blink)
        {
            inst->blink_count ++;
            inst->next_blink += IDENTIFY_DURATION;
            
            // Blink pattern: Both OFF, Rx ON, Both OFF, Tx ON, ...
            led_set_Tx(channel, (inst->blink_count & 3) == 1);       
            led_set_Rx(channel, (inst->blink_count & 3) == 3);
        }
        return;
    }
    
    // --- 2.) severe CAN errors (second highest priority)
    
    // If an error occurred, turn Rx + Tx LEDs on.
    // Severe errors displayed by LED are: Bus Off, Rx failed, Tx failed, Buffer Overflow.
    // Bus Passive is NOT a severe error to be displayed by both LED's turned on.
    if (error_get_state(channel)->bus_status == BUS_StatusOff || 
        error_get_state(channel)->app_flags)
    {
        led_set_both(channel, true);
        inst->error_was_indicating = 1;
        return;
    }
    
    // error state has finished --> return to LEDs off
    if (inst->error_was_indicating)
    {
        led_set_both(channel, false);
        inst->error_was_indicating = 0;
    }
    
    // --- 3.) CAN Rx/Tx packets

    // If LED has been flashing for long enough, turn it off
    if (inst->RX_laston > 0 && tick_now - inst->RX_laston > FLASH_ON_DURATION)
    {
        led_set_Rx(channel, false);
        inst->RX_laston  = 0;
        inst->RX_lastoff = tick_now;
    }

    // If LED has been flashing for long enough, turn it off
    if (inst->TX_laston > 0 && tick_now - inst->TX_laston > FLASH_ON_DURATION)
    {
        led_set_Tx(channel, false);
        inst->TX_laston  = 0;
        inst->TX_lastoff = tick_now;
    }
    
    // --- 4.) CAN open/closed status
    
    if (can_is_open(channel))
    {
        // Turn Tx LED off when the CAN channel has been opened
        if (!inst->can_open)
            led_set_Tx(channel, false);

        inst->can_open = true;
    }
    else // CAN channel is closed
    {
        // Turn Tx LED permanently on while the bus is closed
        led_set_Tx(channel, true);
        inst->can_open = false;
    }
}

// -------------------------------------------------------------------

// Turn one LED on and the other off
void led_set_alternate(uint8_t channel, bool status)
{
    if (SET_CommonPins[channel]) // a common LED for Rx and Tx
    {
        led_set_Rx(channel, status);
    }
    else // separate Rx/Tx LED's
    {
        led_set_Rx(channel, status == true);
        led_set_Tx(channel, status == false);
    }
}

// Set both LEDs
void led_set_both(uint8_t channel, bool status)
{
    led_set_Rx(channel, status);
    led_set_Tx(channel, status);
}

// Set Rx LED
void led_set_Rx(uint8_t channel, bool status)
{
    if (SET_LedRxPins[channel] >= 0)
        HAL_GPIO_WritePin(SET_LedRxPorts[channel], SET_LedRxPins[channel], status ? LED_ON : LED_OFF);
}

// Set Tx LED
void led_set_Tx(uint8_t channel, bool status)
{
    if (SET_LedTxPins[channel] >= 0)
        HAL_GPIO_WritePin(SET_LedTxPorts[channel], SET_LedTxPins[channel], status ? LED_ON : LED_OFF);
}


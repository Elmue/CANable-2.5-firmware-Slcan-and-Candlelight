/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "can.h"
#include "led.h"
#include "dfu.h"
#include "control.h"
#include "system.h"
#include "utils.h"
#include "buffer.h"
#include "usb_def.h"
#include "usb_lowlevel.h"
#include "usb_core.h"

uint32_t tick_last   = 0;
bool     usb_suspend = false;
bool     blink_leds  = true;

int main(void)
{
    if (!system_init() || // init HAL and configure system clock
        !led_init()    || // turn ON Rx + Tx LED
        !USBD_Init())
    {
        // if System or USB initialization fails --> Rx + Tx LED are permanently ON
        while (true) {}
    }
  
    buf_init();
    can_init();
    utils_init();
    control_init(); // AFTER utils_init()
      
    // This loop runs eternally approx 100 times in one millisecond (on STM32G431)
    uint8_t channel = 0;    
    while (true)
    {       
        if (HAL_PCD_Is_Suspended()) // computer is in sleep mode (USB off)
        {
            led_sleep(); // only the power LED is on
            usb_suspend = true;
            continue;
        }
        else if (usb_suspend)
        {
            usb_suspend = false;            
            blink_leds  = true;
        }

        // blink LED's after power-on and after wake-up from sleep mode
        if (blink_leds)
        {           
            blink_leds = false;
            led_blink_power_on(); // blink Rx + Tx LED's 8 times (blocking function)            
        }
        
        uint32_t tick_now = HAL_GetTick();        
        led_process    (channel, tick_now);
        buf_process    (channel, tick_now);
        control_process(channel, tick_now); // calls error_is_report_due() --> First report the error "Bus Off"
        can_process    (channel, tick_now); // AFTER control!              --> After recover from Bus Off
        
        if (tick_now - tick_last >= 100)
        {
            tick_last = tick_now;            
            can_timer_100ms();
            dfu_timer_100ms(tick_now);
        }
        
        // iterate through all channels if multi-channel board
        channel = (channel + 1) % CHANNEL_COUNT;
    }
}


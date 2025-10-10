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

uint32_t tick_last = 0;

int main(void)
{
    led_init(); // turns on blue + green LED
    
    if (!system_init() || !USBD_Init())
    {
        // if System or USB initialization fails --> blue + green LED are on permanently
        while (true) {}
    }

    buf_init();
    can_init();
    utils_init();
    control_init(); // AFTER utils_init()
    
    led_blink_power_on(); // blink blue / green 8 times (blocking function)

    // This loop runs approx 100 times in one millisecond
    while (true)
    {
        if (HAL_PCD_Is_Suspended()) // computer is in sleep mode (USB off)
        {
            led_sleep();
            continue;
        }
        
        uint32_t tick_now = HAL_GetTick();        
        led_process(tick_now);
        buf_process(tick_now);
        control_process(tick_now); // calls error_is_report_due() --> First report the error "Bus Off"
        can_process(tick_now);     // AFTER control!              --> After recover from Bus Off
        
        if (tick_now - tick_last >= 100)
        {
            tick_last = tick_now;            
            can_timer_100ms();
            dfu_timer_100ms(tick_now);
        }
    }
}


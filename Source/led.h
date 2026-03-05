/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once
#include "settings.h"

typedef struct
{
    volatile uint32_t RX_laston;
    volatile uint32_t TX_laston;
    uint32_t          RX_lastoff;
    uint32_t          TX_lastoff;
    uint8_t           error_was_indicating;
    uint32_t          next_blink;
    uint32_t          blink_count;
    bool              identify;
} led_class;

void led_init();
void led_sleep();
void led_blink_power_on();
void led_process(int channel, uint32_t tick_now);
void led_turn_TX(int channel, bool state);
void led_blink_identify(int channel, bool blink_on);
void led_flash_TX(int channel);
void led_flash_RX(int channel);



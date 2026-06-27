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

bool led_init();
void led_sleep();
void led_blink_power_on();
void led_process(uint8_t channel, uint32_t tick_now);
void led_turn_TX(uint8_t channel, bool state);
void led_blink_identify(uint8_t channel, bool blink_on);
void led_flash_TX(uint8_t channel);
void led_flash_RX(uint8_t channel);
void led_set_Pwr(bool status);



/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once
#include "settings.h"

void led_init();
void led_turn_TX(uint8_t state);
void led_blink_power_on();
void led_blink_identify(bool blink_on);
void led_flash_TX();
void led_flash_RX();
void led_process(uint32_t tick_now);
void led_sleep();



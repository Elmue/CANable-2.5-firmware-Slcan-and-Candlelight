/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

void control_init();
void control_parse_command (char *buf, int len);
void control_process(uint32_t tick_now);
void control_report_busload(uint8_t busload_percent);
bool control_send_debug_mesg(const char* message);




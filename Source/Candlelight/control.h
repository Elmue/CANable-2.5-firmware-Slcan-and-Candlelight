/*
    The MIT License
    Implemenatation of USB GS Class (Geschwister Schneider)
    Copyright (c) 2025 ElmueSoft / Hubert Denkmair
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "buffer.h"

void control_init();
void control_process(int channel, uint32_t tick_now);
void control_report_busload(int channel, uint8_t busload_percent);
bool control_send_debug_mesg(int channel, const char* message);
bool control_setup_request (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
void control_setup_OUT_data(USBD_HandleTypeDef *pdev);


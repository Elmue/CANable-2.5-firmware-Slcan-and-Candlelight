/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#pragma once

#include "settings.h"

typedef enum 
{
    OPT_BOR_Level4    = 1, // Set BoR Level4 (2.8 Volt)
    OPT_BOOT0_Enable  = 2, // Pin BOOT0 defines what is booted (flash or bootloader)
    OPT_BOOT0_Disable = 3, // Always boot into flash memory
} eOptionBytes;

typedef enum 
{
    SERIE_Unknown,
    SERIE_G0,      // STM32G0XX
    SERIE_G4,      // STM32G4XX
} eMcuSerie;

bool      system_init();
bool      system_is_option_enabled(eOptionBytes e_Option);
eFeedback system_set_option_bytes (eOptionBytes e_Option);
uint32_t  system_get_can_clock();
eMcuSerie system_get_mcu_serie();

// get timestamp with 1 µs precision
static inline uint32_t system_get_timestamp()
{
    return TIM2->CNT;
}

// ARM's
// "Application Note 321 ARM Cortex-M Programming Guide to Memory Barrier Instructions"
// (from https://developer.arm.com/documentation/dai0321/latest) says that
// the ISBs are actually necessary on Cortex-M0 to avoid a 2-instruction
// delay in the effect of enabling and disabling interrupts.
// That probably doesn't matter here, but it's hard to say what the compiler
// will put in those 2 instructions so it's safer to leave it. The DSB isn't
// necessary on Cortex-M0, but it's architecturally required so we'll
// include it to be safe.
//
// The "memory" and "cc" clobbers tell GCC to avoid moving memory loads or
// stores across the instructions. This is important when an interrupt and the
// code calling disable_irq/enable_irq share memory. The fact that these are
// non-inlined functions probably forces GCC to flush everything to memory
// anyways, but trying to outsmart the compiler is a bad strategy (you never
// know when somebody will turn on LTO or something).

static inline void system_disable_irq()
{
	__disable_irq();
    __DSB(); // Data Synchronization Barrier
	__ISB(); // Instruction Synchronization Barrier
}

static inline void system_enable_irq()
{
	__enable_irq();
	__ISB(); // Instruction Synchronization Barrier
}


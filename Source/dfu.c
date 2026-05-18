/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "dfu.h"
#include "system.h"
#include "can.h"
#include "control.h"

// This address is the start of the system memory, which contains the DFU bootloader.
#define SYSMEM_MEMORY_STM32G4xx   0x1FFF0000 // Reference Manual RM0440, page 83
#define SYSMEM_MEMORY_STM32G0xx   0x1FFF0000 // Reference Manual RM0444, page 61

static uint32_t dfu_sys_memory_base = 0;
static uint32_t dfu_delay_start     = 0;
static bool     dfu_require_reset   = false;

// Run the bootloader after a delay of 300 ms to assure that a reponse has been sent over USB to the host.
// A positive response is sent only if the command "*DFU\r" and processor family are supported.
eFeedback dfu_switch_to_bootloader()
{
    can_close_all();

    // If the pin BOOT0 is disabled, and then enabled in system_set_option_bytes() below, and then
    // the bootloader entry point is called, it will always boot again into flash until the USB cable is reconnected.
    // In this case a hardware reset is required for the modified Option Bytes to become active.
    eOptionStatus status = system_is_option_enabled(OPT_BOOT0_Disable);
    if (status == Option_Active)
        dfu_require_reset = true; // BOOT0 is disabled -> this variable stays true until the USB cable is reconnected.

    // ====================================================================================================
    // ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION
    // The processor will not enter boot mode if register OPTR, bit nSWBOOT0 is zero, not even with the Boot jumper set.
    // Therefore the pin BOOT0 must be enabled here to allow entering boot mode.
    // If you remove the following line and the pin BOOT0 is disabled, you will never ever be able to update the firmware again!
    // You will have a CANable with a frozen firmware!
    eFeedback e_Ret = system_set_option_bytes(OPT_BOOT0_Enable);
    if (e_Ret != FBK_Success &&
        e_Ret != FBK_UnsupportedFeature)
        return e_Ret;

    if (dfu_require_reset)
        return FBK_ResetRequired;

    dfu_delay_start = HAL_GetTick();

    switch (system_get_mcu_serie())
    {
        // The processor is a STM32G0xx.
        case SERIE_G0:
            dfu_sys_memory_base = SYSMEM_MEMORY_STM32G0xx;
            return FBK_Success;

        // The processor is a STM32G4xx
        case SERIE_G4:
            dfu_sys_memory_base = SYSMEM_MEMORY_STM32G4xx;
            return FBK_Success;

        // Processor serie not implemented.
        default:
            return FBK_UnsupportedFeature;
    }
}

// called every 100 ms from main()
// ATTENTION: This function will not work if pin BOOT0 is disabled.
void dfu_timer_100ms(uint32_t tick_now)
{
    if (dfu_sys_memory_base == 0)
        return;

    if (tick_now - dfu_delay_start < 300)
        return;

    // Disable the SysTick timer
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    // Disable all global interrupts
    __disable_irq();
    
#if defined(STM32G4xx)    
    const int reg_count = 4; // clear 4 x 32 = 128 interrupt lines
#elif defined(STM32G0xx)
    const int reg_count = 1; // clear 1 x 32 =  32 interrupt lines
#else
    #error "MCU_SERIE not implemented"
#endif

    // Clear any pending interrupt flags in NVIC
    for (uint8_t i=0; i<reg_count; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }

    // De-initialize peripheral clocks 
    HAL_DeInit();
    HAL_RCC_DeInit();

#if defined(STM32G4xx)
    // Set vector table
    SCB->VTOR = dfu_sys_memory_base;
#elif defined(STM32G0xx)
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    // Remap System Memory to address 0x00000000
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
#else
    #error "MCU_SERIE not implemented"
#endif

    // The vector table is an array of 32-bit pointers (defined by ARM).
    uint32_t stack_pointer = *(__IO uint32_t*)(dfu_sys_memory_base + 0);
    uint32_t reset_vector  = *(__IO uint32_t*)(dfu_sys_memory_base + 4);
    dfu_sys_memory_base = 0; // Avoid endless loop in case of failure
    
    typedef void (*tBootloader)();
    tBootloader fBootloader = (tBootloader)reset_vector;

    // Set stack pointer
    __set_MSP(stack_pointer);

    // Flush instruction pipeline and execute the jump
    __ISB();       
    fBootloader(); 
}


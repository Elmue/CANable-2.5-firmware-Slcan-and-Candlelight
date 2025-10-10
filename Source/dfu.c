/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "dfu.h"
#include "system.h"
#include "can.h"
#include "control.h"

// This address is the start of the system memory, which contains the bootloader.
#define SYSMEM_STM32F042   0x1FFFC400
#define SYSMEM_STM32F072   0x1FFFC800
#define SYSMEM_STM32G0xx   0x1FFF0000
#define SYSMEM_STM32G4xx   0x1FFF0000

static uint32_t dfu_sys_memory_base = 0;
static uint32_t dfu_delay_start     = 0;
static bool     dfu_require_reset   = false;

// Run the bootloader after a delay of 300 ms to assure that a reponse has been sent over USB to the host.
// A positive response is sent only if the command "*DFU\r" and processor family are supported.
eFeedback dfu_switch_to_bootloader()
{
    can_close();
    
    // If the pin BOOT0 is disabled, and then enabled in system_set_option_bytes() below, and then
    // the bootloader entry point is called, it will always boot again into flash until the USB cable is reconnected.
    // In this case a hardware reset is required for the modified Option Bytes to become active.
    if (system_is_option_enabled(OPT_BOOT0_Disable))
        dfu_require_reset = true;   
    
    // ====================================================================================================
    // ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION ## ATTENTION
    // The processor will not enter boot mode if register OPTR, bit nSWBOOT0 is zero, not even with the Boot jumper set.
    // Therefore the pin BOOT0 must be enabled here to allow entering boot mode.    
    // If you remove the following line and the pin BOOT0 is disabled, you will never ever be able to update the firmware again!
    // You will have a CANable with a frozen firmware!
    eFeedback e_Ret = system_set_option_bytes(OPT_BOOT0_Enable);
    if (e_Ret != FBK_Success)
        return e_Ret;
    
    if (dfu_require_reset)
        return FBK_ResetRequired;
    
    dfu_delay_start = HAL_GetTick();

    switch (system_get_mcu_serie())
    {
        case SERIE_G4: // The processor is a STM32G4XX
            dfu_sys_memory_base = SYSMEM_STM32G4xx;
            return FBK_Success;
            
        // Processor serie not implemented.
        default: 
            return FBK_UnsupportedFeature;
    }
}

// called every 100 ms from main()
// ATTENTION:
// This function will not work if register OPTR, bit nSWBOOT0 is zero.
void dfu_timer_100ms(uint32_t tick_now)
{
    if (dfu_sys_memory_base < 0x10000000)
        return;
    
    if (tick_now - dfu_delay_start < 300)
        return;
    
    __disable_irq();
    
    SCB->VTOR = dfu_sys_memory_base;                 // set vector table
    __set_MSP(*(__IO uint32_t*)dfu_sys_memory_base); // set stack pointer    
          
    typedef void (*tBootloader)();
    tBootloader fBootloader = (tBootloader)(dfu_sys_memory_base + 4);
    
    dfu_sys_memory_base = 0; // avoid endless loop
    
    fBootloader(); // jump to bootloader entry point
}

/*
static void dfu_hack_boot_pin_f042(void)
{
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_11, 1);
}
*/

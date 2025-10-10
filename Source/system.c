/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "system.h"
#include "can.h"
#include "control.h"

uint32_t canfd_clock;

// Initialize system clocks
bool system_init(void)
{
    if (HAL_Init() != HAL_OK)
      return false;

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    // Configure the main internal regulator output voltage
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    // Initializes the CPU, AHB and APB bus clocks
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.HSI48State          = RCC_HSI48_ON;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN            = 80;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
      return false;

    // Initializes the CPU, AHB and APB bus clocks
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_8) != HAL_OK)
      return false;

    // Initializes the peripherals clocks
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PCLK1;
    PeriphClkInit.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
      return false;

    // Configures CRS
    RCC_CRSInitTypeDef pInit = {0};
    pInit.Prescaler   = RCC_CRS_SYNC_DIV1;
    pInit.Source      = RCC_CRS_SYNC_SOURCE_USB;
    pInit.Polarity    = RCC_CRS_SYNC_POLARITY_RISING;
    pInit.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
    pInit.ErrorLimitValue = 34;
    pInit.HSI48CalibrationValue = 32;

    HAL_RCCEx_CRSConfig(&pInit);

    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE(); // just nrst is on port G
    
    canfd_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN); // 160 MHz

    system_set_option_bytes(OPT_BOR_Level4);
    return true;
}

// While TARGET_MCU (from the make file) defines for which MCU serie the code was COMPILED,
// this function returns on which MCU the code is actually RUNNING.
// The user may have uploaded the firmware to the wrong processor.
// This function can be used to prove the we run on the expected processor.
eMcuSerie system_get_mcu_serie()
{
    // HAL_GetDEVID() reads a 12 bit identifier (DBG_IDCODE) that is unique for each processor family.
    switch (HAL_GetDEVID())
    {
        case 0x460: // STM32G071 + G081
        case 0x465: // STM32G051 + G061
        case 0x466: // STM32G031 + G041
        case 0x467: // STM32G0B1 + G0C1
            return SERIE_G0;
            
        case 0x468: // STM32G431 + G441
        case 0x469: // STM32G471 + G473 + G474 + G483 + G484
        case 0x479: // STM32G491 + G4A1
            return SERIE_G4;
            
        default: // processor serie not implemented
            return SERIE_Unknown;
    }
}

uint32_t system_get_can_clock()
{
    return canfd_clock;
}

// b10us = false --> generate timestamps with  1 µs precision (roll over already after 1 hour! Used by legacy protocol)
// b10us = true  --> generate timestamps with 10 µs precision (roll over after 10 hours, used by new protocol)
void system_init_timestamp(bool b10us)
{
    uint32_t divider = 1000000; // 1 MHz
    if (b10us)
        divider /= 10;
    
    // configure timer 2 for 1 µs / 10 µs interval
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1   = 0;
    TIM2->CR2   = 0;
    TIM2->SMCR  = 0;
    TIM2->DIER  = 0;
    TIM2->CCMR1 = 0;
    TIM2->CCMR2 = 0;
    TIM2->CCER  = 0;
    TIM2->PSC   = (SystemCoreClock / divider) - 1;
    TIM2->ARR   = 0xFFFFFFFF;
    TIM2->CR1  |= TIM_CR1_CEN;
    TIM2->EGR   = TIM_EGR_UG;
}

// returns true if the requested option is set in the Option Bytes
bool system_is_option_enabled(eOptionBytes e_Option)
{
    // Get option bytes
    FLASH_OBProgramInitTypeDef cur_values = {0};
    HAL_FLASHEx_OBGetConfig(&cur_values);
    
    switch (e_Option)
    {
        case OPT_BOR_Level4:    return (cur_values.USERConfig & FLASH_OPTR_BOR_LEV_Msk)  == OB_BOR_LEVEL_4;
        case OPT_BOOT0_Enable:  return (cur_values.USERConfig & FLASH_OPTR_nSWBOOT0_Msk) == OB_BOOT0_FROM_PIN;
        case OPT_BOOT0_Disable: return (cur_values.USERConfig & FLASH_OPTR_nSWBOOT0_Msk) == OB_BOOT0_FROM_OB; 
    }
    return false;
}

// Set BoR (Brown-Out Reset) level to 4 (2.8 Volt = highet value)
// This means that a reset is generated when power voltage falls below 2.8V.
// This eliminates an issue where poor quality USB hubs that provide low voltage before switching the 5 Volt supply on
// which was causing PoR issues where the microcontroller would enter boot mode incorrectly.
// ----------------
// This function can also define if the pin BOOT0 is ignored.
// This pin is STUPIDLY the same as the CAN RX pin which really sucks.
// By only restarting the computer the CANable goes into Bootloader mode.
// Thefore this firmware gives the user the possibility to ignore pin BOOT0.
// Read the detailed description here: https://netcult.ch/elmue/CANable Firmware Update
// ====================================================================================================
// IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT
// If you modify this code and introduce a bug you may end up in a frozen firmware that cannot be updated anymore!
eFeedback system_set_option_bytes(eOptionBytes e_Option)
{   
    // IMPORTANT:
    // The user may have uploaded the firmware to the wrong processor.
    // The serie STM32G0XX has different bits in the OPTR register than the serie STM32G4XX.
    // It is VERY important not to execute the following code on the wrong processor!
    // Screwing up the Option Bytes may have fatal consquences that can make the board unusable.
    if (system_get_mcu_serie() != SERIE_G4)
        return FBK_UnsupportedFeature;

    if (can_is_opened())
        return FBK_AdapterMustBeClosed;

    if (system_is_option_enabled(e_Option))
        return FBK_Success; // nothing to do
      
    // The following bits apply only to the STM32G4XX serie:
    // OPTR bit 26 nSWBOOT0 == 1 --> pin BOOT0 is enabled
    // OPTR bit 26 nSWBOOT0 == 0 --> pin BOOT0 is disabled, bit nBOOT0 defines boot mode
    // OPTR bit 27 nBOOT0   == 1 --> boot into main flash memory
    // OPTR bit 27 nBOOT0   == 0 --> nBOOT1 defines boot mode
    // OPTR bit 23 nBOOT1   == 1 --> boot into bootloader (System)
    // OPTR bit 23 nBOOT1   == 0 --> boot into SRAM1
    // By default the register OPTR has the value 0xFFEFFCXX
    // After disabling the pin BOOT0 it will have 0xFBEFFCXX
    FLASH_OBProgramInitTypeDef prog_values = {0};    
    switch (e_Option)
    {
        case OPT_BOR_Level4: // set level = 2.8 Volt
            prog_values.OptionType = OPTIONBYTE_USER;
            prog_values.USERType   = OB_USER_BOR_LEV;
            prog_values.USERConfig = OB_BOR_LEVEL_4;
            break;
        case OPT_BOOT0_Enable: // pin BOOT0 defines boot mode (bootloader of flash memory)
            prog_values.OptionType = OPTIONBYTE_USER;
            prog_values.USERType   = OB_USER_nSWBOOT0  | OB_USER_nBOOT0 | OB_USER_nBOOT1;  // 0x00006200
            prog_values.USERConfig = OB_BOOT0_FROM_PIN | OB_nBOOT0_SET  | OB_BOOT1_SYSTEM; // 0x0C800000
            break;
        case OPT_BOOT0_Disable: // Option Byte bits nBOOT0 and nBOOT1 define boot mode
            prog_values.OptionType = OPTIONBYTE_USER;
            prog_values.USERType   = OB_USER_nSWBOOT0  | OB_USER_nBOOT0 | OB_USER_nBOOT1;  // 0x00006200
            prog_values.USERConfig = OB_BOOT0_FROM_OB  | OB_nBOOT0_SET  | OB_BOOT1_SYSTEM; // 0x08800000
            break;
        default:
            return FBK_InvalidParameter;
    }
    
    // The following flash programming procedure takes approx 25 ms.

    // IMPORTANT:
    // If previous errors are not cleared, HAL_FLASHEx_OBProgram() will fail.
    // This was wrong in all legacy firmware versions. (fixed by ElmüSoft)
    // The programmers did not even notice this bug because of a non-existent error handling (sloppy code).
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);        
    
    // All the following functions return either HAL_OK or HAL_ERROR    
    
    if (HAL_FLASH_Unlock()    != HAL_OK || // Unlock flash
        HAL_FLASH_OB_Unlock() != HAL_OK)   // Unlock option bytes
        return FBK_OptBytesProgrFailed;     
               
    bool b_OK1 = HAL_FLASHEx_OBProgram(&prog_values) == HAL_OK; // Program option bytes
    
    // always lock, even if programming should have failed
    bool b_OK2 = HAL_FLASH_OB_Lock() == HAL_OK; // Lock option bytes
    bool b_OK3 = HAL_FLASH_Lock()    == HAL_OK; // Lock flash
    if (!b_OK1 || !b_OK2 || !b_OK3)
        return FBK_OptBytesProgrFailed;
    
    // NOTE:
    // The function HAL_FLASH_OB_Launch() does not work here to activate the new Option Bytes. 
    // Even if the pin BOOT0 has been enabled, the pin will have no effect until a hardware reset is executed.
    // Therefore dfu_switch_to_bootloader() handles this special case.
    return FBK_Success;
}


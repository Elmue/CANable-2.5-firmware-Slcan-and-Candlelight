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
uint32_t timestamp_wrap = 0;

void system_init_timestamp();

/*

See "STM32G4 Series - Clock Generation.png" in subfolder "Documentation"

Oscillators:
 +- HSI16 (16 MHz internal RC)
 +- HSE (4–48 MHz external quartz/resonator)
 +- MSI (100 kHz – 48 MHz multi-speed internal)
 +- HSI48 (48 MHz internal, for USB/RNG/CRS)
 +- LSE (32.768 kHz external quartz, RTC)
 +- LSI (~32 kHz internal RC, watchdog/RTC)

PLL Block:
 +- Input: HSI16 / HSE / MSI
 +- PLLM (÷1..16)  --> divides input
 +- PLLN (×8..127) --> multiplies to VCO (64–344 MHz)
 +- Outputs:
     +- PLLR (÷2,4,6,8) --> SYSCLK
     +- PLLQ (÷2,4,6,8) --> USB, SAI, RNG
     +- PLLP (÷2,4,6,8,10,12,14,16, etc.) --> ADC, other peripherals

System Clock (SYSCLK):
 +- Source mux: HSI16 / HSE / MSI / PLLR
 +- Max 170 MHz

AHB Prescaler:
 +- HCLK = SYSCLK ÷ AHB prescaler

APB Prescalers:
 +- PCLK1 = HCLK ÷ APB1 prescaler (max 80 MHz)
 +- PCLK2 = HCLK ÷ APB2 prescaler (max 80 MHz)

Peripheral Clock Domains:
 +- USB:
 |   +- Source mux: HSI48 OR PLLQ (must be exactly 48 MHz)
 |   +- Needs CRS calibration if HSI48 used
 +- FDCAN:
 |   +- Source mux: HSE OR PLLQ OR PCLK1
 +- ADC:
 |   +- Source mux: PLLP OR SYSCLK OR HCLK
 +- UART/I2C/SPI/Timers:
 |   +- Driven from PCLK1 or PCLK2 depending on bus
 +- RTC:
 |   +- Source mux: LSE OR LSI OR HSE/128
 +- Watchdog:
     +- LSI (always)

*/

bool system_init(void)
{
    if (HAL_Init() != HAL_OK)
      return false;

    // Configure the main internal regulator output voltage
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

    // ------------------------------------------

    RCC_OscInitTypeDef RCC_OscInitStruct  = {0};
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI48; // 48 MHz RC oscillator is always needed as USB clock
    RCC_OscInitStruct.HSI48State          = RCC_HSI48_ON;             // enable HSI (High Speed Internal) 48 MHz RC oscillator

    // HSE_VALUE = QUARTZ_FREQU is assigned in the makefiles
#if HSE_VALUE == 0 // No quartz crystal present on the board

    RCC_OscInitStruct.OscillatorType     |= RCC_OSCILLATORTYPE_HSI; // 16 MHz RC oscillator used as input for PLL if no quartz present
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;             // enable HSI (High Speed Internal) 16 MHz RC oscillator
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;      // use internal oscillator to feed the PLL
    RCC_OscInitStruct.PLL.PLLM            = RCC_PLLM_DIV1;          // divide 16 MHz input clock / 1 --> 16 MHz
    RCC_OscInitStruct.PLL.PLLN            = 20;                     // multiply 16 MHz x 20 --> VCO frequency = 320 MHz (maximum 344 MHz)

#else // quartz crystal is present

    RCC_OscInitStruct.OscillatorType     |= RCC_OSCILLATORTYPE_HSE; // quartz oscillator used as input for PLL
    RCC_OscInitStruct.HSEState            = RCC_HSE_ON;             // enable HSE (High Speed External) quartz oscillator
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;      // use external oscillator to feed the PLL
    
    #if HSE_VALUE == 25000000 // 25 MHz quartz 
        RCC_OscInitStruct.PLL.PLLM        = RCC_PLLM_DIV5;          // divide 25 MHz input clock / 5 --> 5 MHz
        RCC_OscInitStruct.PLL.PLLN        = 64;                     // multiply 5 MHz x 64 --> VCO frequency = 320 MHz (maximum 344 MHz)
    #else
        #error "Quartz frequency not implemented in system.c"
    #endif
#endif    

    // ------------------------------------------

    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2; // PLL output P = VCO / 2 = 160 MHz (for ADC)
    RCC_OscInitStruct.PLL.PLLQ       = RCC_PLLQ_DIV2; // PLL output Q = VCO / 2 = 160 MHz (for USB / FDCAN)
    RCC_OscInitStruct.PLL.PLLR       = RCC_PLLR_DIV2; // PLL output R = VCO / 2 = 160 MHz (for SYSCLK)
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
        return false;

    // Bugfix: The legacy firmware used a PCLK1 and PCLK2 of 160 MHz which is outside the guaranteed operating conditions.
    // The maximum is 80 MHz otherwise these buses are heavily overclocked.
    // HCLK  == SystemCoreClock is used for Cortex-M4, Memory, DMA, Flash, SRAM, SysTick timer, High-speed peripherals.
    // PCLK1 is used for Lower -speed peripherals: I2C, USART2/3, LPUART, SPI2/3, CAN/FDCAN, DAC, TIM2–TIM7.
    // PCLK2 is used for Higher-speed peripherals: USART1, SPI1, TIM1, TIM8, ADCs.
    // See "STM32G4 Series - Clock Generation.png" in subfolder "Documentation"
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; // set SYSCLK = PLL R   = 160 MHz
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         // set HCLK   = SYSCLK  = 160 MHz (AHB  = Advanced High-performance Bus)
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;           // set PCLK1  = HCLK /2 =  80 MHz (APB1 = Advanced Peripheral Bus 1)
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;           // set PCLK2  = HCLK /2 =  80 MHz (APB2 = Advanced Peripheral Bus 2)
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_8) != HAL_OK)
        return false;

    // Initializes the peripherals clocks
    RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit = {0};
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_FDCAN;
    // FDCAN uses PLL output Q (160 MHz)
    RCC_PeriphClkInit.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PLL;
    // Use internal 48 MHz oscillator because 48 MHz for USB cannot be derived from 320 MHz PLL clock with dividers 1,2,4,8
    RCC_PeriphClkInit.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK)
        return false;

    // Clock Recovery System (CRS) calibrates the internal HSI48 oscillator so it stays accurate enough for USB.
    // The USB SOF (Start-Of-Frame) packets, which arrive every 1 ms, are used for synchronization.
    RCC_CRSInitTypeDef pInit = {0};
    pInit.Prescaler   = RCC_CRS_SYNC_DIV1;
    pInit.Source      = RCC_CRS_SYNC_SOURCE_USB;
    pInit.Polarity    = RCC_CRS_SYNC_POLARITY_RISING;
    pInit.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000); // 48000 clock cycles in 1 ms
    pInit.ErrorLimitValue       = 34;  // tolerance window for frequency error
    pInit.HSI48CalibrationValue = 32;  // initial trim value for the HSI48 oscillator

    HAL_RCCEx_CRSConfig(&pInit);

    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE(); // just nrst is on port G

    canfd_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN); // 160 MHz

    system_init_timestamp();
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

// 160 MHz
uint32_t system_get_can_clock()
{
    return canfd_clock;
}

// 1 µs timer
void system_init_timestamp()
{
    // Timer 3 uses PCLK1 (80 MHz)
    // But there is a special rule that this timer runs at 2 x PCLK1 if APB1CLKDivider > 1
    // This means that the timer clock is 160 MHz.
    // Timer 3 is a 16 bit timer!
    __HAL_RCC_TIM3_CLK_ENABLE();
    TIM3->CR1   = 0;
    TIM3->CR2   = 0;
    TIM3->SMCR  = 0;
    TIM3->DIER  = 0;
    TIM3->CCMR1 = 0;
    TIM3->CCMR2 = 0;
    TIM3->CCER  = 0;
    TIM3->PSC   = (SystemCoreClock / 1000000) - 1; // 160 - 1 = 159
    TIM3->ARR   = 0xFFFFFFFF;
    TIM3->CR1  |= TIM_CR1_CEN;
    TIM3->EGR   = TIM_EGR_UG;   
}

// get timestamp with 1 µs precision
// Timer3 must be used because this is written into FDCAN_TxEventFifoTypeDef.TxTimestamp and FDCAN_RxHeaderTypeDef.RxTimestamp
// Timer3 provides only the low 16 bit. The high 16 bit come from the wrap around callback.
uint32_t system_get_timestamp()
{
    // timer_3 has the same value as HAL_FDCAN_GetTimestampCounter()    
    return (timestamp_wrap << 16) | TIM3->CNT;
}

// get only the high 16 bit of the timestamp counter
uint32_t system_get_timewrap()
{
    return timestamp_wrap;
}

// overwrite weak interrupt handler dummy
void FDCAN1_IT0_IRQHandler(void) 
{ 
    HAL_FDCAN_IRQHandler(can_get_handle()); 
}

// overwrite weak callback dummy
// This callback is called by interrupt every 65 ms
void HAL_FDCAN_TimestampWraparoundCallback(FDCAN_HandleTypeDef *hfdcan)
{
    timestamp_wrap ++;
}

// ------------------------------------------------------------------

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


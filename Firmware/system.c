/*
    The MIT License
    Copyright (c) 2025 ElmueSoft / Nakanishi Kiyomaro / Normadotcom
    https://netcult.ch/elmue/CANable Firmware Update
*/

#include "settings.h"
#include "system.h"
#include "can.h"
#include "control.h"

// Linker symbol points to end of firmware (.text section)
extern uint32_t _etext;

uint32_t          canfd_clock;
TIM_HandleTypeDef timer2 = {0};

// private functions
bool system_init_timestamp();

// See "STM32G4/G0 Series - Clock Generation.png" in subfolder "Documentation"
bool system_init(void)
{
    if (HAL_Init() != HAL_OK)
      return false;
   
    // ------------------------------------------
    
    // Bugfix: This was missing in the legacy firmware.
    // If the PWR clock is not enabled, HAL_PWREx_ControlVoltageScaling() has no effect.
    __HAL_RCC_PWR_CLK_ENABLE();    

    // Enable the highest voltage of the internal core voltage regulator.
    // This is indispensable to run the highest clock frequency that the processor supports.
    // Otherwise the processor may crash when configuring the PLL below.
    // Scale 2       = 1.00 Volt for power saving mode at low clock frequency
    // Scale 1       = 1.20 Volt for normal operation
    // Scale 1 Boost = 1.28 Volt for clock >= 150 MHz (only STM32G4xx serie)
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLT_HIGHEST);

    // ------------------------------------------

    // This is not required for the G4xx serie, but indispensable for using SYSCFG in HAL_PCD_IRQHandler() in the G0xx serie!
	__HAL_RCC_SYSCFG_CLK_ENABLE();

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    uint32_t latency;   
    
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI48; // 48 MHz RC oscillator is always needed as USB clock
    RCC_OscInitStruct.HSI48State          = RCC_HSI48_ON;             // enable HSI (High Speed Internal) 48 MHz RC oscillator
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;

    // HSE_VALUE = QUARTZ_FREQU is assigned from QUARTZ_FREQU in the makefiles
#if HSE_VALUE == 0 // No quartz crystal present on the board
    RCC_OscInitStruct.OscillatorType     |= RCC_OSCILLATORTYPE_HSI; // 16 MHz RC oscillator used as input for PLL if no quartz present
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;             // enable HSI (High Speed Internal) 16 MHz RC oscillator
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;      // use internal RC oscillator to feed the PLL
#else // quartz crystal is present
    RCC_OscInitStruct.OscillatorType     |= RCC_OSCILLATORTYPE_HSE; // quartz oscillator used as input for PLL
    RCC_OscInitStruct.HSEState            = RCC_HSE_ON;             // enable HSE (High Speed External) quartz oscillator
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;      // use quartz oscillator to feed the PLL
#endif

#if defined(STM32G4xx) 

    // Configure VCO frequency = 320 MHz (max 344 MHz)
    // HSE_VALUE = QUARTZ_FREQU is assigned from QUARTZ_FREQU in the makefiles
    #if HSE_VALUE == 0 || HSE_VALUE == 16000000     // HSE_VALUE = 0 -> The board has no quartz -> use internal RC oscillator
        RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1; // divide 16 MHz RC oscillator or quartz clock / 1 --> 16 MHz
        RCC_OscInitStruct.PLL.PLLN = 20;            // multiply 16 MHz x 20 --> VCO frequency = 320 MHz
    #elif HSE_VALUE == 8000000  // 8 MHz quartz
        RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1; // divide 8 MHz quartz clock / 1 --> 8 MHz
        RCC_OscInitStruct.PLL.PLLN = 40;            // multiply 8 MHz x 40 --> VCO frequency = 320 MHz
    #elif HSE_VALUE == 25000000 // 25 MHz quartz (Jhoinrch board)
        RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV5; // divide 25 MHz quartz clock / 5 --> 5 MHz
        RCC_OscInitStruct.PLL.PLLN = 64;            // multiply 5 MHz x 64 --> VCO frequency = 320 MHz
    #else
        #error "Quartz frequency not implemented"
    #endif

    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; // PLL output P = VCO / 2 = 160 MHz (for ADC)    (max 170 MHz)
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2; // PLL output Q = VCO / 2 = 160 MHz (for FDCAN)  (max 170 MHz)
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2; // PLL output R = VCO / 2 = 160 MHz (for SYSCLK) (max 170 MHz)
    
    // ------------------------------------------
    
    // See "STM32G4 Series - Clock Generation.png" in subfolder "Documentation"
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; // set SYSCLK = PLL R   = 160 MHz
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         // set HCLK   = SYSCLK  = 160 MHz (AHB  = Advanced High-performance Bus) (max 170 MHz)
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;           // set PCLK1  = HCLK /2 =  80 MHz (APB1 = Advanced Peripheral Bus 1)     (max 170 MHz)
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;           // set PCLK2  = HCLK /2 =  80 MHz (APB2 = Advanced Peripheral Bus 2)     (max 170 MHz)
    latency = FLASH_LATENCY_8;                                  // 8 Wait states if HCLK at 160 MHz   

#elif defined(STM32G0xx) 

    // Configure VCO frequency = 240 MHz (max 344 MHz)
    // ATTENTION: This processor must not run at the maximum allowed frequency of 64 MHz. 
    // A clock of 64 MHz would not allow to generate a CAN FD baurate of 5 Mbaud.
    
    // HSE_VALUE = QUARTZ_FREQU is assigned in the makefiles
    #if HSE_VALUE == 0 || HSE_VALUE == 16000000     // HSE_VALUE = 0 -> The board has no quartz -> use internal RC oscillator
        RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1; // divide 16 MHz RC oscillator or quartz clock / 1 --> 16 MHz
        RCC_OscInitStruct.PLL.PLLN = 15;            // multiply 16 MHz x 15 --> VCO frequency = 240 MHz
    #elif HSE_VALUE == 8000000  // 8 MHz quartz
        RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV1; // divide 8 MHz quartz clock / 1 --> 8 MHz
        RCC_OscInitStruct.PLL.PLLN = 30;            // multiply 8 MHz x 30 --> VCO frequency = 240 MHz
    #else
        #error "Quartz frequency not implemented"
    #endif

    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4; // PLL output P = VCO / 4 = 60 MHz (for ADC)    (max 122 MHz)
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV4; // PLL output Q = VCO / 4 = 60 MHz (for FDCAN)  (max 128 MHz)
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV4; // PLL output R = VCO / 4 = 60 MHz (for SYSCLK) (max  64 MHz)
    
    // ------------------------------------------

    // Set maximum allowed frequency 60 MHz for SYSCLK, HCLK, PCLK, AHB and APB
    // See "STM32G0 Series - Clock Generation.png" in subfolder "Documentation"
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK; // set SYSCLK = PLL R  = 60 MHz
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;         // set HCLK   = SYSCLK = 60 MHz (AHB = Advanced High-performance Bus) (max 64 MHz)
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;           // set PCLK   = HCLK   = 60 MHz (APB = Advanced Peripheral Bus)       (max 64 MHz)
    // PCLK2 and APB2 do not exist on this processor.
    latency = FLASH_LATENCY_2;                                  // 2 Wait states if HCLK at 60 MHz

#else
    #error "MCU_SERIE not implemented"
#endif

    if (HAL_RCC_OscConfig  (&RCC_OscInitStruct)          != HAL_OK ||
        HAL_RCC_ClockConfig(&RCC_ClkInitStruct, latency) != HAL_OK)
        return false;

    // --------------------------------------------

    // Initialize the USB and FDCAN clocks
    RCC_PeriphCLKInitTypeDef RCC_PeriphClkInit = {0};
    RCC_PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB | RCC_PERIPHCLK_FDCAN;
    // FDCAN uses PLL output Q (see settings above)
    RCC_PeriphClkInit.FdcanClockSelection  = RCC_FDCANCLKSOURCE_PLL;
    // The USB subsystem uses the internal 48 MHz oscillator which is synchronized to the USB SOF packets.
    RCC_PeriphClkInit.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;

    if (HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphClkInit) != HAL_OK)
        return false;

    // --------------------------------------------

    // Clock Recovery System (CRS) calibrates the internal HSI48 oscillator so it stays accurate enough for USB.
    // The USB SOF (Start-Of-Frame) packets, which arrive every 1 ms, are used for synchronization.
    RCC_CRSInitTypeDef CRS_Init = {0};
    CRS_Init.Prescaler   = RCC_CRS_SYNC_DIV1;
    CRS_Init.Source      = RCC_CRS_SYNC_SOURCE_USB;
    CRS_Init.Polarity    = RCC_CRS_SYNC_POLARITY_RISING;
    CRS_Init.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000); // 48000 clock cycles in 1 ms
    CRS_Init.ErrorLimitValue       = 34;  // tolerance window for frequency error
    CRS_Init.HSI48CalibrationValue = 32;  // initial trim value for the HSI48 oscillator

    HAL_RCCEx_CRSConfig(&CRS_Init);

    // --------------------------------------------
    
    // Provide GPIO ports with clock, must be executed before calling HAL_GPIO_Init()
    __HAL_RCC_GPIOA_CLK_ENABLE(); // Enable GPIO Port A used for LED's
    __HAL_RCC_GPIOB_CLK_ENABLE(); // Enable GPIO Port B used for LED's + FDCAN
    __HAL_RCC_GPIOC_CLK_ENABLE(); // GPIO Port C is currently not used
    __HAL_RCC_GPIOF_CLK_ENABLE(); // GPIO Port F is currently not used   

    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);

    canfd_clock = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN); // 160 or 60 MHz

    if (!system_init_timestamp())
        return false;

    // ignore errors
    system_set_option_bytes(OPT_BOR_Level4);
    return true;
}

// --------------------------------------------

// Configure 32 bit Timer 2 as 1 µs timer (1 MHz). 
bool system_init_timestamp()
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    // Timer 2 uses PCLK1
    // STM32G431:
    //     PCLK1 = 160 MHz / (APB1CLKDivider == 2) = 80 MHz
    //     But there is a special rule that this timer runs at 2 x PCLK1 if APB1CLKDivider == 2
    //     Timer Clock = PCLK1 x 2 = 160 MHz = SystemCoreClock
    // STM32G0B1: 
    //     PCLK1 = 60 MHz / (APB1CLKDivider == 1) = 60 MHz
    //     Timer Clock = PCLK1 x 1 = 60 MHz = SystemCoreClock
    timer2.Instance               = TIM2;
    timer2.Init.Prescaler         = (SystemCoreClock / 1000000) - 1; // 1 MHz tick
    timer2.Init.Period            = 0xFFFFFFFF;
    timer2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    timer2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    return (HAL_TIM_Base_Init (&timer2) == HAL_OK &&
            HAL_TIM_Base_Start(&timer2) == HAL_OK);
}

// Get timestamp with 1 µs precision
uint32_t system_get_timestamp()
{
    return TIM2->CNT;
}

uint32_t system_get_can_clock()
{
    return canfd_clock;
}

// ===================================================================================================

#if HSE_VALUE == 0 // Board without quartz: FDCAN and SYSCLK are derived from the HSI16 RC oscillator

// The HSI16 RC oscillator has a factory precision of only approx. 1% (worse over temperature).
// This is not good enough for reliable CAN timing, especially for CAN FD.
// The CRS only trims the HSI48 (USB clock). The HSI48 cannot feed the PLL, so FDCAN does not profit from it.
// Therefore HSI16 is calibrated here in software against the USB SOF packets which the host sends every 1 ms.
// The USB host's SOF period is crystal-controlled (USB full speed requires 1 ms +/- 0.05%).
// Timer 2 runs at 1 MHz derived from HSI16 --> over CAL_WINDOW_FRAMES frames it must count exactly 1000 per frame.
// The error is corrected with the HSITRIM bits in RCC->ICSCR (one step is approx. 0.2 ... 0.4 %).
//
// The window is measured from the absolute Timer 2 value and the absolute USB frame number (FNR),
// so intermediate SOF interrupts that are delayed or lost do not matter.
// A window only starts and ends on a "clean" SOF that arrived 1 frame and approx. 1 ms after the previous one,
// so a delayed ISR at the start or end of the window cannot falsify the result.
//
// STM32G4 datasheet (HSI16 characteristics): the trim step is 0.2 ... 0.4 %, but stepping onto a code that is
// a multiple of 64 changes the frequency by -4 ... -8 %. HSITRIM therefore consists of two monotonic segments
// [0..63] and [64..127] where code c in the lower segment is approx. as fast as code c + CAL_SEG_OFFSET in the upper.
// The calibration never steps across the boundary. It jumps to the equivalent code in the other segment instead,
// and only while no CAN channel is open. If the jump turns out to be wrong, it is reverted and never tried again.

#define CAL_WINDOW_FRAMES   1000  // measure over 1 second -> resolution 1 ppm
#define CAL_CLEAN_DEV        200  // a SOF is "clean" if it arrives 1000 +/- 200 us after the previous one
#define CAL_GROSS_DEV        150  // deviation per frame (15%) above which frames were lost (suspend, reset, FNR wrap)
#define CAL_STEP_INIT       2000  // initial estimate of one HSITRIM step in ppm (datasheet min = 0.2%), learned at runtime
#define CAL_STEP_MIN        1000
#define CAL_STEP_MAX        6000
#define CAL_MAX_ERR        30000  // 3%: larger errors are only possible after a wrong segment jump
#define CAL_REVERT_US    3000000  // revert a trim change if no valid measurement was possible within 3 seconds
#define CAL_TRIM_MAX         127

#if defined(STM32G4xx)
    #define CAL_SEGMENTS     1
    #define CAL_SEG_LOW     64    // first code of the upper segment
    #define CAL_SEG_OFFSET  21    // approx. 6% / 0.3%
#endif

static struct
{
    bool     synced;          // last_frame and last_us are valid
    bool     window;          // a measurement window is running
    uint16_t last_frame;      // last USB frame number (11 bit)
    uint32_t last_us;         // Timer 2 at the last SOF
    uint32_t start_us;        // Timer 2 at the start of the measurement window
    uint32_t frames;          // count of frames in the current window
    int32_t  step_ppm;        // learned size of one HSITRIM step
    // state after a trim change
    bool     changed;         // the trim was changed and not yet verified by a measurement
    bool     crossed;         // the change was a jump into the other segment
    bool     no_cross;        // a segment jump has failed -> never jump again
    int32_t  prev_trim;       // trim before the change (for revert)
    int32_t  prev_err_ppm;    // error before the change (for learning the step size)
    int32_t  steps;           // count of steps of the change
    uint32_t since_change_us; // time since the change
} hsi_cal = { .step_ppm = CAL_STEP_INIT };

static volatile int32_t hsi_error_ppm = 0; // last measured error (positive = HSI16 too fast), for the debugger

static inline int32_t cal_abs(int32_t value) { return value < 0 ? -value : value; }

static inline int32_t cal_get_trim()
{
    return (RCC->ICSCR & RCC_ICSCR_HSITRIM) >> RCC_ICSCR_HSITRIM_Pos;
}

static void cal_revert()
{
    __HAL_RCC_HSI_CALIBRATIONVALUE_ADJUST((uint32_t)hsi_cal.prev_trim);
    if (hsi_cal.crossed)
        hsi_cal.no_cross = true;
    hsi_cal.changed = false;
    hsi_cal.window  = false;
}

// Decide about a trim change after a valid measurement
static void cal_evaluate(int32_t err_ppm)
{
    int32_t trim = cal_get_trim();

    if (hsi_cal.changed)
    {
        hsi_cal.changed = false;
        if (hsi_cal.crossed)
        {
            if (cal_abs(err_ppm) > CAL_MAX_ERR) // the segment jump went wrong
            {
                cal_revert();
                return;
            }
        }
        else // learn the real step size from the effect of the change
        {
            int32_t step = cal_abs(hsi_cal.prev_err_ppm - err_ppm) / cal_abs(hsi_cal.steps);
            if (step >= CAL_STEP_MIN && step <= CAL_STEP_MAX)
                hsi_cal.step_ppm = step;
        }
    }

    // Hysteresis of 60% of one step: after a correction the residual error (<= 50% step) does not trigger again.
    if (cal_abs(err_ppm) <= hsi_cal.step_ppm * 6 / 10 || cal_abs(err_ppm) > CAL_MAX_ERR)
        return;

    // steps > 0 --> increase the frequency (HSI16 too slow)
    int32_t steps = (cal_abs(err_ppm) + hsi_cal.step_ppm / 2) / hsi_cal.step_ppm;
    if (steps == 0) steps = 1;
    if (err_ppm > 0) steps = -steps;

    // While CAN is open only single steps (approx. 0.3%) are allowed, which the CAN bit timing tolerates.
    bool can_open = can_is_any_open();
    if (can_open)
        steps = (steps > 0) ? 1 : -1;

    int32_t new_trim = trim + steps;
    bool    crossed  = false;

#if defined(CAL_SEGMENTS)
    if (trim >= CAL_SEG_LOW && new_trim < CAL_SEG_LOW) // upper --> lower segment
    {
        if (can_open || hsi_cal.no_cross) new_trim = CAL_SEG_LOW;
        else { new_trim -= CAL_SEG_OFFSET; crossed = true; }
    }
    else if (trim < CAL_SEG_LOW && new_trim >= CAL_SEG_LOW) // lower --> upper segment
    {
        if (can_open || hsi_cal.no_cross) new_trim = CAL_SEG_LOW - 1;
        else { new_trim += CAL_SEG_OFFSET; crossed = true; }
    }
#endif

    if (new_trim < 0)            new_trim = 0;
    if (new_trim > CAL_TRIM_MAX) new_trim = CAL_TRIM_MAX;
    if (new_trim == trim)
        return;

    __HAL_RCC_HSI_CALIBRATIONVALUE_ADJUST((uint32_t)new_trim);
    hsi_cal.changed         = true;
    hsi_cal.crossed         = crossed;
    hsi_cal.prev_trim       = trim;
    hsi_cal.prev_err_ppm    = err_ppm;
    hsi_cal.steps           = new_trim - trim;
    hsi_cal.since_change_us = 0;
    hsi_cal.window          = false; // the current window started before the change
}

// Called from the USB interrupt on each SOF packet (every 1 ms)
void system_calibrate_hsi(uint16_t frame)
{
    uint32_t now_us = TIM2->CNT;
    frame &= 0x7FF;

    if (!hsi_cal.synced)
    {
        hsi_cal.synced     = true;
        hsi_cal.last_frame = frame;
        hsi_cal.last_us    = now_us;
        return;
    }

    uint32_t d_frames  = (frame - hsi_cal.last_frame) & 0x7FF;
    uint32_t d_us      = now_us - hsi_cal.last_us;
    int32_t  deviation = (int32_t)d_us - (int32_t)(d_frames * 1000);
    hsi_cal.last_frame = frame;
    hsi_cal.last_us    = now_us;

    // A trim change that cannot be verified (e.g. a segment jump that went far off) is reverted.
    if (hsi_cal.changed)
    {
        hsi_cal.since_change_us += d_us;
        if (hsi_cal.since_change_us > CAL_REVERT_US)
        {
            cal_revert();
            return;
        }
    }

    // Frames lost: suspend/resume, bus reset or frame counter wrap-around during a long gap
    if (d_frames == 0 || cal_abs(deviation) > (int32_t)d_frames * CAL_GROSS_DEV)
    {
        hsi_cal.window = false;
        return;
    }

    bool clean = (d_frames == 1 && cal_abs(deviation) <= CAL_CLEAN_DEV);

    if (!hsi_cal.window)
    {
        if (clean)
        {
            hsi_cal.window   = true;
            hsi_cal.start_us = now_us;
            hsi_cal.frames   = 0;
        }
        return;
    }

    hsi_cal.frames += d_frames;
    if (hsi_cal.frames < CAL_WINDOW_FRAMES || !clean)
        return;

    // error in ppm = (measured - expected) / expected * 10^6 with expected = frames * 1000 us
    int32_t diff_us = (int32_t)(now_us - hsi_cal.start_us) - (int32_t)(hsi_cal.frames * 1000);
    int32_t err_ppm = diff_us * 1000 / (int32_t)hsi_cal.frames;
    hsi_error_ppm   = err_ppm;

    // this clean SOF also starts the next window
    hsi_cal.start_us = now_us;
    hsi_cal.frames   = 0;

    cal_evaluate(err_ppm);
}

#endif // HSE_VALUE == 0

// ===================================================================================================

// While TARGET_MCU (from the make file) defines for which MCU serie the code was COMPILED,
// this function returns on which MCU the code is actually RUNNING.
// The user may have uploaded the firmware to the wrong processor which returns SERIE_Invalid.
eMcuSerie system_get_mcu_serie()
{
    eMcuSerie e_Serie = SERIE_Invalid;
    
    // HAL_GetDEVID() reads a 12 bit identifier (DBG_IDCODE) that is unique for each processor family.
    switch (HAL_GetDEVID())
    {
#if defined(STM32G0xx)        
        // Reference Manual RM0444, page 1361
        case 0x456: // STM32G051 + G061
        case 0x460: // STM32G071 + G081
        case 0x466: // STM32G031 + G041
        case 0x467: // STM32G0B1 + G0C1
            e_Serie = SERIE_G0;
            break;

#elif defined(STM32G4xx)
        // Reference Manual RM0440, page 2095
        case 0x468: // STM32G431 + G441
        case 0x469: // STM32G471 + G473 + G474 + G483 + G484
        case 0x479: // STM32G491 + G4A1       
            e_Serie = SERIE_G4;
            break;
#else
    #error "MCU_SERIE not implemented"
#endif
    }
    return e_Serie;
}

// ----------------------------- Option Bytes ----------------------------------

// returns if the requested option is set in the Option Bytes
eOptionStatus system_is_option_enabled(eOptionBytes e_Option)
{
    // Get option bytes
    FLASH_OBProgramInitTypeDef cur_values = {0};
    HAL_FLASHEx_OBGetConfig(&cur_values);

    switch (e_Option)
    {
        case OPT_BOR_Level4:    return ((cur_values.USERConfig & FLASH_OPTR_BOR_LEV_Msk)  == OB_BOR_LEVEL_4)    ? Option_Active : Option_Inactive;
        case OPT_BOOT0_Enable:  return ((cur_values.USERConfig & FLASH_OPTR_nSWBOOT0_Msk) == OB_BOOT0_FROM_PIN) ? Option_Active : Option_Inactive;
        case OPT_BOOT0_Disable: return ((cur_values.USERConfig & FLASH_OPTR_nSWBOOT0_Msk) == OB_BOOT0_FROM_OB)  ? Option_Active : Option_Inactive;
    }

    return Option_Unavailable;
}

// STM32G4xx serie: Set BoR (Brown-Out Reset) level to 4 (2.8 Volt = highet value)
// This means that a reset is generated when power voltage falls below 2.8V.
// This eliminates an issue where poor quality USB hubs that provide low voltage before switching the 5 Volt supply on
// which was causing PoR issues where the microcontroller would enter boot mode incorrectly.
// ----------------
// This function can also define if the pin BOOT0 is ignored.
// In the STM32G4xx serie this pin is STUPIDLY the same as the CAN RX pin which really sucks.
// By only restarting the computer the CANable goes into Bootloader mode.
// Thefore this firmware gives the user the possibility to ignore pin BOOT0.
// ====================================================================================================
// IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT ## IMPORTANT
// Modifying the option bytes are very critical!
// If you modify this code and introduce a bug you may end up in a frozen firmware that cannot be updated anymore!
// ====================================================================================================
eFeedback system_set_option_bytes(eOptionBytes e_Option)
{
    // It does not make sense to allow the user to disable pin BOOT0 on a board where this is not required (STM32G0B1)
#if ALLOW_DISABLE_BOOT0 == 0
    if (e_Option == OPT_BOOT0_Disable)
        return FBK_UnsupportedFeature;
#endif

    if (can_is_any_open())
        return FBK_AdapterMustBeClosed;

    // IMPORTANT:
    // The user may have uploaded the wrong firmware to the processor.
    // The serie STM32G0xx has totally different bits in the OPTR register than the serie STM32G4xx.
    // It is VERY important not to execute the following code on the wrong processor!
    // Screwing up the Option Bytes may have fatal consquences that can make the adapter unusable.
    // system_get_mcu_serie() gets the REAL processor serie.
    if (system_get_mcu_serie() == SERIE_Invalid) 
        return FBK_UnsupportedFeature;

    switch (system_is_option_enabled(e_Option))
    {
        case Option_Active:      return FBK_Success; // nothing to do, option is already active
        case Option_Inactive:    break;
        case Option_Unavailable: return FBK_UnsupportedFeature;
    }

    // The following bits apply only to the STM32G4xx serie:
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
            prog_values.USERConfig = OB_BOR_LEVEL_4;  // Set reset level threshold to 2.8V
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

// ----------------------------- R/W Flash Memory ----------------------------------

// Read/Write user data from/to one segment in flash memory.
// Firmware  is stored at the begin of the flash memory
// User data is stored at the end   of the flash memory
// This avoids that uploading a bigger firmware would corrupt the user data.

// Example: STM32G431 with 128 kB flash memory = 64 segments of 2 kB
// FLASH_BASE      = 0x08000000 (start address of flash memory)
// FLASH_SIZE      = 128 * 1024 (128 kB)
// FLASH_PAGE_SIZE = 2048       (2 kB)
// _etext          = linker constant pointing to end of firmware (.text section)

// ============================================
// ATTENTION:
// ST Microelectronics is so incredibly STUPID that FLASH_SIZE is wrong for the STM32G473.
// FLASH_SIZE is 128 kB although the processor has 512 kB flash.
// FLASH_BANK_SIZE is also wrong: 64 kB instead of 256 kB (the STM32G473 has 2 banks).
// Also LL_GetFlashSize() returns the same wrong size.
// There is no way to obtain the correct flash size because the STM32G473 may have 128, 256 or 512 kB.
// See RM0440 page 75.
// ============================================

// Get the start address of the segment (segment 0 is the last segment at the end of the flash memory)
// returns 0 if segment is invalid or occupied by firmware.
uint32_t system_get_flash_addr(uint32_t segment)
{
    if (segment > 255)
        return 0;

    uint32_t firm_end  = (uint32_t)&_etext; // end of firmware
    uint32_t dest_addr = FLASH_BASE + FLASH_SIZE - (segment + 1) * FLASH_PAGE_SIZE;

    if (dest_addr < firm_end)
        dest_addr = 0;

#if 0 // only for debugging
    char buf[300];
    sprintf(buf, "Fl=%lukB at x%08lX Pg=%lukB Fw=x%08lX Seg %lu --> x%08lX",
            FLASH_SIZE/1024, (uint32_t)FLASH_BASE, (uint32_t)FLASH_PAGE_SIZE/1024, firm_end, segment, dest_addr);
    control_send_debug_mesg(0, buf);
#endif
    return dest_addr;
}

// Erase a flash segment and write user data to it (this takes approx. 22 ms)
// The first 2 bytes in the segment store the data length.
// If the same data is already stored in the flash segment, the function does nothing.
// This avoids wearing off the flash memory.
// The buffer will be modified here and the buffer must have MAX_FLASH_DATA_LEN + 2 bytes!
eFeedback system_write_flash(uint32_t segment, uint8_t* buffer, uint16_t data_len)
{
    uint32_t flash_addr = system_get_flash_addr(segment);
    if (flash_addr == 0 || data_len > MAX_FLASH_DATA_LEN)
        return FBK_ParamOutOfRange;

    // Avoid wearing off the flash memory by useless erase and write operations.
    uint32_t cur_len = ((uint16_t*)flash_addr)[0];
    if (cur_len == 0xFFFF) cur_len = 0; // empty segment
    if (cur_len == data_len && memcmp((uint8_t*)(flash_addr + 2), buffer, data_len) == 0)
        return FBK_Success;

    // Clear pending flash errors
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    if (HAL_FLASH_Unlock() != HAL_OK) // Unlock flash
        return FBK_ErrorFromHAL;

    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Page      = (flash_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.NbPages   = 1;

    // Erase entire 2 kB flash segment.
    uint32_t page_error;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return FBK_ErrorFromHAL;
    }

    // If the user passes a length of zero --> only erase the flash segment (entirely FF FF FF ...)
    if (data_len > 0)
    {
        // Move data two bytes up and store the length (2 bytes) before the data.
        memmove(buffer + 2, buffer, data_len);
        ((uint16_t*)buffer)[0] = data_len;
        data_len += 2;

        // The HAL writes 64 bit double words to the flash memory
        uint16_t  len_64 = (data_len + 7) / 8;
        uint64_t* buf_64 = (uint64_t*)buffer;
        for (uint16_t i=0; i<len_64; i++, flash_addr += 8)
        {
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, flash_addr, buf_64[i]) != HAL_OK)
            {
                HAL_FLASH_Lock();
                return FBK_ErrorFromHAL;
            }
        }
    }

    HAL_FLASH_Lock();
    return FBK_Success;
}
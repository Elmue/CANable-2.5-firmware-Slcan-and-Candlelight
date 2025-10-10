
@echo You must have MingW and the STM32 Cube CLT installed.
@echo Find a detailed description on https://netcult.ch/elmue/CANable Firmware Update

@REM Copy all BIN files after compiling into this directory:
@set COPY_DIRECTORY="C:\Program Files (x86)\HUD ECU Hacker\Driver\CANable Firmware Update\Firmware\"

@echo:
@echo Clean up: Delete build directories
@echo The compiler fails to correctly apply changes in the sourcecode, so everything must be built from scratch each time.

@if exist Build_STM32G431xx_Candlelight_MksMakerbase  @rmdir /S /Q Build_STM32G431xx_Candlelight_MksMakerbase
@if exist Build_STM32G431xx_Candlelight_Openlightlabs @rmdir /S /Q Build_STM32G431xx_Candlelight_Openlightlabs

@echo:
@echo Build Candlelight MksMakerbase firmware for STM32G431
@make -s -f Make_G431_Candle_MksMakerbase

@copy /Y "Build_STM32G431xx_Candlelight_MksMakerbase\*.bin" %COPY_DIRECTORY%
@echo:
@echo Finished.
@pause

@echo:
@echo ===================================================================
@echo ===================================================================
@echo:

@echo:
@echo Build Candlelight Openlightlabs firmware for STM32G431
@make -s -f Make_G431_Candle_Openlightlabs

@copy /Y "Build_STM32G431xx_Candlelight_Openlightlabs\*.bin" %COPY_DIRECTORY%
@echo:
@echo Finished.
@pause

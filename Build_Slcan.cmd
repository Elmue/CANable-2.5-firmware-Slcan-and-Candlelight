
@echo You must have MingW and the STM32 Cube CLT installed.
@echo Find a detailed description on https://netcult.ch/elmue/CANable Firmware Update

@REM Copy all BIN files after compiling into this directory:
@REM The firmware updater will convert them automatically into DFU files.
@set COPY_DIRECTORY="C:\Program Files (x86)\HUD ECU Hacker\Driver\CANable Firmware Update\Firmware\"

@echo:
@echo Clean up: Delete build directories
@echo The compiler fails to correctly apply changes in the sourcecode, so everything must be built from scratch each time.

@if exist Build_STM32G431xx_Slcan_MksMakerbase  @rmdir /S /Q Build_STM32G431xx_Slcan_MksMakerbase
@if exist Build_STM32G431xx_Slcan_Openlightlabs @rmdir /S /Q Build_STM32G431xx_Slcan_Openlightlabs

@echo:
@echo Build Slcan MksMakerbase firmware for STM32G431
@make -s -f Make_G431_Slcan_MksMakerbase

@copy /Y "Build_STM32G431xx_Slcan_MksMakerbase\*.bin" %COPY_DIRECTORY%
@echo:
@echo Finished.
@pause

@echo:
@echo ===================================================================
@echo ===================================================================
@echo: 

@echo: 
@echo Build Slcan Openlightlabs firmware for STM32G431
@make -s -f Make_G431_Slcan_Openlightlabs

@copy /Y "Build_STM32G431xx_Slcan_Openlightlabs\*.bin" %COPY_DIRECTORY%
@echo:
@echo Finished.
@pause

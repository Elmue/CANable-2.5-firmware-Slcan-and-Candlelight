@echo off

rem Copy all BIN files after compiling into this directory:
rem The firmware updater will convert them automatically into DFU files.
set "COPY_DIRECTORY=C:\Program Files (x86)\HUD ECU Hacker\Driver\CANable Firmware Update\Firmware\"

rem -------------------------------------------------------------------------------------------------

echo You must have MingW and the STM32 Cube CLT installed.
echo Find a detailed description on https://netcult.ch/elmue/CANable Firmware Update#Compiling
echo:

:Loop

rem Print menu
echo:
echo ----------------------------------
echo  A: Build all boards
echo  B: Build Slcan Multiboard 431
echo  C: Build Slcan Multiboard 473
echo  D: Build Slcan Jhoinrch
echo  E: Build Slcan Openlightlabs
echo  F: Build Slcan OleksiiSolo
echo  G: Build Slcan OleksiiDual
echo  H: Build Slcan WeActStudioV1
echo  I: Build Slcan WeActStudioV2
echo  J: Build Slcan BigTreeTech
echo  K: Build Candlelight Multiboard 431
echo  L: Build Candlelight Multiboard 473
echo  M: Build Candlelight Jhoinrch
echo  N: Build Candlelight Openlightlabs
echo  O: Build Candlelight OleksiiSolo
echo  P: Build Candlelight OleksiiDual
echo  Q: Build Candlelight WeActStudioV1
echo  R: Build Candlelight WeActStudioV2
echo  S: Build Candlelight BigTreeTech
echo  X: Clean up and Exit
echo ----------------------------------
choice /C XABCDEFGHIJKLMNOPQRS /N /M "Press a key: "

cls

rem The compiler fails to correctly detect changes in sourcecode --> always re-build from scratch.
rem Delete all subfolders "Build\STM32*"
for /D %%i in ("Build\STM*") do (
    rmdir /S /Q "Build\%%~nxi"
)

rem 'X'
if %errorlevel% == 1 exit

rem 'A'
if %errorlevel% == 2 (
    rem compile all files "Make_*" in the current folder
    for %%f in ("Build\Make_*") do (
        call :Compile %%f
    )
)

rem 'B', 'C',...
if %errorlevel% ==  3 call :Compile  Build\Make_G431_Slcan_Multiboard
if %errorlevel% ==  4 call :Compile  Build\Make_G473_Slcan_Multiboard
if %errorlevel% ==  5 call :Compile  Build\Make_G431_Slcan_Jhoinrch
if %errorlevel% ==  6 call :Compile  Build\Make_G431_Slcan_Openlightlabs
if %errorlevel% ==  7 call :Compile  Build\Make_G431_Slcan_OleksiiSolo
if %errorlevel% ==  8 call :Compile  Build\Make_G473_Slcan_OleksiiDual
if %errorlevel% ==  9 call :Compile  Build\Make_G0B1_Slcan_WeActStudioV1
if %errorlevel% == 10 call :Compile  Build\Make_G431_Slcan_WeActStudioV2
if %errorlevel% == 11 call :Compile  Build\Make_G0B1_Slcan_BigTreeTechU2C
if %errorlevel% == 12 call :Compile  Build\Make_G431_Candle_Multiboard
if %errorlevel% == 13 call :Compile  Build\Make_G473_Candle_Multiboard
if %errorlevel% == 14 call :Compile  Build\Make_G431_Candle_Jhoinrch
if %errorlevel% == 15 call :Compile  Build\Make_G431_Candle_Openlightlabs
if %errorlevel% == 16 call :Compile  Build\Make_G431_Candle_OleksiiSolo
if %errorlevel% == 17 call :Compile  Build\Make_G473_Candle_OleksiiDual
if %errorlevel% == 18 call :Compile  Build\Make_G0B1_Candle_WeActStudioV1
if %errorlevel% == 19 call :Compile  Build\Make_G431_Candle_WeActStudioV2
if %errorlevel% == 20 call :Compile  Build\Make_G0B1_Candle_BigTreeTechU2C

if exist "%COPY_DIRECTORY%" (
    rem Copy all BIN files to HUD ECU Hacker's firmware update directory
    for /D %%i in ("Build\STM*") do (
        echo:
        copy /Y "%%~fi\*.bin" "%COPY_DIRECTORY%"
        echo Copied to: "%COPY_DIRECTORY%"
    )
) else (
    echo Directory does not exist: "%COPY_DIRECTORY%"
)

echo:
goto Loop


rem function Compile(Makefile)
:Compile

echo:
echo Compiling '%1' ...
make -s -f %1

rem return from function
exit /b


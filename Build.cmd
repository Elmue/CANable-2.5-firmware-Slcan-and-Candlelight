
@echo off
cls

rem enable !VARIABLE! parsing at run time instead of %VARIABLE% parsing at compile time.
setlocal enabledelayedexpansion

rem Copy all BIN files after compiling into this directory:
rem The firmware updater will convert them automatically into DFU files.
set "COPY_DIRECTORY=C:\Program Files (x86)\HUD ECU Hacker\Driver\CANable Firmware Update\Firmware\"

rem -------------------------------------------------------------------------------------------------

echo:
echo  You must have MingW and the STM32 Cube CLT installed.
echo  Find a detailed description on https://netcult.ch/elmue/CANable Firmware Update#Compiling
echo:

:Loop

rem Print board menu
echo:
echo ----------------------------------
echo  A: Build All make files
echo  B: Build BigTreeTech
echo  C: Build Jhoinrch
echo  D: Build Multiboard 431
echo  E: Build Multiboard 473
echo  F: Build OleksiiSolo
echo  G: Build OleksiiDual
echo  H: Build Openlightlabs
echo  I: Build WeActStudioV1
echo  J: Build WeActStudioV2
echo  X: Clean up and Exit
echo ----------------------------------

choice /C XABCDEFGHIJ /N /M "Press a key: "
set "BOARD_MENU=!errorlevel!"

rem The compiler fails to correctly detect changes in sourcecode --> always re-build from scratch.
rem Delete all subfolders "Build\STM32*"
for /D %%i in ("Build\STM*") do (
    rmdir /S /Q "Build\%%~nxi"
)

rem 'X'
if !BOARD_MENU! EQU 1 exit

rem 'A' --> Compile all "Make_*" files in subfolder 'Build'
if !BOARD_MENU! EQU 2 (
    cls
    for %%f in ("Build\Make_*") do (
        call :Compile %%f
    )
) else (
    rem 'B', 'C',... --> Print firmware menu
    echo:
    echo ----------------------------------
    echo  1: Build Candlelight
    echo  2: Build Slcan
    echo  X: Clean up and Exit
    echo ----------------------------------

    choice /C X12 /N /M "Press a key: "
    set "FIRMWARE_MENU=!errorlevel!"

    rem 'X'
    if !FIRMWARE_MENU! EQU 1 exit

    cls

    rem '1', '2'
    if !FIRMWARE_MENU! EQU 2 set "FIRMW_NAME=Candle"
    if !FIRMWARE_MENU! EQU 3 set "FIRMW_NAME=Slcan"

    rem 'B', 'C',...
    if !BOARD_MENU! EQU  3 call :Compile  Build\Make_G0B1_!FIRMW_NAME!_BigTreeTechU2C
    if !BOARD_MENU! EQU  4 call :Compile  Build\Make_G431_!FIRMW_NAME!_Jhoinrch
    if !BOARD_MENU! EQU  5 call :Compile  Build\Make_G431_!FIRMW_NAME!_Multiboard
    if !BOARD_MENU! EQU  6 call :Compile  Build\Make_G473_!FIRMW_NAME!_Multiboard
    if !BOARD_MENU! EQU  7 call :Compile  Build\Make_G431_!FIRMW_NAME!_OleksiiSolo
    if !BOARD_MENU! EQU  8 call :Compile  Build\Make_G473_!FIRMW_NAME!_OleksiiDual
    if !BOARD_MENU! EQU  9 call :Compile  Build\Make_G431_!FIRMW_NAME!_Openlightlabs
    if !BOARD_MENU! EQU 10 call :Compile  Build\Make_G0B1_!FIRMW_NAME!_WeActStudioV1
    if !BOARD_MENU! EQU 11 call :Compile  Build\Make_G431_!FIRMW_NAME!_WeActStudioV2
)

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
echo Compiling '%1'
echo Please wait...
echo:
make -s -f %1

rem return from function
exit /b



@echo off

rem Copy all BIN files after compiling into this directory:
rem The firmware updater will convert them automatically into DFU files.
@set COPY_DIRECTORY="C:\Program Files (x86)\HUD ECU Hacker\Driver\CANable Firmware Update\Firmware\"

rem -------------------------------------------------------------------------------------------------

echo You must have MingW and the STM32 Cube CLT installed.
echo Find a detailed description on https://netcult.ch/elmue/CANable Firmware Update
echo:

:Loop

rem Print menu
echo:
echo ----------------------------------
echo A: Build all boards
echo B: Build Slcan Multiboard
echo C: Build Slcan Jhoinrch
echo D: Build Slcan Openlightlabs
echo E: Build Candlelight Multiboard
echo F: Build Candlelight Jhoinrch
echo G: Build Candlelight Openlightlabs
echo X: Exit
echo ----------------------------------
choice /C XABCDEFG /N /M "Press a key: "

rem 'X'
if %errorlevel%==1 exit

rem The compiler fails to correctly detect changes in sourcecode --> always built from scratch.
rem Delete all subfolders "Build_STM*"
for /D %%i in ("Build_STM*") do (
    rmdir /S /Q "%%~nxi"
)

rem 'A'
if %errorlevel%==2 (
    rem compile all files "Make_*" in the current folder
    for %%f in ("Make_*") do (
        call :Compile %%f
    )
)

rem 'B', 'C',...
if %errorlevel%==3 call :Compile  Make_G431_Slcan_Multiboard
if %errorlevel%==4 call :Compile  Make_G431_Slcan_Jhoinrch
if %errorlevel%==5 call :Compile  Make_G431_Slcan_Openlightlabs
if %errorlevel%==6 call :Compile  Make_G431_Candle_Multiboard
if %errorlevel%==7 call :Compile  Make_G431_Candle_Jhoinrch
if %errorlevel%==8 call :Compile  Make_G431_Candle_Openlightlabs

rem Copy all BIN files to HUD ECU Hacker
for /D %%i in ("Build_STM*") do (
    echo:
    copy /Y "%%~fi\*.bin" %COPY_DIRECTORY%
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


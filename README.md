# CANable-2.5-firmware-Slcan-and-Candlelight
Two new high quality, speed optmimized firmwares for CANable adapters with lots of new features.

![CANable Adapter](https://github.com/user-attachments/assets/061f60ba-14a2-4896-866f-6226fc9123f6)


This is the first project that combines the two CANable firmware's Slcan and Candlelight into one code base.
Dozens of bugs have been fixed.
Dozens of new features have been added.
This is the first Candlelight firmware for the STM32G431 processor that supports CAN FD and works without bugs.
However the new firmware is still 100% backward compatible with legacy Slcan / Candlelight firmware.
The firmware has been tested on the STM32G431 on the MKS Makerbase isolated board up to 10 Mbaud.
The firmware has been designed to be easily expandable for future processors and boards.
Precompiled binary firmware files can be uploaded to the CANable with the new Firmware Updater.

<img width="577" height="484" alt="CANable STM32 Firmware Updater" src="https://github.com/user-attachments/assets/8b2b658d-a723-4fb5-9329-370d25015336" />


Please read the detailed 
- User Manual
- Slcan Developer Manual
- Candlelight Developer Manual
- Firmware Developer Manual

https://netcult.ch/elmue/CANable%20Firmware%20Update/

## Updating Firmware on Linux & macOS

_Source: [CANtact](https://cantact.io/cantact/users-guide.html#linux--macos). Make sure to have your device in DFU mode._

Flashing on Linux and macOS requires `dfu-util`:

- On macOS, this can be installed from Brew: `brew install dfu-util`.
- On Ubuntu, install dfu-util with: `sudo apt install dfu-util`.

Once `dfu-util` is installed, use it to flash the device:

```sh
sudo dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D FIRMWARE_FILE.bin
```

The latest .bin files can be downloaded from the [Releases](https://github.com/Elmue/CANable-2.5-firmware-Slcan-and-Candlelight/releases) section.

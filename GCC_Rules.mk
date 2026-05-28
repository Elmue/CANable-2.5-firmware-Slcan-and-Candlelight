
# CANable Makefile (from normaldotcom, modified by ElmüSoft)
# https://netcult.ch/elmue/CANable Firmware Update

#######################################
# user configuration:
#######################################

# Define the firmware version in BCD format.
# Version 0x250914 is displayed as "25.09.14" and means 14th september 2025
# The year and month are stored in the device descriptor.
# The entire version is returned by Slcan command "V" and by Candlelight command GS_ReqGetDeviceVersion
# Do not use totally meaningless version numbers like "b158aa7" in legacy firmware on Github.
FIRMWARE_VERSION = 0x260528

# TARGET_BOARD, TARGET_FIRMWARE and TARGET_MCU must be set in the main makefile before including this file

# define procesor family
ifeq ($(TARGET_MCU), STM32G431)
    MCU_SERIE = STM32G4xx
    CPU = cortex-m4
else ifeq ($(TARGET_MCU), STM32G473)
    MCU_SERIE = STM32G4xx
    CPU = cortex-m4
else ifeq ($(TARGET_MCU), STM32G0B1)
    MCU_SERIE = STM32G0xx
    CPU = cortex-m0plus
else
    $(error TARGET_MCU not implemented in GCC_Rules.mk)
endif


# directory to place output files in
BUILD_DIR = Build_$(TARGET_MCU)_$(TARGET_FIRMWARE)_$(TARGET_BOARD)

# File trunk (without extension) of build files: *.bin, *.hex, *.elf
# Example: Trunk = "STM32G431_Slcan2.5_Multiboard_0x250914"
BUILD_TRUNK = $(BUILD_DIR)/$(TARGET_MCU)_$(TARGET_FIRMWARE)2.5_$(TARGET_BOARD)_$(FIRMWARE_VERSION)

# user C flags (enable warnings, enable debug info)
# Flag -O3 optimizes for higher speed, Flag -Os optimizes for smaller size
# Further these flags may be added: -MMD -MP -fstack-usage -std=gnu11
USER_CFLAGS = -Wall -g -ffunction-sections -fdata-sections -O3

# user LD flags
USER_LDFLAGS = -fno-exceptions -ffunction-sections -fdata-sections -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

#######################################
# binaries
#######################################
CC = arm-none-eabi-gcc
AR = arm-none-eabi-ar
RANLIB = arm-none-eabi-ranlib
SIZE = arm-none-eabi-size
OBJCOPY = arm-none-eabi-objcopy

ifeq ($(OS), Windows_NT)
    # On Windows mkdir is already implemented in the console.
    # But Windows mkdir takes other parameters than Linux mkdir.
    # Simply rename the file mkdir.exe in your MingW installation folder into mmkdir.exe
    MKDIR = mmkdir -p
else
    # Linux
    MKDIR = mkdir -p
endif

#######################################
# build configuration
#######################################

# output directory to build STM32Cube
CUBELIB_BUILD_DIR = $(BUILD_DIR)/STM32Cube

# select the folder with the HAL files
DRIVER_PATH = STM32/$(MCU_SERIE)_HAL_Driver

# directory that contains modified template files
CONFIG_DIR = $(DRIVER_PATH)/Config

# location of the linker script
LD_SCRIPT = $(CONFIG_DIR)/$(TARGET_MCU).ld


# includes for gcc
INCLUDES  = -ISTM32/CMSIS
INCLUDES += -I$(DRIVER_PATH)/Inc
INCLUDES += -I$(CONFIG_DIR)
INCLUDES += -ISource
INCLUDES += -ISource/USB
INCLUDES += -ISource/$(TARGET_FIRMWARE)

# compile gcc flags
CFLAGS =  $(INCLUDES)
CFLAGS += -mcpu=$(CPU) -mthumb
CFLAGS += $(USER_CFLAGS)
CFLAGS += -D$(TARGET_BOARD)
CFLAGS += -D$(TARGET_FIRMWARE)
CFLAGS += -D$(TARGET_MCU)xx
CFLAGS += -D$(MCU_SERIE)
CFLAGS += -DTARGET_BOARD=\"$(TARGET_BOARD)\"
CFLAGS += -DTARGET_FIRMWARE=\"$(TARGET_FIRMWARE)\"
CFLAGS += -DTARGET_MCU=\"$(TARGET_MCU)\"
CFLAGS += -DMCU_SERIE=\"$(MCU_SERIE)\"
CFLAGS += -DHSE_VALUE=$(QUARTZ_FREQU)
CFLAGS += -DFIRMWARE_VERSION_BCD=$(FIRMWARE_VERSION)
CFLAGS += -DUSER_VECT_TAB_ADDRESS

# default action: build the user application
all: $(BUILD_TRUNK).bin $(BUILD_TRUNK).hex

flash: all
	sudo dfu-util -w -d 0483:df11 -c 1 -i 0 -a 0 -s 0x08000000:leave -D $(BUILD_TRUNK).bin


#######################################
# build the ST micro peripherial library
# (STM32 and CMSIS)
#######################################

CUBELIB = $(CUBELIB_BUILD_DIR)/libstm32cube.a

# List of stm32 driver objects
# The HAL driver comes with some template files that are not meant to be compiled, like stm32g4xx_hal_timebase_tim_template.c
# STM did not put these templates into a separate subdirectory. If we filter them out here, this allows 
# building against an external driver directory without further modification.
CUBELIB_DRIVER_OBJS = $(addprefix $(CUBELIB_BUILD_DIR)/, $(patsubst %.c, %.o, $(notdir $(filter-out %/*_template.c,$(wildcard $(DRIVER_PATH)/Src/*.c)))))

# shortcut for building core library (make cubelib)
cubelib: $(CUBELIB)

$(CUBELIB): $(CUBELIB_DRIVER_OBJS)
	$(AR) rc $@ $(CUBELIB_DRIVER_OBJS)
	$(RANLIB) $@

$(CUBELIB_BUILD_DIR)/%.o: $(DRIVER_PATH)/Src/%.c | $(CUBELIB_BUILD_DIR)
	$(CC) -c $(CFLAGS) -o $@ $^

$(CUBELIB_BUILD_DIR):
	$(MKDIR) $@

#######################################
# build the firmware specific files
#######################################

FIRM_BUILD_DIR = $(BUILD_DIR)/$(TARGET_FIRMWARE)
FIRM_SOURCES += control.c buffer.c usb_class.c usb_interface.c
# list of firmware specific library objects
FIRM_OBJECTS += $(addprefix $(FIRM_BUILD_DIR)/,$(notdir $(FIRM_SOURCES:.c=.o)))

firm: $(FIRM_OBJECTS)

$(FIRM_BUILD_DIR)/%.o: Source/$(TARGET_FIRMWARE)/%.c | $(FIRM_BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

$(FIRM_BUILD_DIR):
	@echo $(FIRM_BUILD_DIR)
	$(MKDIR) $@

#######################################
# build the user application
#######################################

# list of common source files
SOURCES = main.c system_$(MCU_SERIE).c system.c interrupts.c can.c error.c led.c dfu.c utils.c usb_ctrlreq.c usb_ioreq.c usb_core.c usb_lowlevel.c 

# list of user program objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.c=.o)))
# add an object for the startup code
OBJECTS += $(BUILD_DIR)/startup_$(TARGET_MCU).o

# use the periphlib core library, plus generic ones (libc, libm, libnosys)
LIBS = -lstm32cube -lc -lm -lnosys
LDFLAGS = -T $(LD_SCRIPT) -L $(CUBELIB_BUILD_DIR) -static $(LIBS) $(USER_LDFLAGS)

$(BUILD_TRUNK).hex: $(BUILD_TRUNK).elf
	$(OBJCOPY) -O ihex $(BUILD_TRUNK).elf $@

$(BUILD_TRUNK).bin: $(BUILD_TRUNK).elf
	$(OBJCOPY) -O binary $(BUILD_TRUNK).elf $@

$(BUILD_TRUNK).elf: $(OBJECTS) $(FIRM_OBJECTS) $(CUBELIB)
	$(CC) -o $@ $(CFLAGS) $(OBJECTS) $(FIRM_OBJECTS) \
		$(LDFLAGS) -Xlinker \
		-Map=$(BUILD_TRUNK).map
	$(SIZE) $@

$(BUILD_DIR)/%.o: Source/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD_DIR)/system_$(MCU_SERIE).o: $(CONFIG_DIR)/system_$(MCU_SERIE).c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD_DIR)/startup_$(TARGET_MCU).o: $(CONFIG_DIR)/startup_$(TARGET_MCU).s | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD_DIR):
	$(MKDIR) $@

# delete all user application files, keep the libraries
clean:
		-rm $(BUILD_DIR)/*.o
		-rm $(BUILD_DIR)/*.elf
		-rm $(BUILD_DIR)/*.hex
		-rm $(BUILD_DIR)/*.map
		-rm $(BUILD_DIR)/*.bin

.PHONY: clean all cubelib

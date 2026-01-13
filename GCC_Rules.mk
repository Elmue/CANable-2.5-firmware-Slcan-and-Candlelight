
# CANable Makefile (from normaldotcom, modified by ElmüSoft)
# https://netcult.ch/elmue/CANable Firmware Update

#######################################
# user configuration:
#######################################

# TARGET_BOARD, TARGET_FIRMWARE, TARGET_FILE and TARGET_MCU must be set in the main makefile before including this file

# directory to place output files in
BUILD_DIR = Build_$(TARGET_MCU)_$(TARGET_FIRMWARE)_$(TARGET_BOARD)

# directory to place modified template files
CONFIG_DIR = STM32/$(TARGET_MCU)_Config

# location of the linker script
LD_SCRIPT = $(CONFIG_DIR)/$(TARGET_MCU).ld

# user C flags (enable warnings, enable debug info)
USER_CFLAGS = -Wall -g -ffunction-sections -fdata-sections -Os

# user LD flags
USER_LDFLAGS = -fno-exceptions -ffunction-sections -fdata-sections -Wl,--gc-sections

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

# core and CPU type for Cortex M0
# ARM core type (CORE_M0, CORE_M3)
#CORE = CORE_M4F

# ARM CPU type (cortex-m0, cortex-m3)
CPU = cortex-m4

# where to build STM32Cube
CUBELIB_BUILD_DIR = $(BUILD_DIR)/STM32Cube

# various paths within the STmicro library
DRIVER_PATH = STM32/STM32G4xx_HAL_Driver

# includes for gcc
INCLUDES  = -ISTM32/CMSIS/Include
INCLUDES += -ISTM32/CMSIS/Device
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
CFLAGS += -D$(TARGET_MCU)
CFLAGS += -DTARGET_BOARD=\"$(TARGET_BOARD)\"
CFLAGS += -DTARGET_FIRMWARE=\"$(TARGET_FIRMWARE)\"
CFLAGS += -DTARGET_MCU=\"$(TARGET_MCU)\"
CFLAGS += -DHSE_VALUE=$(QUARTZ_FREQU)

# default action: build the user application
all: $(BUILD_DIR)/$(TARGET_FILE).bin $(BUILD_DIR)/$(TARGET_FILE).hex

flash: all
	sudo dfu-util -w -d 0483:df11 -c 1 -i 0 -a 0 -s 0x08000000:leave -D $(BUILD_DIR)/$(TARGET_FILE).bin


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
	$(CC) -Os $(CFLAGS) -c -o $@ $^

$(FIRM_BUILD_DIR):
	@echo $(FIRM_BUILD_DIR)
	$(MKDIR) $@

#######################################
# build the user application
#######################################

# list of common source files
SOURCES = main.c system_stm32g4xx.c system.c interrupts.c can.c error.c led.c dfu.c utils.c usb_ctrlreq.c usb_ioreq.c usb_core.c usb_lowlevel.c usb_desc.c 

# list of user program objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.c=.o)))
# add an object for the startup code
OBJECTS += $(BUILD_DIR)/startup_$(TARGET_MCU).o

# use the periphlib core library, plus generic ones (libc, libm, libnosys)
LIBS = -lstm32cube -lc -lm -lnosys
LDFLAGS = -T $(LD_SCRIPT) -L $(CUBELIB_BUILD_DIR) -static $(LIBS) $(USER_LDFLAGS)

$(BUILD_DIR)/$(TARGET_FILE).hex: $(BUILD_DIR)/$(TARGET_FILE).elf
	$(OBJCOPY) -O ihex $(BUILD_DIR)/$(TARGET_FILE).elf $@

$(BUILD_DIR)/$(TARGET_FILE).bin: $(BUILD_DIR)/$(TARGET_FILE).elf
	$(OBJCOPY) -O binary $(BUILD_DIR)/$(TARGET_FILE).elf $@

$(BUILD_DIR)/$(TARGET_FILE).elf: $(OBJECTS) $(FIRM_OBJECTS) $(CUBELIB)
	$(CC) -o $@ $(CFLAGS) $(OBJECTS) $(FIRM_OBJECTS) \
		$(LDFLAGS) -Xlinker \
		-Map=$(BUILD_DIR)/$(TARGET_FILE).map
	$(SIZE) $@

$(BUILD_DIR)/%.o: Source/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -c -o $@ $^

$(BUILD_DIR)/%.o: $(CONFIG_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -c -o $@ $^

$(BUILD_DIR)/%.o: $(CONFIG_DIR)/%.s | $(BUILD_DIR)
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

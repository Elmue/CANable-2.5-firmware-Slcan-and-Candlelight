CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++20 -ICandlelight -ICandlelight/Linux -I/usr/include/libusb-1.0
LDFLAGS = -lusb-1.0

# default Build Mode = Debug
BUILD ?= Debug

ifeq ($(BUILD), Release)
   CXXFLAGS += -O2
   BUILD_DIR = Release
else
   CXXFLAGS += -g -O0
   BUILD_DIR = Debug
endif

# Binary settings
TARGET_NAME = canabledemo
BIN_DIR = Output
TARGET = $(BIN_DIR)/$(TARGET_NAME)

# Explicit list of source files
SRCS = CANableDemo.cpp \
       Candlelight/Utils.cpp \
       Candlelight/Candlelight.cpp \
       Candlelight/Linux/OsLibrary.cpp

# Map source files to intermediate object files inside the BUILD_DIR directory
OBJS = $(addprefix $(BUILD_DIR)/, $(SRCS:.cpp=.o))

all: $(TARGET)

# Link rule
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CXX) $(OBJS) $(LDFLAGS) -o $(TARGET)

# Compile rule
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create Output directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf Debug Release Output

.PHONY: all clean

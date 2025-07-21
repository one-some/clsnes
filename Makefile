.SILENT:

# Compiler and flags
CC = ccache gcc
# SANFLAGS = -fsanitize=undefined
SANFLAGS = -fsanitize=address
CCFLAGS = \
	-MMD -MP \
	-I./src \
	-g \
	-march=native \
	$(SANFLAGS) \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wshadow \
	-Wno-switch \
	-Wno-sign-conversion \
	-Wno-unused-parameter \
	-Wno-implicit-float-conversion \
	-Wno-implicit-int-float-conversion \
	-Wno-shorten-64-to-32 \
	-Wno-gnu-zero-variadic-macro-arguments \
	-Wno-float-conversion \

#-fsanitize=memory -fsanitize-memory-track-origins=2
LDFLAGS = \
	$(SANFLAGS) \
	-lraylib \
	-lGL \
	-lX11 \
	-lpthread \
	-lm \
	-lrt \

# Directories
SRC_DIR = src
BUILD_DIR = build
BIN_DIR =.

# Executable name
TARGET = main
EXEC_PATH =$(patsubst ./%,%,$(BIN_DIR)/$(TARGET))
EXEC_FULL_PATH =$(CURDIR)/$(patsubst ./%,%,$(BIN_DIR)/$(TARGET))

SOURCES := $(shell find $(SRC_DIR) -name '*.c')
OBJECTS := $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS	:= $(OBJECTS:.o=.d)

NPROC ?= $(shell nproc || echo 1)
MAKEFLAGS += -j$(NPROC)

.DEFAULT_GOAL := $(EXEC_PATH)
all: $(EXEC_PATH)

# Link object files into the final executable
$(EXEC_PATH): $(OBJECTS)
	@echo "LD    :: $@"
	$(CC) $^ -o $@ $(LDFLAGS)

# Compile source files into object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	@echo "CC    :: $@"
	$(CC) $(CCFLAGS) -c $< -o $@


# Clean target to remove build files and the executable
clean:
	rm -rf $(BUILD_DIR) $(EXEC_PATH)

run: $(EXEC_PATH)
	@echo "RUN    :: $(EXEC_FULL_PATH)"
	$(EXEC_FULL_PATH)


.PHONY: all clean run

-include $(DEPS)


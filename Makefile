all: dom1speedtest.z64
.PHONY: all

BUILD_DIR = build
include $(N64_INST)/include/n64.mk

SRC = dom1speedtest.c pif.c
OBJS = $(SRC:%.c=$(BUILD_DIR)/%.o)
DEPS = $(SRC:%.c=$(BUILD_DIR)/%.d)
N64_CFLAGS += -Wl,--build-id=none
N64_CFLAGS += -DDEFAULT_DOM1_LAT=0xFF -DDEFAULT_DOM1_PWD=0xFF
N64_CFLAGS += -G0 -Os

dom1speedtest.z64: N64_ROM_TITLE = "Dom1 Speed Test"

$(BUILD_DIR)/dom1speedtest.elf: $(OBJS)

clean:
	rm -rf $(BUILD_DIR) *.z64
.PHONY: clean

-include $(DEPS)


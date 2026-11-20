CROSS ?= arm-none-eabi-
DEPS ?= deps
HOST_CC ?= cc
AXIS ?= 2
DSP := $(DEPS)/dsp
CMSIS := $(DEPS)/cmsis/CMSIS/Core/Include
F7 := $(DEPS)/f7
RTOS := $(DEPS)/rtos
PORT := $(RTOS)/portable/GCC/ARM_CM7/r0p1
ARCH := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
OPT := -O2 -g -ffunction-sections -fdata-sections
DSP_INCLUDES := -I$(DSP)/Include -I$(DSP)/PrivateInclude -I$(CMSIS)
INCLUDES := $(DSP_INCLUDES) -Ifirmware -I$(F7)/Include -I$(RTOS)/include -I$(PORT)
DSP_SOURCES := $(addprefix $(DSP)/Source/TransformFunctions/,arm_rfft_fast_f32.c \
               arm_rfft_fast_init_f32.c arm_cfft_f32.c arm_cfft_init_f32.c \
               arm_cfft_radix8_f32.c arm_bitreversal2.c) \
               $(addprefix $(DSP)/Source/CommonTables/,arm_common_tables.c arm_const_structs.c)
SOURCES := $(wildcard firmware/*.c) $(DSP_SOURCES) \
           $(F7)/Source/Templates/system_stm32f7xx.c $(F7)/Source/Templates/gcc/startup_stm32f767xx.s \
           $(addprefix $(RTOS)/,tasks.c queue.c list.c) $(PORT)/port.c
HEADERS := $(wildcard firmware/*.h)
EXTRA_FLAGS ?=

.PHONY: all deps test clean FORCE
all: build/monitor.bin

deps:
	sh scripts/fetch-deps.sh "$(DEPS)"

build:
	mkdir -p $@

build/axis: FORCE | build
	@if ! test -f $@ || ! grep -qx '$(AXIS)' $@; then echo '$(AXIS)' > $@; fi

build/monitor.elf: $(SOURCES) $(HEADERS) firmware/linker.ld Makefile build/axis | build
	$(CROSS)gcc $(ARCH) $(OPT) $(INCLUDES) -DSTM32F767xx -DANALYSIS_AXIS=$(AXIS) \
		-std=c11 -Wall -Wextra -Werror $(EXTRA_FLAGS) -nostartfiles -Wl,--gc-sections \
		-Wl,-Map,build/monitor.map -T firmware/linker.ld $(SOURCES) -lm -lc -lnosys -o $@
	$(CROSS)size $@

build/monitor.bin: build/monitor.elf
	$(CROSS)objcopy -O binary $< $@

build/libanalysis.so: firmware/analysis.c firmware/analysis.h $(DSP_SOURCES) Makefile | build
	$(HOST_CC) $(OPT) $(DSP_INCLUDES) -shared -fPIC -Wl,--gc-sections \
		firmware/analysis.c $(DSP_SOURCES) -lm -o $@

test: build/libanalysis.so
	python3 -m pytest -q

clean:
	rm -rf build

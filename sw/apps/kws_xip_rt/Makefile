SHELL := /usr/bin/env bash

ROOT ?= $(abspath ../../..)
FW_OUT ?= $(ROOT)/build/fw
FW_NAME ?= kws_xip_rt
FW_LINKER ?= fw/linker.ld
MARCH ?= rv32im_zicsr
MABI ?= ilp32
OPT ?= -Os

FREERTOS_DIR ?= $(ROOT)/third_party/FreeRTOS-Kernel
PORT_DIR ?= $(FREERTOS_DIR)/portable/GCC/RISC-V
CHIP_EXT_DIR ?= $(PORT_DIR)/chip_specific_extensions/RISCV_no_extensions
BSP_DIR ?= $(ROOT)/sw/bsp

FW_SRCS ?= fw/kws_xip_rt.c \
  fw/kws_frontend_fixed.c \
  fw/rtos_port.c \
  fw/rtos_lib.c \
  $(FREERTOS_DIR)/list.c \
  $(FREERTOS_DIR)/queue.c \
  $(FREERTOS_DIR)/tasks.c \
  $(PORT_DIR)/port.c \
  $(PORT_DIR)/portASM.S \
  $(BSP_DIR)/drivers/uart/uart.c \
  $(BSP_DIR)/drivers/gpio/gpio.c \
  $(BSP_DIR)/drivers/timer/murax_timer.c \
  $(BSP_DIR)/drivers/venus/venus_driver.c \
  $(BSP_DIR)/syscalls.c \
  drivers/msm261s/msm261s.c

FW_INCS ?= . \
  fw \
  $(FREERTOS_DIR)/include \
  $(PORT_DIR) \
  $(CHIP_EXT_DIR) \
  $(BSP_DIR) \
  drivers/msm261s

FW_DEFS ?=

KWS_CORE_CLK_HZ ?= 50000000
FW_DEFS += KWS_CORE_CLK_HZ=$(KWS_CORE_CLK_HZ)

KWS_MSM261S_PCLK_HZ ?= 50000000
FW_DEFS += KWS_MSM261S_PCLK_HZ=$(KWS_MSM261S_PCLK_HZ)

KWS_UART_CLKDIV ?= 53
FW_DEFS += KWS_UART_CLKDIV=$(KWS_UART_CLKDIV)

KWS_VAD_ENABLE ?= 1
FW_DEFS += KWS_VAD_ENABLE=$(KWS_VAD_ENABLE)

KWS_VAD_AVG_ABS_TH ?= 256
FW_DEFS += KWS_VAD_AVG_ABS_TH=$(KWS_VAD_AVG_ABS_TH)

KWS_VAD_PEAK_TH ?= 2048
FW_DEFS += KWS_VAD_PEAK_TH=$(KWS_VAD_PEAK_TH)

KWS_VAD_TRIGGER_SAMPLES ?= 1024
FW_DEFS += KWS_VAD_TRIGGER_SAMPLES=$(KWS_VAD_TRIGGER_SAMPLES)

KWS_VAD_TRIGGER_AVG_ABS_TH ?= 128
FW_DEFS += KWS_VAD_TRIGGER_AVG_ABS_TH=$(KWS_VAD_TRIGGER_AVG_ABS_TH)

KWS_VAD_TRIGGER_PEAK_TH ?= 1024
FW_DEFS += KWS_VAD_TRIGGER_PEAK_TH=$(KWS_VAD_TRIGGER_PEAK_TH)

KWS_VAD_PREROLL_SAMPLES ?= 2048
FW_DEFS += KWS_VAD_PREROLL_SAMPLES=$(KWS_VAD_PREROLL_SAMPLES)

KWS_CONF_SCORE_TH ?= 10
FW_DEFS += KWS_CONF_SCORE_TH=$(KWS_CONF_SCORE_TH)

KWS_CONF_MARGIN_TH ?= 1
FW_DEFS += KWS_CONF_MARGIN_TH=$(KWS_CONF_MARGIN_TH)

KWS_LOG_REJECTED_EVERY ?= 8
FW_DEFS += KWS_LOG_REJECTED_EVERY=$(KWS_LOG_REJECTED_EVERY)

KWS_LOG_MIC_OVERFLOW_EVERY ?= 0
FW_DEFS += KWS_LOG_MIC_OVERFLOW_EVERY=$(KWS_LOG_MIC_OVERFLOW_EVERY)

KWS_LOG_RAW_TOP1 ?= 0
FW_DEFS += KWS_LOG_RAW_TOP1=$(KWS_LOG_RAW_TOP1)

KWS_GPIO_ACTIVITY_EN ?= 0
FW_DEFS += KWS_GPIO_ACTIVITY_EN=$(KWS_GPIO_ACTIVITY_EN)

KWS_GPIO_LED_RESULT_MODE ?= 1
FW_DEFS += KWS_GPIO_LED_RESULT_MODE=$(KWS_GPIO_LED_RESULT_MODE)

KWS_LED_ACTIVE_LOW ?= 1
FW_DEFS += KWS_LED_ACTIVE_LOW=$(KWS_LED_ACTIVE_LOW)

KWS_UART_LOG_EN ?= 1
FW_DEFS += KWS_UART_LOG_EN=$(KWS_UART_LOG_EN)

KWS_GPIO_STATUS_EN ?= 1
FW_DEFS += KWS_GPIO_STATUS_EN=$(KWS_GPIO_STATUS_EN)

KWS_RUN_TESTVECTOR ?= 0
FW_DEFS += KWS_RUN_TESTVECTOR=$(KWS_RUN_TESTVECTOR)

KWS_FAST_RT ?= 0
FW_DEFS += KWS_FAST_RT=$(KWS_FAST_RT)

FAST_I2S ?= 0
ifeq ($(FAST_I2S),1)
FW_DEFS += KWS_I2S_DIV_OVERRIDE=0u
endif

HEARTBEAT_EN ?= 1
FW_DEFS += KWS_HEARTBEAT_EN=$(HEARTBEAT_EN)

FLASH_BASE ?= 0x00100000
FLASH_LOAD_OFFSET ?= 0x00400000
FLASH_SECTIONS ?= \
	--only-section=.text \
	--only-section=.data \
	--only-section=.data.* \
	--only-section=.sdata \
	--only-section=.sdata.* \
	--only-section=.venus_uops \
	--only-section=.venus_params \
	--only-section=.srodata \
	--only-section=.srodata.* \
	--only-section=.rodata \
	--only-section=.rodata.*

RISCV_TOOLCHAIN_PREFIX ?= riscv-none-elf-
RISCV_GCC ?= $(RISCV_TOOLCHAIN_PREFIX)gcc
OBJCOPY ?= $(RISCV_TOOLCHAIN_PREFIX)objcopy

.PHONY: help fw boot_stub install clean

help:
	@printf '%s\n' \
	  'Targets:' \
	  '  make fw        Build XIP RTOS firmware' \
	  '  make boot_stub Build SRAM boot initmem' \
	  '  make install   Build both and stage flash image under build/fw' \
	  '  make clean     Remove staged firmware artifacts'

fw:
	python3 $(ROOT)/scripts/build_firmware.py \
	  --out-dir $(FW_OUT) \
	  --name $(FW_NAME) \
	  --linker $(FW_LINKER) \
	  --march=$(MARCH) --mabi=$(MABI) --opt=$(OPT) \
	  --ldflags "-lm -lc -lgcc" \
	  $(foreach i,$(FW_INCS),-I $(i)) \
	  $(foreach d,$(FW_DEFS),-D $(d)) \
	  $(foreach s,$(FW_SRCS),--src $(s))

boot_stub:
	@mkdir -p $(FW_OUT)
	$(RISCV_GCC) -march=$(MARCH) -mabi=$(MABI) -nostdlib -nostartfiles -ffreestanding \
	  -Wl,-T,fw/boot_stub.ld -Wl,-e,_start -Wl,--undefined=_start \
	  -o $(FW_OUT)/$(FW_NAME)_boot.elf fw/boot_stub.S
	$(OBJCOPY) -O binary $(FW_OUT)/$(FW_NAME)_boot.elf $(FW_OUT)/$(FW_NAME)_boot.bin
	python3 tools/bin2memb32.py \
	  --in-bin $(FW_OUT)/$(FW_NAME)_boot.bin \
	  --out-memb $(FW_OUT)/$(FW_NAME)_boot.memb \
	  --out-lane-prefix $(FW_OUT)/$(FW_NAME)_boot

install: fw boot_stub
	$(OBJCOPY) -O binary --wildcard $(FLASH_SECTIONS) --change-addresses=-$(FLASH_BASE) \
	  $(FW_OUT)/$(FW_NAME).elf $(FW_OUT)/$(FW_NAME)_flash.bin
	python3 tools/bin2hex8.py --in-bin $(FW_OUT)/$(FW_NAME)_flash.bin --out-hex $(FW_OUT)/$(FW_NAME)_flash.hex
	@echo "[OK] firmware staged under $(FW_OUT)"

clean:
	rm -rf $(FW_OUT)

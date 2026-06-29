SHELL := /usr/bin/env bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help

TINYML_NPU_DIR ?= third_party/TinyML_NPU
SERIAL_BAUD ?= 115200
FLASH_FW_OFFSET ?= 0x400000
VCS ?= vcs
IVERILOG ?= iverilog
VVP ?= vvp
SIM_DIR ?= $(abspath build/sim)
SIM_VCS_DIR ?= $(SIM_DIR)/vcs
SIM_IVERILOG_DIR ?= $(SIM_DIR)/iverilog
SIM_TB := $(abspath sw/apps/kws_xip_rt/tb/VenusCoreRVTop_kws_xip_rt_tb.sv)
SIM_RTL := $(abspath build/rtl/VenusCoreRVTop.v)
SIM_PCM_WAV ?=
SIM_PCM_WORD ?= one
SIM_EXPECTED_LABEL ?= 0

.PHONY: help env setup check check-public-tree check-scripts soc-rtl gw5a-fw gw5a-bitstream \
	gw5a-probe gw5a-detect-flash gw5a-flash-bitstream gw5a-flash-kws gw5a-reboot monitor \
	gw5a-demo-check play-kws-test sim-iverilog-build sim-testvector-fw sim-testvector \
	sim-pcm-fw sim-pcm-data sim-pcm sim-vcs-build sim-vcs-testvector sim-vcs-pcm clean

help:
	@printf '%s\n' \
	  'TinyML_SOC targets:' \
	  '  make env                  Check local tool availability' \
	  '  make setup                Initialize submodules' \
	  '  make check-scripts        Validate Bash and Python scripts' \
	  '  make soc-rtl              Generate GW5A SoC RTL under build/rtl' \
	  '  make gw5a-fw              Build KWS XIP firmware under build/fw' \
	  '  make gw5a-bitstream       Run Gowin implementation' \
	  '  make gw5a-probe           Read connected GW5A device ID' \
	  '  make gw5a-detect-flash    Read external SPI Flash ID' \
	  '  make gw5a-flash-bitstream Program FPGA bitstream to Flash offset 0x000000' \
	  '  make gw5a-flash-kws       Program KWS image to Flash offset $(FLASH_FW_OFFSET)' \
	  '  make gw5a-reboot          Reconfigure FPGA from Flash' \
	  '  make monitor              Stream UART logs; requires SERIAL_PORT' \
	  '  make play-kws-test        Play reproducible English KWS prompts' \
	  '  make gw5a-demo-check      Reboot, play prompts, require ready and detect' \
	  '  make sim-testvector       Run the NPU reference-vector RTL simulation' \
	  '  make sim-pcm              Run one generated/provided PCM KWS simulation' \
	  '  make sim-vcs-testvector   Run the same reference vector with VCS' \
	  '  make check                Run automated public checks'

env:
	bash scripts/check_env.sh

setup:
	git submodule update --init third_party/TinyML_NPU third_party/FreeRTOS-Kernel third_party/VexRiscv
	python3 -m pip install --user --disable-pip-version-check -r requirements-dev.txt

check-public-tree:
	bash scripts/check_public_tree.sh

check-scripts:
	bash -n scripts/*.sh
	python3 -m py_compile scripts/*.py sw/apps/kws_xip_rt/tools/*.py
	python3 scripts/check_docs.py

soc-rtl:
	bash scripts/gen_rtl.sh

gw5a-fw:
	$(MAKE) -C sw/apps/kws_xip_rt install
	bash scripts/check_fw_layout.sh

gw5a-bitstream: soc-rtl gw5a-fw
	bash scripts/run_gowin_soc_impl.sh

gw5a-probe:
	bash scripts/gw5a_program.sh probe

gw5a-detect-flash:
	bash scripts/gw5a_program.sh detect-flash

gw5a-flash-bitstream:
	bash scripts/gw5a_program.sh flash-bitstream

gw5a-flash-kws:
	FLASH_FW_OFFSET=$(FLASH_FW_OFFSET) bash scripts/gw5a_program.sh flash-kws

gw5a-reboot:
	bash scripts/gw5a_program.sh reboot

monitor:
	python3 scripts/monitor_serial.py --port "$${SERIAL_PORT:?set SERIAL_PORT}" --baud "$(SERIAL_BAUD)"

gw5a-demo-check:
	bash scripts/gw5a_demo_check.sh

play-kws-test:
	bash scripts/play_kws_words.sh

sim-vcs-build: soc-rtl gw5a-fw
	@mkdir -p $(SIM_VCS_DIR)
	cd $(SIM_VCS_DIR) && $(VCS) $(SIM_RTL) $(SIM_TB) \
	  +define+TINYML_SOC_SIM_FAST_QSPI \
	  -top VenusCoreRVTop_kws_xip_rt_tb \
	  -full64 -sverilog +vc +v2k +maxdelays -timescale=1ns/1ps -negdelay +neg_tchk \
	  -o simv

sim-iverilog-build: soc-rtl gw5a-fw
	@mkdir -p $(SIM_IVERILOG_DIR)
	$(IVERILOG) -g2012 -s VenusCoreRVTop_kws_xip_rt_tb \
	  -DTINYML_SOC_SIM_FAST_QSPI \
	  -o $(SIM_IVERILOG_DIR)/simv $(SIM_RTL) $(SIM_TB)

sim-testvector-fw:
	$(MAKE) -C sw/apps/kws_xip_rt install \
	  FW_OUT=$(SIM_DIR)/fw_testvector \
	  KWS_RUN_TESTVECTOR=1 KWS_GPIO_LED_RESULT_MODE=0 HEARTBEAT_EN=0

sim-testvector: sim-iverilog-build sim-testvector-fw
	cd $(abspath .) && $(VVP) $(SIM_IVERILOG_DIR)/simv \
	  +FLASH_HEX=$(SIM_DIR)/fw_testvector/kws_xip_rt_flash.hex \
	  +FLASH_LOAD_OFFSET=400000 +FAST_QSPI_AHB=1 +UART_DIV=432 \
	  +STOP_ON_TV=1 +STOP_ON_KWS=0 +CHECK_NPU_QSPI=1 +TB_TIMEOUT_NS=5000000000 \
	  | tee $(SIM_DIR)/testvector.log
	@grep -Fq '[KWS_XIP_RT_TB] TV PASS' $(SIM_DIR)/testvector.log

sim-vcs-testvector: sim-vcs-build sim-testvector-fw
	cd $(abspath .) && $(SIM_VCS_DIR)/simv \
	  +FLASH_HEX=$(SIM_DIR)/fw_testvector/kws_xip_rt_flash.hex \
	  +FLASH_LOAD_OFFSET=400000 +FAST_QSPI_AHB=1 +UART_DIV=432 \
	  +STOP_ON_TV=1 +STOP_ON_KWS=0 +CHECK_NPU_QSPI=1 +TB_TIMEOUT_NS=5000000000 \
	  | tee $(SIM_DIR)/testvector-vcs.log
	@grep -Fq '[KWS_XIP_RT_TB] TV PASS' $(SIM_DIR)/testvector-vcs.log

sim-pcm-fw:
	$(MAKE) -C sw/apps/kws_xip_rt install \
	  FW_OUT=$(SIM_DIR)/fw_pcm FAST_I2S=1 \
	  KWS_GPIO_LED_RESULT_MODE=0 KWS_VAD_ENABLE=0 \
	  KWS_CONF_SCORE_TH=0 KWS_CONF_MARGIN_TH=0 HEARTBEAT_EN=0

sim-pcm-data:
	@mkdir -p $(SIM_DIR)/pcm
	@if [[ -n "$(SIM_PCM_WAV)" ]]; then \
	  cp "$(SIM_PCM_WAV)" $(SIM_DIR)/pcm/input.wav; \
	else \
	  command -v espeak-ng >/dev/null || { echo '[ERR] espeak-ng is required when SIM_PCM_WAV is unset'; exit 1; }; \
	  espeak-ng -s 135 -w $(SIM_DIR)/pcm/input.wav "$(SIM_PCM_WORD)"; \
	fi
	python3 sw/apps/kws_xip_rt/tools/wav_to_pcm_hex.py \
	  --in-wav $(SIM_DIR)/pcm/input.wav --out-hex $(SIM_DIR)/pcm/input.hex --samples 16000

sim-pcm: sim-iverilog-build sim-pcm-fw sim-pcm-data
	cd $(abspath .) && $(VVP) $(SIM_IVERILOG_DIR)/simv \
	  +FLASH_HEX=$(SIM_DIR)/fw_pcm/kws_xip_rt_flash.hex \
	  +FLASH_LOAD_OFFSET=400000 +FAST_QSPI_AHB=1 +UART_DIV=432 \
	  +PCM_HEX=$(SIM_DIR)/pcm/input.hex +PCM_SAMPLES=16000 \
	  +EXPECTED_LABEL=$(SIM_EXPECTED_LABEL) +STOP_ON_KWS=1 +STOP_ON_TV=0 \
	  +TB_TIMEOUT_NS=5000000000 | tee $(SIM_DIR)/pcm.log
	@grep -Fq '[KWS_XIP_RT_TB] PASS expected=' $(SIM_DIR)/pcm.log

sim-vcs-pcm: sim-vcs-build sim-pcm-fw sim-pcm-data
	cd $(abspath .) && $(SIM_VCS_DIR)/simv \
	  +FLASH_HEX=$(SIM_DIR)/fw_pcm/kws_xip_rt_flash.hex \
	  +FLASH_LOAD_OFFSET=400000 +FAST_QSPI_AHB=1 +UART_DIV=432 \
	  +PCM_HEX=$(SIM_DIR)/pcm/input.hex +PCM_SAMPLES=16000 \
	  +EXPECTED_LABEL=$(SIM_EXPECTED_LABEL) +STOP_ON_KWS=1 +STOP_ON_TV=0 \
	  +TB_TIMEOUT_NS=5000000000 | tee $(SIM_DIR)/pcm-vcs.log
	@grep -Fq '[KWS_XIP_RT_TB] PASS expected=' $(SIM_DIR)/pcm-vcs.log

check: check-public-tree check-scripts env
	@echo '[CHECK] PASS'

clean:
	rm -rf build

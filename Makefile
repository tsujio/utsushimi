# 新規環境の導入手順: firmware/main/README.md「新規環境の構築」
SHELL := /bin/bash
.SHELLFLAGS := -e -o pipefail -c
.DEFAULT_GOAL := help

HOST_TOOLS := $(CURDIR)/build/host-tools
LOCAL_IDF := $(CURDIR)/build/utsushimi-esp-idf

# リポジトリ内に導入済みのツールを使用する。インストールは行わない。
ifneq ($(wildcard $(HOST_TOOLS)/usr/lib/x86_64-linux-gnu),)
export PATH := $(HOST_TOOLS)/usr/bin:$(PATH)
export LD_LIBRARY_PATH := $(HOST_TOOLS)/usr/lib/x86_64-linux-gnu$(if $(LD_LIBRARY_PATH),:$(LD_LIBRARY_PATH))
endif

# 環境変数・コマンドラインのCXXを優先する。
ifeq ($(origin CXX),default)
ifeq ($(shell PATH="$(PATH)" command -v g++ 2>/dev/null),)
ifneq ($(shell test -x "$(HOST_TOOLS)/usr/bin/x86_64-linux-gnu-g++-15" && echo yes),)
CXX := $(HOST_TOOLS)/usr/bin/x86_64-linux-gnu-g++-15 --sysroot=$(HOST_TOOLS) -B$(HOST_TOOLS)/usr/lib/x86_64-linux-gnu
endif
endif
endif

ifeq ($(origin IDF_PATH),undefined)
ifneq ($(wildcard $(LOCAL_IDF)/export.sh),)
IDF_PATH := $(LOCAL_IDF)
export IDF_TOOLS_PATH ?= $(CURDIR)/build/utsushimi-idf-tools
else
IDF_PATH := $(HOME)/esp/esp-idf
endif
endif
PORT ?= /dev/ttyACM0
BAUD ?= 460800
FIRMWARE := $(CURDIR)/firmware

.PHONY: help build install flash monitor menuconfig test
help:
	@printf '%s\n' 'make build                  ビルド' 'make install PORT=/dev/ttyACM0  ビルドして実機へ書き込み' 'make monitor PORT=/dev/ttyACM0  ログ表示（Ctrl+]で終了）' 'make menuconfig             設定' 'make test                   ホスト側の描画・身体反応・音声波形テスト' 'IDF_PATH=... でESP-IDFの場所を指定できます。' '新規環境の導入手順: firmware/main/README.md（新規環境の構築）'

build:
	@source "$(IDF_PATH)/export.sh" >/dev/null; idf.py -C "$(FIRMWARE)" build

# idf.py flashは必要なビルドも実行する。ホストへのインストールではなく実機書き込み。
install flash:
	@source "$(IDF_PATH)/export.sh" >/dev/null; idf.py -C "$(FIRMWARE)" -p "$(PORT)" -b "$(BAUD)" flash

monitor:
	@source "$(IDF_PATH)/export.sh" >/dev/null; idf.py -C "$(FIRMWARE)" -p "$(PORT)" monitor

menuconfig:
	@source "$(IDF_PATH)/export.sh" >/dev/null; idf.py -C "$(FIRMWARE)" menuconfig

test:
	@tmpdir=$$(mktemp -d /tmp/utsushimi-test.XXXXXX); \
	trap 'rm -rf "$$tmpdir"' EXIT; \
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/tests/tone_test.cpp" -o "$$tmpdir/tone"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/display/eye_renderer.cpp" "$(FIRMWARE)/tests/eye_renderer_test.cpp" -o "$$tmpdir/eyes"; \
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/tests/blink_test.cpp" -o "$$tmpdir/blink"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/tests/body_behavior_test.cpp" -o "$$tmpdir/body"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/main/display/eye_renderer.cpp" "$(FIRMWARE)/tests/expression_test.cpp" -o "$$tmpdir/expression"; \
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/touch/contact_detector.cpp" "$(FIRMWARE)/tests/contact_detector_test.cpp" -o "$$tmpdir/contact"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/touch/contact_detector.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/tests/touch_body_test.cpp" -o "$$tmpdir/touch_body"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/touch/contact_detector.cpp" "$(FIRMWARE)/main/touch/petting_detector.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/tests/petting_detector_test.cpp" -o "$$tmpdir/petting"; \
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/tests/input_rms_test.cpp" -o "$$tmpdir/input_rms"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/tests/wake_word_test.cpp" -o "$$tmpdir/wake_word"; \
	"$$tmpdir/wake_word"; \
	"$$tmpdir/input_rms"; "$$tmpdir/tone"; "$$tmpdir/petting"; "$$tmpdir/touch_body"; "$$tmpdir/contact"; "$$tmpdir/expression"; "$$tmpdir/eyes"; "$$tmpdir/blink"; "$$tmpdir/body"; printf '%s\n' '描画・瞬き・身体反応・音声波形・入力RMS・ウェイク視線/PCMテスト成功'

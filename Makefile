SHELL := /bin/bash
.SHELLFLAGS := -e -o pipefail -c
.DEFAULT_GOAL := help

IDF_PATH ?= $(HOME)/esp/esp-idf
PORT ?= /dev/ttyACM0
BAUD ?= 460800
FIRMWARE := $(CURDIR)/firmware

.PHONY: help build install flash monitor menuconfig test
help:
	@printf '%s\n' 'make build                  ビルド' 'make install PORT=/dev/ttyACM0  ビルドして実機へ書き込み' 'make monitor PORT=/dev/ttyACM0  ログ表示（Ctrl+]で終了）' 'make menuconfig             設定' 'make test                   ホスト側の描画・瞬きテスト' 'IDF_PATH=... でESP-IDFの場所を指定できます。'

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
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/display/eye_renderer.cpp" "$(FIRMWARE)/tests/eye_renderer_test.cpp" -o "$$tmpdir/eyes"; \
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/tests/blink_test.cpp" -o "$$tmpdir/blink"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/tests/body_behavior_test.cpp" -o "$$tmpdir/body"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/main/display/eye_renderer.cpp" "$(FIRMWARE)/tests/expression_test.cpp" -o "$$tmpdir/expression"; \
	$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/touch/contact_detector.cpp" "$(FIRMWARE)/tests/contact_detector_test.cpp" -o "$$tmpdir/contact"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/touch/contact_detector.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/tests/touch_body_test.cpp" -o "$$tmpdir/touch_body"; \
	$(CXX) -O2 -std=c++17 -Wall -Wextra -Werror -I"$(FIRMWARE)/main" "$(FIRMWARE)/main/touch/petting_detector.cpp" "$(FIRMWARE)/main/behavior/body_behavior.cpp" "$(FIRMWARE)/main/behavior/automatic_blink.cpp" "$(FIRMWARE)/main/behavior/blink.cpp" "$(FIRMWARE)/tests/petting_detector_test.cpp" -o "$$tmpdir/petting"; \
	"$$tmpdir/petting"; "$$tmpdir/touch_body"; "$$tmpdir/contact"; "$$tmpdir/expression"; "$$tmpdir/eyes"; "$$tmpdir/blink"; "$$tmpdir/body"; printf '%s\n' '描画・瞬き・身体反応テスト成功'

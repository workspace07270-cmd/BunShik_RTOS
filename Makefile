SHELL := /bin/bash

BUILD_DIR ?= build
WSL_GATEWAY_IP := $(shell ip route show default 2>/dev/null | awk 'NR == 1 { print $$3 }')
WSL_DNS_IP := $(shell awk '/^nameserver / { print $$2; exit }' /etc/resolv.conf 2>/dev/null)
WSL_HOST_IP := $(if $(WSL_GATEWAY_IP),$(WSL_GATEWAY_IP),$(WSL_DNS_IP))
BUNSHIK_API_BASE_URL ?= http://$(if $(WSL_HOST_IP),$(WSL_HOST_IP),127.0.0.1):8080

.PHONY: setup configure build run test clean

setup:
	@read -r -p "관리자 아이디: " username; \
	read -r -s -p "관리자 비밀번호: " password; echo; \
	printf 'export BUNSHIK_ADMIN_USERNAME=%q\n' "$$username" > .rtos.env; \
	printf 'export BUNSHIK_ADMIN_PASSWORD=%q\n' "$$password" >> .rtos.env; \
	chmod 600 .rtos.env; \
	echo "관리자 자동 로그인 설정을 저장했습니다."

configure:
	@if [[ -f "$(BUILD_DIR)/CMakeCache.txt" ]]; then \
		cached_source="$$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' \
			"$(BUILD_DIR)/CMakeCache.txt" | head -n 1)"; \
		if [[ -n "$$cached_source" && "$$cached_source" != "$(CURDIR)" ]]; then \
			echo "소스 위치가 변경되어 $(BUILD_DIR) 생성물을 새로 구성합니다."; \
			cmake -E remove_directory "$(BUILD_DIR)"; \
		fi; \
	fi
	cmake -S . -B $(BUILD_DIR) \
		-DBUNSHIK_API_BASE_URL=$(BUNSHIK_API_BASE_URL)

build: configure
	cmake --build $(BUILD_DIR)

run: configure
	@if [[ ! -f .rtos.env ]]; then \
		echo "먼저 make setup을 한 번 실행하세요."; exit 1; \
	fi
	@set -a; source ./.rtos.env; set +a; \
	cmake --build $(BUILD_DIR) --target run

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake --build $(BUILD_DIR) --target clean

SHELL := /bin/bash

BUILD_DIR ?= build
BUNSHIK_API_BASE_URL ?= http://172.23.0.1:8080

.PHONY: setup configure build run test clean

setup:
	@read -r -p "관리자 아이디: " username; \
	read -r -s -p "관리자 비밀번호: " password; echo; \
	printf 'export BUNSHIK_ADMIN_USERNAME=%q\n' "$$username" > .rtos.env; \
	printf 'export BUNSHIK_ADMIN_PASSWORD=%q\n' "$$password" >> .rtos.env; \
	chmod 600 .rtos.env; \
	echo "관리자 자동 로그인 설정을 저장했습니다."

configure:
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

# BunShik RTOS

WSL Ubuntu에서 실행되는 분식집 주문 처리 RTOS 시뮬레이터입니다. 기존
BunShik 키오스크의 관리자 주문 처리 흐름을 C와 POSIX thread로 모델링합니다.

## 현재 구현

- 관리자 CLI에서 주문 접수·상세 조회·상태별 목록 조회
- 관리자가 `접수 -> 조리중 -> 완료` 상태를 직접 변경
- 접수·조리 중 주문 취소와 잘못된 상태 전이 차단
- 우선순위 주문 큐 (같은 우선순위는 FIFO)
- 작업 스레드 기반 `접수 -> 조리중 -> 완료` 처리
- mutex와 condition variable을 이용한 공유 상태 보호
- 기본 스케줄러 테스트

## 빌드와 실행

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/admin/bunshik_admin
```

CLI에서 `help`를 입력하면 명령 목록을 볼 수 있습니다.

```text
receive 101 1 2000 떡볶이 1인분
start 1
detail 1
complete 1
list completed
quit
```

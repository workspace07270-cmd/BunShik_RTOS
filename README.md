# BunShik RTOS

WSL Ubuntu에서 실행되는 분식집 주문 처리 RTOS 시뮬레이터입니다. 기존
BunShik Spring Boot 백엔드와 REST API로 연결하며, 핵심 주문 처리 흐름은
C와 POSIX thread로 모델링합니다.

## 현재 구현

- Spring Boot 관리자 로그인 및 JWT 인증
- 실제 백엔드 주문 목록 조회
- 실제 주문 상태 변경과 취소
- 10초 주기의 백엔드 주문 감시 태스크와 신규 주문 알림
- 활성·접수·조리중·완료·전체 주문 필터
- 접수 후 10분 지연 주문 감지
- 네트워크 지수 재시도, JWT 만료 감지 및 변경 상태 재확인
- 날짜별 관리자 작업 로그와 안전 종료
- 기존 백엔드 API를 이용한 다중 주문 상태 변경과 취소
- 기존 관리자 주문 화면과 같은 날짜·유형·상태 검색 및 알림음
- 관리자가 `접수 -> 조리중 -> 완료` 상태를 직접 변경
- 접수·조리 중 주문 취소와 잘못된 상태 전이 차단
- 작업 스레드 기반 `접수 -> 조리중 -> 완료` 처리
- mutex와 condition variable을 이용한 공유 상태 보호
- 기본 스케줄러 테스트

## 빌드와 실행

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/bunshik_rtos
```

실행 파일은 하나이며, 시작할 때 관리자 모드와 고객 출력 모드 중 하나를
선택합니다. 모드를 명령행에서 바로 지정할 수도 있습니다.

```bash
./build/bunshik_rtos admin
./build/bunshik_rtos customer
```

기본 백엔드 주소는 `http://127.0.0.1:8080`입니다. 다른 주소라면 실행 전에
환경변수를 지정합니다.

```bash
BUNSHIK_API_BASE_URL=http://서버주소:8080 ./build/bunshik_rtos admin
```

고객 모드만 별도 주소를 사용해야 한다면
`BUNSHIK_CUSTOMER_API_BASE_URL`을 지정하거나 URL을 마지막 인자로 넘깁니다.

```bash
./build/bunshik_rtos customer http://서버주소:8080
```

CLI에서 `help`를 입력하면 명령 목록을 볼 수 있습니다.

```text
login 관리자아이디
sync
list active
list received
detail 260
start 1
complete 1
cancel 2
quit
```

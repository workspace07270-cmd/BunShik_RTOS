# BunShik RTOS

WSL Ubuntu에서 FreeRTOS POSIX 포트로 실행되는 분식집 주문 처리 RTOS
시뮬레이터입니다. 기존 BunShik Spring Boot 백엔드와 REST API로 연결하며,
관리자와 고객 출력 기능을 FreeRTOS Task와 Semaphore로 처리합니다.

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
- FreeRTOS 관리자 CLI Task와 주문 감시 Task
- FreeRTOS 고객 연결·출력 감시·인쇄 Worker Task
- FreeRTOS Mutex, Binary Semaphore와 Task Notification 기반 동기화

## 빌드와 실행

프로젝트 최상위 폴더에서 다음 한 줄로 구성·빌드·실행합니다.

```bash
make setup  # 최초 한 번: 관리자 자동 로그인 계정 저장
make run
```

백엔드가 다른 주소라면 실행할 때 지정합니다.

```bash
BUNSHIK_ADMIN_USERNAME=관리자아이디 \
BUNSHIK_ADMIN_PASSWORD=관리자비밀번호 \
make run BUNSHIK_API_BASE_URL=http://서버주소:8080
```

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/bunshik_rtos
```

CMake 구성 후에는 빌드 폴더에서 `make run`으로 빌드와 실행을 한 번에 할 수
있습니다. 기본 백엔드 주소는 `http://172.23.0.1:8080`입니다.

```bash
cd build
make run
```

다른 백엔드 주소를 기본값으로 사용하려면 다시 구성합니다.

```bash
cmake -S . -B build -DBUNSHIK_API_BASE_URL=http://서버주소:8080
```

실행 파일을 시작하면 명령을 입력하지 않습니다. 하나의 FreeRTOS 스케줄러에서
고객 출력 태스크가 자동으로 시작됩니다. 관리자 계정 환경변수까지 설정하면
관리자 자동 로그인·주문 감시 태스크도 동시에 시작됩니다. 주문
상태 변경은 기존 React 관리자 화면에서 수행하며 RTOS는 변경을 자동 감지해
이벤트 Queue로 전달합니다.

기본 백엔드 주소는 `http://127.0.0.1:8080`입니다. 다른 주소라면 실행 전에
환경변수를 지정합니다.

```bash
BUNSHIK_API_BASE_URL=http://서버주소:8080 ./build/bunshik_rtos
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

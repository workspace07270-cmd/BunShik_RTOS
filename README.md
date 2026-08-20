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

`make run`은 WSL의 Windows 호스트 주소를 자동으로 찾아 백엔드에 연결합니다.
팀원마다 자기 Windows에서 백엔드를 실행한다면 별도의 주소를 입력하지 않아도
됩니다. 자동 탐지가 맞지 않거나 다른 PC의 백엔드를 사용한다면 실행할 때 주소를
직접 지정합니다.

```bash
make run BUNSHIK_API_BASE_URL=http://서버주소:8080
```

백엔드와 RTOS를 모두 같은 WSL에서 실행한다면 다음 주소를 사용합니다.

```bash
make run BUNSHIK_API_BASE_URL=http://127.0.0.1:8080
```

백엔드를 Windows에서, RTOS를 WSL에서 실행할 때 자동 연결이 되지 않으면 WSL에서
Windows 호스트 주소를 확인한 뒤 지정합니다.

```bash
ip route show default
make run BUNSHIK_API_BASE_URL=http://default-via-뒤의-IP:8080
```

연결 여부는 RTOS 실행 전에 확인할 수 있습니다.

```bash
curl http://백엔드주소:8080/api/menus
```

Windows에서는 응답하지만 WSL에서는 연결되지 않는다면 Windows 방화벽에서 전체
방화벽을 끄지 말고 TCP 8080 인바운드 규칙만 허용합니다. Spring Boot도 필요하면
`server.address=0.0.0.0`으로 외부 인터페이스의 연결을 받도록 설정합니다.

프로젝트를 다른 폴더나 다른 컴퓨터로 복사했을 때 기존 `build/`의 CMake
경로가 달라도 `make run`이 이를 감지하고 build 생성물만 자동 재구성합니다.

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

다른 백엔드 주소를 사용하려면 다시 구성합니다.

```bash
cmake -S . -B build -DBUNSHIK_API_BASE_URL=http://서버주소:8080
```

실행 파일을 시작하면 명령을 입력하지 않습니다. 하나의 FreeRTOS 스케줄러에서
관리자 자동 로그인·주문 감시 태스크와 고객 연결·출력 감시·인쇄 태스크가 모두
자동으로 시작됩니다. 주문 상태 변경은 기존 React 관리자 화면에서 수행하며,
RTOS는 변경을 자동 감지해 이벤트 Queue로 전달합니다. 종료할 때만 `Ctrl+C`를
누릅니다.

정상 연결되면 다음과 같은 메시지가 출력됩니다.

```text
[AdminOrderPollTask] 관리자 자동 로그인 성공
[BunShik Customer RTOS] 서버 연결 확인 완료: http://백엔드주소:8080
```

## 프로젝트 폴더

```text
config/   FreeRTOS 설정
include/  공개 헤더
src/      RTOS 및 백엔드 연동 구현
tests/    단위·연동 테스트
build/    CMake 빌드 결과물
logs/     실행 중 생성되는 관리자 로그
```

`build/`와 `logs/`는 삭제해도 다시 생성되며 Git에 포함되지 않습니다. 관리자
계정이 저장되는 `.rtos.env`도 보안상 Git에 포함되지 않으므로 팀원마다
`make setup`을 실행해야 합니다. `tests/`는 CMake가 참조하고 있으므로 폴더만
삭제하면 빌드 구성이 실패합니다.

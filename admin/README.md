# BunShik 관리자 RTOS

WSL Ubuntu에서 실행되는 C 기반 관리자 주문 처리 프로그램입니다. 기존
BunShik Spring Boot 백엔드에 REST API로 연결하여 실제 주문을 조회하고
상태를 변경합니다.

## 연결 대상

기존 백엔드 프로젝트 위치:

```text
Windows: C:\BunShik_minjun_backend\bunshik-back
WSL:     /mnt/c/BunShik_minjun_backend/bunshik-back
```

현재 사용 중인 백엔드 주소:

```text
http://172.23.0.1:8080
```

연결하는 관리자 API:

- `POST /api/admin/login`: 관리자 로그인 및 JWT 발급
- `GET /api/admin/orders`: 실제 주문 목록 조회
- `PATCH /api/admin/orders/{id}/status`: 주문 상태 변경
- `PATCH /api/admin/orders/{id}/cancel`: 주문 취소

## 주요 기능

- 관리자 계정으로 Spring Boot 백엔드 로그인
- JWT 인증을 사용한 API 요청
- 실제 주문 목록 동기화
- 10초마다 새 주문 자동 확인
- 신규 활성 주문 알림
- 접수·조리중·완료·전체 주문 필터
- 주문 상태 변경 및 취소
- 네트워크 오류와 연결 복구 안내
- 조회 실패 시 1초, 2초, 4초 간격 재시도
- JWT 만료 감지와 재로그인 안내
- 상태 변경 응답이 불확실할 때 실제 상태 재확인
- mutex를 이용한 관리자 명령과 감시 태스크 동기화

## 빌드

프로젝트 최상위 폴더에서 실행합니다.

```bash
cd ~/BunShik_RTOS
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 실행

현재 백엔드 주소를 환경변수로 지정하여 실행합니다.

```bash
BUNSHIK_API_BASE_URL=http://172.23.0.1:8080 \
./build/admin/bunshik_admin
```

백엔드가 같은 WSL 환경의 8080 포트에서 실행된다면 환경변수를 생략할 수
있습니다. 이 경우 기본 주소는 `http://127.0.0.1:8080`입니다.

## 명령어

### 로그인

```text
login <아이디>
```

명령 실행 후 비밀번호를 화면에 표시하지 않는 별도 입력란이 나타납니다.
로그인에 성공하면 실제 주문을 즉시 가져오고, 이후 10초마다 자동으로
동기화합니다.

### 주문 조회

```text
sync
list
list active
list received
list cooking
list delayed
list completed
list all
detail 260
connection
next
start-next
```

- `sync`: 백엔드 주문을 즉시 다시 가져옵니다.
- `list`, `list active`: 접수 및 조리중 주문만 표시합니다.
- `list received`: 접수 주문만 표시합니다.
- `list cooking`: 조리중 주문만 표시합니다.
- `list delayed`: 접수 후 10분 이상 지난 주문만 표시합니다.
- `list completed`: 완료 주문만 표시합니다.
- `list all`: 백엔드에서 가져온 모든 주문을 표시합니다.
- `detail <주문 ID>`: 주문 메뉴, 수량, 옵션과 세트 구성을 표시합니다.
- `connection`: 서버, 인증, 연결 및 마지막 동기화 상태를 표시합니다.
- `next`: 우선순위와 대기시간으로 다음 처리 주문을 추천합니다.
- `start-next`: 추천 주문 상세를 보여주고 확인 후 조리를 시작합니다.

### 주문 상태 변경

```text
start <백엔드 주문 ID>
complete <백엔드 주문 ID>
cancel <백엔드 주문 ID>
```

예시:

```text
start 196
complete 196
cancel 165
```

이 명령들은 목업 데이터가 아니라 실제 백엔드와 MySQL의 주문 상태를
변경합니다. 명령 실행 후 `yes`를 입력해야 API가 호출됩니다. 테스트
주문인지 확인한 후 실행해야 합니다.

## 주문 상태 규칙

```text
접수 -> 조리중 -> 완료
  \---------\-> 취소
          \---> 취소
```

- `start`: 접수 주문을 조리중으로 변경합니다.
- `complete`: 조리중 주문을 완료로 변경합니다.
- `cancel`: 접수 또는 조리중 주문을 취소합니다.
- 완료·취소 주문은 다시 변경할 수 없습니다.
- 최종 검증은 Spring Boot 백엔드가 수행합니다.

## 지연 주문 우선순위

- 접수 후 10분 미만: 정상
- 접수 후 10분 이상: 지연
- 접수 후 20분 이상: 긴급
- 활성 주문 목록은 긴급, 지연, 정상 순서로 표시됩니다.
- 같은 지연 주문의 자동 경고는 실행 중 한 번만 표시됩니다.

## 소스 구조

```text
admin/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── backend_client.h
│   ├── http_client.h
│   ├── order.h
│   └── scheduler.h
└── src/
    ├── backend_client.c
    ├── http_client.c
    ├── main.c
    ├── order.c
    └── scheduler.c
```

- `main.c`: 관리자 CLI와 10초 자동 감시 태스크
- `backend_client.c`: 로그인, 주문 조회, 상태 변경 API 처리
- `http_client.c`: Ubuntu POSIX socket 기반 HTTP 통신
- `scheduler.c`: 주문 큐, 상태 전이 및 동시성 처리
- `order.c`: 주문 상태 이름과 공통 주문 모델

## 종료

```text
quit
```

감시 태스크를 안전하게 종료한 후 프로그램이 끝납니다.

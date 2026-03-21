# sv_assignment — Controller–Agent 분산 장치 관리 시스템

## 개요

C++14, 직접 설계한 **TCP Wire Protocol** 기반 분산 장치 시뮬레이션 시스템.
Agent N개(≥50 동시 연결)가 HEARTBEAT/STATE를 주기적으로 보고하면,
Controller가 정책 엔진으로 제어 명령(START/STOP/SET_MODE/UPDATE_CONFIG)을 발행한다.

외부 메시지 브로커 없음 — Kafka·ZMQ 없이 POSIX TCP 소켓으로 직접 구현.

```
Agent-001 ──HEARTBEAT/STATE──►
Agent-002 ──HEARTBEAT/STATE──► Controller:9090 ──CMD_SET_MODE(broadcast)──► 모든 Agent
Agent-003 ──HEARTBEAT/STATE──►              └──CMD_STOP(targeted)────────► Agent-001
                                            └──ACK/NACK 수신 및 재시도
```

자세한 아키텍처는 [DESIGN.md](DESIGN.md) 참조.

---

## 저장소 구조

```
sv_assignment/                        ← 메인 git 저장소
├── src/
│   ├── CMakeLists.txt
│   ├── cmake/setup.cmake
│   ├── controller/                   ← Controller 서비스
│   ├── agent/                        ← Agent 서비스
│   └── libs/                         ← sv-libs 단일 git 서브모듈
│       ├── cmake/setup.cmake         ← _add_package_sv_libs()
│       ├── core/                     ← IProtocol, ICommandBus, IStateStore 등
│       └── logger/                   ← ILogger, JSON Lines 출력
├── config/
│   ├── controller_config.json
│   └── agent_config.json
├── docker-compose.yml
├── Makefile
├── .gitmodules
├── scripts/
│   ├── build.sh
│   ├── asan_run.sh
│   └── valgrind_run.sh
└── *.md
```

---

## 의존성

### 시스템 패키지 (Ubuntu 22.04)

```bash
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    nlohmann-json3-dev \
    git \
    valgrind
```

| 패키지 | 버전 | 용도 |
|--------|------|------|
| `cmake` | ≥ 3.14 | `FetchContent_Declare`/`FetchContent_MakeAvailable` 지원 (GoogleTest 등) |
| `nlohmann-json3-dev` | 3.10.5 | JSON 직렬화 (C++14 호환) |
| `valgrind` | - | 메모리 누수 검증 |

> **외부 메시지 브로커 없음**:.
> 네트워크는 POSIX `socket/bind/accept/connect/read/write` 로 직접 구현.

### CMake FetchContent (자동)

| 라이브러리 | 버전 | 용도 |
|-----------|------|------|
| Catch2 | v2.13.10 | 단위 테스트 (C++14 호환) |

### Git 서브모듈

| 경로 | 설명 |
|------|------|
| `src/libs/` | **sv-libs 단일 저장소** — core(IProtocol·ICommandBus·IStateStore·MessagePool) + logger(ILogger·JSON Lines) |
https://github.com/jinwoo-Sync/sv_assignment_core_module
---

## 서브모듈 초기화

```bash
git clone https://github.com/jinwoo-Sync/sv_assignment_core_module sv_assignment
cd sv_assignment
git submodule update --init --recursive
```

> 상세 설명: [SUBMODULE_GUIDE.md](SUBMODULE_GUIDE.md)

---

## 빌드

```bash
cd sv_assignment/src

# Debug (기본)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc)

# Release
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -- -j$(nproc)
```

빌드 결과물:
```
src/build/
├── controller/controller
├── agent/agent
└── tests/
    ├── test_protocol       ← IProtocol encode/decode
    ├── test_command_bus    ← ACK 추적, 재시도 로직
    ├── test_policy_engine  ← 임계값 규칙 평가
    ├── test_state_store    ← dispatch → notify_all
    ├── test_message_pool   ← 풀 할당/해제
    └── test_integration    ← Controller-Agent 상호작용
```

---

## 실행

### 로컬 실행

```bash
# 터미널 1: Controller (기본 포트 9090)
./src/build/controller/controller config/controller_config.json

# 터미널 2-4: Agent 각각
./src/build/agent/agent agent-001 config/agent_config.json
./src/build/agent/agent agent-002 config/agent_config.json
./src/build/agent/agent agent-003 config/agent_config.json
```

### Docker Compose (권장)

```bash
# 전체 스택 기동 (Controller 1 + Agent 3)
docker compose up --build -d

# 로그 (JSON Lines 포맷)
docker compose logs -f | jq .

# 중지
docker compose down
```

Prometheus 메트릭: `http://localhost:9091/metrics`

---

## 테스트

### 단위 테스트

```bash
cmake -S . -B build -DENABLE_ASAN=ON
cmake --build build
LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R LoggerTest --output-on-failure
LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R MemoryPoolTest --output-on-failure
```
### 실패 시나리오 테스트

```bash
# 네트워크 지연 주입 → 타임아웃 로직 검증
ctest --test-dir src/build -R test_timeout --output-on-failure

# 잘못된 페이로드 → NACK 처리 검증
ctest --test-dir src/build -R test_bad_payload --output-on-failure

# 중복 메시지 → 시퀀스 번호 필터링 검증
ctest --test-dir src/build -R test_duplicate_msg --output-on-failure
```

### ASan + UBSan

```bash
bash scripts/asan_run.sh
```

### Valgrind

```bash
bash scripts/valgrind_run.sh
```

---

## Config 핫-리로드 데모

```bash
docker compose up --build -d
docker compose logs -f controller &

# CPU 임계값 낮춤 → 즉시 정책 발동
sed -i 's/"cpu_threshold_pct": 80.0/"cpu_threshold_pct": 40.0/' \
    config/controller_config.json

# ~1초 내 JSON 로그:
# {"lvl":"INFO","comp":"Controller","msg":"Config reloaded"}
# {"lvl":"WARN","comp":"Policy","msg":"SetMode triggered","fields":{"reason":"high_load"}}
```

---

## Makefile 주요 타겟

```bash
make build        # CMake Debug 빌드
make release      # CMake Release 빌드
make test         # ctest 실행
make asan         # ASan/UBSan 빌드 + 테스트
make valgrind     # Valgrind 실행
make up           # docker compose up -d
make down         # docker compose down
make logs         # docker compose logs -f | jq .
make metrics      # curl localhost:9091/metrics
```

---

## 문서 목록

| 문서 | 내용 |
|------|------|
| [DESIGN.md](DESIGN.md) | Wire Protocol 명세, 인터페이스 계층, 시퀀스 다이어그램, 소유권 모델 |
| [PERF.md](PERF.md) | 측정 시나리오, 스크립트, 병목 분석 |
| [OPERATIONS.md](OPERATIONS.md) | JSON Lines 로그 이벤트, Prometheus 메트릭, 알람, Runbook |
| [SUBMODULE_GUIDE.md](SUBMODULE_GUIDE.md) | sv-libs 단일 서브모듈 설정, setup.cmake 패턴 |

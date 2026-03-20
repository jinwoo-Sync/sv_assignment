# sv_assignment — Controller–Agent 분산 장치 관리 시스템

## 1. 개요

여러 Agent가 주기적으로 상태를 보고하고, Controller가 정책에 따라 제어 명령을 내리는 분산 시스템.
C++14, POSIX TCP 소켓(외부 메시지 브로커 없음), Docker Compose 기반.

```
docker compose up --build -d
→ controller × 1  ←──TCP 9090──→  agent × 3
```

## 2. 저장소 구조

```
sv_assignment/
├── src/
│   ├── CMakeLists.txt
│   ├── controller/
│   │   ├── CMakeLists.txt
│   │   ├── Dockerfile
│   │   ├── include/controller/
│   │   └── src/
│   ├── agent/
│   │   ├── CMakeLists.txt
│   │   ├── Dockerfile
│   │   ├── include/agent/
│   │   └── src/
│   └── libs/                    ← sv_assignment_core_module 서브모듈
│       ├── cmake/setup.cmake
│       ├── core/
│       │   ├── CMakeLists.txt
│       │   ├── include/core/
│       │   │   ├── message.hpp
│       │   │   ├── protocol.hpp
│       │   │   ├── agent_connection.hpp
│       │   │   ├── command_bus.hpp
│       │   │   ├── state_store.hpp
│       │   │   ├── policy_engine.hpp
│       │   │   └── message_pool.hpp
│       │   └── src/
│       └── logger/
│           ├── CMakeLists.txt
│           ├── include/logger/
│           └── src/
├── config/
│   ├── controller_config.json
│   └── agent_config.json
├── docs/
├── docker-compose.yml
├── Makefile
├── .gitmodules
└── scripts/
```

## 3. 의존성

| 항목 | 내용 |
|------|------|
| 언어 | C++14 |
| 빌드 | CMake ≥ 3.14 |
| JSON | nlohmann/json 3.10.5 (apt 또는 FetchContent) |
| 테스트 | Catch2 v2.13.10 (FetchContent 자동) |
| 컨테이너 | Docker + Docker Compose |
| 서브모듈 | sv_assignment_core_module (`src/libs/`) |

## 4. 서브모듈 초기화

```bash
git clone --recurse-submodules <repo-url>
# 또는
git submodule update --init --recursive
```

## 5. 빌드 및 실행

```bash
# Docker (권장)
docker compose up --build -d
docker compose logs -f | jq .

# 로컬
cmake -S src -B src/build -DCMAKE_BUILD_TYPE=Debug
cmake --build src/build -- -j$(nproc)
```

## 6. 테스트

```bash
cmake -S src -B src/build -DBUILD_TESTS=ON
cmake --build src/build -- -j$(nproc)
ctest --test-dir src/build --output-on-failure

# ASan/UBSan
bash scripts/asan_run.sh

# Valgrind
bash scripts/valgrind_run.sh
```

## 7. 문서 목록

| 파일 | 내용 |
|------|------|
| README.md | 빌드·실행·테스트 (이 문서) |
| DESIGN.md | 아키텍처·인터페이스·Wire Protocol |
| PERF.md | 성능 목표 및 측정 계획 |
| OPERATIONS.md | 로그·메트릭·운영 |
| SUBMODULE_GUIDE.md | sv_assignment_core_module 서브모듈 설정 |

# sv_assignment — Controller–Agent 분산 장치 관리 시스템

## 개요

`sv_assignment`는 C++14와 `epoll` 기반으로 구축된 Controller–Agent 분산 장치 관리 시스템이다. 단일 Controller가 다수 Agent의 HEARTBEAT/STATE를 수신하고 필요 시 명령을 내려 장치 플릿을 조율한다. 현재 1:50 스케일까지 검증된 비차단 I/O 경로와 공용 라이브러리가 준비되어 있다.

## 현재 구현 상태

- Controller/Agent 모두 단일 `epoll` 이벤트 루프 기반 비차단 TCP 파이프라인.
- `sv_core`: `MemoryPool`, `TcpProtocol`, `StreamBuffer`, `socket_utils`.
- `sv_logger`: JSON Lines 출력(`LoggerFactory`, `LOG_xxx` 매크로).
- 단위 테스트: `LoggerTest` 3건, `MemoryPoolTest` 3건.
- `test_epoll_scale.sh`: 기존 컨테이너 정리 → 빌드 → Controller 1 vs Agent N 기동 → 로그 스트리밍 자동화.

## 의존성 (Ubuntu 22.04)

```bash
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    nlohmann-json3-dev \
    git \
    valgrind \
    docker.io \
    docker-compose
```

| 패키지 | 최소 버전 | 용도 |
|--------|-----------|------|
| `cmake` | 3.14 | 프로젝트 빌드 및 gtest FetchContent |
| `nlohmann-json3-dev` | 3.10 | Protocol payload 직렬화 |
| `docker` / `docker-compose` | 20.10 / 2.0 | Controller/Agent 컨테이너 실행 |
| `valgrind` | - | MemoryPool 등 메모리 검증 |

## 프로젝트 구조

```
sv_assignment/
├── src/
│   ├── controller/        # epoll 서버 (9090)
│   ├── agent/             # 비동기 클라이언트
│   └── libs/
│       ├── core/          # MemoryPool, TcpProtocol, socket_utils
│       └── logger/        # JSON Lines logger
├── config/                # controller/agent 설정
├── tests/                 # unit/integration (gtest)
├── docker/                # Dockerfile, compose
├── scripts/, test_epoll_scale.sh
└── *.md                   # 문서
```

## 빌드 방법

```bash
cd src
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc)

# Release
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -- -j$(nproc)
```

## 테스트

- **단위 테스트 (`sv_logger`, `sv_core`)**
  ```bash
  cmake -S . -B build -DENABLE_ASAN=ON
  cmake --build build
  LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R LoggerTest --output-on-failure
  LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R MemoryPoolTest --output-on-failure
  ```

- **비동기 부하 테스트 (1:N)**
  ```bash
  chmod +x test_epoll_scale.sh
  ./test_epoll_scale.sh 50    # Ctrl+C로 종료 시 자동 정리
  ```

## 실행 (Docker Compose)

```bash
# Controller 1대, Agent 1대
docker compose -f docker/docker-compose.yml up --build

# Controller 1대, Agent 50대
docker compose -f docker/docker-compose.yml up --build --scale agent=50 -d
docker compose -f docker/docker-compose.yml logs -f
```

## 관찰 및 디버깅

- 리소스 모니터링: `docker stats sv-controller --no-stream`.
- 컨테이너 재기동: `run_step0.sh` 또는 `docker compose down && docker compose up`.
- 추가 운영/성능 기록은 `OPERATIONS.md`, `PERF.md` 참고.

## 참고 문서

- [DESIGN.md](DESIGN.md): epoll 아키텍처, 소유권/MemoryPool 모델
- [OPERATIONS.md](OPERATIONS.md): 로그·운영 가이드
- [PERF.md](PERF.md): 성능 측정 시나리오, 빌드/테스트 로그

## 다음 목표

1. Wire Protocol encode/decode 구현 및 런타임 통합 (TcpProtocol 사용 대상 정의).
2. Agent HEARTBEAT + STATE 주기 전송 (cpu/temp/load_avg 보고) 정식 구현.
3. Controller STATE 수신 → AgentStateStore dispatch 파이프 구축.
4. ThresholdPolicyEngine 구현으로 상태 기반 명령 결정.
5. 설정 파일 mtime 폴링 기반 핫 리로드(`loadConfig()` 자동 호출) 완성.

향후 항목은 `TODO` 목록과 커밋 로그에서 지속 추적한다.

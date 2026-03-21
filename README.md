# sv_assignment — Controller–Agent 분산 장치 관리 시스템

## 개요

`sv_assignment`는 C++14와 `epoll` 기반으로 구축된 Controller–Agent 분산 장치 관리 시스템이다. 단일 Controller가 다수 Agent의 연결을 수용하고, Agent는 500ms마다 간단한 메시지를 주고받는 구조를 확인했다. 현재는 1:50 스케일까지 검증된 비차단 I/O 파이프라인에 집중하고 있다.

## 현재 구현 상태

- Controller/Agent 모두 단일 `epoll` 이벤트 루프 기반 비차단 TCP 파이프라인으로 구동된다.
- `sv::TcpProtocol`을 사용해 HELLO/HEARTBEAT/STATE/ACK 프레임을 주고받고, CRC32와 시퀀스 검증이 적용된다.
- Agent는 1초 HEARTBEAT, 3초 STATE 주기를 유지하며 Controller는 각 메시지에 ACK으로 응답한다.
- `test_epoll_scale.sh`는 기존 컨테이너 정리 → 빌드 → Controller 1 vs Agent N 기동 → Frame 로그 스트리밍까지 자동화한다.

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
| `nlohmann-json3-dev` | 3.10 | JSON 처리 준비 (향후 Custom Wire Protocol 통합용) |
| `docker` / `docker-compose` | 20.10 / 2.0 | Controller/Agent 컨테이너 실행 |
| `valgrind` | - | 런타임 디버깅/검증 도구 |

## 프로젝트 구조

```
sv_assignment/
├── src/
│   ├── controller/        # epoll 서버 (9090)
│   ├── agent/             # 비동기 클라이언트
│   └── libs/
│       ├── core/          # 공용 라이브러리 원본 (향후 통합 예정)
│       └── logger/        # 로깅 라이브러리 원본 (향후 통합 예정)
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

`test_epoll_scale.sh`로 Controller 1대와 Agent N대를 동시에 띄워 통신을 확인한다.

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

- [DESIGN.md](DESIGN.md): epoll 기반 동작 구조와 TODO
- [OPERATIONS.md](OPERATIONS.md): 컨테이너 운영 및 리소스 확인 방법
- [PERF.md](PERF.md): 부하 테스트/측정 절차,, 빌드/테스트 로그

## 다음 목표 (요구사항)

### 네트워킹 & 프로토콜
1. Custom Wire Protocol encode/decode를 Controller/Agent 이벤트 루프에 통합하고 HELLO/HEARTBEAT/STATE/CMD/ACK/NACK 흐름 구현.
2. HEARTBEAT(1s)·STATE(3s) 타이머, Controller 헬스체크(3s 타임아웃) + 자동 재연결/재시도/백오프 로직 추가.
3. 브로드캐스트 명령(`CMD_SET_MODE`)과 부분 실패 보상(재시도/로그) 처리, CRC32 등 메시지 무결성 검증.

### 시스템 제어
4. AgentStateStore와 ThresholdPolicyEngine 구현 → 평균 load/온도 임계 초과 시 그룹 모드 변경.
5. Policy/threshold config JSON 핫-리로드(mtime 감시 → `loadConfig()`), 명령 결과 ACK/NACK 추적.

### 메모리/설계 품질
6. `sv_core` MemoryPool/StreamBuffer를 네트워크 경로에 실제 적용하고 소유권/이동语 의미를 문서화, ASan/Valgrind 스크립트 추가.
7. 인터페이스(`IAgentConnection`, `IProtocol`, `ICommandBus`, `IStateStore`, `IPolicyEngine`) 기반 구조 재정비 및 DI 적용.

### 테스트/운영/성능
8. 단위 테스트(프로토콜 codec, 정책 엔진, 타임아웃)와 통합 테스트(Controller↔Agent 정상/실패 시나리오) 추가, fault injection 스크립트 준비.
9. JSON 구조화 로그와 Prometheus 텍스트 메트릭(`sv_agent_alive`, RTT 등) 노출, graceful shutdown 처리.
10. 성능 측정 자동화(라운드트립 P50/P95, CPU/RSS) 및 PERF.md에 수치 기록, TODO 업데이트.

향후 진행 상황은 커밋 메시지와 각 문서의 TODO 절에서 추적한다.

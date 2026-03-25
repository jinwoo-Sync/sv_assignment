# sv_assignment — Controller–Agent 분산 장치 관리 시스템

C++14 + epoll 기반. 단일 Controller가 다수 Agent를 논블로킹 I/O로 관리. 1:50 스케일 검증 완료.

---

## 시나리오 확인

### 빠른 시작 

```bash
./run_init_step.sh   # Controller 1 + agents.json 구성대로 기동
```

별도 터미널에서:

```bash
# 1. 헬스체크 — agent 연결 · HEARTBEAT/STATE 수신 확인
./run_logs.sh | grep -E "Agent connected|HELLO|HEARTBEAT|STATE"

# 2. 명령 브로드캐스트 — CMD_SET_MODE 전송 · ACK 수신 확인
./run_logs.sh | grep -E "CMD_SET_MODE|ACK"

# 3. 정책 발동 — avgLoad 임계치 초과 → 모드 전환 확인
python3 scripts/fault_injector.py load
./run_logs.sh | grep -E "mode change|hot reload"
```

### 시나리오 1 — 정상 플로우

```bash
./run_init_step.sh
./run_logs.sh | grep -E "Agent connected|HELLO|HEARTBEAT|STATE|ACK"
```

### 시나리오 2 — 부분 실패 (NACK)

imu-1이 CMD_SET_MODE를 항상 거부하는 상태로 기동됨 (`docker-compose.yml`에 `FAULT_MODE=nack` 설정).
PolicyEngine이 모드 전환을 감지하면 imu 그룹에 CMD_SET_MODE 전송 → imu가 NACK → 1s 후 retry → fallback.

```bash
./run_init_step.sh
./run_logs.sh | grep -E "NACK|fallback"
```

### 시나리오 3 — 장애/복구

agent 컨테이너를 강제 종료하면 controller가 3s 후 Unhealthy 판정, 10s 초과 시 docker restart.

```bash
./run_init_step.sh
docker stop sv-agent-imu-1
./run_logs.sh | grep -E "Unhealthy|Restarting|Recovered|Agent connected"
```

### 시나리오 4 — 정책 발동

```bash
./run_init_step.sh
python3 scripts/fault_injector.py load   # safe 임계값 5로 낮춤 → 20초 후 원복
./run_logs.sh | grep -E "mode change|hot reload"
```

### 시나리오 5 — 핫-리로드

```bash
./run_init_step.sh
# configs/policy.json 수동 편집 후
./run_logs.sh | grep -E "hot reload"
```

---

## 구현 상태

> `[x]` 완료 / `[-]` 부분 구현 또는 미검증 / `[ ]` 미구현

### 요구 기능

#### 1. 네트워킹 & 분산 처리
- [x] 논블로킹 I/O로 50개 이상 agent 동시 연결
- [x] agent 주기적 HEARTBEAT(1s) 및 STATE 보고
- [x] 메시지 무결성 검사 (CRC32)
- [x] 헬스체크 타임아웃 — 3s 미응답 시 Unhealthy, 10s 초과 시 docker restart 재기동 (3s~10s 자가 회복 대기)
- [ ] 자동 재시도/재연결 (지수 백오프)
- [x] CMD_SET_MODE 브로드캐스트
- [-] CMD_SET_MODE 부분 실패 시 재시도 1회 + 30s backoff 구현 / reason="always" 시 즉시 30s 대기 / 지수 백오프(최대 N회) 미구현

#### 2. 시스템 제어 도메인 로직
- [x] 정책 엔진 — 평균 부하 > 임계치 시 그룹 모드 변경 (5개 그룹, 3모드: performance/normal/safe)
- [x] Policy/Threshold 핫-리로드 (파일 감시, 1s 주기 감지, 무중단 반영)

#### 3. 동적 메모리 관리
- [-] 스마트 포인터 사용 — `unique_ptr<AgentStream>`, `shared_ptr<ILogger>` 적용 / move semantics는 FrameQueue에서만 적용
- [-] 자체 메모리 풀 (메시지 객체 풀링) — 클래스 구현됨, `acquire()/release()` 미구현
- [ ] ASan/UBSan 누수·UB 없음 증빙

#### 4. 객체지향 설계
- [x] IProtocol 인터페이스 분리 (`encode/decode`)
- [x] IFrameHandler 인터페이스 — MessageType별 콜백 분리, void* + 함수 포인터 DI 패턴
- [x] IPolicyEngine 인터페이스 분리 + PolicyEngine 구현
- [x] ILogger 인터페이스 분리 + JsonLogger 구현
- [x] 새로운 명령/메시지 타입 최소 변경으로 추가 가능 (message.h + dispatch + handler 3곳)
- [x] 의존성 역전 — Controller는 추상 인터페이스에 의존, 실제 구현은 주입 (`DESIGN.md § 5`)
- [ ] IStateStore 인터페이스 — AgentStream struct로 대체, 인터페이스 부재
- [ ] PImpl 패턴 — 미적용

#### 5. 테스트 기반 개발
- [x] 단위 테스트 — Logger (싱글턴/레벨 필터/매크로 3케이스 PASS)
- [-] 단위 테스트 — MemoryPool (`available()` 초기화 1케이스 PASS / `acquire()/release()` 미구현으로 관련 테스트 주석)
- [x] 단위 테스트 — PolicyEngine (4케이스 PASS), CMD_SET_MODE ACK 프로토콜 (3케이스 PASS)
- [ ] 단위 테스트 — TcpProtocol encode/decode 라운드트립, SvStreamBuffer TCP 분할 수신 재조립, 타임아웃 로직
- [x] 통합 테스트 (Controller–Agent 상호작용 시나리오) — 시나리오 1~5 PASS (`tests/integration/`)
- [ ] 실패 시나리오 테스트 (네트워크 지연/드랍, 중복 메시지, 잘못된 페이로드, CRC 불일치)

#### 6. 관측 가능성 & 운영성
- [x] 구조화 로그 (JSON Lines) + 로그 레벨
- [ ] Prometheus 메트릭 노출 (연결 수, 평균 RTT, 명령 실패율)
- [ ] graceful shutdown (미전달 명령 flush 후 종료)

---

### 산출물 (Deliverables)

#### 소스 코드
- [x] controller, agent, 공용 라이브러리 (sv_core, sv_logger)

#### 빌드/실행
- [x] `cmake -S . -B build && cmake --build build`
- [x] docker build 스크립트 & docker-compose.yml
- [x] `docker compose up` 으로 controller 1 + agents 기동

#### 테스트
- [x] `ctest` 실행
- [ ] 실패 시나리오 포함
- [x] ASan/UBSan 실행 스크립트

#### 검증 시나리오
- [x] 정상 플로우: agent 기동 → HELLO 수락 → HEARTBEAT/STATE 수집 → ACK 수신
- [x] 정상 플로우: avgLoad 임계치 초과 → PolicyEngine 모드 결정 → CMD_SET_MODE 브로드캐스트 → ACK 수신
- [x] 부분 실패: imu-1(`FAULT_MODE=nack`) → NACK 수신 → 1s 후 retry → 폴백 LOG_ERROR / 나머지 그룹 정상 운영
- [x] 장애/복구: agent disconnect/timeout → controller가 3s 후 docker restart → 재연결
- [x] 정책 발동: `fault_injector.py load`로 policy.json safe 임계값 낮춤 → load 초과 감지 → safe 모드 전환 → 원복 후 performance 복귀
- [x] 핫-리로드: policy.json 변경 → 무중단 반영 확인

---

### 평가 기준 (총 100점)

- [-] 아키텍처/설계 — 인터페이스 분리(IProtocol·IPolicyEngine·IFrameHandler·ILogger 완료), DI·RAII 적용 / PImpl 미적용, IStateStore·IAgentConnection 부재 `20점`
- [-] 코드 품질 — 가독성, 명명, 에러 처리, 스레드/동시성 안전성 `15점`
- [-] 동적 메모리/리소스 관리 — 소유권 명확성, 스마트 포인터/커스텀 allocator, 누수·UB 없음 `15점`
- [-] 프로토콜/네트워크 — 견고한 프레이밍, 재시도/백오프, 타임아웃/헬스체크 `15점`
- [-] 시스템 제어 로직 — 정책 엔진 타당성, 핫-리로드 안정성 `10점`
- [-] 분산 처리 회복력 — 장애/지연 내성, 부분 실패 대응 `10점`
- [-] 테스트/TDD — 커버리지, 실패 시나리오, 테스트 독립성/속도 `10점`
- [-] 빌드/배포/운영 — CMake 구조, Docker 멀티스테이지, 로그/메트릭 `5점`
- [ ] 성능/프로파일링 — 측정/분석/개선 근거 제시 `5점`

---

## 의존성

```bash
sudo apt-get install -y build-essential cmake pkg-config nlohmann-json3-dev git valgrind docker.io docker-compose
```

---

## 빌드

```bash
git clone --recurse-submodules https://github.com/jinwoo-Sync/sv_assignment
```

> `src/libs`가 submodule로 분리되어 있어 `--recurse-submodules` 없이 클론하면 빌드 시 헤더를 찾지 못합니다.
> 이미 클론한 경우: `git submodule update --init --recursive`

```bash
./build.sh Debug    # → bin/Debug/   (단위 테스트 포함)
./build.sh Release  # → bin/Release/ (Docker 배포용)
```

libs 단독 빌드:
```bash
cd src/libs
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc)
```

---

## 실행

```bash
# Controller 1 + agents.json에 정의된 그룹/개수대로 기동
./run_init_step.sh   # camera×2, lidar×1, imu×1, sync_board×1, pc×1

# 로그 확인
./run_logs.sh

# 종료
./run_stop.sh
```

---

## 테스트

```bash
# 전체 단위 테스트
./build.sh Debug
ctest --test-dir build --output-on-failure

# libs 단독 단위 테스트
cd src/libs && cmake -S . -B build && cmake --build build -- -j$(nproc)
ctest --test-dir build --output-on-failure

# 스케일 통합 테스트
./test_epoll_scale.sh 50

# 통합 테스트 (시나리오 1~5)
bash tests/integration/test_scenarios.sh

# 시나리오 3 단독
bash tests/integration/test_scenario3.sh
```

---

## 프로젝트 구조

```
sv_assignment/
├── src/
│   ├── controller/        # epoll 서버 (9090)
│   ├── agent/             # 비동기 클라이언트
│   └── libs/
│       ├── core/          # MemoryPool, TcpProtocol, SvStreamBuffer 등
│       ├── logger/        # sv_logger
│       └── policy/        # IPolicyEngine, PolicyEngine (3모드)
├── configs/               # policy.json (임계치), agents.json (그룹·개수)
├── scripts/               # fault_injector.py, run_sanitizers.sh 등
├── tests/unit/            # gtest 단위 테스트
└── docker/                # Dockerfile, docker-compose.yml
```

---

## 참고 문서

- [DESIGN.md](DESIGN.md): 아키텍처, 클래스 책임, 수신 흐름
- [OPERATIONS.md](OPERATIONS.md): 로그·메트릭·트러블슈팅
- [DECISIONS.md](DECISIONS.md): 설계 결정 이유 기록
- [PERF.md](PERF.md): 성능 목표·측정 절차·결과

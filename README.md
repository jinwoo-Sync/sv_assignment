# sv_assignment — Controller–Agent 분산 장치 관리 시스템

C++14 + epoll 기반. 단일 Controller가 다수 Agent를 논블로킹 I/O로 관리. 1:50 스케일 검증 완료.

---

## 구현 상태

| 시나리오 | 항목 | 상태 |
|---------|------|------|
| 1 | HELLO / HEARTBEAT / STATE / ACK 교환 | 완료 |
| 1 | CRC32 무결성 검증 | 완료 |
| 1 | JSON Lines 로그 (`sv_logger`) | 완료 |
| 1 | CMD_SET_MODE 브로드캐스트 | 미구현 |
| 2 | Agent 헬스체크 & 자동 재연결 | 미구현 |
| 3 | AgentStateStore + ThresholdPolicyEngine | 미구현 |
| 4 | ACK 매칭, Gap Detection, Idempotency | 미구현 |
| 5 | Graceful Shutdown, Prometheus 메트릭 | 미구현 |

---

## 의존성

```bash
sudo apt-get install -y build-essential cmake pkg-config nlohmann-json3-dev git valgrind docker.io docker-compose
```

---

## 빌드

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
# Controller 1 + Agent 1
docker compose -f docker/docker-compose.yml up --build

# Controller 1 + Agent 50
docker compose -f docker/docker-compose.yml up --build --scale agent=50 -d
docker compose -f docker/docker-compose.yml logs -f
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
│       └── logger/        # sv_logger
├── config/                # controller/agent 설정
├── tests/unit/            # gtest 단위 테스트
├── docker/                # Dockerfile, docker-compose.yml
└── scripts/               # build, sanitizer, perf 스크립트
```

---

## 참고 문서

- [DESIGN.md](DESIGN.md): 아키텍처, 클래스 책임, 수신 흐름
- [OPERATIONS.md](OPERATIONS.md): 로그·메트릭·트러블슈팅
- [PERF.md](PERF.md): 성능 목표·측정 절차·결과

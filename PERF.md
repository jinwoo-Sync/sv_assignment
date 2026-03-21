# PERF — 성능 목표 및 측정 계획

## 1. 목표 수치

요구사항 기준 (로컬 Docker, agent × 3):

| ID | 시나리오 | 목표 |
|----|----------|------|
| P1 | 명령 round-trip P50 | < 30 ms |
| P2 | 명령 round-trip P95 | < 100 ms |
| P3 | 50 Agent 동시 연결 시 Controller CPU | < 5 % |
| P4 | Controller 메모리 (50 Agent 정상 상태) | < 50 MB RSS |
| P5 | MessagePool 히트율 (정상 부하) | ≥ 95 % |
| P6 | Config 핫-리로드 반응 시간 | < 1.5 s |

## 2. 측정 방법

### 2.1 Round-trip 지연 (P1, P2)

Agent 가 HEARTBEAT 전송 시 `sent_ms` 기록, ACK 수신 시 `rtt_ms` 를 로그에 포함.

```bash
docker compose logs agent-1 --since 60s \
  | jq -r 'select(.msg == "Heartbeat ACK") | .fields.rtt_ms' \
  | sort -n \
  | awk 'BEGIN{n=0} {v[n++]=$1} END{
      printf "p50=%.1f p95=%.1f p99=%.1f\n",
        v[int(n*0.5)], v[int(n*0.95)], v[int(n*0.99)]}'
```

### 2.2 CPU / 메모리 (P3, P4)

```bash
docker compose up --scale agent=50 -d
sleep 30
docker stats sv_assignment-controller-1 --no-stream \
  --format "CPU={{.CPUPerc}}  MEM={{.MemUsage}}"
```

### 2.3 MessagePool 히트율 (P5)

```bash
curl -s http://localhost:9091/metrics | grep sv_message_pool \
  | awk '/hits/{h=$2} /misses/{m=$2} END{printf "hit=%.1f%%\n", h/(h+m)*100}'
```

### 2.4 Config 핫-리로드 (P6)

```bash
T=$(date +%s%3N)
sed -i 's/"load_avg_threshold": 1.5/"load_avg_threshold": 0.1/' config/controller_config.json
docker compose logs -f controller \
  | jq -r 'select(.msg=="Config reloaded") | .ts' | head -1
```

## 3. 수치 기록표

실측 후 기입:

| ID | 실측값 | 목표 | Pass/Fail |
|----|--------|------|-----------|
| P1 | — | < 30 ms | — |
| P2 | — | < 100 ms | — |
| P3 | — | < 5 % | — |
| P4 | — | < 50 MB | — |
| P5 | — | ≥ 95 % | — |
| P6 | — | < 1.5 s | — |

## 4. 예상 병목

| 병목 | 원인 | 개선 방안 |
|------|------|-----------|
| TCP 지연 | Nagle 알고리즘 | `TCP_NODELAY` 소켓 옵션 |
| mutex 경합 | IStateStore 읽기/쓰기 공유 | 읽기 빈도 높으면 `shared_mutex` 고려 |
| 스레드 과다 | Thread-per-connection | 50개 이상 시 epoll 전환 (`-DENABLE_EPOLL=ON`) |
| MessagePool miss | 풀 용량 부족 | `pool_capacity` 를 예상 동시 메시지 수 × 여유율로 설정 |

## 5. 빌드/테스트 이슈 로그

| 시점 | 명령 | 증상 | 원인 / 조치 |
|------|------|------|--------------|
| 2024-03-21 | `cmake --build build -DENABLE_ASAN=ON` | `tests/unit/test_logger.cpp` 컴파일 실패 (`auto*` 추론 불가) | `LoggerFactory::instance().get()` 이 `std::shared_ptr<ILogger>` 를 반환하는데 테스트/매크로가 `auto*` 로 받으면서 타입 불일치 발생. `auto logger = ...;` 후 `logger.get()` 사용 or 매크로 내부에서 `.get()` 호출하도록 수정 예정. |

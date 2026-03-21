# PERF — 성능 측정 및 검증

`epoll` 기반 Controller–Agent 구조의 CPU/메모리 사용량을 추적한다. 모든 수치는 실측값으로만 채우며, 추가 작업은 TODO 섹션에서 관리한다.

## 1. 성능 목표

| ID | 시나리오 | 목표 |
|----|----------|------|
| P1 | 명령 round-trip P50 | < 30 ms |
| P2 | 명령 round-trip P95 | < 100 ms |
| P3 | Agent 50대 연결 시 Controller CPU | < 5 % |
| P4 | Controller RSS | < 50 MB |

## 2. 측정 환경

- OS: Linux (Docker Engine)
- Controller: 1 인스턴스 (`sv-controller`, port 9090)
- Agent: 1 ~ 50 인스턴스 (`sv-agent`)
- 전송 주기: Agent당 2Hz (500ms)

## 3. 측정 절차

```bash
# 1) 부하 생성
chmod +x test_epoll_scale.sh
./test_epoll_scale.sh 50

# 2) 정상 상태 확보
sleep 30

# 3) 리소스 측정
docker stats sv-controller --no-stream --format "CPU={{.CPUPerc}} MEM={{.MemUsage}}"
docker stats sv-agent --no-stream
```

필요 시 `scripts/perf_bench.sh`로 단일 측정을 자동화할 수 있음.

## 4. 측정 기록

| 시나리오 | 연결 수 | Controller CPU (%) | Controller Mem (MB) | 비고 |
|----------|---------|--------------------|---------------------|------|
| 1v3 | 3 |  |  | 기본 통신 |
| 1v10 | 10 | | | |
| 1v50 | 50 | | | Steady state 기준 |

## 5. 빌드/테스트 이슈 로그

| 시점 | 명령 | 내용 |
|------|------|------|
| 2024-03-21 | `cmake ...` | `test_logger.cpp` 컴파일 오류 수정 |
| 2024-03-21 | `ctest ...` | 단위 테스트 통과 (Logger 3/3) |
| 2024-03-21 | `ctest ...` | MemoryPool 단계별 재구현 및 단위 테스트 시작 (1단계 성공) |
| 2024-03-22 | `docker compose up --build && ./test_epoll_scale.sh 3` | Custom Wire Protocol HELLO/HEARTBEAT/STATE/ACK 교환 및 CRC 검증 확인 |

## 6. 단위 테스트 결과

### 6.1 Logger (`sv_logger`)
- 싱글톤 검증: `LoggerFactory` 인스턴스 고정.
- 로그 레벨 필터링: WARN 이상 설정 시 하위 레벨 억제.
- 안정성: `LOG_XXXX` 연속 호출 시 크래시 없음.

### 6.2 Memory Pool (`sv_core`)
- 1단계: 초기화/가용 개수 검증 (`available()` 일치) 완료.
- 이미지 증빙: ![MemoryPool 재구현 시작](memory_pool재구현_단위테스트재시작.png)

## 7. TODO (성능/최적화)

- Round-trip latency 계측(Controller↔Agent) 자동 스크립트 추가.
- Agent STATE payload 크기에 따른 Throughput 기록.
- ThresholdPolicyEngine 도입 후 CPU 영향 재측정.

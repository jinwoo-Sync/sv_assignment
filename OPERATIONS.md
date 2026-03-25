# OPERATIONS — 운영 가이드

## 1. 로그 확인

로그 형식: JSON Lines
```json
{"ts":"2026-03-24T19:23:12.149Z","lvl":"WARN","component":"Controller","message":"...","fields":{...}}
```

```bash
./run_logs.sh                                                   # 전체 서비스 로그 스트리밍
docker logs -f sv-controller                                    # controller만
docker compose -f docker/docker-compose.yml logs -f controller
```

### 주요 로그 패턴

| 이벤트 | 출처 | lvl | 검색 키워드 |
|--------|------|-----|------------|
| Agent 연결 | controller | INFO | `Agent connected` |
| HELLO 수신 | controller | INFO | `HELLO` |
| HEARTBEAT 수신 | controller | INFO | `HEARTBEAT` |
| STATE 수신 | controller | INFO | `STATE` |
| 그룹 모드 전환 | controller | INFO | `mode change` |
| CMD_SET_MODE 전송 실패 | controller | WARN | `CMD_SET_MODE send failed` |
| NACK 주입 (FAULT_MODE=nack) | agent | WARN | `NACK injected` |
| NACK 수신 (첫 번째) | controller | WARN | `NACK received, will retry in 1s` |
| NACK retry 재전송 | controller | WARN | `NACK retry: resending CMD_SET_MODE` |
| NACK fallback | controller | ERROR | `NACK fallback: giving up` |
| NACK always (30s 대기) | controller | ERROR | `NACK always: blocked 30s` |
| Unhealthy (3s 미응답) | controller | WARN | `Unhealthy` |
| 회복 | controller | INFO | `Recovered` |
| Heartbeat timeout (10s) | controller | WARN | `Heartbeat timeout, force restart` |
| 컨테이너 재시작 | controller | WARN | `Restarting agent container` |
| Policy hot-reload | controller | INFO | `hot reload` |

### NACK 시나리오 로그 예시

```bash
docker logs sv-controller 2>&1 | grep -E "NACK|fallback"
```

```json
{"ts":"...","lvl":"WARN","message":"NACK received, will retry in 1s","fields":{"fd":8,"agent_id":"imu-1"}}
{"ts":"...","lvl":"WARN","message":"NACK retry: resending CMD_SET_MODE","fields":{"fd":8,"agent_id":"imu-1","mode":"safe"}}
{"ts":"...","lvl":"ERROR","message":"NACK fallback: giving up","fields":{"fd":8,"agent_id":"imu-1","last_mode":"safe"}}
```

`will retry` → `resending` 사이는 최소 1초 차이 (1s tick에서 전송).
`resending` → `fallback`은 imu가 즉각 응답하므로 같은 타임스탬프일 수 있음.

---

## 2. 런타임 점검

```bash
docker compose -f docker/docker-compose.yml ps
docker logs sv-controller
```

가동/재가동:
```bash
./run_init_step.sh   # agents.json 기반 그룹·개수로 기동
./run_stop.sh        # 전체 종료
./run_init_step.sh   # 재가동
```

`agents.json` 현재 구성: camera×2, lidar×1, imu×1, sync_board×1, pc×1

---

## 3. 리소스 모니터링

```bash
docker stats sv-controller --no-stream --format "CPU={{.CPUPerc}} MEM={{.MemUsage}}"
docker stats --no-stream
```

> Prometheus `/metrics` 엔드포인트 미구현.

---

## 4. 정책 엔진 임계치 설정

`configs/policy.json` — Controller 실행 중 변경 시 1s 내 자동 핫-리로드:

```json
{ "performance": 20.0, "normal": 50.0, "safe": 70.0 }
```

| 모드 | 진입 조건 | 의미 |
|------|----------|------|
| performance | avgLoad < 20 | 여유 — agent 풀가동 |
| normal | avgLoad ≥ 50 | 보통 부하 |
| safe | avgLoad ≥ 70 | 과부하 — agent 스로틀 다운 |

> **임계 구간 (20~50)**: 현재 모드 유지. 불필요한 모드 진동 방지.

그룹별 에이전트 개수: `configs/agents.json` → `run_init_step.sh`에서 읽어 `--scale` 인수 생성.

---

## 5. 시나리오 실행

전체 자동 실행:
```bash
bash tests/integration/test_scenarios.sh
```

### 시나리오 1 — 정상 플로우

```bash
./run_init_step.sh
docker logs sv-controller 2>&1 | grep -E "Agent connected|HELLO|HEARTBEAT|STATE"
```

### 시나리오 2 — NACK 부분 실패

imu-1이 `FAULT_MODE=nack` 환경변수로 가동되어 CMD_SET_MODE를 항상 거부.
`run_init_step.sh` 실행 시 자동 적용 (docker-compose.yml에 고정).

```bash
./run_init_step.sh
docker logs sv-controller 2>&1 | grep -E "NACK|fallback"
```

### 시나리오 3 — 장애/복구

```bash
./run_init_step.sh
docker stop sv-assignment-imu-1
# 10s 후 controller가 docker restart → 재연결
docker logs sv-controller 2>&1 | grep -E "Restarting|Agent connected"
```

### 시나리오 4 — 정책 발동 (fault injection)

```bash
./run_init_step.sh
python3 scripts/fault_injector.py load
# safe 임계값을 5로 낮춤 → 평균 load(~50)가 즉시 초과 → safe 모드 전환
# 20초 후 원복 → load < 20 시 performance 복귀
docker logs sv-controller 2>&1 | grep -E "mode change|hot reload"
```

### 시나리오 5 — 핫-리로드

```bash
./run_init_step.sh
# configs/policy.json 수정 후 1s 내 자동 반영
docker logs sv-controller 2>&1 | grep "hot reload"
```

---

## 6. 트러블슈팅

| 증상 | 확인 방법 |
|------|----------|
| 모드 전환 안됨 | `docker logs sv-controller \| grep "mode change"` — 로그 없으면 `avgLoad` 임계치 미달 또는 agent STATE 미수신 |
| hot-reload 미동작 | `docker logs sv-controller \| grep "hot reload"` — 로그 없으면 `configs/policy.json` 경로 및 파일 권한 확인 |
| Agent ID 중복 | `docker logs sv-controller \| grep "HELLO"` 로 agent_id 확인 — 중복이면 `COMPOSE_SERVICE_INDEX` 미주입, docker-compose.yml 환경변수 설정 확인 |


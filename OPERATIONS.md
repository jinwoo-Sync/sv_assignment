# OPERATIONS — 운영 가이드

## 1. 로그 확인

로그 형식: JSON Lines (`{"ts":"...","lvl":"INFO","comp":"Controller","msg":"...","fields":{...}}`)

```bash
./run_logs.sh                                      # 전체 서비스 로그 스트리밍
docker compose -f docker/docker-compose.yml logs -f controller
docker compose -f docker/docker-compose.yml logs -f camera
```

### 주요 로그 패턴

| 이벤트 | 검색 키워드 | 예시 |
|--------|------------|------|
| 그룹 모드 전환 | `mode change` | `"group":"camera","from":"safe","to":"performance"` |
| CMD_SET_MODE 전송 | `CMD_SET_MODE` | `"group":"camera","mode":"performance","targets":3` |
| ACK 수신 | `ACK` | `"agent_id":"camera-abc12","mode":"performance"` |
| avgLoad 계산 | `avgLoad` | `"group":"camera","avgLoad":74.73` |

## 2. 런타임 점검

```bash
docker compose -f docker/docker-compose.yml ps
docker logs sv-controller
```

가동/재가동:
```bash
./run_init_step.sh   # agents.json 기반 그룹·개수로 기동 (camera×3 등)
./run_stop.sh        # 전체 종료
./run_init_step.sh   # 재가동
```

## 3. 리소스 모니터링

```bash
docker stats sv-controller --no-stream --format "CPU={{.CPUPerc}} MEM={{.MemUsage}}"
docker stats sv-agent --no-stream
```

> Prometheus `/metrics` 엔드포인트 미구현 — TODO 참고.

## 4. 정책 엔진 임계치 설정

`configs/policy.json` — Controller 이미지 빌드 시 포함됨:

```json
{ "performance": 20.0, "normal": 50.0, "safe": 70.0 }
```

| 모드 | 진입 조건 | 의미 |
|------|----------|------|
| performance | avgLoad < 20 | 여유 — agent 풀가동 |
| normal | avgLoad ≥ 50 | 보통 부하 |
| safe | avgLoad ≥ 70 | 과부하 — agent 스로틀 다운 |

> **dead zone (20~50)**: 현재 모드 유지. 불필요한 모드 진동 방지.

그룹별 에이전트 개수: `configs/agents.json` → `run_init_step.sh` 에서 읽어 `--scale` 인수 생성.

## 5. 트러블슈팅

| 증상 | 확인 방법 |
|------|----------|
| Agent 연결 수 | controller 로그 `"New agent connected"` 메시지 수 |
| 모드 전환 안됨 | `avgLoad` 로그 확인 → 임계치 미달 or 모든 agent STATE 미수신 |
| ACK 안옴 | agent 로그에서 `CMD_SET_MODE` 수신 여부 확인, `sendAck` 호출 여부 확인 |
| FD 오류 | 로그에서 해당 fd 검색 → `close(fd)` 호출 위치 확인 |
| DNS 실패 | agent 로그 `"DNS lookup failed"` 반복 → Compose 네트워크 상태 확인 |
| 모든 agent가 같은 ID | `AGENT_ID` 환경변수 미설정 → docker-compose.yml `AGENT_ID=${HOSTNAME}` 확인 |

## 6. TODO

- Prometheus 지표 노출: `sv_agents_connected`, `sv_rtt_ms`, `sv_cmd_fail_total`
- Config 핫-리로드 이벤트 로그 (`"Config reloaded"`) 추가
- ACK 매칭/재시도 로직 (현재 ACK 수신만 로깅, 미매칭 시 재전송 없음)

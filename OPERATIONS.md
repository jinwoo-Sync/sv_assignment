# OPERATIONS — 로그·메트릭·운영

## 1. 구조화 로그

모든 프로세스는 **stdout** 에 **JSON Lines** 형식으로 출력한다.
Docker Compose 가 수집, `jq` 로 즉시 파싱 가능.

```
{"ts":"<ISO-8601>","lvl":"<LEVEL>","comp":"<Component>","msg":"<message>","fields":{...}}
```

예시:
```json
{"ts":"2024-01-15T10:23:45.123Z","lvl":"INFO","comp":"Controller","msg":"Agent registered","fields":{"agent_id":"agent-001"}}
{"ts":"2024-01-15T10:23:48.456Z","lvl":"WARN","comp":"Policy","msg":"SetMode triggered","fields":{"reason":"high_load","load_avg":1.85}}
{"ts":"2024-01-15T10:23:50.789Z","lvl":"ERROR","comp":"CommandBus","msg":"Send failed","fields":{"agent_id":"agent-003","attempts":3}}
```

로그 레벨: `DEBUG` / `INFO` / `WARN` / `ERROR`

```bash
# 경고 이상만 필터
docker compose logs -f | jq 'select(.lvl == "WARN" or .lvl == "ERROR")'
```

## 2. Prometheus 메트릭

Controller 가 `:9091/metrics` 에 Prometheus 텍스트 포맷 노출 (외부 클라이언트 라이브러리 없음).

```
sv_agent_connections          # 현재 연결 수
sv_agent_alive{agent_id}      # 1=alive, 0=dead
sv_rtt_ms{agent_id}           # HEARTBEAT 왕복 시간
sv_commands_total{type,result} # 명령 발행 수
sv_message_pool_hits_total    # MessagePool 히트
sv_message_pool_misses_total  # MessagePool 미스
```

```bash
curl -s http://localhost:9091/metrics
```

## 3. 주요 알람 포인트

| 조건 | 심각도 |
|------|--------|
| `sv_agent_alive == 0` for > 30s | CRITICAL |
| command timeout rate > 0.1/min | HIGH |
| agent temperature > 75°C | HIGH |
| 재연결 시도 > 3/min | MEDIUM |

## 4. 헬스체크

```bash
# 서비스 상태
docker compose ps

# Agent 생존 확인
curl -s http://localhost:9091/metrics | grep sv_agent_alive
```

## 5. 운영 명령

```bash
# Agent 1개 재시작
docker compose restart agent-1

# 전체 스케일아웃
docker compose up --scale agent=10 -d

# Config 핫-리로드 확인
docker compose logs -f controller | jq 'select(.msg == "Config reloaded")'
```

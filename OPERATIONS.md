# OPERATIONS — 로그·메트릭·운영

## 1. 구조화 로그 (구현 완료)

모든 프로세스는 **stdout** 에 **JSON Lines** 형식으로 출력한다.

```json
{"ts":"2024-01-15T10:23:45.123Z","lvl":"INFO","comp":"Controller","msg":"Agent registered","fields":{"agent_id":"agent-001"}}
```

- **로그 레벨**: `DEBUG`, `INFO`, `WARN`, `ERROR`
- **사용법**: `LoggerFactory` 및 `LOG_XXXX` 매크로 사용.

## 2. 메트릭 수집
(Prometheus 엔드포인트 `:9091/metrics` 구현 예정)

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

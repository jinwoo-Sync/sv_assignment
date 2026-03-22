# OPERATIONS — 운영 가이드

## 1. 로그 확인

로그 형식: JSON Lines (`{"ts":"...","lvl":"INFO","comp":"Controller","msg":"...","fields":{...}}`)

```bash
docker compose -f docker/docker-compose.yml logs -f controller
docker compose -f docker/docker-compose.yml logs -f agent
```

## 2. 런타임 점검

```bash
docker compose -f docker/docker-compose.yml ps
docker logs sv-controller
docker logs sv-agent
```

재기동:
```bash
docker compose -f docker/docker-compose.yml down && docker compose -f docker/docker-compose.yml up --build
```

## 3. 리소스 모니터링

```bash
docker stats sv-controller --no-stream --format "CPU={{.CPUPerc}} MEM={{.MemUsage}}"
docker stats sv-agent --no-stream
```

> Prometheus `/metrics` 엔드포인트 미구현 — TODO 참고.

## 4. 트러블슈팅

| 증상 | 확인 방법 |
|------|----------|
| Agent 연결 수 | controller 로그 `"New agent connected"` 메시지 수 |
| FD 오류 | 로그에서 해당 fd 검색 → `close(fd)` 호출 위치 확인 |
| DNS 실패 | agent 로그 `"DNS lookup failed"` 반복 → Compose 네트워크 상태 확인 |

## 5. TODO

- Prometheus 지표 노출: `sv_agents_connected`, `sv_rtt_ms`, `sv_cmd_fail_total`
- Config 핫-리로드 이벤트 로그 (`"Config reloaded"`) 추가

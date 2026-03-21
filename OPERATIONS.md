# OPERATIONS — 로그·운영 가이드

## 1. 로그 확인

- Controller/Agent 모두 `stdout`에 단순 텍스트 로그를 남김 (`std::cout` 기반).
- 새 연결/수신/종료 이벤트가 발생할 때마다 `"New agent connected"`, `"Received from fd ..."`, `"Agent disconnected"` 등의 메시지가 출력됨.
- 확인 방법:
  ```bash
  docker compose -f docker/docker-compose.yml logs -f controller
  docker compose -f docker/docker-compose.yml logs -f agent
  ```

## 2. 런타임 점검

```bash
# 컨테이너 상태
docker compose -f docker/docker-compose.yml ps

# 특정 컨테이너 로그
docker logs sv-controller
docker logs sv-agent
```

컨테이너 재기동은 `run_step0.sh` 또는 `docker compose down && docker compose up --build`로 수행함.

## 3. 리소스 모니터링

Prometheus `/metrics` 엔드포인트는 아직 제공하지 않으므로 Docker 통계를 직접 확인함.

```bash
docker stats sv-controller --no-stream --format "CPU={{.CPUPerc}} MEM={{.MemUsage}}"
docker stats sv-agent --no-stream
```

## 4. 헬스체크/트러블슈팅

- Agent 연결 수 확인: 컨트롤러 로그에서 `"New agent connected"` 메시지 수로 파악함.
- TCP 종료 처리: 문제가 되는 FD를 로그에서 찾고 컨트롤러 코드에서 `close(fd)` 호출함.
- DNS 실패 시: Agent 로그에 "DNS lookup failed" 반복 시 Compose 네트워크 상태 확인함.

## 5. TODO (운영 영역)

- Prometheus 엔드포인트 및 지표(`sv_agent_alive`, `sv_controller_cmd_latency` 등) 노출.
- Threshold 기반 알람 정책 정의 후 Alertmanager 연동.
- Config 핫-리로드 이벤트 로그(`"Config reloaded"`) 및 검증 절차 추가.

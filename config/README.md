# Config 레퍼런스 및 핫-리로드 데모

## 파일 목록

| 파일 | 사용처 | 설명 |
|------|--------|------|
| `controller_config.json` | Controller | TCP 포트, 헬스체크 주기, 정책 임계값 |
| `agent_config.json` | Agent | Controller 주소, 보고 주기, 시뮬레이터 파라미터 |

Docker Compose 에서 컨테이너에 **read-only 볼륨 마운트**:
```yaml
volumes:
  - ./config/controller_config.json:/etc/sv/controller_config.json:ro
```

---

## controller_config.json 필드

### `network`
| 필드 | 기본값 | 설명 |
|------|--------|------|
| `listen_port` | `9090` | Agent 접속 TCP 포트 |
| `metrics_port` | `9091` | Prometheus metrics HTTP 포트 |

### `health`
| 필드 | 기본값 | 설명 |
|------|--------|------|
| `heartbeat_timeout_ms` | `3000` | 이 시간 내 HEARTBEAT 없으면 dead 처리 |
| `check_interval_ms` | `500` | 헬스체크 폴링 주기 |

### `policy`
| 필드 | 기본값 | 설명 |
|------|--------|------|
| `load_avg_threshold` | `1.5` | 초과 시 CMD_SET_MODE(Maintenance) 브로드캐스트 |
| `temp_threshold_c` | `75.0` | 초과 시 CMD_STOP |

---

## agent_config.json 필드

### `network`
| 필드 | 기본값 | 설명 |
|------|--------|------|
| `controller_host` | `controller` | Controller 호스트 (Docker DNS) |
| `controller_port` | `9090` | Controller TCP 포트 |

### `reporting`
| 필드 | 기본값 | 설명 |
|------|--------|------|
| `heartbeat_interval_ms` | `1000` | HEARTBEAT 전송 주기 |
| `state_interval_ms` | `3000` | STATE 전송 주기 |

---

## 핫-리로드 동작

```
Config 파일 mtime 폴링 (1s 주기)
  → 변경 감지 시 JSON 파싱
  → 성공: PolicyEngine::load_config() 호출 (무중단)
  → 실패: LOG_ERROR, 이전 설정 유지
```

---

## 데모 시나리오

### 시나리오 1: 정책 임계값 변경

```bash
# load_avg 임계값 낮춰서 SetMode 즉시 트리거
sed -i 's/"load_avg_threshold": 1.5/"load_avg_threshold": 0.1/' \
    config/controller_config.json

docker compose logs -f controller | jq 'select(.msg == "Config reloaded" or .msg == "SetMode triggered")'

# 원복
sed -i 's/"load_avg_threshold": 0.1/"load_avg_threshold": 1.5/' \
    config/controller_config.json
```

### 시나리오 2: JSON 오류 → 이전 설정 유지 확인

```bash
echo '{ "broken": ' > config/controller_config.json
docker compose logs --tail=5 controller | jq 'select(.lvl == "ERROR")'
# → Parse error 로그, 기존 정책 그대로 동작

# 원복 (백업 복사)
cp config/controller_config.json.bak config/controller_config.json
```

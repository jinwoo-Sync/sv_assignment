#!/bin/bash
# 통합 테스트 — 시나리오 3: 장애/복구
# 실행: bash tests/integration/test_scenario3.sh

COMPOSE="docker compose -f docker/docker-compose.yml"
SCALE="--scale camera=2 --scale imu=1"

PASS=0
FAIL=0

echo "=== 시나리오 3: 장애/복구 ==="

$COMPOSE down --remove-orphans 2>/dev/null
$COMPOSE up --build -d $SCALE 2>/dev/null
sleep 8

docker stop sv-assignment-imu-1 2>/dev/null
echo "  imu-1 종료 → 15s 대기 (restart 10s + 재연결)"
sleep 15

echo ""
echo "--- 관련 로그 ---"
docker logs sv-controller 2>&1 | grep -E "imu|Restarting|Recovered|Agent connected"

echo ""

if docker logs sv-controller 2>&1 | grep -q "Restarting"; then
    echo "[PASS] docker restart 확인"
    PASS=$((PASS + 1))
else
    echo "[FAIL] docker restart — 'Restarting' 미확인"
    FAIL=$((FAIL + 1))
fi

if docker logs sv-controller 2>&1 | grep -q "Agent connected"; then
    echo "[PASS] 재연결 확인"
    PASS=$((PASS + 1))
else
    echo "[FAIL] 재연결 — 'Agent connected' 미확인"
    FAIL=$((FAIL + 1))
fi

echo ""
echo "==============================="
echo "결과: PASS $PASS / FAIL $FAIL (총 2)"
echo "==============================="

$COMPOSE down 2>/dev/null

#!/bin/bash
# 통합 테스트 — 시나리오 1~5
# 실행: bash tests/integration/test_scenarios.sh

COMPOSE="docker compose -f docker/docker-compose.yml"
SCALE="--scale camera=2 --scale imu=1"
PASS=0
FAIL=0

# ── 시나리오 1 — 정상 플로우 ─────────────────────────────────────
echo "=== 시나리오 1: 정상 플로우 ==="
$COMPOSE down 2>/dev/null
$COMPOSE up --build -d $SCALE 2>/dev/null
sleep 8

if docker logs sv-controller 2>&1 | grep -q "HEARTBEAT"; then
    echo "[PASS] 시나리오 1"; PASS=$((PASS+1))
else
    echo "[FAIL] 시나리오 1"; FAIL=$((FAIL+1))
fi

$COMPOSE down 2>/dev/null; sleep 2

# ── 시나리오 2 — 부분 실패 (NACK) ────────────────────────────────
echo ""
echo "=== 시나리오 2: 부분 실패 (NACK) ==="
$COMPOSE down 2>/dev/null
$COMPOSE up --build -d $SCALE 2>/dev/null
sleep 18

if docker logs sv-controller 2>&1 | grep -q "NACK"; then
    echo "[PASS] 시나리오 2"; PASS=$((PASS+1))
else
    echo "[FAIL] 시나리오 2"; FAIL=$((FAIL+1))
fi

$COMPOSE down 2>/dev/null; sleep 2

# ── 시나리오 3 — 장애/복구 ───────────────────────────────────────
echo ""
echo "=== 시나리오 3: 장애/복구 ==="
$COMPOSE down 2>/dev/null
$COMPOSE up --build -d $SCALE 2>/dev/null
sleep 8

docker stop sv-assignment-imu-1 2>/dev/null
echo "  imu-1 종료 → 15s 대기"
sleep 15

if docker logs sv-controller 2>&1 | grep -q "Restarting"; then
    echo "[PASS] 시나리오 3"; PASS=$((PASS+1))
else
    echo "[FAIL] 시나리오 3"; FAIL=$((FAIL+1))
fi

$COMPOSE down 2>/dev/null; sleep 2

# ── 시나리오 4 — 정책 발동 ───────────────────────────────────────
echo ""
echo "=== 시나리오 4: 정책 발동 ==="
$COMPOSE down 2>/dev/null
$COMPOSE up --build -d $SCALE 2>/dev/null
sleep 8

python3 scripts/fault_injector.py load &
echo "  fault_injector 실행 중 (20s 후 원복)"
sleep 25

if docker logs sv-controller 2>&1 | grep -q "mode change"; then
    echo "[PASS] 시나리오 4"; PASS=$((PASS+1))
else
    echo "[FAIL] 시나리오 4"; FAIL=$((FAIL+1))
fi

$COMPOSE down 2>/dev/null; sleep 2

# ── 시나리오 5 — 핫-리로드 ───────────────────────────────────────
echo ""
echo "=== 시나리오 5: 핫-리로드 ==="
$COMPOSE down 2>/dev/null
$COMPOSE up --build -d $SCALE 2>/dev/null
sleep 8

cp configs/policy.json configs/policy.json.bak
echo '{"performance": 25.0, "normal": 50.0, "safe": 70.0}' > configs/policy.json
echo "  policy.json 수정 → 3s 대기"
sleep 3
cp configs/policy.json.bak configs/policy.json

if docker logs sv-controller 2>&1 | grep -q "hot reload"; then
    echo "[PASS] 시나리오 5"; PASS=$((PASS+1))
else
    echo "[FAIL] 시나리오 5"; FAIL=$((FAIL+1))
fi

$COMPOSE down 2>/dev/null

# ── 결과 요약 ────────────────────────────────────────────────────
echo ""
echo "==============================="
echo "결과: PASS $PASS / FAIL $FAIL (총 5)"
echo "==============================="

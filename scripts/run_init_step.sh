#!/bin/bash
set -e

docker compose -f docker/docker-compose.yml down --remove-orphans 2>/dev/null || true

SCALE=$(python3 - <<'EOF'
import json
d = json.load(open("configs/agents.json"))
print(" ".join("--scale {}={}".format(k, v["count"]) for k, v in d.items()))
EOF
)

docker compose -f docker/docker-compose.yml up --build -d $SCALE
sleep 5

docker logs -f sv-controller

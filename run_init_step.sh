#!/bin/bash
set -e

docker compose -f docker/docker-compose.yml down --remove-orphans 2>/dev/null || true

SCALE=$(python3 -c "
import json
print(' '.join(f'--scale {k}={v}' for k, v in json.load(open('configs/agents.json')).items()))
")

docker compose -f docker/docker-compose.yml up --build -d $SCALE
sleep 5

docker logs sv-controller

#!/usr/bin/env bash
set -e
docker compose -f docker/docker-compose.yml up --scale agent=3 -d
sleep 30
docker stats sv-controller --no-stream --format "CPU={{.CPUPerc}}  MEM={{.MemUsage}}"
docker compose -f docker/docker-compose.yml down

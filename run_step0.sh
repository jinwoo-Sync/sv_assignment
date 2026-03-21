#!/bin/bash

echo "1. Cleaning up existing containers..."
# 관련 컨테이너 및 프로젝트 정리
docker rm -f sv-controller sv-agent 2>/dev/null
docker compose down --remove-orphans

echo "2. Building and starting containers..."
docker compose up --build -d

echo "3. Waiting for communication (5s)..."
sleep 5

echo "4. Checking Controller logs:"
docker logs sv-controller

echo ""
echo "5. Checking Agent logs:"
docker logs sv-agent

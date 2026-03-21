#!/bin/bash

# 인자가 없으면 기본값 1로 설정
AGENT_COUNT=${1:-1}

echo "=========================================================="
echo " [epoll 테스트] Controller 1 vs Agent ${AGENT_COUNT}"
echo " Agent 전송: 2Hz (500ms)"
echo "=========================================================="

# 이전 컨테이너와 네트워크 찌꺼기를 완전히 정리
echo "[1/3] 기존 컨테이너 정리..."
docker compose -f docker/docker-compose.yml down -v > /dev/null 2>&1

# 빌드 및 스케일 아웃 실행 (백그라운드)
echo "[2/3] 빌드 + Agent ${AGENT_COUNT} 스케일 아웃..."
docker compose -f docker/docker-compose.yml up --build --scale agent=$AGENT_COUNT -d

echo "[3/3] 기동 완료, 로그 스트리밍"
echo "=========================================================="
echo " 💡 종료: [Ctrl + C]"
echo "     컨테이너 자동 중지/삭제"
echo "=========================================================="

# Ctrl+C (SIGINT) 또는 SIGTERM 수신 시 모든 컨테이너 종료
trap "echo -e '\n[종료] 컨테이너 안전 종료...'; docker compose -f docker/docker-compose.yml down; exit 0" SIGINT SIGTERM

# 로그 확인 (에이전트와 컨트롤러가 주고받는 내용 확인)
docker compose -f docker/docker-compose.yml logs -f

# sv_assignment — Controller–Agent 분산 장치 관리 시스템

## 개요

C++14 기반 분산 장치 시뮬레이션 시스템.
Agent N개가 HEARTBEAT/STATE를 보고하면 Controller가 제어 명령을 발행한다.

- **현재 진행**: `epoll` 기반 비동기 네트워킹 구축 및 1:50 부하 테스트 진행 중.

---

## 의존성

### 시스템 패키지 (Ubuntu 22.04)

```bash
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    nlohmann-json3-dev \
    git \
    valgrind
```

| 패키지 | 버전 | 용도 |
|--------|------|------|
| `cmake` | ≥ 3.14 | `FetchContent`를 통한 외부 라이브러리(GTest 등) 관리 |
| `nlohmann-json3-dev` | 3.10.5 | JSON 데이터 직렬화 및 파싱 |
| `docker` | ≥ 20.10 | 컨테이너 기반 격리 환경 구축 |
| `docker-compose` | ≥ 2.0 | 다중 컨테이너(Agent 50대 등) 오케스트레이션..? 음.. |
| `valgrind` | - | 메모리 누수 검증 |

---

## 프로젝트 구조

```
sv_assignment/
├── src/
│   ├── controller/      ← 초기 단계 구현 완료
│   ├── agent/           ← 초기 단계 구현 완료
│   └── libs/            ← sv_assignment_core_module (서브모듈)
│       ├── core/        ← MemoryPool 구현 완료
│       └── logger/      ← ILogger, JSON Lines 구현 완료
├── config/              ← 설정 파일
├── tests/               ← 단위 테스트
└── *.md                 ← 문서
```

---

## 빌드

```bash
cd src

# Debug (기본)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc)

# Release
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -- -j$(nproc)
```


## 실행 (Docker Compose)

요구사항 프로세스 배치 부분의 요구사항을 수행하고자 도커 컴포즈로 다중 에이전트를 세팅하는 부분 추가.

```bash
# 1. 기본 실행 (Controller 1대, Agent 1대)
docker compose up --build

# 2. 대규모 부하 테스트 (Controller 1대, Agent 50대)
docker compose up --build --scale agent=50 -d
```

---

## 테스트

### 1. 비동기 부하 테스트 (1:N Scale Test)
단일 컨트롤러가 50개의 에이전트로를 운용시에 cpu 부하 성능 검증
```bash
# Agent 50대를 띄워 테스트 (종료 시 Ctrl+C로 자동 정리)
chmod +x test_epoll_scale.sh
./test_epoll_scale.sh 50
```

### 2. 단위 테스트 (Local Build)
핵심 라이브러리(`sv_logger`, `sv_core`)의 로직을 검증.
```bash
cmake -S . -B build -DENABLE_ASAN=ON
cmake --build build
LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R LoggerTest --output-on-failure
LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R MemoryPoolTest --output-on-failure
```

### 3. 리소스 모니터링
50대 기동 시 컨트롤러의 실제 CPU/메모리 점유율을 확인.
```bash
docker stats
```

---

## 문서 목록
- [COMMON.md](COMMON.md): 공통 코딩 규칙 및 인터페이스 정의
- [DESIGN.md](DESIGN.md): **비동기 네트워킹(epoll) 아키텍처** 및 1:N 상호작용 설계 추가
- [PERF.md](PERF.md): 성능 측정 시나리오 및 결과 기록 양식

- [OPERATIONS.md](OPERATIONS.md): 로그 및 운영 가이드

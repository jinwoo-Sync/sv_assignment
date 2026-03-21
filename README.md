# sv_assignment — Controller–Agent 분산 장치 관리 시스템

## 개요

C++14 기반 분산 장치 시뮬레이션 시스템.
Agent N개가 HEARTBEAT/STATE를 보고하면 Controller가 제어 명령을 발행한다.

- **현재 진행**: `sv_logger` 및 `sv_core` (MemoryPool) 구현 및 단위 테스트 완료.

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
| `cmake` | ≥ 3.14 | `FetchContent_Declare`/`FetchContent_MakeAvailable` 지원 (GoogleTest 등) |
| `nlohmann-json3-dev` | 3.10.5 | JSON 직렬화 (C++14 호환) |
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
cd sv_assignment/src

# Debug (기본)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -- -j$(nproc)

# Release
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -- -j$(nproc)
```

## 테스트

### 단위 테스트

```bash
cmake -S . -B build -DENABLE_ASAN=ON
cmake --build build
LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R LoggerTest --output-on-failure
LSAN_OPTIONS=detect_leaks=0 ctest --test-dir build -R MemoryPoolTest --output-on-failure
```

### 실패 시나리오 테스트

```bash
# 네트워크 지연 주입 → 타임아웃 로직 검증
ctest --test-dir build -R test_timeout --output-on-failure

# 잘못된 페이로드 → NACK 처리 검증
ctest --test-dir build -R test_bad_payload --output-on-failure

# 중복 메시지 → 시퀀스 번호 필터링 검증
ctest --test-dir build -R test_duplicate_msg --output-on-failure
```

### ASan + UBSan

```bash
bash scripts/asan_run.sh
```

---

## 실행 (구현 예정)
(Controller 및 Agent 연동 로직 구현 후 작성)

---

## 문서 목록

- [COMMON.md](COMMON.md): 공통 규칙 및 인터페이스
- [DESIGN.md](DESIGN.md): 시스템 아키텍처 및 설계
- [PERF.md](PERF.md): 성능 목표 및 테스트 결과
- [OPERATIONS.md](OPERATIONS.md): 로그 및 운영 가이드

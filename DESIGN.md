# DESIGN — 아키텍처 및 설계

## 1. 시스템 구성

```
[agent-1]──┐
[agent-2]──┼──TCP 9090 (sv_network)──► [controller]
[agent-N]──┘
```

![epoll 다이어그램](asset/epoll다이어그램.png)

| 컴포넌트 | 역할 |
|---------|------|
| controller | epoll 서버. Agent 연결 수락·관리, 정책 판단, 명령 브로드캐스트 |
| agent | 비차단 connect. HEARTBEAT/STATE 주기 송신 + CMD 수신 |
| libs (`src/libs`) | sv_core (MemoryPool, TcpProtocol 등), sv_logger — 양측 공유 |

---

## 2. 메시지 타입

| Type | 방향 | 주기 | 설명 |
|------|------|------|------|
| HELLO | Agent→Controller | 1회 | 기동 시 등록 |
| HEARTBEAT | Agent→Controller | 1s | 생존 신호 |
| STATE | Agent→Controller | 3s | CPU·온도·모드 메트릭 |
| ACK | 양방향 | 응답 | 동일 seq로 확인 응답 |
| NACK | 양방향 | 응답 | 거부/오류 응답 |
| CMD_START / CMD_STOP / CMD_SET_MODE | Controller→Agent | 명령 시 | 장치 제어 |
| ERROR | 양방향 | 이상 시 | 오류 알림 |

---

## 3. Wire Protocol

**Frame 구조**
```
[Magic 'S''V'][Version 1B][Type 1B][Seq 4B][Length 4B][Payload...][CRC32 4B]
```

| 필드 | 크기 | 설명 |
|------|------|------|
| Magic | 2B | `0x53 0x56` — 시스템 식별자 |
| Version | 1B | 프로토콜 버전 |
| Type | 1B | 메시지 타입 enum |
| Seq | 4B | ACK 매칭용 시퀀스 번호 |
| Length | 4B | Payload 길이 |
| Payload | N B | JSON 직렬화 데이터 |
| CRC32 | 4B | Payload 무결성 검증 |

**payload 소유권:** `decode()` 에서 raw 버퍼 → `Frame::payload(vector)` 1회 복사, 이후 전 구간 `std::move`.

---

## 4. 파일 책임

| 파일 | 역할 |
|:-----|:-----|
| `message.h` | `Frame`, `MessageType` 공통 데이터 구조 |
| `protocol.h` | `IProtocol` 인터페이스 (encode/decode) |
| `tcp_protocol.h/cpp` | `IProtocol` 구현체. magic+version+CRC32 바이너리 프레이밍 |
| `stream_buffer.h/cpp` | TCP 스트림 재조립. Frame 완성 시 콜백 호출 |
| `iframe_handler.h` | `IFrameHandler` — MessageType별 분기 인터페이스 |
| `memory_pool.h/cpp` | 고정 블록 메모리 풀 |
| `socket_utils.h` | `set_nonblocking()`, `send_frame()`, `FrameQueue` |
| `ilogger.h` | `ILogger` 인터페이스 + `LogLevel` enum |
| `logger_factory.h` | `LoggerFactory` 싱글턴 + `LOG_INFO/WARN/ERROR` 매크로 |
| `agent/src/main.cpp` | `AgentSender` + `AgentFrameHandler` + epoll 루프 |
| `controller/src/main.cpp` | `ControllerFrameHandler` + `AgentStream` + epoll 루프 |

---

## 5. epoll 수신 흐름

**Controller** (단일 쓰레드, 별도 처리 쓰레드 없음)
```
epoll_wait()
  └─ recv() → SvStreamBuffer.appendReceivedBytes()
       └─ IFrameHandler::onFrame() [static void* 콜백]
             └─ ControllerFrameHandler::dispatch()
                   ├─ HELLO     → onHello()     → ACK
                   ├─ HEARTBEAT → onHeartbeat() → ACK
                   ├─ STATE     → onState()     → ACK
                   ├─ NACK      → onNack()
                   └─ ERROR     → onError()
```

**Agent**
```
epoll_wait()
  └─ recv() → SvStreamBuffer.appendReceivedBytes()
       └─ IFrameHandler::onFrame() [static void* 콜백]
             └─ AgentFrameHandler::dispatch()
                   ├─ ACK          → onAck()
                   ├─ NACK         → onNack()
                   ├─ CMD_START    → onCmdStart()
                   ├─ CMD_STOP     → onCmdStop()
                   ├─ CMD_SET_MODE → onCmdSetMode()
                   └─ ERROR        → onError()
```

**새 메시지 타입 추가 시 수정 위치: 3곳**
```
1. message.h     MessageType enum 값 추가
2. dispatch()    switch case 추가
3. onXxx()       핸들러 메서드 구현
```

---

## 6. 빌드 구조

| 구분 | 진입점 | 출력 |
|------|--------|------|
| 전체 빌드 | `sv_assignment/CMakeLists.txt` | `bin/{Debug,Release}/` — controller, agent, libs, 단위 테스트 |
| libs 단독 | `src/libs/CMakeLists.txt` | `src/libs/bin/Debug/` — sv_core, sv_logger, 단위 테스트 |

`SV_ALL` 가드: 루트 CMake → `set(SV_ALL ON)` 후 `add_subdirectory(src/libs)`.
libs CMake → `if(NOT DEFINED SV_ALL)` 로 단독 진입점 분기.

```
sv_assignment/
├── CMakeLists.txt          ← 전체 빌드 루트 (SV_ALL ON)
├── bin/{Debug,Release}/    ← controller, agent, *.so, test_*
├── src/
│   ├── controller/
│   ├── agent/
│   └── libs/
│       ├── CMakeLists.txt  ← 단독 빌드 진입점
│       ├── core/
│       ├── logger/
│       └── bin/Debug/      ← 단독 빌드 출력
└── tests/unit/             ← 단위 테스트 단일 출처
```

---

## 7. 단위 테스트 현황

| 모듈 | 테스트 항목 | 상태 |
|------|------------|------|
| MemoryPool | 초기화, acquire/release, pool 고갈 | 완료 |
| Logger | 싱글턴, 레벨 필터링, 매크로 안정성 | 완료 |
| TcpProtocol encode/decode | 라운드트립 검증 | TODO |
| SvStreamBuffer | TCP 분할 수신 재조립 | TODO |
| 실패 시나리오 | 잘못된 페이로드, CRC 불일치 | TODO |

---

## 8. 구현 단계

| Phase | 내용 | 점수 |
|-------|------|------|
| 1 | AgentStateStore (fd, agentId, mode, lastHeartbeat, lastSeq) | 10 |
| 2 | 헬스체크 타임아웃 (3s) + Agent 자동 재연결 (지수 백오프) | 15+15 |
| 3 | CMD_SET_MODE 브로드캐스트 + ThresholdPolicyEngine | 10+10 |
| 4 | Config 핫-리로드 (policy.json mtime 감시) | 10 |
| 5 | ACK 매칭/재시도, Gap Detection, Idempotency, NACK 폴백 | 10 |
| 6 | Graceful Shutdown + Prometheus `/metrics` | 5 |

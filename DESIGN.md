# DESIGN — 아키텍처 및 설계

## 1. 시스템 구성 [DONE && Todo]

```
[agent-1]──┐
[agent-2]──┼──TCP 9090 (sv_network)──► [controller]──:9091 Prometheus metrics
[agent-3]──┘
```

- **controller**: Agent 상태 수집, 헬스체크, 정책 평가, 명령 발행 (Docker 서비스명: `controller`)
- **agent**: 장치 시뮬레이터. 센서값(CPU/온도/부하) 생성, 명령 수신·실행 (Docker 서비스명: `agent`)
- **libs** (`sv_assignment_core_module`): 두 이미지가 공유하는 공용 라이브러리
- **Network**: Docker `bridge` 네트워크(`sv_network`)를 통해 서비스 이름으로 상호 호스트 해석 가능

## 2. Wire Protocol

Length-Prefixed Binary + JSON payload.

```
[magic 2B][version 1B][type 1B][seq uint32 LE][payload_len uint32 LE][payload JSON]
  0x53 0x56   0x01
```

| type | 값 | 방향 |
|------|----|------|
| HELLO | 0x01 | Agent → Controller |
| HEARTBEAT | 0x02 | Agent → Controller |
| STATE | 0x03 | Agent → Controller |
| CMD_START | 0x04 | Controller → Agent |
| CMD_STOP | 0x05 | Controller → Agent |
| CMD_SET_MODE | 0x06 | Controller → Agent |
| CMD_UPDATE_CONFIG | 0x07 | Controller → Agent |
| ACK | 0x08 | 양방향 |
| NACK | 0x09 | 양방향 |
| ERROR | 0x0A | 양방향 |

## 3. 핵심 인터페이스

공통 인터페이스는 `src/libs/core/include/core/` 에 정의한다.

```cpp
// Frame — 통신 단위
struct Frame {
    MessageType          type;
    uint32_t             seq{0};
    std::vector<uint8_t> payload; // JSON bytes
};

// IProtocol — 직렬화/역직렬화
class IProtocol {
public:
    virtual std::vector<uint8_t> encode(const Frame&) const = 0;
    virtual std::unique_ptr<Frame> decode(const uint8_t* data, size_t len,
                                          size_t& consumed) = 0;
};

// IAgentConnection — TCP 연결 추상화
class IAgentConnection {
public:
    virtual bool send(Frame frame) = 0;
    virtual bool is_alive() const = 0;
    virtual int64_t last_heartbeat_ms() const = 0;
    virtual void set_receive_callback(std::function<void(Frame)> cb) = 0;
};

// ICommandBus — 명령 발행 단일 입구 (재시도·백오프 내장)
class ICommandBus {
public:
    virtual void dispatch(const std::string& agent_id, Frame cmd) = 0;
    virtual std::vector<std::string> broadcast(Frame cmd,
                                               const std::string& group = "") = 0;
};

// IStateStore<T> — 상태 중앙 관리 (C# INotifyPropertyChanged)
// dispatch → reduce → notify_all(old, new)
template<typename TState>
class IStateStore {
public:
    virtual void dispatch(const std::string& event_type,
                          const nlohmann::json& payload) = 0;
    virtual TState get_state() const = 0;
    virtual void subscribe(std::function<void(const TState&, const TState&)> cb) = 0;
};

// IPolicyEngine — 상태 변화 시 자동 평가
class IPolicyEngine {
public:
    virtual void evaluate(const ControllerState& old_s,
                          const ControllerState& new_s) = 0;
    virtual void load_config(const nlohmann::json& cfg) = 0;
};
```

## 4. 주요 자료형

```cpp
struct AgentMetrics {
    double cpu_pct{0.0};
    double temperature{0.0};
    double load_avg{0.0};
};

struct AgentInfo {
    std::string  agent_id;
    std::string  group;
    std::string  mode{"Active"};
    AgentMetrics metrics;
    int64_t      last_heartbeat_ms{0};
    bool         alive{false};
};

struct ControllerState {
    std::map<std::string, AgentInfo> agents;
    int64_t updated_at_ms{0};
};
```

## 5. 소유권 모델

```
Controller
 ├── unique_ptr<TcpServer>
 ├── shared_ptr<IStateStore<ControllerState>>
 ├── shared_ptr<ICommandBus>
 │     └── weak_ptr<IAgentConnection>  ← PendingCmd 에서 사용 (소유 안 함)
 ├── unique_ptr<IPolicyEngine>
 └── map<string, shared_ptr<IAgentConnection>>  ← registry

Agent
 ├── unique_ptr<TcpClient>
 └── unique_ptr<DeviceSimulator>
```

### 5.1 Message/Buffer 소유권 (MemoryPool)

- **왜 먼저 하나?** `요구사항.pdf` 에 명시된 “가변 길이 payload 버퍼 수명/소유권 관리 + 메모리 풀” 요구를 바로 만족시키기 위해 가장 먼저 MemoryPool 모듈을 추가 하였다. Wire Protocol encode/decode, CommandBus 재시도 큐, Agent TCP 송신 큐 등 기본적으로 요구사항이 카메라 4k 여러대나 혹은 여러 페이로드로 쪼개서 보내야 하는 데이터를 다루기 위해 만들어진 요구사항이라고 생각되어서 3.동적 메모리 관리 요구사항 및 6. 프로토콜(예시)의 요구: 가변 길이 payload의 버퍼 수명/소유권을 안전하게 처리(복사/이동/뷰 전략 명시). 기능을 먼저 구현하여 검증된 코드로 이후 진행될 부분들을 진행하고자 한다. 

- **어떻게 쓰나?**
  - `MemoryPool::acquire()` → `PooledBuffer` (복사 금지, 이동만 가능) 을 받는다. 함수나 큐 사이를 옮길 때 `std::move` 를 쓰면 “버퍼 주인”이 누군지 바로 보인다.


- **추후 검증**
  - MemoryPool hit/miss 를 Prometheus 메트릭으로 노출해 PERF 목표(P5 ≥ 95%) 확인.
  - Wire Protocol encode/decode 가 완성되면 실제 사용 예/다이어그램을 추가해 다시 정리한다.

### 5.2 구현 주의사항 (진행 중)

- **scope 보장**: MemoryPool 은 Controller(or Agent) 소유 멤버로 고정하고, `PooledBuffer` 는 그 내부에서만 생성/소멸한다. Controller 생명주기 밖에서 버퍼가 남아 있을 수 없도록 구조 자체로 보장한다.
- **현황**: RAII 기반 move-only 핸들 구현 완료 (Resource Acquisition Is Initialization) + 소유권 검증 로직(owns) 반영 중.

## 6. 주요 동작 흐름

### 6.0 기초 통신 확인 [DONE]
현재 구현된 가장 기본적인 통신 구조 / 연결을 먼저 시키고 모듈과 기능을 붙이는 작업 진행 예정.

1. **대기**: **Controller**가 9090 포트를 열고 연결을 기다림.
2. **노크**: **Agent**가 `controller:9090` 주소로 TCP 연결을 시도.
3. **인사**: 연결 성공 시 **Agent**가 `"hello from agent"` 메시지를 전송.
4. **응답**: **Controller**는 메시지 수신 후 `"tcp connection ok"`라고 Respone.
5. **확인**: 양측 로그에 성공 메시지가 기록되면 기초 통신 환경 구축이 완료된 것.

### 6.1 정상 흐름

```
Agent 기동 → HELLO 전송 → Controller 등록
Agent 1s마다 HEARTBEAT → Controller last_heartbeat 갱신
Agent 3s마다 STATE     → IStateStore::dispatch → IPolicyEngine::evaluate
PolicyEngine: load_avg > threshold → ICommandBus::broadcast(CMD_SET_MODE)
Agent CMD_SET_MODE 수신 → ACK 응답 → mode 변경
```

### 6.2 헬스체크 타임아웃

```
HealthMonitor 500ms 주기 폴링
now - last_heartbeat > 3000ms → agent.alive = false
→ IStateStore dispatch(agent_dead) → IPolicyEngine → CMD_STOP (보상)
```

### 6.3 명령 재시도

```
ICommandBus::dispatch → ACK 대기 2s
실패 시 exponential backoff: 1s → 2s → 4s (max 3회)
3회 초과 → LOG_ERROR, failed_agents 에 기록
```

## 7. 설정 핫-리로드

- Controller/Agent 모두 1s 주기로 config 파일 mtime 감지
- 변경 감지 시 mutex 보호하에 정책 임계값 교체 (재시작 없음)

## 8. 단위 테스트 계획

| 테스트 | 검증 내용 |
|--------|----------|
| test_protocol | Frame encode/decode, magic 검증, 부분 수신 |
| test_command_bus | dispatch ACK/NACK, retry backoff |
| test_policy_engine | 임계값 평가, config 로드 |
| test_state_store | dispatch→reduce→notify_all |
| test_message_pool | 풀 할당/해제, 히트율 |
| test_integration | Controller–Agent HELLO/HEARTBEAT/CMD 전체 흐름 |

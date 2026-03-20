# DESIGN — 아키텍처 및 설계

## 1. 시스템 구성

```
[agent-1]──┐
[agent-2]──┼──TCP 9090──► [controller]──:9091 Prometheus metrics
[agent-3]──┘
```

- **controller**: Agent 상태 수집, 헬스체크, 정책 평가, 명령 발행
- **agent**: 장치 시뮬레이터. 센서값(CPU/온도/부하) 생성, 명령 수신·실행
- **libs** (`sv_assignment_core_module`): 두 이미지가 공유하는 공용 라이브러리

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

// IStateStore<T> — 상태 중앙 관리 (C# INotifyPropertyChanged 대응)
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

## 6. 주요 동작 흐름

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

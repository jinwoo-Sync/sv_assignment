# DECISIONS

설계/구현 중 선택 근거 및 수정 이력.

---

## 1. epoll 선택

50개 agent를 스레드 없이 처리. `poll`은 fd 전체를 매번 스캔하므로 agent 수 증가 시 선형 저하. `epoll`은 이벤트 발생 fd만 반환하므로 연결 수와 무관하게 실제 처리 대상만 확인. ET 모드로 EAGAIN까지 드레인하는 방식 적용.

---

## 2. 바이너리 프로토콜 직접 설계

TCP는 스트림이라 패킷 경계 없음. 메시지 끝 식별을 위한 length-prefix 필요. 손상 감지를 위해 CRC32 추가.

```
[Magic 2B][Version 1B][Type 1B][Seq 4B][Length 4B][Payload NB][CRC32 4B]
```

Magic은 스트림 중간 동기화 포인트 탐색용. Seq는 ACK 매칭 및 중복 감지용.

---

## 3. 함수 포인터 + void* 콜백

`SvStreamBuffer`가 Frame 조립 후 상위 로직 호출 필요. 라이브러리가 구현체를 직접 참조하면 의존 방향 역전. 함수 포인터 + `void*`로 pUser에 객체 주소를 넘겨 libs와 구현부(UI/제어 로직)를 완전히 분리하는 방식은 이전부터 사용해온 패턴.

`IFrameHandler::onFrame`을 `static`으로 선언한 이유는 일반 멤버함수는 `this`가 숨겨진 인자로 포함되어 함수 포인터 타입 불일치 발생. `static` 선언 후 객체를 `void*`로 전달, 내부에서 `static_cast`로 복원.

```cpp
static void onFrame(const Frame* frame, void* pUser) {
    static_cast<IFrameHandler*>(pUser)->dispatch(*frame);
}
```

---

## 4. AgentStream struct

`broadcast_set_mode()` 구현 중 문제 발생 — fd는 있는데 해당 agent가 어느 그룹인지 알 수 없는 상황. group 정보가 handler 내부에만 존재하여 외부 접근 불가. `AgentStream` struct에 `agentId`·`group` 필드를 추가하고, 생성자에서 handler에 레퍼런스로 전달하여 HELLO 수신 시 파싱 결과가 struct 필드에 바로 반영되도록 구조 변경.

```cpp
struct AgentStream {
    std::string            agentId;
    std::string            group;
    ControllerFrameHandler handler;
    sv::SvStreamBuffer     stream;

    AgentStream(int fd, sv::IProtocol& protocol)
        : handler(fd, protocol, agentId, group)   // struct 필드를 레퍼런스로 전달
        , stream(protocol, sv::IFrameHandler::onFrame, &handler) {}
};
```

`unique_ptr`로 관리하여 `erase(fd)` 한 번으로 전체 해제.

---

## 5. agentStreamMap을 unordered_map으로

epoll이 fd 반환 시 O(1) 탐색 필요. fd는 커널이 프로세스 내에서 고유하게 할당하는 값이므로 키 중복 없음. `unique_ptr` 값이라 `erase` 시 자동 소멸. fd는 IP 같은 정렬 기준이 없는 식별자이므로 순서 보장이 필요한 `map` 대신 `unordered_map` 선택.

---

## 6. PolicyEngine 연결

**문제 1** — `evaluate()`가 빈 함수라 모드 전환이 전혀 동작하지 않았음. 실제 로직은 `decide()`에만 존재.

→ `evaluate()`가 결정된 모드를 `std::string`으로 반환하도록 수정, main loop에서 반환값이 있을 때만 `broadcast_set_mode()` 호출.

```cpp
std::string mode = policyEngine.evaluate(group, avgLoad);
if (!mode.empty())
    broadcast_set_mode(group, mode, agentStreamMap, tcpProtocolCodec);
```

**문제 2** — `epoll_wait` 타임아웃이 `-1`(무한 대기)이라 STATE 패킷이 없으면 정책 평가 진입 자체가 불가. 

→ 1000ms로 변경하여 agent 패킷 유무와 무관하게 1초마다 평가 실행.

**문제 3** — agent `onCmdSetMode()`에 ACK 전송 코드 없음. CMD_SET_MODE를 보내도 controller 측 로그에 ACK가 찍히지 않아 원인 확인. 
→ `onCmdSetMode()` 내부에 ACK 전송 코드 추가.

---

## 7. AGENT_ID 중복 문제

**문제** — 헬스체크 로그에서 모든 agent의 `agent_id`가 `agent-1`으로 동일하게 찍힘. TCP 패킷 내 ID가 중복이면 어느 agent가 타임아웃됐는지 식별 불가.

**원인** — 세 가지가 겹쳤음. 첫째, 컨테이너에서 프로세스가 PID 1로 기동되어 fallback이 항상 `agent-1`. 둘째, `docker-compose.yml` anchor에 `AGENT_ID=${HOSTNAME}`을 넣었으나 각 서비스에서 `environment`를 재정의하면 anchor 전체가 덮어써짐 — YAML anchor merge는 키 단위 병합이 아님. 셋째, `${HOSTNAME}`은 컨테이너 내부 hostname이 아닌 compose 실행 시점의 호스트 머신 hostname.

**해결** — Docker Compose v2.22+에서 `--scale` 복제 시 각 레플리카에 인덱스를 자동 주입하는 `COMPOSE_SERVICE_INDEX` 사용.

```yaml
- AGENT_ID=camera-${COMPOSE_SERVICE_INDEX:-1}
```

`:-1`은 단독 실행 시 fallback.

---

## 8. 자동 재연결 + NACK 부분 실패 대응

### agent 복구를 controller에서 담당한 이유

agent 프로세스가 죽으면 자기 자신이 복구 코드를 실행할 수 없음. controller가 watchdog 역할을 직접 담당 — heartbeat timeout(10s) 감지 후 Docker socket으로 해당 컨테이너 `docker restart` 요청 → agent 재기동 → HELLO 재등록.

### hot-reload · dead_agents 공통 버그

**문제** — `check_config_mtime()`과 `dead_agents` 재시작 루프를 `num_events == 0` 블록에 배치. `num_events == 0`은 1초 타임아웃 동안 socket 이벤트가 아예 없을 때만 진입하는 블록인데, agent HEARTBEAT가 1s마다 수신되므로 epoll은 거의 항상 `num_events > 0`으로 반환 → 두 로직이 실질적으로 실행되지 않는 버그. epoll의 `num_events == 0` 조건을 주기 타이머처럼 착각한 것이 원인.

**해결** — socket 이벤트와 무관하게 주기 실행이 필요한 로직은 별도 타이머로 분리. `last_mtime_check` 1s 타이머 블록으로 이동하여 이벤트 유무와 무관하게 1s마다 실행.

### NACK retry를 onNack()에서 즉시 전송하지 않은 이유

**문제** — `onNack()`에서 즉시 재전송 시 recv drain 루프 내에서 imu가 즉각 NACK 응답 → 무한루프 발생.

**해결** — hot-reload · dead_agents 버그와 동일하게 1s 타이머 블록으로 위임. `process_nack_retries()`를 1초 tick에서 호출하여 재전송 주기 제한.

### NackState enum 선택

**문제** — NACK 상태를 `nack_wait`·`nack_sent` bool 2개로 관리. 유효 조합이 3가지인데 `(true, true)`는 절대 발생하지 않아 bool 2개는 과잉.

**해결** — 3-state 머신이므로 단일 enum 변수로 교체.

```cpp
enum NackState { NACK_IDLE = 0, NACK_WAIT = 1, NACK_SENT = 2 };
```

### cmd_id 매칭 — stale NACK 무시

**문제** — 이전 CMD_SET_MODE에 대한 NACK이 늦게 도착하면 현재 명령의 NACK으로 잘못 처리될 가능성.

**해결** — 프로토콜 Seq 필드를 활용. Seq는 어느 명령에 대한 응답인지 식별하는 용도로, CMD_SET_MODE 전송마다 `++last_cmd_seq`를 seq로 사용. agent는 받은 seq를 그대로 NACK에 담아 반환. `onNack()`에서 `frame.seq != last_cmd_seq`이면 이전 명령에 대한 NACK으로 무시.

### reason="always" → 즉시 30s 대기

**문제** — `reason="always"`로 거부하는 agent(FAULT_MODE=nack)는 어차피 다음 retry도 NACK을 반환. 무의미한 재전송 반복.

**해결** — controller가 `reason="always"` NACK 수신 시 retry 없이 즉시 해당 agent의 `nack_send_after = now + 30s` 세팅. 이후 `broadcast_set_mode()` 호출 시 controller가 30초가 지나지 않은 agent는 전송 대상에서 제외.

---

## 9. 통합 테스트 — 시나리오 3 장애/복구 디버깅

`tests/integration/test_scenarios.sh` 작성 중 시나리오 3에서 `Restarting` 로그 미확인 현상 발생.

**원인 1**: `docker compose kill imu-1`을 사용했는데 Compose에서 서비스 이름은 `imu`이고 컨테이너 이름은 `sv-assignment-imu-1`으로 다름. 명령이 조용히 무시되어 agent가 실제로 종료되지 않은 채 검증이 진행됐음.

**원인 2**: `docker kill`은 프로세스를 즉시 강제 종료하여 TCP 연결이 바로 끊김. controller는 연결이 끊기는 순간 해당 agent를 재시작 대상으로 등록하는데, heartbeat가 오지 않아서 Unhealthy 판정을 내리는 경로와는 다른 경로. 그래서 `Unhealthy` 로그는 찍히지 않음.

**수정**: `docker stop sv-assignment-imu-1`으로 컨테이너 이름을 직접 지정하는 것으로 스크립트 수정.

---

---

## 설계 과정 기록

### 1단계 — Docker 위에서 단일 agent · 단일 controller 연결 확인

가장 먼저 한 것은 "일단 기초적인 통신 시스템 구축"이었습니다. 요구사항 문서를 보고 폴더 구조를 잡은 뒤, Docker Compose로 agent 컨테이너 하나와 controller 컨테이너 하나를 띄워 TCP 연결이 맺어지는지 확인했습니다. 이 시점에는 프로토콜이나 클래스 설계가 없었고, 소켓 연결이 되고 데이터가 흐르는 것만 검증하는 것이 목표였습니다.

동시에 `src/libs`를 git submodule로 분리했습니다. 이후 controller·agent가 공통으로 쓸 라이브러리(logger, memory_pool, 프로토콜 코덱 등)를 libs에 쌓고, 메인 레포는 빌드·런타임 로직만 담는 구조를 처음부터 의도했습니다.

```
f27582a — 도커 세팅과 agent-client 연결 확인 완료 & git submodule 등록
```

---

### 2단계 — 클래스 설계 (libs 내부 구조 확립)

연결이 확인된 뒤 본격적으로 libs 내부를 설계했습니다.

**Logger**
회사에서 써온 C# 스타일 singleton 패턴을 C++로 옮겼습니다. 가변인자 매크로로 필드를 자유롭게 붙이되, 출력 포맷은 항상 일정한 틀을 유지하도록 `LOG_INFO` / `LOG_ERROR` 매크로를 정리했습니다. 로거 초기화 순서 문제(소켓 생성 실패 시 로거를 못 쓰는 버그)가 나중에 발견되어 init을 소켓 생성 전으로 이동했습니다.

**MemoryPool**
첫 구현이 잘못되어 전면 재구현했습니다. block이라는 표현이 buffer와 혼동되는 것도 발견해 변수명을 buffer로 통일했습니다. 아쉽게도 처음부터 payload 가변 길이를 염두에 두고 MemoryPool을 설계했다면 요구사항의 "자체 메모리 풀 또는 커스텀 allocator를 최소 1개 모듈에서 사용(예: 메시지 객체 풀링)"에 온전히 대응할 수 있었을 것입니다. 다만 전체 프로젝트를 5일(4일 개발 + 1일 문서 정리)로 계획하는 과정에서 딜레이가 예상되었고, 회사 프로젝트를 진행하듯 해당 기능을 scope out하여 나머지 기능의 1차 완성에 집중하는 방향을 선택했습니다.

**TcpProtocol / IProtocol**
요구사항에 명시된 대로 length-prefix + Magic + CRC32 구조의 바이너리 프로토콜을 구현했습니다. 초기에는 `TcpProtocol` 구체 클래스에 직접 의존했는데, 이후 `IProtocol` 인터페이스를 추출하여 DIP를 적용했습니다. libs가 구현체를 모르고 인터페이스만 바라보게 함으로써 테스트 시 mock으로 교체할 수 있을 것으로 예상됩니다.

**SvStreamBuffer · IFrameHandler**
수신 스트림을 Frame 단위로 조립한 뒤 상위 로직을 호출하는 구조가 필요했습니다. 라이브러리가 애플리케이션 코드를 직접 참조하면 의존 방향이 역전되므로, 함수 포인터 + `void*` 콜백 패턴으로 분리했습니다.

**단위 테스트**
gTest를 붙여 logger·memory_pool·PolicyEngine·CMD_SET_MODE ACK 각각을 단독으로 검증할 수 있도록 구성했습니다. submodule 위주의 전체빌드·단독빌드를 CMake flag로 구분하여 libs만 독립 빌드·테스트가 가능하게 만들었습니다.

```
e3f7963 — gTest logger / memory_pool 단위 테스트
c08b29c — TcpProtocol 클래스 계층 분리 및 전면 개편
c122d3d — 단위 테스트용 레플리카 submodule 구조 + 빌드 flag 분리
1609cc1 — IProtocol DIP 적용 (libs)
```

---

### 3단계 — 세부 통신 시스템 구축

libs 기반이 잡히자 controller·agent 간 실제 통신 흐름을 붙였습니다.

**epoll 기반 이벤트 루프**
50개 agent를 스레드 없이 처리하기 위해 epoll ET 모드를 선택했습니다. `poll`은 매번 전체 fd를 스캔하지만, epoll은 이벤트가 발생한 fd만 반환하기 때문입니다.

**AgentStream struct**
`broadcast_set_mode()` 구현 중 fd는 있는데 그 agent의 group을 알 수 없는 문제가 생겼습니다. group은 HELLO를 파싱한 handler 내부에만 있고, 외부에서 읽을 방법이 없었습니다.

람다나 `std::function`을 쓰면 콜백 안에서 `agentId`·`group`을 캡처해 해결할 수 있지만 두 가지 모두 사용하지 않습니다. `SvStreamBuffer` 콜백 타입이 `void (*)(const Frame*, void*)` 순수 C 함수 포인터라 `static` 멤버 함수 + `void*` pUser 외에 선택지가 없었습니다.

그래서 방향을 바꿨습니다. handler가 데이터를 들고 있으면 외부에서 못 읽으니, 데이터를 handler 밖 — `AgentStream` struct 필드 — 에 두고, handler 생성 시 그 필드들의 reference를 넘겼습니다. handler는 HELLO·STATE를 파싱하면 reference를 통해 struct 필드에 직접 쓰고, 외부 코드는 `agentStreamMap[fd]->group`으로 바로 읽습니다. `agentId`·`group`뿐 아니라 `cpu_percent`·`temperature`·`lastHeartbeat`·`nack_state` 등 handler가 갱신하는 모든 상태를 이 방식으로 관리합니다.

`class` 대신 `struct`를 쓴 건 private할 변수가 없고 단순히 데이터를 묶는 역할이라서입니다. fd와 agent의 모든 상태를 한 struct에 묶었기 때문에 `agentStreamMap[fd]` 하나로 group, 부하, NACK 상태 등 모든 정보에 접근할 수 있고, `unique_ptr`로 관리하므로 `erase(fd)` 한 번으로 전체가 소멸됩니다.

**PolicyEngine 연결**
정책 엔진 쪽에서는 세 가지 문제를 확인했습니다. (1) `evaluate()`가 비어 있어 controller가 모드를 바꾸지 못했고, (2) `epoll_wait`를 무한 대기로 호출해 STATE 패킷이 잠시라도 끊기면 정책 평가 루프가 완전히 멈췄으며, (3) agent `onCmdSetMode()`에서 ACK를 보내지 않아 controller가 같은 SET_MODE를 반복 전송했습니다. 이 세 부분을 모두 보완해 정책 평가와 모드 전환이 끊김 없이 이뤄지도록 정리했습니다.

**파일 구조 분리**
agent-client 코드가 길어지면서 `.cpp` + `.h`로 분리했습니다.

```
654d106 — AgentStream에 agentId/group 추가 및 HELLO 파싱
7443925 — evaluate() 완성 / epoll_wait 1000ms / policy.json 3-mode
077f11e — agent disconnect → docker restart
```

---

### 4단계 — 요구사항에 맞는 시나리오 구축

통신 흐름이 완성된 뒤 요구사항의 4가지 시나리오를 하나씩 구현·검증했습니다.

**시나리오 1 — 정상 모드 전환**
5개 그룹을 `--scale`로 띄우고 그룹별 평균 부하에 따라 모드 전환을 확인했습니다. `agents.json`으로 그룹 구성을 선언적으로 관리하고, 기동 스크립트가 이를 읽어 `--scale` 파라미터를 자동 생성합니다.

**시나리오 2 — NACK 부분 실패 + fallback**
`FAULT_MODE=nack` agent가 항상 NACK을 반환하도록 fault injector를 만들었습니다. `onNack()`에서 즉시 재전송하면 recv drain 루프 안에서 무한루프가 발생해, 재전송 로직을 1s tick 쪽으로 옮겼습니다. NACK 상태는 bool 2개에서 `NackState` enum으로 교체하고, `reason="always"` 또는 retry 후 재실패 시 즉시 30s backoff도 추가했습니다. retry 시 seq를 올려 재전송하는데, 이전 CMD의 NACK이 늦게 도착하면 현재 CMD의 NACK으로 잘못 처리돼 엉뚱한 backoff로 빠질 수 있어 seq가 다르면 버리도록 했습니다.

일반 NACK은 "지금 당장은 못 받겠다"는 뜻이라 1s 후 retry가 의미 있습니다. 반면 `reason="always"`는 agent가 "앞으로도 계속 거부할 것"을 명시적으로 알려주는 것이라 retry 자체가 낭비입니다. controller가 이를 받으면 retry를 건너뛰고 바로 30s backoff로 넘어갑니다.

```
일반 NACK:  CMD → NACK → 1s 대기 → retry → NACK → 30s backoff
always:     CMD → NACK(always) → 즉시 30s backoff
```

**시나리오 3 — 장애 agent 자동 재시작**
agent가 죽으면 스스로 복구할 수 없으니 controller가 watchdog 역할을 맡았습니다. heartbeat 10s 타임아웃 감지 후 `docker restart`로 해당 컨테이너를 재기동합니다. 테스트 중 `docker compose kill imu-1`이 조용히 무시되는 버그(서비스 이름 vs 컨테이너 이름 혼동)와, `dead_agents` 재시작 루프를 `num_events == 0` 블록에 두어 heartbeat가 오는 한 실질적으로 실행되지 않는 버그를 발견해 수정했습니다.

**시나리오 4 — hot-reload**
`mtime` 폴링으로 `policy.json` 변경을 감지해 재로드합니다. 변경 감지 누락 버그를 수정했고, `fault_injector.py`로 임계값을 조작해 모드 전환을 확인했습니다.

**AGENT_ID 중복**
헬스체크 로그에서 모든 agent ID가 `agent-1`로 찍히는 문제가 있었습니다. PID 1 fallback, YAML anchor merge 덮어쓰기, `${HOSTNAME}`이 호스트 머신 hostname인 것 세 가지가 겹친 원인이었고, `COMPOSE_SERVICE_INDEX`로 해결했습니다.

```
41993e7 — agent 단일 서비스 → 5그룹 구성
ed336ac — AGENT_ID 중복 수정 (COMPOSE_SERVICE_INDEX)
8f8f892 — policy.json 변경 감지 버그 수정 + fault_injector.py
e0479f4 — cmd_id 매칭 / reason 파싱 / 30s backoff
f51884a — heartbeat·dead_agents 타이머 블록 이동 (hotfix)
```

---

## 미구현 항목

시간 부족으로 구현하지 못한 항목들을 기록합니다.

### 실패 케이스 단위 테스트

현재 단위 테스트는 정상 경로(happy path)만 검증하고 있습니다. CRC32 불일치 수신, Magic 불일치로 인한 스트림 재동기화, 연결 중간에 프로세스가 죽었을 때의 partial frame 처리 등 실패 경로에 대한 테스트가 없습니다. 특히 `SvStreamBuffer`의 드레인 루프는 EAGAIN까지 읽는 구조라 경계 조건이 많은데, 이 부분을 gTest로 재현하는 케이스를 작성하지 못했습니다.

### payload 가변 길이 지원

현재 프로토콜은 payload 길이를 헤더의 `Length` 필드로 선언하고 있지만, 실제 payload 버퍼는 고정 크기로 잡혀 있습니다. 센서 데이터처럼 메시지마다 크기가 달라지는 경우를 제대로 다루려면 수신 시점에 `Length`를 읽은 뒤 그 크기만큼 동적으로 버퍼를 할당하는 구조가 필요합니다. 이 부분은 설계만 해두고 구현하지 못했습니다.

### payload 가변 길이를 위한 MemoryPool 연동

libs에 MemoryPool은 구현돼 있지만 고정 크기 할당만 지원합니다. 가변 길이 payload를 지원하는 순간 매번 `malloc`/`free`를 호출하게 되고, 크기가 제각각인 할당·해제가 반복되면 메모리 파편화가 쌓여 성능이 저하됩니다. MemoryPool을 제대로 연동하려면 payload 크기에 따라 맞는 풀을 골라 쓰는 로직이 필요한데 그 부분이 미완성입니다. 지금은 payload가 고정 크기라 문제없습니다.

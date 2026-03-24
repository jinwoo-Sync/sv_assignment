#include "tcp_protocol.h"
#include "message.h"
#include <gtest/gtest.h>
#include <string>

// CMD_SET_MODE → ACK 왕복 검증
// 목적: controller가 CMD_SET_MODE를 보내고 agent가 ACK를 돌려보내는
//       프로토콜 레이어가 올바르게 동작하는지 확인

static sv::Frame encodeAndDecode(sv::TcpProtocol& protocol, const sv::Frame& src) {
    std::vector<uint8_t> bytes = protocol.encode(src);
    sv::Frame out;
    size_t consumed = 0;
    protocol.decode(bytes.data(), bytes.size(), consumed, out);
    return out;
}

// 1. CMD_SET_MODE 프레임 encode/decode 왕복
TEST(CmdSetModeAckTest, CmdSetModeFrameRoundTrip) {
    sv::TcpProtocol protocol;

    sv::Frame cmd;
    cmd.type = sv::MessageType::CMD_SET_MODE;
    cmd.seq  = 42;
    const std::string payload = "{\"mode\":\"performance\"}";
    cmd.payload.assign(payload.begin(), payload.end());

    sv::Frame decoded = encodeAndDecode(protocol, cmd);

    EXPECT_EQ(decoded.type, sv::MessageType::CMD_SET_MODE);
    EXPECT_EQ(decoded.seq,  42u);
    EXPECT_EQ(std::string(decoded.payload.begin(), decoded.payload.end()), payload);
}

// 2. ACK 프레임 encode/decode 왕복
TEST(CmdSetModeAckTest, AckFrameRoundTrip) {
    sv::TcpProtocol protocol;

    sv::Frame ack;
    ack.type = sv::MessageType::ACK;
    ack.seq  = 42;
    const std::string payload = "{\"agent_id\":\"camera-1\",\"mode\":\"performance\"}";
    ack.payload.assign(payload.begin(), payload.end());

    sv::Frame decoded = encodeAndDecode(protocol, ack);

    EXPECT_EQ(decoded.type, sv::MessageType::ACK);
    EXPECT_EQ(decoded.seq,  42u);
    EXPECT_EQ(std::string(decoded.payload.begin(), decoded.payload.end()), payload);
}

// 3. agent가 CMD_SET_MODE의 seq를 ACK에 그대로 복사하는지 검증
//    (controller가 seq로 ACK 매칭하는 전제 조건)
TEST(CmdSetModeAckTest, AckSeqMirrorsCmdSeq) {
    sv::TcpProtocol protocol;
    const uint32_t cmdSeq = 99;

    // controller → agent: CMD_SET_MODE(seq=99)
    sv::Frame cmd;
    cmd.type = sv::MessageType::CMD_SET_MODE;
    cmd.seq  = cmdSeq;
    const std::string cmdPayload = "{\"mode\":\"performance\"}";
    cmd.payload.assign(cmdPayload.begin(), cmdPayload.end());
    sv::Frame receivedCmd = encodeAndDecode(protocol, cmd);

    // agent → controller: ACK(seq=receivedCmd.seq)
    sv::Frame ack;
    ack.type = sv::MessageType::ACK;
    ack.seq  = receivedCmd.seq;   // onCmdSetMode()에서 frame.seq를 그대로 사용
    const std::string ackPayload = "{\"agent_id\":\"camera-1\",\"mode\":\"performance\"}";
    ack.payload.assign(ackPayload.begin(), ackPayload.end());
    sv::Frame receivedAck = encodeAndDecode(protocol, ack);

    EXPECT_EQ(receivedAck.type, sv::MessageType::ACK);
    EXPECT_EQ(receivedAck.seq,  cmdSeq); // seq가 보존되어야 매칭 가능
}

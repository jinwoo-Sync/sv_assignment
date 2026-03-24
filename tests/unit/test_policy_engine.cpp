#include "PolicyEngine.h"
#include <gtest/gtest.h>

// 임계값: safe=20, normal=50, performance=70 (기본값)
// 그룹 첫 진입 시 기본 모드: "safe"

// 1. avgLoad가 performance 임계값 이상이면 "performance" 반환
TEST(PolicyEngineTest, AbovePerformanceThresholdReturnsPerformance) {
    sv::PolicyEngine engine;
    EXPECT_EQ(engine.evaluate("camera", 75.0), "performance");
}

// 2. 동일 모드 재진입 시 빈 문자열 반환 (중복 broadcast 방지)
TEST(PolicyEngineTest, SameModeReturnsEmpty) {
    sv::PolicyEngine engine;
    engine.evaluate("camera", 75.0);               // → performance
    EXPECT_EQ(engine.evaluate("camera", 80.0), ""); // 동일 모드 → 변화 없음
}

// 3. dead zone(20~50) 구간에서는 현재 모드 유지
TEST(PolicyEngineTest, DeadZoneKeepsCurrentMode) {
    sv::PolicyEngine engine;
    engine.evaluate("camera", 60.0);                // → normal
    EXPECT_EQ(engine.evaluate("camera", 35.0), ""); // dead zone → 변화 없음
}

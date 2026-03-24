#include "memory_pool.h"
#include <gtest/gtest.h>

using namespace sv;

// 1단계: 초기화 확인
TEST(MemoryPoolTest, InitialAvailableCountMatchesBufferCount) {
    const size_t bufferSize = 1024;
    const size_t bufferCount = 5;
    MemoryPool pool(bufferSize, bufferCount);
    
    EXPECT_EQ(pool.available(), bufferCount);
}

// // 2단계: 빌려오기 및 자동 반환 확인 (추후 재검증 예정)
// TEST(MemoryPoolTest, AcquireAndAutoRelease) {
//     MemoryPool pool(1024, 2);
//
//     {
//         auto buf = pool.acquire();// 버퍼 하나 빌림
//         EXPECT_TRUE(buf);
//         EXPECT_EQ(pool.available(), 1); // 남은 개수 1개
//         // 빌려온 버퍼에 데이터 써보기 (크래시 여부 확인)
//         buf.data()[0] = 0xFF;
//     } // buf가 여기서 소멸됨 -> 자동으로 풀에 반납되어야 함
//
//     EXPECT_EQ(pool.available(), 2); // 다시 2개로 복구됨
// }
//
// // 2단계: 풀 고갈 시 처리 확인 (추후 재검증 예정)
// TEST(MemoryPoolTest, ExhaustionReturnsEmptyBuffer) {
//     MemoryPool pool(1024, 1);
//
//     auto buf1 = pool.acquire();
//     EXPECT_TRUE(buf1);
//
//     auto buf2 = pool.acquire();
//     EXPECT_FALSE(buf2); // 더 이상 빌릴 수 없음
// }

#include "memory_pool.h"
#include <gtest/gtest.h>

using namespace sv;

TEST(MemoryPoolTest, InitialAvailableCountMatchesBufferCount) {
    const size_t bufferSize = 1024;
    const size_t bufferCount = 5;
    MemoryPool pool(bufferSize, bufferCount);
    
    EXPECT_EQ(pool.available(), bufferCount);
    EXPECT_EQ(pool.bufferSize(), bufferSize);
}

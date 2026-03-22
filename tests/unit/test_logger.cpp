#include <gtest/gtest.h>
#include "logger_factory.h"

TEST(LoggerTest, InitAndGet) {
    sv::LoggerFactory::instance().init(sv::LogLevel::DEBUG);
    EXPECT_NE(sv::LoggerFactory::instance().get(), nullptr);
}

TEST(LoggerTest, LogLevelFilter) {
    sv::LoggerFactory::instance().init(sv::LogLevel::WARN);
    std::shared_ptr<sv::ILogger> logger = sv::LoggerFactory::instance().get();
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->getLevel(), sv::LogLevel::WARN);
}

TEST(LoggerTest, MacrosDoNotCrash) {
    sv::LoggerFactory::instance().init(sv::LogLevel::DEBUG);
    EXPECT_NO_FATAL_FAILURE({
        LOG_DEBUG("Test", "debug message", "{}");
        LOG_INFO("Test", "info message", "{}");
        LOG_WARN("Test", "warn message", "{}");
        LOG_ERROR("Test", "error message", "{}");
    });
}

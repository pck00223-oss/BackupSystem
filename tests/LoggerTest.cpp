// LoggerTest.cpp - 日志级别与文件输出接口测试
#include <gtest/gtest.h>

#include "TestEnv.h"
#include "TestUtil.h"

#include "core/Logger.h"

using namespace backup;

TEST(Logger, LevelFilteringAndFileOutput) {
    testenv::LoggerGuard loggerGuard;
    testenv::TempDir dir(L"logger");
    const std::wstring logFile = dir.file(L"test.log");

    Logger& log = Logger::instance();
    log.init(logFile);
    log.setLevel(LogLevel::Error);
    log.info(L"Unit", L"info-should-be-filtered");
    log.error(L"Unit", L"error-should-appear");

    const std::string content = testutil::readFile(logFile);
    EXPECT_NE(content.find("error-should-appear"), std::string::npos);
    EXPECT_EQ(content.find("info-should-be-filtered"), std::string::npos);
    EXPECT_NE(content.find("[ERROR]"), std::string::npos);
}

TEST(Logger, InitEmptyClosesFile) {
    testenv::LoggerGuard loggerGuard;
    testenv::TempDir dir(L"logger2");
    const std::wstring logFile = dir.file(L"test.log");

    Logger& log = Logger::instance();
    log.init(logFile);
    log.error(L"Unit", L"first");
    log.init(L"");  // 关闭文件
    log.error(L"Unit", L"second-after-close");

    const std::string content = testutil::readFile(logFile);
    EXPECT_NE(content.find("first"), std::string::npos);
    EXPECT_EQ(content.find("second-after-close"), std::string::npos);
}

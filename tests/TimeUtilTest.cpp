// TimeUtilTest.cpp - 时间工具测试
#include <gtest/gtest.h>

#include "core/TimeUtil.h"

using namespace backup;

TEST(ParseHHMM, Valid) {
    int hour = -1, minute = -1;
    EXPECT_TRUE(parseHHMM(L"00:00", hour, minute));
    EXPECT_EQ(hour, 0);
    EXPECT_EQ(minute, 0);

    EXPECT_TRUE(parseHHMM(L"12:34", hour, minute));
    EXPECT_EQ(hour, 12);
    EXPECT_EQ(minute, 34);

    EXPECT_TRUE(parseHHMM(L"23:59", hour, minute));
    EXPECT_EQ(hour, 23);
    EXPECT_EQ(minute, 59);
}

TEST(ParseHHMM, Invalid) {
    int hour = -1, minute = -1;

    // 范围越界
    EXPECT_TRUE(!parseHHMM(L"24:00", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"23:60", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"-1:30", hour, minute));

    // 格式错误（长度/分隔符/非数字）
    EXPECT_TRUE(!parseHHMM(L"9:00", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"20:0", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"20;00", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"20:0a", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"abc", hour, minute));
    EXPECT_TRUE(!parseHHMM(L"", hour, minute));
}

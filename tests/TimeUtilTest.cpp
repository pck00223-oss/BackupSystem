// TimeUtilTest.cpp - 时间工具测试
#include "TestFramework.h"

#include "core/TimeUtil.h"

using namespace backup;

TEST(ParseHHMM_Valid) {
    int hour = -1, minute = -1;
    CHECK(parseHHMM(L"00:00", hour, minute));
    CHECK_EQ(hour, 0);
    CHECK_EQ(minute, 0);

    CHECK(parseHHMM(L"12:34", hour, minute));
    CHECK_EQ(hour, 12);
    CHECK_EQ(minute, 34);

    CHECK(parseHHMM(L"23:59", hour, minute));
    CHECK_EQ(hour, 23);
    CHECK_EQ(minute, 59);
}

TEST(ParseHHMM_Invalid) {
    int hour = -1, minute = -1;

    // 范围越界
    CHECK(!parseHHMM(L"24:00", hour, minute));
    CHECK(!parseHHMM(L"23:60", hour, minute));
    CHECK(!parseHHMM(L"-1:30", hour, minute));

    // 格式错误（长度/分隔符/非数字）
    CHECK(!parseHHMM(L"9:00", hour, minute));
    CHECK(!parseHHMM(L"20:0", hour, minute));
    CHECK(!parseHHMM(L"20;00", hour, minute));
    CHECK(!parseHHMM(L"20:0a", hour, minute));
    CHECK(!parseHHMM(L"abc", hour, minute));
    CHECK(!parseHHMM(L"", hour, minute));
}

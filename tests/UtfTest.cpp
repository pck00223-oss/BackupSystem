// UtfTest.cpp - 编码与字符串比较接口测试
#include <gtest/gtest.h>

#include "core/Utf.h"

using namespace backup;

TEST(Utf, RoundTripChineseAndEmoji) {
    const std::wstring wide = L"D:\\资料\\我的 文件\\emoji_\U0001F600.txt";
    const std::string utf8 = wideToUtf8(wide);
    EXPECT_FALSE(utf8.empty());
    EXPECT_EQ(utf8ToWide(utf8), wide);
}

TEST(Utf, EmptyAndAscii) {
    EXPECT_TRUE(wideToUtf8(L"").empty());
    EXPECT_TRUE(utf8ToWide("").empty());
    EXPECT_EQ(wideToUtf8(L"abc"), std::string("abc"));
    EXPECT_EQ(utf8ToWide("abc"), std::wstring(L"abc"));
}

TEST(Utf, CaseInsensitiveCompare) {
    EXPECT_EQ(wcsicmpSafe(L"ABC", L"abc"), 0);
    EXPECT_LT(wcsicmpSafe(L"abc", L"abd"), 0);
    EXPECT_GT(wcsicmpSafe(L"abd", L"abc"), 0);
    EXPECT_LT(wcsicmpSafe(L"ab", L"abc"), 0);
}

TEST(Utf, StartsWithNoCase) {
    EXPECT_TRUE(startsWithNoCase(L"Docs\\Sub\\a.txt", L"docs"));
    EXPECT_FALSE(startsWithNoCase(L"docs", L"docs2"));
    EXPECT_TRUE(startsWithNoCase(L"abc", L""));
}

// ResidualUtilTest.cpp - 崩溃残留命名识别的接口测试
#include <gtest/gtest.h>

#include "core/ResidualUtil.h"

using namespace backup;

TEST(ResidualUtil, ParseOldResidual) {
    EXPECT_EQ(parseOldResidual(L"a.txt.baktmp.old"), L"a.txt");
    EXPECT_EQ(parseOldResidual(L"dir\\a.txt.baktmp.old12"), L"dir\\a.txt");
    EXPECT_EQ(parseOldResidual(L"a.txt.baktmp.oldx"), L"");
    EXPECT_EQ(parseOldResidual(L"a.txt.baktmp"), L"");
    EXPECT_EQ(parseOldResidual(L".baktmp.old"), L"");
}

TEST(ResidualUtil, IsTempResidual) {
    EXPECT_TRUE(isTempResidual(L"a.txt.baktmp"));            // 旧格式
    EXPECT_TRUE(isTempResidual(L"a.txt.baktmp.123"));        // 新格式（单段）
    EXPECT_TRUE(isTempResidual(L"a.txt.baktmp.12345.0"));    // 新格式（pid.counter）
    EXPECT_TRUE(isTempResidual(L".baktmp"));
    EXPECT_FALSE(isTempResidual(L"a.txt.baktmp.abc"));
    EXPECT_FALSE(isTempResidual(L"a.txt.baktmp.1.2.3"));
    EXPECT_FALSE(isTempResidual(L"normal.txt"));
}

TEST(ResidualUtil, IsCryptoTempResidual) {
    EXPECT_TRUE(isCryptoTempResidual(L"enc1A2B.tmp"));
    EXPECT_TRUE(isCryptoTempResidual(L"dec00ff.TMP"));
    EXPECT_FALSE(isCryptoTempResidual(L"enc1A2B.tmp2"));
    EXPECT_FALSE(isCryptoTempResidual(L"encZZZZ.tmp"));
    EXPECT_FALSE(isCryptoTempResidual(L"xyz1A2B.tmp"));
}

TEST(ResidualUtil, IsResidualName) {
    EXPECT_TRUE(isResidualName(L"a.txt.baktmp"));
    EXPECT_TRUE(isResidualName(L"a.txt.baktmp.old"));
    EXPECT_TRUE(isResidualName(L"enc1A2B.tmp"));
    EXPECT_FALSE(isResidualName(L"a.txt"));
}

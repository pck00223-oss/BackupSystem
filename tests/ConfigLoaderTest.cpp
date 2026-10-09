// ConfigLoaderTest.cpp - 配置文件解析接口测试
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "TestEnv.h"

#include "app/ConfigLoader.h"
#include "core/Utf.h"

using namespace backup;

namespace {

bool hasWarning(const std::vector<std::wstring>& warnings, const std::wstring& needle) {
    return std::any_of(warnings.begin(), warnings.end(), [&needle](const std::wstring& w) {
        return w.find(needle) != std::wstring::npos;
    });
}

}  // namespace

TEST(ConfigLoader, ParsesAllSupportedKeys) {
    const std::string sourceLine = "source=" + wideToUtf8(L"D:\\资料\\源目录");
    const std::string targetLine = "target=" + wideToUtf8(L"D:\\资料\\备份");
    const std::vector<std::string> lines = {
        "# comment",
        sourceLine,
        targetLine,
        "type=inc",
        "include_ext=cpp,.H",
        "exclude_ext=.tmp,.log",
        "include_path=docs",
        "exclude_path=temp",
        "min_size=1K",
        "max_size=2M",
        "skip_empty=yes",
        "overwrite_restore=true",
        "keep_snapshots=3",
        "modified_within_days=7",
        "no_equals_sign_line_is_ignored",
    };

    BackupConfig cfg;
    std::vector<std::wstring> warnings;
    ASSERT_TRUE(ConfigLoader::parseLines(lines, cfg, warnings));

    EXPECT_EQ(cfg.sourcePath, std::wstring(L"D:\\资料\\源目录"));
    EXPECT_EQ(cfg.targetPath, std::wstring(L"D:\\资料\\备份"));
    EXPECT_EQ(cfg.mode, BackupMode::Incremental);
    ASSERT_EQ(cfg.filter.includeExtensions.size(), size_t(2));
    EXPECT_EQ(cfg.filter.includeExtensions[0], std::wstring(L".cpp"));
    EXPECT_EQ(cfg.filter.includeExtensions[1], std::wstring(L".h"));
    ASSERT_EQ(cfg.filter.excludeExtensions.size(), size_t(2));
    EXPECT_EQ(cfg.filter.includeSubPaths[0], std::wstring(L"docs"));
    EXPECT_EQ(cfg.filter.excludeSubPaths[0], std::wstring(L"temp"));
    EXPECT_EQ(cfg.filter.minSize, uint64_t(1024));
    EXPECT_EQ(cfg.filter.maxSize, uint64_t(2 * 1024 * 1024));
    EXPECT_TRUE(cfg.filter.skipEmptyFiles);
    EXPECT_TRUE(cfg.overwriteOnRestore);
    EXPECT_EQ(cfg.keepSnapshots, 3);
    EXPECT_TRUE(cfg.filter.modifiedAfterEnabled);
    EXPECT_TRUE(hasWarning(warnings, L"忽略无法解析的行"));
}

TEST(ConfigLoader, InvalidValuesProduceWarnings) {
    const std::vector<std::string> lines = {
        "type=unknown",
        "keep_snapshots=-1",
        "keep_snapshots=abc",
        "unknown_key=1",
    };

    BackupConfig cfg;
    std::vector<std::wstring> warnings;
    ASSERT_TRUE(ConfigLoader::parseLines(lines, cfg, warnings));
    EXPECT_EQ(cfg.mode, BackupMode::Full);          // 非法类型保持默认
    EXPECT_EQ(cfg.keepSnapshots, 0);                // 非法值不生效
    EXPECT_TRUE(hasWarning(warnings, L"未知备份类型"));
    EXPECT_TRUE(hasWarning(warnings, L"keep_snapshots 不能为负数"));
    EXPECT_TRUE(hasWarning(warnings, L"keep_snapshots 格式无效"));
    EXPECT_TRUE(hasWarning(warnings, L"未知配置项"));
}

// Utf.h - 字符串编码工具
// 职责：宽字符(wchar_t/UTF-16，Windows 内部路径表示)与 UTF-8 文本之间的转换，
//       以及大小写不敏感的宽字符串比较。
#pragma once

#include <string>

namespace backup {

// UTF-16(Windows 内部) -> UTF-8
std::string wideToUtf8(const std::wstring& w);

// UTF-8 -> UTF-16(Windows 内部)
std::wstring utf8ToWide(const std::string& s);

// 大小写不敏感的宽字符串比较（Windows 文件系统默认不区分大小写，排序/查找均使用）。
// 返回：<0 a<b；0 相等；>0 a>b
int wcsicmpSafe(const std::wstring& a, const std::wstring& b);

// 宽字符串是否以指定前缀开头（大小写不敏感）
bool startsWithNoCase(const std::wstring& s, const std::wstring& prefix);

}  // namespace backup

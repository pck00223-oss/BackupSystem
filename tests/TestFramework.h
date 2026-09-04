// TestFramework.h - 极简测试框架（不依赖第三方库）
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace testfw {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline int g_total = 0;
inline int g_failed = 0;

inline void check(bool cond, const char* expr, const char* file, int line) {
    ++g_total;
    if (cond) {
        std::cout << "    [OK] " << expr << "\n";
    } else {
        ++g_failed;
        std::cout << "    [FAIL] " << expr << "  at " << file << ":" << line << "\n";
    }
}

inline void checkEq(bool eq, const char* expr, const char* file, int line) {
    ++g_total;
    if (eq) {
        std::cout << "    [OK] " << expr << "\n";
    } else {
        ++g_failed;
        std::cout << "    [FAIL] " << expr << "  at " << file << ":" << line << "\n";
    }
}

inline int runAll() {
    for (auto& c : registry()) {
        std::cout << "== " << c.name << " ==\n";
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++g_failed;
            std::cout << "    [EXCEPTION] " << e.what() << "\n";
        }
    }
    std::cout << "\n共 " << g_total << " 项断言, 失败 " << g_failed << " 项\n";
    return g_failed == 0 ? 0 : 1;
}

}  // namespace testfw

#define TEST(name) \
    static void t_##name();                        \
    static ::testfw::Registrar r_##name(#name, t_##name); \
    static void t_##name()

#define CHECK(cond) ::testfw::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::testfw::checkEq(((a) == (b)), #a " == " #b, __FILE__, __LINE__)

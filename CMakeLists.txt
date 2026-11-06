cmake_minimum_required(VERSION 3.16)
project(BackupSystem VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ============ 健壮性检查开关 ============
# cmake -S . -B build -DSTRICT_WARNINGS=ON   严格警告（含 -Wconversion/-Wshadow 等）
# cmake -S . -B build -DHARDENING=ON         运行时加固（GLibC++ 调试模式 + 栈保护 + 整数溢出 trap）
# cmake -S . -B build -DSANITIZE=ON          AddressSanitizer + UBSan（需编译器带 libasan/libubsan）
# cmake -S . -B build -DENABLE_CPPCHECK=ON   编译时自动跑 cppcheck
option(STRICT_WARNINGS "Enable strict compiler warnings" OFF)
option(HARDENING "Enable runtime hardening (libstdc++ debug + stack protector + trapv)" OFF)
option(SANITIZE "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ENABLE_CPPCHECK "Run cppcheck static analysis during build" OFF)

if(ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_CXX_CPPCHECK "${CPPCHECK_EXE}"
            --enable=warning,style,performance,portability
            --inconclusive
            --std=c++17
            --suppress=missingIncludeSystem
            --suppress=unusedFunction
            --error-exitcode=1)
        message(STATUS "cppcheck enabled: ${CPPCHECK_EXE}")
    else()
        message(WARNING "cppcheck not found, ENABLE_CPPCHECK ignored")
    endif()
endif()

# 检测编译器是否真正支持 sanitizer（MinGW 某些发行版只有头文件没有库）
if(SANITIZE AND NOT MSVC)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
    set(CMAKE_REQUIRED_LIBRARIES "-fsanitize=address,undefined")
    check_cxx_source_compiles("int main(){return 0;}" SANITIZE_WORKS)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT SANITIZE_WORKS)
        message(WARNING "SANITIZE=ON but compiler lacks libasan/libubsan; falling back to HARDENING mode")
        set(HARDENING ON)
        set(SANITIZE OFF)
    endif()
endif()

# ============ 统一编译选项（警告 + sanitize，所有 target 共享）============
add_library(backup_flags INTERFACE)
target_compile_features(backup_flags INTERFACE cxx_std_17)
if(MSVC)
    target_compile_options(backup_flags INTERFACE /W4 /utf-8 /permissive-)
    if(STRICT_WARNINGS)
        target_compile_options(backup_flags INTERFACE /WX /analyze)
    endif()
else()
    target_compile_options(backup_flags INTERFACE -Wall -Wextra)
    if(STRICT_WARNINGS)
        target_compile_options(backup_flags INTERFACE
            -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnull-dereference -Wdouble-promotion -Wold-style-cast
            -Wnon-virtual-dtor -Woverloaded-virtual)
    endif()
endif()
if(HARDENING AND NOT MSVC)
    # libstdc++ 调试模式：捕获 STL 越界、迭代器失效、非法比较
    target_compile_definitions(backup_flags INTERFACE _GLIBCXX_DEBUG _GLIBCXX_ASSERTIONS)
    # 栈保护 + 整数溢出 trap
    target_compile_options(backup_flags INTERFACE -fstack-protector-strong -ftrapv)
    message(STATUS "Hardening enabled: _GLIBCXX_DEBUG + stack-protector + trapv")
endif()
if(SANITIZE)
    target_compile_options(backup_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(backup_flags INTERFACE -fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled: AddressSanitizer + UndefinedBehaviorSanitizer")
endif()

# ============ 核心库（分层：core / engine / business / app）============
add_library(backup_core STATIC
    src/core/BackupLock.cpp
    src/core/BcryptBlockCipher.cpp
    src/core/EncryptionFactory.cpp
    src/core/Error.cpp
    src/core/HashCalculator.cpp
    src/core/Logger.cpp
    src/core/TimeUtil.cpp
    src/core/Utf.cpp
    src/engine/FileCopier.cpp
    src/engine/FileScanner.cpp
    src/engine/FileSystem.cpp
    src/engine/TarPacker.cpp
    src/business/BackupManager.cpp
    src/business/FileComparator.cpp
    src/business/FileFilter.cpp
    src/business/Manifest.cpp
    src/business/RestoreManager.cpp
    src/business/SnapshotManager.cpp
    src/business/VerifyManager.cpp
    src/app/BackupTask.cpp
    src/app/ConfigLoader.cpp
    src/app/ScheduleManager.cpp
    src/app/TaskScheduler.cpp
)
target_include_directories(backup_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(backup_core PUBLIC backup_flags)

# CommandLineToArgvW 位于 shell32；BCryptGenRandom 位于 bcrypt
if(WIN32)
    target_link_libraries(backup_core PUBLIC shell32 bcrypt)
endif()

# ============ 命令行入口 ============
add_executable(backupapp src/main.cpp)
target_link_libraries(backupapp PRIVATE backup_core backup_flags)

# ============ Win32 GUI 入口（零第三方依赖，纯 Win32 API）============
add_executable(backupgui WIN32
    src/gui/gui_main.cpp
    src/gui/MainWindow.cpp
    src/gui/TaskStore.cpp
)
target_link_libraries(backupgui PRIVATE backup_core backup_flags comctl32 crypt32)
# MinGW 需要 -municode 才能使用 wWinMain 入口
if(NOT MSVC)
    target_link_options(backupgui PRIVATE -municode)
endif()

# ============ 单元/集成测试 ============
enable_testing()

# GoogleTest：优先使用系统/包管理器安装的版本，找不到时用 FetchContent 拉取固定版本
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(googletest)
    message(STATUS "GoogleTest not found locally; fetched via FetchContent (v1.15.2)")
else()
    message(STATUS "GoogleTest found locally: ${GTest_DIR}")
endif()

# cppcheck 只检查本项目代码，跳过第三方 googletest 源码
if(TARGET gtest)
    set_target_properties(gtest PROPERTIES CXX_CPPCHECK "")
endif()
if(TARGET gtest_main)
    set_target_properties(gtest_main PROPERTIES CXX_CPPCHECK "")
endif()
if(TARGET gmock)
    set_target_properties(gmock PROPERTIES CXX_CPPCHECK "")
endif()
if(TARGET gmock_main)
    set_target_properties(gmock_main PROPERTIES CXX_CPPCHECK "")
endif()

add_executable(backup_tests
    tests/TimeUtilTest.cpp
    tests/ResidualUtilTest.cpp
    tests/UtfTest.cpp
    tests/ConfigLoaderTest.cpp
    tests/LoggerTest.cpp
    tests/ManifestEdgeTest.cpp
    tests/EncryptionAlgorithmTest.cpp
    tests/FileSystemTest.cpp
    tests/FileCopierTest.cpp
    tests/FileScannerTest.cpp
    tests/TarPackerTest.cpp
    tests/SnapshotManagerTest.cpp
    tests/VerifySnapshotTest.cpp
    tests/GuiTaskStoreTest.cpp
    tests/HashCalculatorTest.cpp
    tests/ManifestTest.cpp
    tests/FileComparatorTest.cpp
    tests/FileFilterTest.cpp
    tests/BackupRestoreTest.cpp
    src/gui/TaskStore.cpp
)
target_link_libraries(backup_tests PRIVATE backup_core backup_flags crypt32 GTest::gtest_main)
add_test(NAME backup_tests COMMAND backup_tests)

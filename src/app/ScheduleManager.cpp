// ScheduleManager.cpp - Windows 任务计划程序注册/卸载实现
#include "app/ScheduleManager.h"

#include <fstream>
#include <string>
#include <vector>

#include <windows.h>

#include "core/Utf.h"
#include "engine/FileSystem.h"

namespace backup {

namespace {

// 执行命令行，等待结束，返回退出码。stderr 捕获到 errOutput。
int runCommand(const std::wstring& cmd, std::string& errOutput) {
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hErrRead = nullptr, hErrWrite = nullptr;
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) return -1;
    SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0);

    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = hErrWrite;
    si.hStdOutput = hErrWrite;

    std::vector<wchar_t> cmdLine(cmd.begin(), cmd.end());
    cmdLine.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hErrRead);
        CloseHandle(hErrWrite);
        return -1;
    }
    CloseHandle(hErrWrite);

    char buf[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hErrRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
        errOutput.append(buf, bytesRead);
    }
    CloseHandle(hErrRead);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

// schtasks 输出在中文 Windows 上是 GBK(CP_ACP)，需用系统代码页转换
std::wstring acpToWide(const std::string& s) {
    if (s.empty()) return L"";
    const int len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len);
    return out;
}

// 检测是否为"拒绝访问"错误（中英文，在 wide 字符串中检测）
bool isAccessDenied(const std::wstring& err) {
    return err.find(L"Access is denied") != std::wstring::npos ||
           err.find(L"拒绝访问") != std::wstring::npos;
}

// 检测是否为"任务不存在"错误（中英文）
bool isTaskNotFound(const std::wstring& err) {
    return err.find(L"cannot find the file specified") != std::wstring::npos ||
           err.find(L"系统找不到指定的文件") != std::wstring::npos ||
           err.find(L"任务名不存在") != std::wstring::npos;
}

// XML 转义
std::wstring xmlEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 16);
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

// 两位数字补零
std::wstring pad2(int v) {
    return (v < 10 ? L"0" : L"") + std::to_wstring(v);
}

// 校验 HH:MM 格式，返回 true 表示合法，hour/minute 输出解析结果
bool parseTimeHHMM(const std::wstring& time, int& hour, int& minute) {
    if (time.size() != 5 || time[2] != L':') return false;
    for (size_t i = 0; i < time.size(); ++i) {
        if (i == 2) continue;
        if (time[i] < L'0' || time[i] > L'9') return false;
    }
    hour = std::stoi(time.substr(0, 2));
    minute = std::stoi(time.substr(3, 2));
    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

// 生成任务计划 XML
// StartBoundary 直接用今天的日期（即使已过触发时间，Windows 也会从下一个周期开始执行），
// 避免手动 wDay+=1 导致月末/年末非法日期。
std::wstring buildTaskXml(const std::wstring& executable,
                           const std::wstring& arguments,
                           const std::wstring& time,
                           const std::wstring& description) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    int hour = 20, minute = 0;
    parseTimeHHMM(time, hour, minute);  // 调用方已校验，此处仅解析

    const std::wstring boundary =
        std::to_wstring(st.wYear) + L"-" + pad2(st.wMonth) + L"-" + pad2(st.wDay) +
        L"T" + pad2(hour) + L":" + pad2(minute) + L":00";

    std::wstring xml;
    xml += L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n";
    xml += L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n";
    xml += L"  <RegistrationInfo>\r\n";
    xml += L"    <Description>" + xmlEscape(description) + L"</Description>\r\n";
    xml += L"  </RegistrationInfo>\r\n";
    xml += L"  <Triggers>\r\n";
    xml += L"    <CalendarTrigger>\r\n";
    xml += L"      <StartBoundary>" + boundary + L"</StartBoundary>\r\n";
    xml += L"      <Enabled>true</Enabled>\r\n";
    xml += L"      <ScheduleByDay>\r\n";
    xml += L"        <DaysInterval>1</DaysInterval>\r\n";
    xml += L"      </ScheduleByDay>\r\n";
    xml += L"    </CalendarTrigger>\r\n";
    xml += L"  </Triggers>\r\n";
    xml += L"  <Principals>\r\n";
    xml += L"    <Principal id=\"Author\">\r\n";
    xml += L"      <LogonType>InteractiveToken</LogonType>\r\n";
    xml += L"      <RunLevel>LeastPrivilege</RunLevel>\r\n";
    xml += L"    </Principal>\r\n";
    xml += L"  </Principals>\r\n";
    xml += L"  <Settings>\r\n";
    xml += L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n";
    xml += L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n";
    xml += L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n";
    xml += L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n";
    xml += L"    <StartWhenAvailable>true</StartWhenAvailable>\r\n";
    xml += L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n";
    xml += L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n";
    xml += L"    <Enabled>true</Enabled>\r\n";
    xml += L"    <Hidden>false</Hidden>\r\n";
    xml += L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n";
    xml += L"    <WakeToRun>false</WakeToRun>\r\n";
    xml += L"    <ExecutionTimeLimit>PT72H</ExecutionTimeLimit>\r\n";
    xml += L"    <Priority>7</Priority>\r\n";
    xml += L"  </Settings>\r\n";
    xml += L"  <Actions Context=\"Author\">\r\n";
    xml += L"    <Exec>\r\n";
    xml += L"      <Command>" + xmlEscape(executable) + L"</Command>\r\n";
    xml += L"      <Arguments>" + xmlEscape(arguments) + L"</Arguments>\r\n";
    xml += L"    </Exec>\r\n";
    xml += L"  </Actions>\r\n";
    xml += L"</Task>\r\n";
    return xml;
}

}  // namespace

bool ScheduleManager::registerDaily(const std::wstring& taskName,
                                     const std::wstring& executable,
                                     const std::wstring& arguments,
                                     const std::wstring& time,
                                     std::string* errMsg) {
    if (taskName.empty() || executable.empty() || time.empty()) {
        if (errMsg) *errMsg = "taskName, executable, time must not be empty";
        return false;
    }
    int hour = 0, minute = 0;
    if (!parseTimeHHMM(time, hour, minute)) {
        if (errMsg) *errMsg = "invalid time format (expected HH:MM, 00:00-23:59): " + wideToUtf8(time);
        return false;
    }

    // 临时 XML 文件写到系统临时目录，避免只读工作目录失败
    wchar_t tmpDir[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmpDir) == 0) {
        if (errMsg) *errMsg = "GetTempPath failed";
        return false;
    }
    wchar_t tmpPath[MAX_PATH];
    if (GetTempFileNameW(tmpDir, L"btsk", 0, tmpPath) == 0) {
        if (errMsg) *errMsg = "GetTempFileName failed";
        return false;
    }
    std::wstring xmlPath(tmpPath);

    const std::wstring xml = buildTaskXml(
        executable, arguments, time,
        L"BackupSystem daily backup task (registered by backupapp schedule --register)");

    std::ofstream ofs(xmlPath.c_str(), std::ios::binary | std::ios::trunc);
    const uint8_t bom[2] = {0xFF, 0xFE};
    ofs.write(reinterpret_cast<const char*>(bom), 2);
    ofs.write(reinterpret_cast<const char*>(xml.data()),
              static_cast<std::streamsize>(xml.size() * sizeof(wchar_t)));
    ofs.close();

    std::wstring cmd = L"schtasks /create /xml \"" + xmlPath + L"\" /tn \"" +
                        taskName + L"\" /f";
    std::string errOutput;
    const int rc = runCommand(cmd, errOutput);

    ::DeleteFileW(xmlPath.c_str());

    if (rc != 0) {
        if (errMsg) {
            const std::wstring wideErr = acpToWide(errOutput);
            *errMsg = "schtasks /create failed (exit " + std::to_string(rc) + ")";
            if (!errOutput.empty()) *errMsg += ": " + wideToUtf8(wideErr);
            if (isAccessDenied(wideErr)) *errMsg += " [需要以管理员身份运行]";
        }
        return false;
    }
    return true;
}

bool ScheduleManager::unregister(const std::wstring& taskName, std::string* errMsg) {
    // 直接执行 delete，不先查询（避免查询因权限错误被误判为"未注册"）。
    // 任务不存在时 schtasks 返回错误，但 isTaskNotFound 视为成功（幂等）。
    std::wstring cmd = L"schtasks /delete /tn \"" + taskName + L"\" /f";
    std::string errOutput;
    const int rc = runCommand(cmd, errOutput);
    if (rc != 0) {
        const std::wstring wideErr = acpToWide(errOutput);
        if (isTaskNotFound(wideErr)) return true;  // 任务不存在，视为成功
        if (errMsg) {
            *errMsg = "schtasks /delete failed (exit " + std::to_string(rc) + ")";
            if (!errOutput.empty()) *errMsg += ": " + wideToUtf8(wideErr);
        }
        return false;
    }
    return true;
}

bool ScheduleManager::isRegistered(const std::wstring& taskName) {
    std::wstring cmd = L"schtasks /query /tn \"" + taskName + L"\"";
    std::string errOutput;
    return runCommand(cmd, errOutput) == 0;
}

std::wstring ScheduleManager::getNextRunTime(const std::wstring& taskName) {
    std::wstring cmd = L"schtasks /query /tn \"" + taskName + L"\" /v /fo list";
    std::string errOutput;
    if (runCommand(cmd, errOutput) != 0) return L"";

    // 先转成 wide 字符串（中文系统是 GBK），再同时匹配中英文标记
    const std::wstring wide = acpToWide(errOutput);
    const std::vector<std::wstring> markers = {L"Next Run Time:", L"下次运行时间:"};

    for (const auto& marker : markers) {
        const size_t pos = wide.find(marker);
        if (pos == std::wstring::npos) continue;
        size_t start = pos + marker.size();
        while (start < wide.size() && (wide[start] == L' ' || wide[start] == L'\t')) {
            ++start;
        }
        size_t end = wide.find(L"\r\n", start);
        if (end == std::wstring::npos) end = wide.find(L'\n', start);
        if (end == std::wstring::npos) end = wide.size();
        return wide.substr(start, end - start);
    }
    return L"";
}

}  // namespace backup

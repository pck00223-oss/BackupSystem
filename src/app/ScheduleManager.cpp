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
    STARTUPINFOW si = {sizeof(si)};
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

// 生成任务计划 XML
std::wstring buildTaskXml(const std::wstring& executable,
                           const std::wstring& arguments,
                           const std::wstring& time,
                           const std::wstring& description) {
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    int hour = 20, minute = 0;
    if (time.size() >= 5 && time[2] == L':') {
        hour = std::stoi(time.substr(0, 2));
        minute = std::stoi(time.substr(3, 2));
    }
    // 如果当前时间已过今天的触发点，起始日设为明天
    bool pastToday = (st.wHour > hour) || (st.wHour == hour && st.wMinute >= minute);
    if (pastToday) st.wDay += 1;
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

    wchar_t tmpPath[MAX_PATH];
    GetTempFileNameW(L".", L"btsk", 0, tmpPath);
    std::wstring xmlPath = tmpPath;

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
    if (!isRegistered(taskName)) return true;

    std::wstring cmd = L"schtasks /delete /tn \"" + taskName + L"\" /f";
    std::string errOutput;
    const int rc = runCommand(cmd, errOutput);
    if (rc != 0) {
        if (errMsg) {
            *errMsg = "schtasks /delete failed (exit " + std::to_string(rc) + ")";
            if (!errOutput.empty()) *errMsg += ": " + wideToUtf8(acpToWide(errOutput));
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
    const std::string marker = "Next Run Time:";
    const size_t pos = errOutput.find(marker);
    if (pos == std::string::npos) return L"";
    size_t start = pos + marker.size();
    while (start < errOutput.size() && (errOutput[start] == ' ' || errOutput[start] == '\t')) {
        ++start;
    }
    size_t end = errOutput.find("\r\n", start);
    if (end == std::string::npos) end = errOutput.find('\n', start);
    if (end == std::string::npos) end = errOutput.size();
    return acpToWide(errOutput.substr(start, end - start));
}

}  // namespace backup

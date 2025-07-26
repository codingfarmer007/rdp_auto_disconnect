// Copyright 2025 Your Name

// 实现远程桌面连接自动断开功能：
// 1. 开机自启动
// 2. 检测活跃的RDP会话
// 3. 监控用户输入活动
// 4. 当闲置时间超过阈值时自动断开RDP连接
// 5. 记录关键事件日志到程序目录

#include <windows.h>
#include <tchar.h>
#include <lmaccess.h>
#include <wtsapi32.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <sstream>
#include <ctime>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "netapi32.lib")

// 闲置时间阈值（分钟），可根据需要修改
const int kIdleTimeoutMinutes = 10;

// 获取当前时间的字符串表示
std::string GetCurrentTimeString() {
    std::time_t now = std::time(nullptr);
    std::tm time_info;
    localtime_s(&time_info, &now);

    char time_str[100];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &time_info);
    return std::string(time_str);
}

// 获取日志文件路径（程序同目录下的rdp_auto_disconnect.log）
std::string GetLogFilePath() {
    TCHAR app_path[MAX_PATH];
    if (GetModuleFileName(nullptr, app_path, MAX_PATH) == 0) {
        return "rdp_auto_disconnect.log";
    }

    std::wstring w_app_path(app_path);
    std::string app_path_str(w_app_path.begin(), w_app_path.end());

    size_t last_slash = app_path_str.find_last_of("\\/");
    if (last_slash == std::string::npos) {
        return "rdp_auto_disconnect.log";
    }

    return app_path_str.substr(0, last_slash + 1) + "rdp_auto_disconnect.log";
}

// 写入日志到文件
void WriteLog(const std::string& message) {
    try {
        std::string log_file = GetLogFilePath();
        std::ofstream log_stream(log_file, std::ios::app);

        if (log_stream.is_open()) {
            std::string log_entry = "[" + GetCurrentTimeString() + "] " + message + "\n";
            log_stream << log_entry;
            log_stream.close();
        }
    }
    catch (...) {
        // 日志写入失败时不做处理，避免影响主程序
    }
}

// 设置程序开机自启动
// 参数：app_path - 程序的完整路径
// 返回值：成功返回true，失败返回false
bool SetAutoStart(const TCHAR* app_path) {
    HKEY h_key;
    // 打开注册表自启动项
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER,
        _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        0, KEY_SET_VALUE, &h_key);

    if (result != ERROR_SUCCESS) {
        WriteLog("设置自启动失败：无法打开注册表项");
        return false;
    }

    // 设置自启动值
    result = RegSetValueEx(h_key, _T("RdpAutoDisconnect"), 0, REG_SZ,
        (const BYTE*)app_path, (_tcslen(app_path) + 1) * sizeof(TCHAR));

    RegCloseKey(h_key);

    if (result == ERROR_SUCCESS) {
        WriteLog("成功设置程序自启动");
        return true;
    }
    else {
        WriteLog("设置自启动失败：无法写入注册表值");
        return false;
    }
}

// 检查是否有活跃的RDP会话
// 返回值：存在活跃RDP会话返回true，否则返回false
bool HasActiveRdpSession() {
    PWTS_SESSION_INFO session_info = nullptr;
    DWORD session_count = 0;

    // 枚举所有会话
    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &session_info, &session_count)) {
        WriteLog("枚举会话失败");
        return false;
    }

    bool has_active_rdp = false;

    // 检查每个会话是否是RDP会话且活跃
    for (DWORD i = 0; i < session_count; ++i) {
        WTS_SESSION_INFO session = session_info[i];

        // 检查会话状态是否为活跃
        if (session.State == WTSActive) {
            // 获取会话类型
            LPTSTR session_name = nullptr;
            DWORD bytes_returned = 0;

            if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, session.SessionId,
                WTSWinStationName, &session_name, &bytes_returned)) {

                // RDP会话的名称通常是"RDP-Tcp#xxx"
                if (_tcsstr(session_name, _T("RDP-Tcp")) != nullptr) {
                    has_active_rdp = true;

                    // 将会话ID转换为字符串写入日志
                    std::stringstream ss;
                    ss << "检测到活跃的RDP会话，会话ID: " << session.SessionId;
                    WriteLog(ss.str());
                }

                WTSFreeMemory(session_name);
                if (has_active_rdp) {
                    break;
                }
            }
        }
    }

    WTSFreeMemory(session_info);

    if (!has_active_rdp) {
        WriteLog("未检测到活跃的RDP会话");
    }

    return has_active_rdp;
}

// 获取最后一次输入到现在的时间（毫秒）
// 返回值：闲置时间（毫秒）
DWORD GetLastInputTime() {
    LASTINPUTINFO last_input_info;
    last_input_info.cbSize = sizeof(LASTINPUTINFO);

    if (GetLastInputInfo(&last_input_info)) {
        DWORD idle_time_ms = GetTickCount() - last_input_info.dwTime;
        DWORD idle_time_minutes = idle_time_ms / (1000 * 60);

        std::stringstream ss;
        ss << "当前用户闲置时间: " << idle_time_minutes << "分钟";
        WriteLog(ss.str());

        return idle_time_ms;
    }

    WriteLog("获取用户输入时间失败");
    return 0;
}

// 断开所有RDP会话
void DisconnectRdpSessions() {
    PWTS_SESSION_INFO session_info = nullptr;
    DWORD session_count = 0;

    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &session_info, &session_count)) {
        WriteLog("枚举会话失败，无法断开RDP连接");
        return;
    }

    for (DWORD i = 0; i < session_count; ++i) {
        WTS_SESSION_INFO session = session_info[i];

        LPTSTR session_name = nullptr;
        DWORD bytes_returned = 0;

        if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, session.SessionId,
            WTSWinStationName, &session_name, &bytes_returned)) {

            if (_tcsstr(session_name, _T("RDP-Tcp")) != nullptr) {
                // 断开RDP会话
                if (WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, session.SessionId, TRUE)) {
                    std::stringstream ss;
                    ss << "已断开RDP会话，会话ID: " << session.SessionId;
                    WriteLog(ss.str());
                }
                else {
                    std::stringstream ss;
                    ss << "断开RDP会话失败，会话ID: " << session.SessionId;
                    WriteLog(ss.str());
                }
            }

            WTSFreeMemory(session_name);
        }
    }

    WTSFreeMemory(session_info);
}

// 主监控函数，循环检查RDP连接和用户活动
void MonitorAndDisconnect() {
    WriteLog("监控线程已启动");

    while (true) {
        WriteLog("开始新一轮检查...");

        // 检查是否有活跃的RDP会话
        if (HasActiveRdpSession()) {
            // 获取最后一次输入到现在的毫秒数
            DWORD idle_time_ms = GetLastInputTime();
            DWORD idle_time_minutes = idle_time_ms / (1000 * 60);

            // 如果闲置时间超过阈值，断开连接
            if (idle_time_minutes >= kIdleTimeoutMinutes) {
                std::stringstream ss;
                ss << "闲置时间(" << idle_time_minutes << "分钟)超过阈值("
                    << kIdleTimeoutMinutes << "分钟)，准备断开RDP连接";
                WriteLog(ss.str());

                DisconnectRdpSessions();
                // 断开后稍等片刻再继续监控
                std::this_thread::sleep_for(std::chrono::minutes(1));
            }
        }

        WriteLog("本轮检查结束，等待下一次检查...");
        // 每分钟检查一次
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

// 程序入口函数
int WINAPI WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance,
    LPSTR lp_cmd_line, int n_cmd_show) {
    WriteLog("程序启动");
    
    // 获取当前程序路径并设置自启动
    TCHAR app_path[MAX_PATH];
    if (GetModuleFileName(nullptr, app_path, MAX_PATH)) {
        std::wstring w_app_path(app_path);
        std::string app_path_str(w_app_path.begin(), w_app_path.end());
        WriteLog("程序路径: " + app_path_str);
        SetAutoStart(app_path);
    }
    else {
        WriteLog("获取程序路径失败");
    }
    
    // 启动监控线程
    std::thread monitor_thread(MonitorAndDisconnect);
    monitor_thread.detach();

    // 隐藏窗口，作为后台进程运行
    HWND hwnd = CreateWindowEx(0, _T("STATIC"), _T("RdpAutoDisconnect"), 0,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, HWND_MESSAGE, nullptr, h_instance, nullptr);

    if (hwnd == nullptr) {
        WriteLog("创建窗口失败");
        return 0;
    }

    WriteLog("程序初始化完成，进入消息循环");

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WriteLog("程序退出");
    return static_cast<int>(msg.wParam);
}

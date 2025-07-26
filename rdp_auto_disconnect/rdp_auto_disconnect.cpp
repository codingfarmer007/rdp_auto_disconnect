// Copyright 2025 Your Name

// ʵ��Զ�����������Զ��Ͽ����ܣ�
// 1. ����������
// 2. ����Ծ��RDP�Ự
// 3. ����û�����
// 4. ������ʱ�䳬����ֵʱ�Զ��Ͽ�RDP����
// 5. ��¼�ؼ��¼���־������Ŀ¼

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

// ����ʱ����ֵ�����ӣ����ɸ�����Ҫ�޸�
const int kIdleTimeoutMinutes = 10;

// ��ȡ��ǰʱ����ַ�����ʾ
std::string GetCurrentTimeString() {
    std::time_t now = std::time(nullptr);
    std::tm time_info;
    localtime_s(&time_info, &now);

    char time_str[100];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &time_info);
    return std::string(time_str);
}

// ��ȡ��־�ļ�·��������ͬĿ¼�µ�rdp_auto_disconnect.log��
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

// д����־���ļ�
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
        // ��־д��ʧ��ʱ������������Ӱ��������
    }
}

// ���ó��򿪻�������
// ������app_path - ���������·��
// ����ֵ���ɹ�����true��ʧ�ܷ���false
bool SetAutoStart(const TCHAR* app_path) {
    HKEY h_key;
    // ��ע�����������
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER,
        _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        0, KEY_SET_VALUE, &h_key);

    if (result != ERROR_SUCCESS) {
        WriteLog("����������ʧ�ܣ��޷���ע�����");
        return false;
    }

    // ����������ֵ
    result = RegSetValueEx(h_key, _T("RdpAutoDisconnect"), 0, REG_SZ,
        (const BYTE*)app_path, (_tcslen(app_path) + 1) * sizeof(TCHAR));

    RegCloseKey(h_key);

    if (result == ERROR_SUCCESS) {
        WriteLog("�ɹ����ó���������");
        return true;
    }
    else {
        WriteLog("����������ʧ�ܣ��޷�д��ע���ֵ");
        return false;
    }
}

// ����Ƿ��л�Ծ��RDP�Ự
// ����ֵ�����ڻ�ԾRDP�Ự����true�����򷵻�false
bool HasActiveRdpSession() {
    PWTS_SESSION_INFO session_info = nullptr;
    DWORD session_count = 0;

    // ö�����лỰ
    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &session_info, &session_count)) {
        WriteLog("ö�ٻỰʧ��");
        return false;
    }

    bool has_active_rdp = false;

    // ���ÿ���Ự�Ƿ���RDP�Ự�һ�Ծ
    for (DWORD i = 0; i < session_count; ++i) {
        WTS_SESSION_INFO session = session_info[i];

        // ���Ự״̬�Ƿ�Ϊ��Ծ
        if (session.State == WTSActive) {
            // ��ȡ�Ự����
            LPTSTR session_name = nullptr;
            DWORD bytes_returned = 0;

            if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, session.SessionId,
                WTSWinStationName, &session_name, &bytes_returned)) {

                // RDP�Ự������ͨ����"RDP-Tcp#xxx"
                if (_tcsstr(session_name, _T("RDP-Tcp")) != nullptr) {
                    has_active_rdp = true;

                    // ���ỰIDת��Ϊ�ַ���д����־
                    std::stringstream ss;
                    ss << "��⵽��Ծ��RDP�Ự���ỰID: " << session.SessionId;
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
        WriteLog("δ��⵽��Ծ��RDP�Ự");
    }

    return has_active_rdp;
}

// ��ȡ���һ�����뵽���ڵ�ʱ�䣨���룩
// ����ֵ������ʱ�䣨���룩
DWORD GetLastInputTime() {
    LASTINPUTINFO last_input_info;
    last_input_info.cbSize = sizeof(LASTINPUTINFO);

    if (GetLastInputInfo(&last_input_info)) {
        DWORD idle_time_ms = GetTickCount() - last_input_info.dwTime;
        DWORD idle_time_minutes = idle_time_ms / (1000 * 60);

        std::stringstream ss;
        ss << "��ǰ�û�����ʱ��: " << idle_time_minutes << "����";
        WriteLog(ss.str());

        return idle_time_ms;
    }

    WriteLog("��ȡ�û�����ʱ��ʧ��");
    return 0;
}

// �Ͽ�����RDP�Ự
void DisconnectRdpSessions() {
    PWTS_SESSION_INFO session_info = nullptr;
    DWORD session_count = 0;

    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &session_info, &session_count)) {
        WriteLog("ö�ٻỰʧ�ܣ��޷��Ͽ�RDP����");
        return;
    }

    for (DWORD i = 0; i < session_count; ++i) {
        WTS_SESSION_INFO session = session_info[i];

        LPTSTR session_name = nullptr;
        DWORD bytes_returned = 0;

        if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, session.SessionId,
            WTSWinStationName, &session_name, &bytes_returned)) {

            if (_tcsstr(session_name, _T("RDP-Tcp")) != nullptr) {
                // �Ͽ�RDP�Ự
                if (WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, session.SessionId, TRUE)) {
                    std::stringstream ss;
                    ss << "�ѶϿ�RDP�Ự���ỰID: " << session.SessionId;
                    WriteLog(ss.str());
                }
                else {
                    std::stringstream ss;
                    ss << "�Ͽ�RDP�Ựʧ�ܣ��ỰID: " << session.SessionId;
                    WriteLog(ss.str());
                }
            }

            WTSFreeMemory(session_name);
        }
    }

    WTSFreeMemory(session_info);
}

// ����غ�����ѭ�����RDP���Ӻ��û��
void MonitorAndDisconnect() {
    WriteLog("����߳�������");

    while (true) {
        WriteLog("��ʼ��һ�ּ��...");

        // ����Ƿ��л�Ծ��RDP�Ự
        if (HasActiveRdpSession()) {
            // ��ȡ���һ�����뵽���ڵĺ�����
            DWORD idle_time_ms = GetLastInputTime();
            DWORD idle_time_minutes = idle_time_ms / (1000 * 60);

            // �������ʱ�䳬����ֵ���Ͽ�����
            if (idle_time_minutes >= kIdleTimeoutMinutes) {
                std::stringstream ss;
                ss << "����ʱ��(" << idle_time_minutes << "����)������ֵ("
                    << kIdleTimeoutMinutes << "����)��׼���Ͽ�RDP����";
                WriteLog(ss.str());

                DisconnectRdpSessions();
                // �Ͽ����Ե�Ƭ���ټ������
                std::this_thread::sleep_for(std::chrono::minutes(1));
            }
        }

        WriteLog("���ּ��������ȴ���һ�μ��...");
        // ÿ���Ӽ��һ��
        std::this_thread::sleep_for(std::chrono::minutes(1));
    }
}

// ������ں���
int WINAPI WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance,
    LPSTR lp_cmd_line, int n_cmd_show) {
    WriteLog("��������");
    
    // ��ȡ��ǰ����·��������������
    TCHAR app_path[MAX_PATH];
    if (GetModuleFileName(nullptr, app_path, MAX_PATH)) {
        std::wstring w_app_path(app_path);
        std::string app_path_str(w_app_path.begin(), w_app_path.end());
        WriteLog("����·��: " + app_path_str);
        SetAutoStart(app_path);
    }
    else {
        WriteLog("��ȡ����·��ʧ��");
    }
    
    // ��������߳�
    std::thread monitor_thread(MonitorAndDisconnect);
    monitor_thread.detach();

    // ���ش��ڣ���Ϊ��̨��������
    HWND hwnd = CreateWindowEx(0, _T("STATIC"), _T("RdpAutoDisconnect"), 0,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, HWND_MESSAGE, nullptr, h_instance, nullptr);

    if (hwnd == nullptr) {
        WriteLog("��������ʧ��");
        return 0;
    }

    WriteLog("�����ʼ����ɣ�������Ϣѭ��");

    // ��Ϣѭ��
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WriteLog("�����˳�");
    return static_cast<int>(msg.wParam);
}


#pragma once
#include <string>

/// 日志输出
class Logger {
public:

    /// 是否输出到控制台，否则使用 OutputDebugString WinAPI
    static bool CONSOLE;

    enum OutputLevel {
        OL_INFO, ///< 输出普通信息与错误信息
        OL_ERROR, ///< 只输出错误信息
    };

    static OutputLevel LEVEL;

    /// 输出日志
    /// 
    /// 级别由参数 @a level 提供
    static void Log(const char *msg, OutputLevel level);
    static void Log(const std::string &msg, OutputLevel level) {
        Log(msg.c_str(), level);
    }

    /// 输出普通信息
    static void LogInfo(const char *msg);
    static void LogInfo(const std::string &msg) {
        LogInfo(msg.c_str());
    }

    /// 输出错误信息
    static void LogError(const char *msg);
    static void LogError(const std::string &msg) {
        LogError(msg.c_str());
    }

    /// 输出 Windows LogLastError() 错误信息
    static void LogWindowsLastError(const char *msg);
};

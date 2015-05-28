
#include "Logger.hpp"
#include <mutex>
#include <sstream>

#include <Windows.h>

//////////////////////////////////////////////////////////////////////////

bool Logger::CONSOLE = false;
Logger::OutputLevel Logger::LEVEL = Logger::OL_ERROR;

static std::mutex gs_loggerMutex;
const static char *gs_format = "[%d]------------------------\n%s\n\n";

std::string Format(const char *msg) {
    static std::ostringstream ss;
    ss.str(std::string());

    ss << '[' << GetCurrentThreadId() << ']'
       << "------------------------\n"
       << msg << "\n\n";

    return ss.str();
}

void Logger::Log(const char *msg, OutputLevel level) {
    if (level == OL_INFO) {
        LogInfo(msg);
    }
    else {
        LogError(msg);
    }
}

void Logger::LogInfo(const char *msg) {
    if (LEVEL != OL_INFO) {
        return;
    }

    gs_loggerMutex.lock();
    
    if (CONSOLE) {
        printf(gs_format, GetCurrentThreadId(), msg);
    }
    else {
        OutputDebugStringA(Format(msg).c_str());
    }

    gs_loggerMutex.unlock();
}

void Logger::LogError(const char *msg) {
    gs_loggerMutex.lock();

    if (CONSOLE) {
        fprintf(stderr, gs_format, GetCurrentThreadId(), msg);
    }
    else {
        OutputDebugStringA(Format(msg).c_str());
    }

    gs_loggerMutex.unlock();
}

void Logger::LogWindowsLastError(const char *msg) {
    gs_loggerMutex.lock();
    


    gs_loggerMutex.unlock();
}

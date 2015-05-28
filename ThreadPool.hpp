
#pragma once
#include <Windows.h>

/// 线程池
/// 
/// Windows 为每个线程默认创建的线程池似乎不支持设置最小线程数
class ThreadPool {
public:

    /// 构造函数
    ThreadPool();

    /// 析构函数
    ~ThreadPool();

public:

    /// 线程池是否可用
    bool IsOk() const {
        return m_handle != nullptr;
    }

    /// 创建一个工作
    bool CreateWork(PTP_SIMPLE_CALLBACK pfns, void *pv);

    /// 设置最小线程数
    bool SetThreadMinimum(int minimun);

    /// 设置最大线程数
    void SetThreadMaximum(int maximun);

private:

    PTP_POOL m_handle;
    TP_CALLBACK_ENVIRON m_env;
};

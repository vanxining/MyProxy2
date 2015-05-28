
#pragma once
#include "ws-util.h"
#include "ThreadPool.hpp" // 必须放在 "ws-util.h" 之后

struct RxContext;
class Request;

/// 代理对象
class MyProxy {
public:

    /// 构造函数
    MyProxy();

    /// 析构函数
    ~MyProxy();

    /// 允许主循环
    bool Run(const char *addr, u_short port);

private:

    // 获取 IOCP 相关 API 的函数指针
    static bool GetIocpFunctionPointers(SOCKET sd);

    // 初始化监听套接字
    bool SetUpListener(const char *addr, int port);

    // 提交 @a num 个 AcceptEx() 异步请求
    bool SpawnAcceptors(int num);

    bool PostAccept(RxContext &context);

    // 创建工作线程
    bool SpawnThreads();

    // 线程入口函数
    static DWORD CALLBACK ProxyHandler(PVOID pv);

    // 接受一个浏览器请求
    void DoAccept(RxContext &context);

private:

    SOCKET m_listener;
    HANDLE m_cp;

    ThreadPool m_threadPool;

    int m_requestPoolSize;
    int m_numActiveRequests;
};

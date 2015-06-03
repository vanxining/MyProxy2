
#pragma once
#include "ws-util.h"

#include <vector>
#include <atomic>
#include <memory>

struct RxContext;
class Request;

/// 代理对象
class MyProxy {
public:

    /// 构造函数
    MyProxy();

    /// 析构函数
    ~MyProxy();

    /// 开始服务
    bool Start(const char *addr, u_short port);

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

    HANDLE m_cp = nullptr;
    SOCKET m_listener;

    typedef std::vector<std::shared_ptr<RxContext>> AcceptorVec;

    AcceptorVec m_acceptors;
    std::atomic_int m_numExitedThreads; // 已退出的线程数目
};

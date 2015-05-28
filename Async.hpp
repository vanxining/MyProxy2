
#pragma once
#include "ws-util.h"
#include <ws2tcpip.h>

/// 异步解释 DNS
class AsyncResolver {
public:

    class Callback;

    /// 构造函数
    AsyncResolver(Callback *callback);

    /// 析构函数
    ~AsyncResolver();

    /// DNS 解释请求
    struct Request {
        const char *host; ///< 远程主机名
        u_short port; ///< 端口号
        void *userData; ///< 用户自定义信息
    };

    /// 提交解释请求
    bool PostResolve(const Request &request);

public:

    /// DNS 解释操作上下文信息
    struct QueryContext {
        OVERLAPPED ol;
        ADDRINFOEX *results;

        wchar_t *host;
        u_short port;

        ADDRINFOEX hints;
        wchar_t service[6];

        AsyncResolver *resolver;
        void *userData;

        /// 构造函数
        QueryContext(AsyncResolver *resolver, const Request &request);
        ~QueryContext();
    };

    /// 回调类
    class Callback {
    public:

        /// 回调虚函数
        /// 
        /// @param context 被调用者应该接管这个指针
        virtual void OnQueryCompleted(QueryContext *context) = 0;
    };

    /// 设置回调对象
    void SetCallback(Callback *cb) {
        m_callback = cb;
    }

private:

    static void CALLBACK OnDnsResolved(DWORD, DWORD, LPWSAOVERLAPPED);

private:

    Callback *m_callback = nullptr;
};

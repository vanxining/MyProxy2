
#pragma once

#include "Logger.hpp"
#include "PerIoContext.hpp"
#include "Async.hpp"
#include "MemoryPool.hpp"
#include "ws-util.h"

#include <ctime>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
using namespace std;

#define SOCKET_bind ::bind

/// 来自浏览器的一次请求
/// 
/// 可能包含若干 HTTP 连接请求。
class Request : public AsyncResolver::Callback {
public:

    enum {
        /// 对象池静态分配的数目
        STATIC_POOL_SIZE = 256,

        /// 对象池每次动态分配的数目
        DYNAMIC_POOL_SIZE = STATIC_POOL_SIZE,
    };

    /// 构造函数
    Request();

    /// 析构函数
    ~Request();

    /// 初始化
    void Init(HANDLE cp, const RxContext &acceptContext);

    /// 处理来自浏览器的连接
    void HandleBrowser();

    /// 便捷定义
    typedef AsyncResolver::QueryContext QueryContext;

    /// 由 IOCP 分发的 DNS 解释已完成事件
    void OnIocpQueryCompleted(QueryContext &context);

    /// 异步连接操作已完成
    void OnConnectCompleted();

    /// 异步读操作已完成
    void OnRecvCompleted(RxContext &context);

    /// 异步写操作已完成
    void OnSendCompleted(TxContext *&context);

public:

    /// 打印 HTTP 头的第一行，包含 GET、POST 等信息
    void PrintRequest(Logger::OutputLevel level) const;

    /// 统计信息
    struct Statistics {
        /// 总的请求数
        atomic_int requests;

        /// 输入、输出字节数
        atomic_llong inBytes, outBytes;

        /// DNS 查询数
        atomic_int dnsQueries;

        /// DNS 缓存命中数
        atomic_int dnsCacheHit;
    };

    /// 获取统计信息
    static Statistics GetStatistics();

public:

    /// 当前是否可以重用
    bool IsRecyclable() const;

private:

    // 重置对象为“清洁”的状态
    void Clear();

    // 销毁自己
    void DeleteThis();

private:

    // 缓冲区类型
    typedef vector<char> Buffer;

    // 尝试处理浏览器请求
    bool TryParsingHeaders();

    // 解析主机
    void SplitHost(const string &decl, int defaultPort);

    // 浏览器端的数据数据上传已完成
    void OnUploadDone();

    // 转发至服务器
    bool HandleServer();
    bool DoHandleServer();

    // 断开与浏览器的连接
    void ShutdownBrowserSocket();

    // 断开与服务器的连接
    bool ShutdownServerSocket();

    // 尝试使用 DNS 缓存的 IP 地址连接到服务器
    bool TryDNSCache();

    // DNS 解释完成
    virtual void OnQueryCompleted(QueryContext *context) override;

    // 提交异步 DNS 解释请求
    bool PostDnsQuery();

    // 销毁当前使用的 QueryContext
    void DelQueryContext();

    // 使用地址链表中的头结点连接到服务器
    void PostConnect();

    // 过滤来自浏览器的 HTTP 头部
    // 
    // 主要是去掉代理声明信息。
    void FilterBrowserHeaders();

    // 是否已然转发所有浏览器上行数据到服务器
    bool IsUploadDone() const;

    // 是否为面向浏览器的 per-io-context
    bool IsBrowserOrientedContext(const PerIoContext &context);

    // 便利函数：快速创建一个 TxContext
    static TxContext *NewTxContext(SOCKET sd, const RxContext &context);
    static TxContext *NewTxContext(SOCKET sd, const char *buf, int len);

    // 销毁一个 TxContext
    static void DelTxContext(TxContext *context);

    // 提交异步接收请求
    bool PostRecv(RxContext &context);

    // 提交异步发送请求
    bool PostSend(TxContext *context);

    // 设置/取消“异步接收请求已提交”标志布尔变量
    void SetRxReqPostedMark(bool browser, bool posted);

private:

    void LogInfo(const string &msg) const;
    void LogError(const string &msg) const;

    void Log(const string &msg, Logger::OutputLevel level) const;

private:

    HANDLE m_cp = nullptr;

    // 保存由浏览器发来的包含完整 HTTP 头部的一段数据
    // 可能不单单只是 HTTP 头部信息。
    Buffer m_vbuf;

    struct Host {
        void Clear() {
            this->name.clear();
            this->port = 0;
            this->tunel = false;
        }

        bool operator!=(const Host &other) {
            return this->name != other.name || this->port != other.port;
        }

        // 获取全名（加上端口）
        string GetFullName() const;

        string name;
        unsigned short port = 0;

        bool tunel = false; // 是否为隧道
    };

    Host m_host;

    AsyncResolver m_resolver;
    QueryContext *m_qcontext = nullptr;
    ADDRINFOEX *m_ai = nullptr; // 当前尝试的 addrinfo 结构
    ConnectContext m_ccontext;

    // HTTP 头部
    struct Headers {
    public:

        // 解析头部
        // 
        // @param buf 必须保证以 0 结尾
        // @param browser 是否来自浏览器
        bool Parse(const char *buf, bool browser);

        // 是否已经解析成功
        bool IsOk() const;

        // 清空内容
        void Clear();

        // 是否保持连接
        bool KeepAlive() const;

        // 根据状态码确定传输是否已然结束
        bool DetermineFinishedByStatusCode() const;

        // 是否分段
        bool IsChunked() const;

    public:

        int status_code = 0;

        unordered_map<string, string> m;
        int bodyOffset = -1;
    };

    Headers m_headers;

    RxContext m_bcontext;
    size_t m_btotal = 0; // 当前请求全长度
    size_t m_brx = 0; // 当前请求已接收的数据长度

    RxContext m_scontext;

    bool m_brxPosted = false; // 当前是否存在面向浏览器的接收请求
    bool m_srxPosted = false; // 当前是否存在面向服务器的接收请求

    // 统计信息
    static Statistics ms_stat;

private:

    time_t m_delTS = 0; // 被删除时的时间戳

private:

    // 调试用
    bool m_everRx = false; // 确实接收到过数据
    bool m_noAttachedData = true; // 一开始就没有数据进来

    // 来自服务器的第一个请求是否已然收到
    bool m_firstResponseRecv = false;
};

/// Request 对象内存池
class RequestPool : public MemoryPool<Request, mutex> {
public:

    /// 获取单体对象
    static RequestPool &GetInstance();

private:

    RequestPool() {}
};

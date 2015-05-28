
#pragma once
#include "ws-util.h"

/// IOCP 异步操作上下文
struct PerIoContext {
    /// 异步操作类型
    enum Action {
        NONE, ///< 无效状态
        /// AcceptEx
        /// 
        /// 此时 #sd 属性是被接受的客户端连接套接字，
        /// 而非监听套接字。
        ACCEPT,
        NAME_RESOLVE, ///< DNS 解析
        CONNECT, ///< ConnectEx
        RECV, ///< WSARecv
        SEND, ///< WSASend
    };

    /// 构造函数
    PerIoContext(SOCKET sd, Action action);

    /// 复制构造函数
    PerIoContext(const PerIoContext &other)
        : PerIoContext(other.sd, other.action) {}

    /// 是否有效
    bool IsOk() const;

    WSAOVERLAPPED ol; ///< 重叠结构
    SOCKET sd; ///< 套接字

    /// 操作类型
    Action action;
};

/// 连接操作上下文
struct ConnectContext : public PerIoContext {
    /// 构造函数
    ConnectContext(SOCKET sd);

    /// 重置
    void Reset();

    /// 已发送的初始数据长度
    DWORD tx;
    /// 是否已然连接成功
    bool connected;
};

/// IOCP 接收缓冲区
struct RxContext : public PerIoContext {
    enum {
        BUFFER_SIZE = kBufferSize, ///< 缓冲区大小
        ADDR_LEN = sizeof(sockaddr_in) + 16, ///< 地址长度
        DATA_CAPACITY = BUFFER_SIZE - (ADDR_LEN * 2),
    };

    /// 构造函数
    RxContext(SOCKET sd);

    /// 复制构造函数
    RxContext(const RxContext &other);

    /// 为下次 RECV 操作做好准备
    void PrepareForNextRecv();

    /// 重置
    void Reset();

    WSABUF bufSpec; ///< 用于支持 WSARecv() 函数
    CHAR buf[BUFFER_SIZE]; ///< 缓冲区

    /// 缓冲区已接收的数据长度
    DWORD rx;
};

/// IOCP 发送缓冲区
struct TxContext : public PerIoContext {
    /// 构造函数
    TxContext(SOCKET sd, const char *buf, int len);
    /// 禁止复制
    TxContext(const TxContext &) = delete;

    /// 析构函数
    ~TxContext();

    WSABUF *buffers; ///< 缓冲区列表
    DWORD nb; ///< 缓冲区个数

    /// 缓冲区已被发送的数据长度
    DWORD tx;
};

/// 一些特殊的标记用 completion key
enum SpecialCompKeys {
    SCK_EXIT = 1, ///< 退出
    SCK_NAME_RESOLVE, ///< 异步 DNS 解释操作已完成
};

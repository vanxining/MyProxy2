
#pragma once
#include "ws-util.h"
#include <ws2tcpip.h> // for ADDRINFOEX

#include <string>
#include <map>

/// DNS 缓存
class DNSCache {
public:

    /// 条目失效时间
    /// 
    /// 默认为一个小时之内没有任何访问条目则失效。
    /// 每次访问都会更新时间戳。
    static double EXPIRATION;

    /// WinSock 有特殊的定义
    typedef ADDRINFOEX AI;

    /// 解析域名
    /// 
    /// 调用者需要接管返回的指针，删除可通过 #DestroyAddrInfo() 函数
    static AI *Resolve(const std::string &dname);

    /// 销毁一个由 #Resolve() 返回的 AI 对象
    static void DestroyAddrInfo(AI *ai);

    /// 将域名对应的 IP 地址加入缓存
    static void Add(const std::string &dname, const AI &ai);

    /// 删除失效条目
    static bool Remove(const std::string &dname);

private:

    /// 一个缓存条目
    struct Entry {
        /// 构造函数
        Entry(const AI *ai_other);

        /// 禁用复制构造函数
        Entry(const Entry &other) = delete;

        /// 析构函数
        ~Entry();

        /// 条目是否有效
        bool IsOk() const;

        /// 复制 #ai 结构
        AI *CopyAddrInfo() const;

        AI ai;
        time_t ts = 0; ///< 加入缓存的时间
    };

private:

    typedef std::map<std::string, Entry> Cache;
    static Cache ms_cache;
};

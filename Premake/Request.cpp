
#include "Request.hpp"
#include "DNSCache.hpp"

#include <Ws2tcpip.h> // for getaddrinfo()
#include <mswsock.h> // for LPFN_CONNECTEX

#include <sstream>
#include <cassert>
#include <cstdio>


//////////////////////////////////////////////////////////////////////////

string ToLower(const string &s) {
    string ret(s);

    for (auto &ch : ret) {
        ch = tolower(ch);
    }

    return ret;
}

bool AssociateWithCompletionPort(SOCKET sd, HANDLE cp, ULONG_PTR key);
extern LPFN_CONNECTEX lpfnConnectEx;

//////////////////////////////////////////////////////////////////////////

Request::Statistics Request::ms_stat;

Request::Request(const RxContext &acceptContext, HANDLE cp)
    : m_cp(cp),
      m_vbuf(0),
      m_resolver(this),
      m_ccontext(INVALID_SOCKET),
      m_bcontext(acceptContext),
      m_scontext(INVALID_SOCKET) {}

Request::~Request() {
    ShutdownBrowserSocket();
    ShutdownServerSocket();
}

void Request::ShutdownBrowserSocket() {
    if (m_bcontext.IsOk()) {
        ostringstream ss;

        if (!m_everRx) {
            ss << __FUNC__ "No received data? noAttachedData -- "
               << m_noAttachedData;

            LogError(ss.str());
            ss.str(string());
        }
        else {
            m_everRx = false;
        }

        auto sd = m_bcontext.sd;
        m_bcontext.Reset();
        m_pendingBRx = false;

        ShutdownConnection(sd);
    }
}

bool Request::ShutdownServerSocket() {
    if (!m_scontext.IsOk()) {
        return true;
    }

    auto sd = m_scontext.sd;
    m_scontext.Reset();
    m_pendingSRx = false; // 若为真，是否需要取消？

    return ShutdownConnection(sd);
}

void Request::DeleteThis() {
    ShutdownBrowserSocket();
    ShutdownServerSocket();

    return;
    delete this;
}

void Request::HandleBrowser() {
    if (m_bcontext.rx > 0) {
        m_everRx = true;
    }
    else if (!m_noAttachedData) {
        m_noAttachedData = true;
    }

    //-------------------------------------------

    auto p = m_bcontext.buf, pEnd = p + m_bcontext.rx;
    m_vbuf.insert(m_vbuf.end(), p, pEnd);
    m_vbuf.push_back(0);

    if (!TryParsingHeaders()) {
        m_vbuf.pop_back();
        return;
    }

    PrintRequest(Logger::OL_INFO);

    //-------------------------------------------

    if (IsSSL()) {
        SplitHost(m_vbuf.data() + 8, 443);

        RelaySSLConnection();
    }
    else {
        // 匿名代理
        FilterBrowserHeaders();

        Host lastHost = m_host;
        SplitHost(m_headers.m["Host"], 80);

        if (lastHost != m_host && m_scontext.IsOk()) {
            ShutdownServerSocket();
        }

        if (!HandleServer()) {
            DeleteThis();
            return;
        }

        if (IsUploadDone()) {
            OnUploadDone();
        }

        // 始终继续监听
        PostRecv(m_bcontext);
    }
}

void Request::OnUploadDone() {
    assert(m_brx >= m_btotal);
    assert(m_btotal > 0);

    m_btotal = 0;
    m_brx = 0;

    m_headers.Clear();
    m_vbuf.clear();

    m_firstResponseRecv = false;
}

bool Request::IsSSL() const {
    return strncmp(m_vbuf.data(), "CONNECT ", 8) == 0;
}

bool Request::TryParsingHeaders() {
    if (!m_headers.Parse(m_vbuf.data(), true)) {
        PostRecv(m_bcontext);
        return false;
    }

    //-------------------------------------------
    // 确定数据长度

    m_brx = m_vbuf.size() - 1;

    auto it(m_headers.m.find("Content-Length"));
    if (it != m_headers.m.end()) {
        m_btotal = m_headers.bodyOffset + atoi(it->second.c_str());
    }
    else {
        m_btotal = m_brx;
    }

    return true;
}

void Request::OnRecvCompleted(RxContext &context) {
    if (!context.IsOk()) { // TODO: 为什么？
        return;
    }

    if (IsBrowserOrientedContext(context)) {
        assert(m_pendingBRx);
        m_pendingBRx = false;
    }
    else {
        assert(m_pendingSRx);
        m_pendingSRx = false;
    }

    // 不管是谁断开了连接，终止服务
    if (context.rx == 0) {
        if (IsBrowserOrientedContext(context)) {
            LogInfo("Browser disconnected");
        }
        else {
            LogInfo("Server disconnected");
        }

        DeleteThis();
        return;
    }

    if (IsBrowserOrientedContext(context)) {
        if (!m_headers.IsOk()) {
            HandleBrowser(); // 已提交了新的 Recv 请求，直接返回
            return;
        }
        else {
            if (IsSSL()) {
                PostSend(NewTxContext(m_scontext.sd, context));
            }
            else {
                m_brx += context.rx;

                // 上传尚未结束，继续转发到服务器
                PostSend(NewTxContext(m_scontext.sd, context));

                if (IsUploadDone()) {
                    OnUploadDone();
                }
            }
        }
    }
    else if (context.sd == m_scontext.sd) {
        if (!m_firstResponseRecv) {
            m_firstResponseRecv = true;

            if (!IsSSL()) {
                if (strncmp(context.buf, "HTTP/", 5) == 0) {
                    string resp(context.buf, strchr(context.buf, '\r'));
                    LogInfo(__FUNC__ + resp);
                }
                else {
                    LogError(__FUNC__ "Fatal: Incorrect response header");
                    assert(false);
                }
            }
        }

        // 转发到浏览器
        PostSend(NewTxContext(m_bcontext.sd, context));
    }
    else {
        // 不应该来到这里！！
        assert(false);
        return;
    }

    // 始终继续监听，因为我们必须知道连接在什么时候断开了
    PostRecv(context);
}

void Request::OnSendCompleted(TxContext *&context) {
    if (context->tx != context->buffers->len) {
        ostringstream oss;
        oss << __FUNC__ "Byte count: " << context->buffers->len
            << " Transfered: " << context->tx;

        LogError(oss.str());
    }

    // TODO: memory pool
    delete context;
    context = nullptr;
}

void Request::PrintRequest(Logger::OutputLevel level) const {
    auto p = min(strchr(m_vbuf.data(), '\r'), m_vbuf.data() + 100);
    string firstLine(m_vbuf.data(), p);

    Log(firstLine, level);
}

Request::Statistics Request::GetStatistics() {
    return ms_stat;
}

void Request::SplitHost(const string &host_decl, int default_port) {
    m_host.port = default_port;

    auto pos = host_decl.find(':');
    if (pos != string::npos) {
        m_host.port = atoi(host_decl.c_str() + pos + 1);
    }
    else {
        pos = host_decl.length();
    }

    m_host.name = host_decl.substr(0, pos);
}

bool Request::HandleServer() {
    if (m_scontext.IsOk()) {
        if (!DoHandleServer()) {
            ShutdownServerSocket(); // 忽略错误处理

            return DoHandleServer();
        }

        LogInfo(__FUNC__ "Successfully reused socket.");
        return true;
    }

    return DoHandleServer();
}

bool Request::DoHandleServer() {
    if (!m_scontext.IsOk() && !SetUpServerSocket()) {
        return false;
    }

    // 转发第一批数据到服务器
    TxContext *context = new TxContext
        (m_scontext.sd, m_vbuf.data(), m_vbuf.size() - 1);

    if (!PostSend(context)) {
        return false;
    }

    // 读取服务器回应
    // 必须保证同一时刻只有一个读取请求
    if (!m_pendingSRx) {
        if (!PostRecv(m_scontext)) {
            return false;
        }
    }

    return true;
}

SOCKET Request::DoConnect(const addrinfo &ai) {
    SOCKET sd = socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol);

    if (sd != INVALID_SOCKET) {
        if (connect(sd, ai.ai_addr, ai.ai_addrlen) == 0) {
            if (AssociateWithCompletionPort(sd, m_cp, (ULONG_PTR) this)) {
                return sd;
            }
        }

        closesocket(sd);
        sd = INVALID_SOCKET;
    }

    return sd;
}

bool Request::SetUpServerSocket() {
    if (TryDNSCache()) {
        return true;
    }

    addrinfo hints, *result;

    memset(&hints, 0, sizeof(addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char portstr[6];
    sprintf_s(portstr, sizeof(portstr), "%d", m_host.port);

    // 将域名解释为一个 IP 地址链表
    if (getaddrinfo(m_host.name.c_str(), portstr, &hints, &result) != 0) {
        LogError(WSAGetLastErrorMessage(__FUNC__ "getaddrinfo() failed"));
        return false;
    }
    
    addrinfo *ai = result;

    do {
        m_scontext.sd = DoConnect(*ai);
        if (m_scontext.sd != INVALID_SOCKET) {
            DNSCache::Add(m_host.GetFullName(), *ai);

            freeaddrinfo(result);
            return true;
        }
    } while (ai = ai->ai_next);

    LogError(__FUNC__ "No appropriate IP address");

    freeaddrinfo(result);
    return false;
}

bool Request::TryDNSCache() {
    ms_stat.dnsQueries++;

    auto fullName(m_host.GetFullName());
    auto entry = DNSCache::Resolve(fullName);
    if (entry) {
        m_scontext.sd = DoConnect(entry->ai);
        if (m_scontext.sd != INVALID_SOCKET) {
            ms_stat.dnsCacheHit++;
            return true;
        }

        // 删除失效条目
        DNSCache::Remove(fullName);
    }

    return false;
}

bool Request::PostDnsQuery() {
    return m_resolver.PostResolve({m_host.name.c_str(), m_host.port, this});
}

void Request::OnQueryCompleted(QueryContext *context) {
    assert(!m_scontext.IsOk());
    assert(!m_qcontext);

    m_qcontext = context;
    m_ai = m_qcontext->results;

    PostConnect();
}

void Request::PostConnect() {
    if (!m_ai) {
        DeleteThis();
        return;
    }

    const ADDRINFOEX &ai = *m_ai;

    m_ccontext.sd = socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol);
    if (!m_ccontext.IsOk()) {
        LogError(WSAGetLastErrorMessage(__FUNC__ "socket() failed"));

        DeleteThis();
        return;
    }

    // 前进！
    m_ai = m_ai->ai_next;

    BOOL bResult = lpfnConnectEx(m_ccontext.sd,
                                 ai.ai_addr, 
                                 ai.ai_addrlen,
                                 m_vbuf.data(),
                                 m_vbuf.size() - 1,
                                 nullptr,
                                 &m_ccontext.ol);

    if (!bResult) {
        int ec = WSAGetLastError();
        if (ec != WSA_IO_PENDING) {
            auto prefix = __FUNC__ "ConnectEx() failed";
            LogError(WSAGetLastErrorMessage(prefix, ec));

            closesocket(m_ccontext.sd);
            m_ccontext.sd = INVALID_SOCKET;

            OnConnectCompleted();
        }
    }
}

void Request::OnConnectCompleted() {
    if (!m_ccontext.IsOk()) {
        PostConnect();
        return;
    }

    if (AssociateWithCompletionPort(m_ccontext.sd, m_cp, (ULONG_PTR) this)) {
        m_scontext.sd = m_ccontext.sd;
    }
    else {
        DeleteThis();
        return;
    }
}

void Request::FilterBrowserHeaders() {
    assert(!m_vbuf.empty());
    ostringstream ss;

    string firstLine(m_vbuf.data(), strstr(m_vbuf.data(), "\r\n"));
    string needle(" http://" + m_headers.m["Host"]);

    auto pos = firstLine.find(needle);
    if (pos != string::npos) {
        firstLine.replace(pos, needle.length(), " ", 1);
    }

    ss << firstLine << "\r\n";

    auto it(m_headers.m.find("Proxy-Connection"));
    if (it != m_headers.m.end()) {
        auto conn = it->second;
        m_headers.m.erase(it);

        if (m_headers.m.find("Connection") == m_headers.m.end()) {
            m_headers.m.emplace("Connection", conn);
        }
    }

    for (auto &header : m_headers.m) {
        ss << header.first << ": " << header.second << "\r\n";
    }

    ss << "\r\n";

    //-------------------------------------------

    auto const s(ss.str());
    auto const bodyOffset = m_headers.bodyOffset;
    
    Buffer buf(0);
    buf.reserve(s.size() + m_vbuf.size() - bodyOffset);

    buf.insert(buf.end(), s.begin(), s.end());
    buf.insert(buf.end(), m_vbuf.begin() + bodyOffset, m_vbuf.end());

    m_vbuf.swap(buf);
}

bool Request::RelaySSLConnection() {
    if (!ShutdownServerSocket() || !SetUpServerSocket()) {
        return false;
    }

    const char *confirm = "HTTP/1.1 200 Connection Established\r\n\r\n";
    if (!PostSend(new TxContext(m_bcontext.sd, confirm, strlen(confirm)))) {
        return false;
    }

    if (!PostRecv(m_bcontext)) {
        return false;
    }

    if (!PostRecv(m_scontext)) {
        return false;
    }

    return true;
}

bool Request::IsUploadDone() const {
    return m_btotal == m_brx;
}

inline
bool Request::IsBrowserOrientedContext(const PerIoContext &context) {
    return context.sd == m_bcontext.sd;
}

/*static*/
TxContext *Request::NewTxContext(SOCKET sd, const RxContext &context) {
    assert(context.rx > 0);
    assert(context.IsOk());

    return new TxContext(sd, context.buf, context.rx);
}

bool Request::PostRecv(RxContext &context) {
    assert(context.sd != m_bcontext.sd || !m_pendingBRx);
    assert(context.sd != m_scontext.sd || !m_pendingSRx);

    assert(context.IsOk());
    context.PrepareForNextRecv();

    DWORD flags = 0;

    // Using a single buffer improves performance.
    int iResult = WSARecv(context.sd,
                          &context.bufSpec, 1,
                          nullptr,
                          &flags,
                          &context.ol,
                          nullptr);

    if (iResult != 0) {
        int ec = WSAGetLastError();
        if (ec != WSA_IO_PENDING) {
            auto prefix = __FUNC__ "WSARecv() failed";
            LogError(WSAGetLastErrorMessage(prefix, ec));

            return false;
        }
    }

    if (IsBrowserOrientedContext(context)) {
        m_pendingBRx = true;
    }
    else {
        m_pendingSRx = true;
    }

    return true;
}

bool Request::PostSend(TxContext *context) {
    assert(context);
    assert(context->IsOk());

    int iResult = WSASend(context->sd,
                          context->buffers,
                          context->nb,
                          nullptr,
                          0,
                          &context->ol,
                          nullptr);

    if (iResult == 0) {
        return true;
    }

    int ec = WSAGetLastError();
    if (ec == WSA_IO_PENDING) {
        return true;
    }

    auto prefix = __FUNC__ "WSASend() failed";
    LogError(WSAGetLastErrorMessage(prefix, ec));

    return false;
}

void Request::LogInfo(const string &msg) const {
    Log(msg, Logger::OL_INFO);
}

void Request::LogError(const string &msg) const {
    Log(msg, Logger::OL_ERROR);
}

void Request::Log(const string &msg, Logger::OutputLevel level) const {
    ostringstream ss;
    ss << "[0x" << hex << this << "] " << dec;

    if (m_host.port > 0) {
        ss << m_host.name << ':' << m_host.port << '\n';
    }
    else {
        sockaddr_in inaddr;
        socklen_t len = sizeof(inaddr);
        if (getsockname(m_bcontext.sd, (sockaddr *) &inaddr, &len) == 0) {
            ss << "[Local] " << inet_ntoa(inaddr.sin_addr)
               << ":" << ntohs(inaddr.sin_port) << '\n';
        }
    }

    ss << msg;
    Logger::Log(ss.str(), level);
}

//////////////////////////////////////////////////////////////////////////

bool Request::Headers::Parse(const char *buf, bool browser) {
    this->Clear();

    const char *p = strstr(buf, "\r\n\r\n");
    if (p) {
        if (!browser && strncmp(buf, "HTTP/", 5) == 0) {
            this->status_code = atoi(buf + 9);
        }

        const char *b = strstr(buf, "\r\n") + 2;

        while (*b != '\r') {
            const char *e = strstr(b, "\r\n");
            const char *colon = strstr(b, ": ");
            if (!colon || colon > e) {
                colon = strchr(b, ':');
            }

            if (colon && colon + 2 < e) {
                string k(b, colon), v(colon + 2, e);
                this->m.emplace(make_pair(k, v));
            }

            b = e + 2;
        }

        this->bodyOffset = p + 4 - buf;
        return true;
    }

    return false;
}

bool Request::Headers::IsOk() const {
    return !m.empty() && bodyOffset > 0;
}

void Request::Headers::Clear() {
    this->status_code = 0;
    this->m.clear();
    this->bodyOffset = -1;
}

bool Request::Headers::KeepAlive() const {
    auto it(m.find("Connection"));
    if (it != m.end()) {
        return ToLower(it->second) != "close";
    }

    // TODO: HTTP/1.1 默认是保持连接
    return true;
}

bool Request::Headers::DetermineFinishedByStatusCode() const {
    if (status_code == 0) {
        return false;
    }

    if (status_code < 200 || status_code == 304) {
        return true;
    }

    return false;
}

bool Request::Headers::IsChunked() const {
    auto it(this->m.find("Transfer-Encoding"));
    if (it != this->m.end()) {
        return ToLower(it->second) == "chunked";
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////

string Request::Host::GetFullName() const {
    ostringstream ss;
    ss << name << ':' << port;

    return ss.str();
}

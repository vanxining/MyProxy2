
#include "Request.hpp"
#include "DNSCache.hpp"

#include <Ws2tcpip.h> // for getaddrinfo()
#include <mswsock.h> // for LPFN_CONNECTEX

#include <sstream>
#include <cassert>
#include <cstdio>

#include "Debug.hpp"


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

Request::Request()
    : m_vbuf(0),
      m_resolver(this),
      m_ccontext(INVALID_SOCKET),
      m_bcontext(INVALID_SOCKET),
      m_scontext(INVALID_SOCKET) {}

Request::~Request() {
    Clear();
}

void Request::Init(HANDLE cp, const RxContext &acceptContext) {
    m_cp = cp;
    m_bcontext = acceptContext;

    m_delTS = 0;
}

void Request::ShutdownBrowserSocket() {
    if (m_bcontext.IsOk()) {
        if (!m_everRx) {
            ostringstream oss;
            oss << __FUNC__ "No received data? noAttachedData -- "
                << m_noAttachedData;

            LogError(oss.str());
        }
        else {
            m_everRx = false;
        }

        auto sd = m_bcontext.sd;
        m_bcontext.Reset();
        m_brxPosted = false;

        ShutdownConnection(sd);
    }
}

bool Request::ShutdownServerSocket() {
    if (!m_scontext.IsOk()) {
        if (m_ccontext.IsOk()) {
            DelQueryContext();

            auto sd = m_ccontext.sd;
            m_ccontext.Reset();

            return ShutdownConnection(sd);
        }

        return true;
    }

    assert(!m_qcontext);

    auto sd = m_scontext.sd;
    m_scontext.Reset();
    m_srxPosted = false; // 若为真，是否需要取消？

    return ShutdownConnection(sd);
}

void Request::Clear() {
    m_cp = nullptr;
    m_vbuf.clear();
    m_host.Clear();

    m_resolver.Cancel();
    DelQueryContext();
    m_ccontext.Reset();

    m_headers.Clear();

    m_bcontext.Reset();
    m_brx = m_btotal = 0;
    m_scontext.Reset();
    m_brxPosted = m_srxPosted = false;

    m_delTS = 0;

    m_everRx = false;
    m_noAttachedData = true;
    m_firstResponseRecv = false;
}

void Request::DeleteThis() {
    ShutdownBrowserSocket();
    ShutdownServerSocket();

    Clear();

    m_delTS = time(nullptr);
    RequestPool::GetInstance().DeAllocate(this);
}

bool Request::IsRecyclable() const {
    assert(m_delTS != 0);
    return difftime(time(nullptr), m_delTS) > 20;
}

void Request::HandleBrowser() {
    if (m_bcontext.rx > 0) {
        m_everRx = true;
        m_noAttachedData = false;
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

    if (strncmp(m_vbuf.data(), "CONNECT ", 8) == 0) {
        SplitHost(m_vbuf.data() + 8, 443);
        m_host.tunel = true;

        ShutdownServerSocket();
    }
    else {
        // 匿名代理
        FilterBrowserHeaders();

        Host lastHost = m_host;
        SplitHost(m_headers.m["Host"], 80);
        m_host.tunel = false;

        if (lastHost != m_host && m_scontext.IsOk()) {
            ShutdownServerSocket();
        }
    }

    if (!HandleServer()) {
        DeleteThis();
        return;
    }
}

void Request::OnUploadDone() {
    assert(m_brx >= m_btotal);
    assert(m_btotal > 0);

    m_btotal = 0;
    m_brx = 0;

    m_vbuf.clear();
    m_headers.Clear();

    m_firstResponseRecv = false;
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
    // 场景：浏览器终止了连接，我们也终止了与服务器的连接，
    // 此时服务器发了最后一个 FIN 包过来
    if (!context.IsOk()) {
        return;
    }

    SetRxReqPostedMark(IsBrowserOrientedContext(context), false);

    // 一方断开了连接
    if (context.rx == 0) {
        if (IsBrowserOrientedContext(context)) {
            LogInfo("Browser disconnected");

            // 一并关闭服务器连接
            DeleteThis();
        }
        else {
            LogInfo("Server disconnected");

            // 保留浏览器端连接
            ShutdownServerSocket();
        }

        return;
    }

    if (IsBrowserOrientedContext(context)) {
        if (!m_headers.IsOk()) {
            HandleBrowser(); // 已提交了新的 Recv 请求，直接返回
            return;
        }
        else {
            if (m_host.tunel) {
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

            if (!m_host.tunel) {
                if (strncmp(context.buf, "HTTP/", 5) == 0) {
                    string resp(context.buf, strchr(context.buf, '\r'));
                    LogInfo(__FUNC__ + resp);
                }
                else {
                    LogError(__FUNC__ "Fatal: Incorrect response header");
                    
                    DeleteThis();
                    return;
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
    if (m_bcontext.IsOk() && context->tx != context->buffers->len) {
        ostringstream oss;
        oss << __FUNC__ "Byte count: " << context->buffers->len
            << " Transfered: " << context->tx;

        LogError(oss.str());
    }

    DelTxContext(context);
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

void Request::SplitHost(const string &decl, int defaultPort) {
    m_host.port = defaultPort;

    auto pos = decl.find(':');
    if (pos != string::npos) {
        m_host.port = atoi(decl.c_str() + pos + 1);
    }
    else {
        pos = decl.length();
    }

    m_host.name = decl.substr(0, pos);
}

bool Request::HandleServer() {
    assert(m_bcontext.IsOk());

    if (m_scontext.IsOk()) {
        if (DoHandleServer()) {
            LogInfo(__FUNC__ "Successfully reused socket");
            return true;
        }

        ShutdownServerSocket();
    }

    if (TryDNSCache()) {
        return true;
    }
    else {
        return PostDnsQuery();
    }
}

bool Request::DoHandleServer() {
    assert(m_scontext.IsOk());
    assert(!m_host.tunel);

    // 转发第一批数据到服务器
    auto tc = NewTxContext(m_scontext.sd, m_vbuf.data(), m_vbuf.size() - 1);
    if (!PostSend(tc)) {
        return false;
    }

    if (IsUploadDone()) {
        OnUploadDone();
    }

    // 必须保证同一时刻只有一个读取请求
    assert(m_srxPosted);

    // 始终监听浏览器
    if (!PostRecv(m_bcontext)) {
        return false;
    }

    return true;
}

bool Request::TryDNSCache() {
    assert(!m_qcontext);
    ms_stat.dnsQueries++;

    m_ai = DNSCache::Resolve(m_host.GetFullName());
    if (m_ai) {
        PostConnect();
        return true;
    }

    return false;
}

void Request::DelQueryContext() {
    if (m_qcontext) {
        delete m_qcontext;
        m_qcontext = nullptr;
    }
    // 来自 DNS 缓存
    else if (m_ai) {
        DNSCache::DestroyAddrInfo(m_ai);
    }
    
    m_ai = nullptr;
}

bool Request::PostDnsQuery() {
    AsyncResolver::Request req{m_host.name.c_str(), m_host.port, this};
    return m_resolver.PostResolve(req);
}

void Request::OnQueryCompleted(QueryContext *context) {
    BOOL bResult = PostQueuedCompletionStatus
        (m_cp, 0, SCK_NAME_RESOLVE, &context->ol);

    if (!bResult) {
        ostringstream oss;
        oss << __FUNC__ "PostQueuedCompletionStatus() failed -- "
            << GetLastError();
        LogError(oss.str());

        DeleteThis();
        return;
    }
}

void Request::OnIocpQueryCompleted(QueryContext &context) {
    assert(!m_scontext.IsOk());
    assert(!m_qcontext);

    m_qcontext = &context;
    m_ai = m_qcontext->results;

    PostConnect();
}

bool Bind(SOCKET sd, const ADDRINFOEX &ai) {
    if (ai.ai_family == AF_INET) {
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        addr.sin_family = ai.ai_family;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0;

        if (SOCKET_bind(sd, (sockaddr *)&addr, sizeof(addr)) != 0) {
            auto prefix = __FUNC__ "bind() failed";
            Logger::LogError(WSAGetLastErrorMessage(prefix));

            return false;
        }
    }
    else {
        sockaddr_in6 addr;
        memset(&addr, 0, sizeof(addr));

        addr.sin6_family = ai.ai_family;
        addr.sin6_addr = in6addr_any;
        addr.sin6_port = 0;

        if (SOCKET_bind(sd, (sockaddr *)&addr, sizeof(addr)) != 0) {
            auto prefix = __FUNC__ "bind() failed";
            Logger::LogError(WSAGetLastErrorMessage(prefix));

            return false;
        }
    }

    return true;
}

void Request::PostConnect() {
    if (!m_ai) {
        DelQueryContext();
        DeleteThis();

        return;
    }

    const ADDRINFOEX &ai = *m_ai;

    SOCKET sd = socket(ai.ai_family, ai.ai_socktype, ai.ai_protocol);
    if (sd == INVALID_SOCKET) {
        LogError(WSAGetLastErrorMessage(__FUNC__ "socket() failed"));

        DeleteThis();
        return;
    }

    if (!AssociateWithCompletionPort(sd, m_cp, (ULONG_PTR) this)) {
        DeleteThis();
        return;
    }

    if (!Bind(sd, ai)) {
        DeleteThis();
        return;
    }

    m_ccontext.sd = sd;

    // 假如本身来自缓存，就不要重新加进去了
    if (!m_qcontext) {
        DNSCache::Add(m_host.GetFullName(), *m_ai);
    }
    
    m_ai = m_ai->ai_next;

    //-------------------------------------------

    void *p = nullptr;
    size_t len = 0;

    if (!m_host.tunel) {
        p = m_vbuf.data();
        len = m_vbuf.size() - 1;
    }

    BOOL bResult = lpfnConnectEx(m_ccontext.sd,
                                 ai.ai_addr, 
                                 ai.ai_addrlen,
                                 p, len,
                                 &m_ccontext.tx,
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
    assert(m_ccontext.IsOk());

    if (!m_ccontext.connected) {
        DNSCache::Remove(m_host.GetFullName());

        closesocket(m_ccontext.sd);
        m_ccontext.Reset();

        PostConnect();
        return;
    }

    DelQueryContext();

    if (!m_host.tunel && m_ccontext.tx < m_vbuf.size() - 1) {
        DeleteThis(); // TODO: 重发？
        return;
    }

    m_scontext.sd = m_ccontext.sd;
    m_ccontext.Reset();

    LogInfo("Connected to server");

    // 读取服务器回应
    if (!PostRecv(m_scontext)) {
        DeleteThis();
        return;
    }

    // 转而处理浏览器

    if (m_host.tunel) {
        const char *confirm = "HTTP/1.1 200 Connection Established\r\n\r\n";
        auto tc = NewTxContext(m_bcontext.sd, confirm, strlen(confirm));

        if (!PostSend(tc)) {
            DeleteThis();
            return;
        }
    }
    else {
        if (IsUploadDone()) {
            OnUploadDone();
        }
    }

    // 始终继续监听浏览器端
    if (!PostRecv(m_bcontext)) {
        DeleteThis();
        return;
    }
}

void Request::FilterBrowserHeaders() {
    assert(!m_vbuf.empty());
    ostringstream ss;

    string firstLine(m_vbuf.data(), strchr(m_vbuf.data(), '\r'));
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

    return NewTxContext(sd, context.buf, context.rx);
}

/*static*/
TxContext *Request::NewTxContext(SOCKET sd, const char *buf, int len) {
    TxContext *tc = TxContextPool::GetInstance().Allocate();
    tc->Init(sd, buf, len);

    return tc;
}

/*static*/
void Request::DelTxContext(TxContext *context) {
    assert(context);

    context->Reset();
    TxContextPool::GetInstance().DeAllocate(context);
}

void Request::SetRxReqPostedMark(bool browser, bool posted) {
    if (browser) {
        assert(posted && !m_brxPosted || !posted && m_brxPosted);
        m_brxPosted = posted;
    }
    else {
        assert(posted && !m_srxPosted || !posted && m_srxPosted);
        m_srxPosted = posted;
    }
}

bool Request::PostRecv(RxContext &context) {
    // 其他线程关闭了连接
    if (!m_bcontext.IsOk() || !context.IsOk()) {
        return false;
    }

    assert(context.sd != m_bcontext.sd || !m_brxPosted);
    assert(context.sd != m_scontext.sd || !m_srxPosted);

    context.PrepareForNextRecv();

    // 先设置“读已提交”标志位
    // 接收完成通知可能是立刻发送的，若标志位没有被及时设置，
    // 多线程环境下另一线程测试标志位时可能会失败。
    SetRxReqPostedMark(IsBrowserOrientedContext(context), true);

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

            // 取消标志位
            SetRxReqPostedMark(IsBrowserOrientedContext(context), false);

            return false;
        }
    }

    return true;
}

bool Request::PostSend(TxContext *context) {
    assert(context);

    // 当我们向浏览器发送数据时，其他线程关闭了面向浏览器的读连接
    if (!m_bcontext.IsOk() || !context->IsOk()) {
        DelTxContext(context);
        return false;
    }

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
    if (level >= Logger::LEVEL) {
        ostringstream oss;
        oss << "[0x" << hex << this << "] " << dec;

        if (m_host.port > 0) {
            oss << m_host.name << ':' << m_host.port << '\n';
        }

        oss << msg;
        Logger::Log(oss.str(), level);
    }
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
                this->m.emplace(piecewise_construct,
                                forward_as_tuple(b, colon),
                                forward_as_tuple(colon + 2, e));
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

    // HTTP/1.1 默认是保持连接
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
    string fullName(name.length() + 1 + 5, 0);
    auto p = &fullName[0];

    p = strcpy(p, name.data()) + name.length();
    *p++ = ':';
    
    auto written = sprintf_s(p, 6, "%u", port);
    fullName.resize(fullName.length() - (5 - written));

    return fullName;
}

//////////////////////////////////////////////////////////////////////////

RequestPool &RequestPool::GetInstance() {
    static RequestPool s_pool;
    return s_pool;
}

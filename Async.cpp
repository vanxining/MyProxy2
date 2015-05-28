
#include "Async.hpp"
#include "Logger.hpp"

#include <cassert>

//////////////////////////////////////////////////////////////////////////

AsyncResolver::QueryContext::QueryContext
(AsyncResolver *resolver, const Request &request) {
    memset(this, 0, sizeof(QueryContext));

    port = request.port;

    const char *p = (const char *) request.host;
    std::mbstate_t state = std::mbstate_t();
    auto len = std::mbsrtowcs(NULL, &p, 0, &state) + 1;
    host = new wchar_t[len];
    std::mbsrtowcs(host, &p, len, &state);

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // WTF!! swprintf_s 都写了一些什么垃圾数据进去？？
    swprintf(service, sizeof(service), L"%u", port);

    this->resolver = resolver;
    userData = request.userData;
}

AsyncResolver::QueryContext::~QueryContext() {
    delete [] host;

    if (results) {
        FreeAddrInfoEx(results);
    }
}

//////////////////////////////////////////////////////////////////////////

AsyncResolver::AsyncResolver(Callback *callback)
    : m_callback(callback) {

}

AsyncResolver::~AsyncResolver() {

}

bool AsyncResolver::PostResolve(const Request &request) {
    QueryContext *context = new QueryContext(this, request);

    int error = GetAddrInfoExW(context->host,
                               context->service,
                               NS_DNS,
                               nullptr,
                               &context->hints,
                               &context->results,
                               nullptr,
                               &context->ol,
                               OnDnsResolved,
                               nullptr);

    //
    //  If GetAddrInfoExW() returns WSA_IO_PENDING, GetAddrInfoExW will
    //  invoke the completion routine. If GetAddrInfoExW returned anything
    //  else we must invoke the completion directly.
    //

    // 当主机名为 IP 地址时，会直接成功
    if (error != WSA_IO_PENDING) {
        OnDnsResolved(error, 0, &context->ol);
        return error == ERROR_SUCCESS;
    }

    return true;
}

/*static*/
void CALLBACK AsyncResolver::OnDnsResolved
(DWORD error, DWORD, LPWSAOVERLAPPED ol) {
    if (error != ERROR_SUCCESS) {
        Logger::LogError(__FUNC__ "GetAddrInfoExW() failed");
    }

    QueryContext *context = (QueryContext *) ol;
    context->resolver->m_callback->OnQueryCompleted(context);
}

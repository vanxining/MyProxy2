
#include "Proxy.hpp"
#include "Request.hpp"
#include "Logger.hpp"

#include <mswsock.h>

#include <sstream>
using namespace std;

#include "Debug.hpp"


//////////////////////////////////////////////////////////////////////////

LPFN_ACCEPTEX lpfnAcceptEx;
LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;
LPFN_CONNECTEX lpfnConnectEx;


//////////////////////////////////////////////////////////////////////////

static int GetThreadCount() {
    return GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
}

bool AssociateWithCompletionPort(SOCKET sd, HANDLE cp, ULONG_PTR key) {
    // Associate the accept socket with the completion port.
    HANDLE cp2 = CreateIoCompletionPort((HANDLE) sd, cp, key, 0);

    // cp2 should be cp if this succeeds.
    if (cp2 != cp) {
        ostringstream ss;
        ss << "CreateIoCompletionPort() associate failed with error: "
           <<  GetLastError() << endl;
        Logger::LogError(ss.str());

        return false;
    }

    return true;
}


//////////////////////////////////////////////////////////////////////////

MyProxy::MyProxy()
    : m_listener(INVALID_SOCKET) {
    m_numExitedThreads = 0;

    // 强制初始化内存池
    TxContextPool::GetInstance();
    RequestPool::GetInstance();
}

MyProxy::~MyProxy() {
    if (m_cp && m_listener != INVALID_SOCKET) {
        int count = GetThreadCount();

        for (int i = 0; i < count; i++) {
            PostQueuedCompletionStatus(m_cp, 0, SCK_EXIT, nullptr);
        }

        while (m_numExitedThreads < count) {
            Sleep(100);
        }

        closesocket(m_listener);
        m_listener = INVALID_SOCKET;

        CloseHandle(m_cp);
        m_cp = nullptr;
    }
}

bool MyProxy::Start(const char *addr, u_short port) {
    enum {
        INIT_REQUEST_POOL_SIZE = 64,
    };

    return SetUpListener(addr, port) &&
           GetIocpFunctionPointers(m_listener) &&
           SpawnThreads() &&
           SpawnAcceptors(INIT_REQUEST_POOL_SIZE);
}

/*static*/
bool MyProxy::GetIocpFunctionPointers(SOCKET sd) {
    if (lpfnAcceptEx && lpfnGetAcceptExSockAddrs) {
        return true;
    }

    // Load the AcceptEx function into memory using WSAIoctl.
    // The WSAIoctl function is an extension of the ioctlsocket()
    // function that can use overlapped I/O. The function's 3rd
    // through 6th parameters are input and output buffers where
    // we pass the pointer to our AcceptEx function. This is used
    // so that we can call the AcceptEx function directly, rather
    // than refer to the Mswsock.lib library.

    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    DWORD dwBytes;

    int iResult = WSAIoctl(sd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                           &GuidAcceptEx, sizeof(GuidAcceptEx), 
                           &lpfnAcceptEx, sizeof(lpfnAcceptEx), 
                           &dwBytes, NULL, NULL);

    if (iResult == SOCKET_ERROR) {
        auto fmt = __FUNC__ "WSAIoctl(WSAID_ACCEPTEX) failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        return false;
    }

    GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
    iResult = WSAIoctl(sd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                       &GuidGetAcceptExSockAddrs, 
                       sizeof(GuidGetAcceptExSockAddrs), 
                       &lpfnGetAcceptExSockAddrs, 
                       sizeof(lpfnGetAcceptExSockAddrs),
                       &dwBytes, NULL, NULL);

    if (iResult == SOCKET_ERROR) {
        auto fmt = __FUNC__ "WSAIoctl(WSAID_GETACCEPTEXSOCKADDRS) failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        return false;
    }
    
    GUID GuidConnectEx = WSAID_CONNECTEX;
    iResult = WSAIoctl(sd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                       &GuidConnectEx, 
                       sizeof(GuidConnectEx),
                       &lpfnConnectEx, 
                       sizeof(lpfnConnectEx),
                       &dwBytes, NULL, NULL);

    if (iResult == SOCKET_ERROR) {
        auto fmt = __FUNC__ "WSAIoctl(WSAID_CONNECTEX) failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        return false;
    }

    return true;
}

bool MyProxy::SetUpListener(const char *addr, int port) {
    ostringstream oss;
    
    m_cp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    if (m_cp == nullptr) {
        oss << __FUNC__ "CreateIoCompletionPort() failed with error: "
            << GetLastError();

        Logger::LogError(oss.str());
        return false;
    }

    // The socket function creates a socket that supports overlapped I/O 
    // operations as the default behavior.
    m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listener == INVALID_SOCKET) {
        oss << WSAGetLastErrorMessage(__FUNC__ "bind() failed") << endl;
        Logger::LogError(oss.str());

        CloseHandle(m_cp);
        m_cp = nullptr;

        return false;
    }
    
    // Associate the listening socket with the completion port.
    HANDLE cp = CreateIoCompletionPort((HANDLE) m_listener, m_cp, 0, 0);

    if (cp == nullptr) {
        oss << "CreateIoCompletionPort() associate failed with error: "
            <<  GetLastError() << endl;
        Logger::LogError(oss.str());

        return false;
    }

    // Sets up a listener on the given interface and port, returning the
    // listening socket if successful; if not, returns INVALID_SOCKET.
    u_long nInterfaceAddr = inet_addr(addr);
    if (nInterfaceAddr == INADDR_NONE) {
        auto fmt = __FUNC__ "inet_addr() failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        goto ERROR_HANDLER;
    }

    sockaddr_in sinInterface;
    sinInterface.sin_family = AF_INET;
    sinInterface.sin_addr.s_addr = nInterfaceAddr;
    sinInterface.sin_port = htons(port);

    if (SOCKET_bind(m_listener, (sockaddr *)&sinInterface,
        sizeof(sockaddr_in)) == SOCKET_ERROR) {
            oss << WSAGetLastErrorMessage(__FUNC__ "bind() failed") << endl;
            Logger::LogError(oss.str());

            goto ERROR_HANDLER;
    }

    if (listen(m_listener, SOMAXCONN) == SOCKET_ERROR) {
        oss << WSAGetLastErrorMessage(__FUNC__ "listen() failed") << endl;
        Logger::LogError(oss.str());

        goto ERROR_HANDLER;
    }

    return true;

ERROR_HANDLER:

    closesocket(m_listener);
    m_listener = INVALID_SOCKET;

    CloseHandle(m_cp);
    m_cp = nullptr;

    return false;
}

bool MyProxy::SpawnAcceptors(int num) {
    m_acceptors.reserve(m_acceptors.size() + num);

    for (int i = 0; i < num; i++) {
        RxContext *context = new RxContext(INVALID_SOCKET);
        m_acceptors.emplace_back(context);

        if (!PostAccept(*context)) {
            return false;
        }
    }

    return true;
}

bool MyProxy::PostAccept(RxContext &context) {
    context.Reset();

    // Create an accepting socket.
    SOCKET bsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (bsocket == INVALID_SOCKET) {
        auto fmt = __FUNC__ "socket() failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        return false;
    }

    context.sd = bsocket;
    context.action = PerIoContext::ACCEPT; // TODO:

    BOOL bRetVal = lpfnAcceptEx(m_listener,
                                context.sd,
                                context.buf,
                                RxContext::DATA_CAPACITY,
                                RxContext::ADDR_LEN, 
                                RxContext::ADDR_LEN,
                                &context.rx,
                                &context.ol);

    if (bRetVal == TRUE) {
        DoAccept(context);
    }
    else {
        int ec = WSAGetLastError();
        if (ec != ERROR_IO_PENDING) {
            auto fmt = __FUNC__ "AcceptEx() failed";
            Logger::LogError(WSAGetLastErrorMessage(fmt, ec));

            // 必须在 WSAGetLastErrorMessage() 之后调用！
            closesocket(bsocket);

            return false;
        }
    }

    return true;
}

bool MyProxy::SpawnThreads() {
    int count = GetThreadCount();
    for (int i = 0; i < count; i++) {
        HANDLE h = CreateThread(nullptr, 0, ProxyHandler, this, 0, nullptr);
        if (h == nullptr) {
            ostringstream ss;
            ss << "CreateThread() failed --" << GetLastError();
            Logger::LogError(ss.str());

            return false;
        }
    }

    return true;
}

/*static*/
DWORD CALLBACK MyProxy::ProxyHandler(PVOID pv) {
    ostringstream oss;

    MyProxy *This = (MyProxy *) pv;

    DWORD transfered;
    ULONG_PTR key;
    PerIoContext *pic;

    while (true) {
        if (!GetQueuedCompletionStatus(This->m_cp, &transfered, &key,
                                      (LPWSAOVERLAPPED *) &pic,
                                       INFINITE)) {
            switch (GetLastError()) {
            // 现时我们只在异步连接操作中应用了超时机制
            case ERROR_SEM_TIMEOUT:
                if (pic->action == PerIoContext::CONNECT) {
                    transfered = -1;

                    goto HANDLERS;
                }
                else {
                    Logger::LogError(__FUNC__ "What timed out?");
                }

                break;

            // 为防止内存泄漏，我们总是处理这些套接字已失效的异常情况

            // 似乎这两个错误是由我们主动 closesocket() 引发的
            case ERROR_OPERATION_ABORTED:
            case ERROR_INVALID_NETNAME:
            // 似乎由浏览器端强制断开引发的
            case ERROR_NETNAME_DELETED:
                transfered = 0;

                goto HANDLERS;

            default:
                oss << __FUNC__ "GetQueuedCompletionStatus() failed -- "
                    << GetLastError();

                Logger::LogError(oss.str());
                oss.str(string());

                break;
            }

            continue;
        }

        if (key == SCK_EXIT) {
            This->m_numExitedThreads++;
            break;
        }
        else if (key == SCK_NAME_RESOLVE) {
            auto context = (AsyncResolver::QueryContext *) pic;
            Request *req = (Request *) context->userData;

            req->OnIocpQueryCompleted(*context);
            continue;
        }

HANDLERS:
        switch (pic->action) {
        case PerIoContext::ACCEPT: {
            RxContext *context = (RxContext *) pic;
            context->rx = transfered;

            if (transfered > 0) {
                This->DoAccept(*context);
            }
            else {
                // TODO: 莫明其妙
                ShutdownConnection(context->sd);
                This->PostAccept(*context);
            }

            break;
        }

        case PerIoContext::CONNECT: {
            ConnectContext *context = (ConnectContext *) pic;
            
            if (transfered == -1) {
                context->tx = 0;
                context->connected = false;
            }
            else {
                context->tx = transfered;
                context->connected = true;
            }

            Request *req = (Request *) key;
            req->OnConnectCompleted();

            break;
        }

        case PerIoContext::RECV: {
            RxContext *context = (RxContext *) pic;
            context->rx = transfered;

            Request *req = (Request *) key;
            req->OnRecvCompleted(*context);

            break;
        }

        case PerIoContext::SEND: {
            TxContext *context = (TxContext *) pic;
            context->tx = transfered;

            Request *req = (Request *) key;
            req->OnSendCompleted(context);

            break;
        }

        default:
            break;
        }
    }

    Logger::LogInfo("Worker thread ended.");
    return 0;
}

void MyProxy::DoAccept(RxContext &context) {
    Request *req = RequestPool::GetInstance().Allocate();
    req->Init(m_cp, context);

    // Associate the accept socket with the completion port.
    if (!AssociateWithCompletionPort(context.sd, m_cp, (ULONG_PTR) req)) {
        RequestPool::GetInstance().DeAllocate(req);
        return;
    }

    sockaddr_in *local, *remote;
    int i1, i2;

    lpfnGetAcceptExSockAddrs((LPVOID) context.buf, 
                              RxContext::DATA_CAPACITY,
                              RxContext::ADDR_LEN,
                              RxContext::ADDR_LEN,
                             (LPSOCKADDR *) &local, &i1,
                             (LPSOCKADDR *) &remote, &i2);

    ostringstream ss;
    ss << "Accepted connection from " << inet_ntoa(remote->sin_addr)
       << ":" << ntohs(remote->sin_port);
    Logger::LogInfo(ss.str());

    req->HandleBrowser();

    // 重新发起异步 acceptor
    PostAccept(context);
}

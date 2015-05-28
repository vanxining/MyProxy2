
#include "Proxy.hpp"
#include "Request.hpp"
#include "Logger.hpp"

#include <mswsock.h>

#include <sstream>
using namespace std;


//////////////////////////////////////////////////////////////////////////

LPFN_ACCEPTEX lpfnAcceptEx;
LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;
LPFN_CONNECTEX lpfnConnectEx;


//////////////////////////////////////////////////////////////////////////

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
    : m_listener(INVALID_SOCKET),
      m_cp(nullptr),
      m_requestPoolSize(0),
      m_numActiveRequests(0) {
    
}

MyProxy::~MyProxy() {
    if (m_cp) {
        CloseHandle(m_cp);
        m_cp = nullptr;
    }

    if (m_listener != INVALID_SOCKET) {
        closesocket(m_listener);
        m_listener = INVALID_SOCKET;
    }
}

bool MyProxy::Run(const char *addr, u_short port) {
    enum {
        INIT_REQUEST_POOL_SIZE = 64,
    };

    if (SetUpListener(addr, port) &&
        GetIocpFunctionPointers(m_listener) &&
        SpawnThreads() &&
        SpawnAcceptors(INIT_REQUEST_POOL_SIZE)) {
            while (true) {
                Sleep(10 * 1000);
            }

            return true;
    }

    return false;
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
    ostringstream ss;
    
    m_cp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
    if (m_cp == nullptr) {
        ss << __FUNC__ "CreateIoCompletionPort() failed with error: "
           << GetLastError();

        Logger::LogError(ss.str());
        return false;
    }

    // The socket function creates a socket that supports overlapped I/O 
    // operations as the default behavior.
    m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listener == INVALID_SOCKET) {
        ss << WSAGetLastErrorMessage(__FUNC__ "bind() failed") << endl;
        Logger::LogError(ss.str());

        return false;
    }
    
    // Associate the listening socket with the completion port.
    HANDLE cp = CreateIoCompletionPort((HANDLE) m_listener, m_cp, 0, 0);

    if (cp == nullptr) {
        ss << "CreateIoCompletionPort() associate failed with error: "
           <<  GetLastError() << endl;

        Logger::LogError(ss.str());
        return false;
    }

    // Sets up a listener on the given interface and port, returning the
    // listening socket if successful; if not, returns INVALID_SOCKET.
    u_long nInterfaceAddr = inet_addr(addr);
    if (nInterfaceAddr == INADDR_NONE) {
        auto fmt = __FUNC__ "inet_addr() failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        return false;
    }

    sockaddr_in sinInterface;
    sinInterface.sin_family = AF_INET;
    sinInterface.sin_addr.s_addr = nInterfaceAddr;
    sinInterface.sin_port = htons(port);

    if (bind(m_listener, (sockaddr *) &sinInterface, 
        sizeof(sockaddr_in)) == SOCKET_ERROR) {
            ss << WSAGetLastErrorMessage(__FUNC__ "bind() failed") << endl;
            Logger::LogError(ss.str());

            return false;
    }

    if (listen(m_listener, SOMAXCONN) == SOCKET_ERROR) {
        ss << WSAGetLastErrorMessage(__FUNC__ "listen() failed") << endl;
        Logger::LogError(ss.str());

        return false;
    }

    return true;
}

bool MyProxy::SpawnAcceptors(int num) {
    for (int i = 0; i < num; i++) {
        RxContext *context = new RxContext(INVALID_SOCKET);
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
    DWORD dwProcessors = GetMaximumProcessorCount(ALL_PROCESSOR_GROUPS);
    for (DWORD i = 0; i < dwProcessors; i++) {
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

                    goto CONNECT_HANDLER;
                }
                else {
                    Logger::LogError(__FUNC__ "What timed out?");
                }

                break;

            // 似乎这两个错误是由我们主动 closesocket() 引发的
            case ERROR_OPERATION_ABORTED:
            case ERROR_INVALID_NETNAME:
                break;

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
CONNECT_HANDLER:
            ConnectContext *context = (ConnectContext *) pic;
            
            if (transfered != -1) {
                context->tx = transfered;
                context->connected = true;
            }
            else {
                context->tx = 0;
                context->connected = false;
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
    Request *req = new Request(context, m_cp);

    // Associate the accept socket with the completion port.
    if (!AssociateWithCompletionPort(context.sd, m_cp, (ULONG_PTR) req)) {
        delete req;
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

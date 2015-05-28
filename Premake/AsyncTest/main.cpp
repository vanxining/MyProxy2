
#include "../../Async.hpp"

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <cstdio>

//////////////////////////////////////////////////////////////////////////

static AsyncResolver::QueryContext *gs_context;

class MyCallback : public AsyncResolver::Callback {
public:

    typedef AsyncResolver::QueryContext QueryContext;

    virtual void OnQueryCompleted(QueryContext *context) override {
        delete context;
    }
};

int main() {
    INT                 Error = ERROR_SUCCESS;
    WSADATA             wsaData;

    //
    //  All Winsock functions require WSAStartup() to be called first
    //

    Error = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (Error != 0) {
        printf("WSAStartup failed with %d\n", Error);
        return -1;
    }

    MyCallback callback;
    AsyncResolver conn(&callback);

    AsyncResolver::Request req{"www.baidu.com", 80, nullptr};
    conn.PostResolve(req);

    Sleep(2 * 1000);

    delete gs_context;
    return 0;
}

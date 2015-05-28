//////////////////////////////////////////////////////////////////////////
// ws-util.h - Declarations for the Winsock utility functions module.
//////////////////////////////////////////////////////////////////////////

#pragma once

#define FD_SETSIZE 2
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <string>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define LINE_NO TOSTRING(__LINE__)

#define __FUNC__ __FUNCTION__ "() [" LINE_NO "] --> "


//// Constants ///////////////////////////////////////////////////////////

enum {
    kBufferSize = 8192,
    kExtraBytes = 256,
    kSafeBufferSize = kBufferSize + kExtraBytes,
};


//// Functions ///////////////////////////////////////////////////////////

std::string WSAGetLastErrorMessage
    (const char* pcMessagePrefix, int nErrorID = 0);

/// 关闭一个连接
bool ShutdownConnection(SOCKET sd, bool gracefully = false);

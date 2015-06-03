/***********************************************************************
 ws-util.cpp - Some basic Winsock utility functions.

 This program is hereby released into the public domain.  There is
 ABSOLUTELY NO WARRANTY WHATSOEVER for this product.  Caveat hacker.
***********************************************************************/

#include "ws-util.h"
#include "Logger.hpp"

#include <iostream>
#include <sstream>
#include <algorithm> // for lower_bound()
#include <cassert>
using namespace std;

#include "Debug.hpp"


//// Statics ///////////////////////////////////////////////////////////

// List of Winsock error constants mapped to an interpretation string.
// Note that this list must remain sorted by the error constants'
// values, because we do a binary search on the list when looking up
// items.
struct ErrorEntry {
    int nID;
    const char* pcMessage;

    ErrorEntry(int id, const char *msg = nullptr)
        :  nID(id), pcMessage(msg)  {

    }

    bool operator<(const ErrorEntry &rhs) const {
        return nID < rhs.nID;
    }
};

static ErrorEntry gaErrorList[] = {
    ErrorEntry(0,                  "No error"),
    ErrorEntry(WSA_IO_PENDING,     "Overlapped I/O operation is in progress"),
    ErrorEntry(WSAEINTR,           "Interrupted system call"),
    ErrorEntry(WSAEBADF,           "Bad file number"),
    ErrorEntry(WSAEACCES,          "Permission denied"),
    ErrorEntry(WSAEFAULT,          "Bad address"),
    ErrorEntry(WSAEINVAL,          "Invalid argument"),
    ErrorEntry(WSAEMFILE,          "Too many open sockets"),
    ErrorEntry(WSAEWOULDBLOCK,     "Operation would block"),
    ErrorEntry(WSAEINPROGRESS,     "Operation now in progress"),
    ErrorEntry(WSAEALREADY,        "Operation already in progress"),
    ErrorEntry(WSAENOTSOCK,        "Socket operation on non-socket"),
    ErrorEntry(WSAEDESTADDRREQ,    "Destination address required"),
    ErrorEntry(WSAEMSGSIZE,        "Message too long"),
    ErrorEntry(WSAEPROTOTYPE,      "Protocol wrong type for socket"),
    ErrorEntry(WSAENOPROTOOPT,     "Bad protocol option"),
    ErrorEntry(WSAEPROTONOSUPPORT, "Protocol not supported"),
    ErrorEntry(WSAESOCKTNOSUPPORT, "Socket type not supported"),
    ErrorEntry(WSAEOPNOTSUPP,      "Operation not supported on socket"),
    ErrorEntry(WSAEPFNOSUPPORT,    "Protocol family not supported"),
    ErrorEntry(WSAEAFNOSUPPORT,    "Address family not supported"),
    ErrorEntry(WSAEADDRINUSE,      "Address already in use"),
    ErrorEntry(WSAEADDRNOTAVAIL,   "Can't assign requested address"),
    ErrorEntry(WSAENETDOWN,        "Network is down"),
    ErrorEntry(WSAENETUNREACH,     "Network is unreachable"),
    ErrorEntry(WSAENETRESET,       "Net connection reset"),
    ErrorEntry(WSAECONNABORTED,    "Software caused connection abort"),
    ErrorEntry(WSAECONNRESET,      "Connection reset by peer"),
    ErrorEntry(WSAENOBUFS,         "No buffer space available"),
    ErrorEntry(WSAEISCONN,         "Socket is already connected"),
    ErrorEntry(WSAENOTCONN,        "Socket is not connected"),
    ErrorEntry(WSAESHUTDOWN,       "Can't send after socket shutdown"),
    ErrorEntry(WSAETOOMANYREFS,    "Too many references, can't splice"),
    ErrorEntry(WSAETIMEDOUT,       "Connection timed out"),
    ErrorEntry(WSAECONNREFUSED,    "Connection refused"),
    ErrorEntry(WSAELOOP,           "Too many levels of symbolic links"),
    ErrorEntry(WSAENAMETOOLONG,    "File name too long"),
    ErrorEntry(WSAEHOSTDOWN,       "Host is down"),
    ErrorEntry(WSAEHOSTUNREACH,    "No route to host"),
    ErrorEntry(WSAENOTEMPTY,       "Directory not empty"),
    ErrorEntry(WSAEPROCLIM,        "Too many processes"),
    ErrorEntry(WSAEUSERS,          "Too many users"),
    ErrorEntry(WSAEDQUOT,          "Disc quota exceeded"),
    ErrorEntry(WSAESTALE,          "Stale NFS file handle"),
    ErrorEntry(WSAEREMOTE,         "Too many levels of remote in path"),
    ErrorEntry(WSASYSNOTREADY,     "Network system is unavailable"),
    ErrorEntry(WSAVERNOTSUPPORTED, "Winsock version out of range"),
    ErrorEntry(WSANOTINITIALISED,  "WSAStartup not yet called"),
    ErrorEntry(WSAEDISCON,         "Graceful shutdown in progress"),
    ErrorEntry(WSAHOST_NOT_FOUND,  "Host not found"),
    ErrorEntry(WSANO_DATA,         "No host data of that type was found")
};

const static int kNumMessages = sizeof(gaErrorList) / sizeof(ErrorEntry);


//// WSAGetLastErrorMessage ////////////////////////////////////////////
// A function similar in spirit to Unix's perror() that tacks a canned 
// interpretation of the value of WSAGetLastError() onto the end of a
// passed string, separated by a ": ".  Generally, you should implement
// smarter error handling than this, but for default cases and simple
// programs, this function is sufficient.
//
// This function returns a pointer to an internal static buffer, so you
// must copy the data from this function before you call it again.  It
// follows that this function is also not thread-safe.

string WSAGetLastErrorMessage(const char *pcMessagePrefix, int nErrorID) {
    // Build basic error string
    ostringstream ss;

    if (pcMessagePrefix && strlen(pcMessagePrefix) > 0) {
        ss << pcMessagePrefix << ": ";
    }

    if (nErrorID == 0) {
        nErrorID = WSAGetLastError();
    }

    // Tack appropriate canned message onto end of supplied message 
    // prefix. Note that we do a binary search here: gaErrorList must be
	// sorted by the error constant's value.
	ErrorEntry *pEnd = gaErrorList + kNumMessages;
    ErrorEntry Target(nErrorID);
    ErrorEntry *it = lower_bound(gaErrorList, pEnd, Target);
    if ((it != pEnd) && (it->nID == Target.nID)) {
        ss << it->pcMessage;
    }
    else {
        // Didn't find error in list, so make up a generic one
        ss << "unknown error";
    }
    ss << " (" << Target.nID << ")";

    // Finish error message off and return it.
    return ss.str();
}


//// ShutdownConnection ////////////////////////////////////////////////
// Gracefully shuts the connection sd down.  Returns true if we're
// successful, false otherwise.

bool ShutdownConnection(SOCKET sd, bool gracefully) {
    assert(sd != INVALID_SOCKET);

    // Disallow any further data sends.  This will tell the other side
    // that we want to go away now.
    // If we skip this step, we don't shut the connection down nicely.
    if (gracefully && shutdown(sd, SD_SEND) == SOCKET_ERROR) {
        auto fmt = __FUNC__ "shutdown() failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));
    }

    if (CancelIoEx((HANDLE) sd, nullptr) == FALSE) {
        if (GetLastError() != ERROR_NOT_FOUND) {
            ostringstream ss;
            ss << __FUNC__ "CancelIoEx() failed -- " << GetLastError();
            Logger::LogError(ss.str());
        }
    }

    // Close the socket.
    if (closesocket(sd) == SOCKET_ERROR) {
        auto fmt = __FUNC__ "closesocket() failed";
        Logger::LogError(WSAGetLastErrorMessage(fmt));

        return false;
    }

    return true;
}

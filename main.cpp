/***********************************************************************
 main.cpp - The main() routine for all the "Basic Winsock" suite of
    programs from the Winsock Programmer's FAQ.  This function parses
    the command line, starts up Winsock, and calls an external function
    called DoWinsock to do the actual work.

 This program is hereby released into the public domain.  There is
 ABSOLUTELY NO WARRANTY WHATSOEVER for this product.  Caveat hacker.
***********************************************************************/

#include "Proxy.hpp"
#include "Logger.hpp"

#pragma comment(lib, "ws2_32.lib")

#include <stdlib.h>
#include <iostream>
using namespace std;

#include "Debug.hpp"


//// Prototypes ////////////////////////////////////////////////////////

extern int DoWinsock(const char *pcHost, int nPort);


//// Constants /////////////////////////////////////////////////////////

// Default port to connect to on the server
const static int kDefaultServerPort = 1990;

// 代理服务器
static MyProxy *gs_proxy;

// 继续运行
static bool gs_runing = true;


BOOL WINAPI ConsoleHandler(DWORD event) {
    switch (event) {
    case CTRL_C_EVENT:
        gs_runing = false;
        return TRUE;

    default:
        break;
    }

    return FALSE;
}


//// main //////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
#ifdef _DEBUG
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flags);
    assert(!errno);
#endif

    const char *pcHost = "127.0.0.1";
    int nPort = kDefaultServerPort;

    // Do we have enough command line arguments?
    if (argc >= 2) {
        // Get host and (optionally) port from the command line
        pcHost = argv[1];
        if (argc >= 3) {
            nPort = atoi(argv[2]);
        }
    }

    // Do a little sanity checking because we're anal.
    int nNumArgsIgnored = (argc - 3);
    if (nNumArgsIgnored > 0) {
        cerr << nNumArgsIgnored << " extra argument" <<
               (nNumArgsIgnored == 1 ? "" : "s") << " ignored.  FYI.\n";
    }

    // Start Winsock up
    WSAData wsaData;
	int nCode;
    if ((nCode = WSAStartup(MAKEWORD(2, 2), &wsaData)) != 0) {
		cerr << "WSAStartup() returned error code " << nCode << ".\n";

        return 255;
    }

    if (SetConsoleCtrlHandler(ConsoleHandler, TRUE)) {
        printf("\nThe Control Handler is installed.\n");
        printf("\n -- Now try pressing Ctrl+C or Ctrl+Break, or");
        printf("\n    try logging off or closing the console...\n");
    }
    else {
        printf("\nERROR: Could not set control handler");
        return 31;
    }

    Logger::CONSOLE = false;
    Logger::LEVEL = Logger::OL_INFO;

    gs_proxy = new MyProxy;
    if (!gs_proxy->Start(pcHost, nPort)) {
        delete gs_proxy;
        return 127;
    }

    gs_runing = true;

    // 一直睡眠
    while (gs_runing) {
        Sleep(1000);
    }

    delete gs_proxy;
    printf("\nProxy server stopped.\n");

    // Shut Winsock back down and take off.
    WSACleanup();

    return 0;
}

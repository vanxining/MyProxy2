/***********************************************************************
 iocp-server.cpp
***********************************************************************/

#include "Proxy.hpp"
#include "Logger.hpp"

#include <iostream>
using namespace std;


//// Global variables //////////////////////////////////////////////////


//// DoWinsock /////////////////////////////////////////////////////////
// The module's driver function -- we just call other functions and
// interpret their results.

int DoWinsock(const char *pcAddress, int nPort) {
    cout << "Waiting for connections..." << endl;

    Logger::CONSOLE = false;
    Logger::LEVEL = Logger::OL_INFO;

    MyProxy proxy;
    return proxy.Run(pcAddress, nPort) ? 0 : 3;
}

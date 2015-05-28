
#include "PerIoContext.hpp"


//////////////////////////////////////////////////////////////////////////

PerIoContext::PerIoContext(SOCKET sd, Action action)
    : sd(sd), action(action) {
    memset(&ol, 0, sizeof(ol));
}

bool PerIoContext::IsOk() const {
    return sd != INVALID_SOCKET;
}

//////////////////////////////////////////////////////////////////////////

ConnectContext::ConnectContext(SOCKET sd)
    : PerIoContext(sd, CONNECT), tx(0), connected(false) {

}

void ConnectContext::Reset() {
    sd = INVALID_SOCKET;
    tx = 0;
}

//////////////////////////////////////////////////////////////////////////

RxContext::RxContext(SOCKET sd)
    : PerIoContext(sd, RECV), rx(0) {
    bufSpec.buf = buf;
    bufSpec.len = BUFFER_SIZE;
}

RxContext::RxContext(const RxContext &other)
    : RxContext(other.sd) {
    rx = other.rx;
    memcpy(buf, other.buf, rx);
}

void RxContext::PrepareForNextRecv() {
    memset(&ol, 0, sizeof(ol));
    rx = 0;
}

void RxContext::Reset() {
    PrepareForNextRecv();
    sd = INVALID_SOCKET;
}

//////////////////////////////////////////////////////////////////////////

TxContext::TxContext(SOCKET sd, const char *buf, int len)
    : PerIoContext(sd, SEND), tx(0) {
    buffers = new WSABUF; // TODO:
    nb = 1;

    buffers->len = len;
    buffers->buf = new char[len];
    memcpy(buffers->buf, buf, len);
}

TxContext::~TxContext() {
    delete [] buffers->buf;
    delete buffers;
    buffers = nullptr;
}

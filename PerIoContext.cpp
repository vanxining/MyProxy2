
#include "PerIoContext.hpp"
#include "Debug.hpp"


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

RxContext &RxContext::operator=(const RxContext &other) {
    Reset();

    sd = other.sd;
    rx = other.rx;
    memcpy(buf, other.buf, rx);

    return *this;
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

namespace {
struct FixedSizeBuffer {
    enum {
        STATIC_POOL_SIZE = TxContext::STATIC_POOL_SIZE,
        DYNAMIC_POOL_SIZE = TxContext::DYNAMIC_POOL_SIZE,
    };

    static bool IsRecyclable() {
        return true;
    }

    char buf[kBufferSize];
};

class FixedSizeBufferPool : public MemoryPool<FixedSizeBuffer, std::mutex> {
public:

    /// 获取单体对象
    static FixedSizeBufferPool &GetInstance() {
        static FixedSizeBufferPool s_pool;
        return s_pool;
    }

private:

    FixedSizeBufferPool() {}
};
} // namespace

//////////////////////////////////////////////////////////////////////////

TxContext::TxContext()
    : PerIoContext(INVALID_SOCKET, SEND),
      buffers(nullptr), nb(0), tx(0) {
    
}

TxContext::~TxContext() {
    Reset();
}

void TxContext::Init(SOCKET sd, const char *buf, int len) {
    this->sd = sd;

    auto nb0 = len / kBufferSize;
    auto rest = len - nb0 * kBufferSize;

    nb = nb0;
    if (rest > 0) {
        nb++;
    }

    buffers = new WSABUF[nb];
    auto &pool = FixedSizeBufferPool::GetInstance();

    for (auto i = 0; i < nb0; i++) {
        FixedSizeBuffer *fsb = pool.Allocate();
        memcpy(fsb->buf, buf, kBufferSize);
        buf += kBufferSize;
        
        buffers[i].buf = fsb->buf;
        buffers[i].len = kBufferSize;
    }

    if (nb != nb0) {
        FixedSizeBuffer *fsb = pool.Allocate();
        memcpy(fsb->buf, buf, rest);
        buf += rest;

        buffers[nb0].buf = fsb->buf;
        buffers[nb0].len = rest;
    }
}

void TxContext::Reset() {
    sd = INVALID_SOCKET;
    tx = 0;

    if (buffers) {
        auto &pool = FixedSizeBufferPool::GetInstance();

        for (size_t i = 0; i < nb; i++) {
            pool.DeAllocate((FixedSizeBuffer *) buffers[i].buf);
        }

        delete [] buffers;
        buffers = nullptr;

        nb = 0;
    }
}

//////////////////////////////////////////////////////////////////////////

TxContextPool::TxContextPool() {
    FixedSizeBufferPool::GetInstance();
}

TxContextPool &TxContextPool::GetInstance() {
    static TxContextPool s_pool;
    return s_pool;
}

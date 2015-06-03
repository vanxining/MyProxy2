
#pragma once
#include <list>

/// 空的线程同步对象
struct NullThreadSynchronizer {
    /// 锁定访问
    void lock() {}
    /// 解锁访问
    void unlock() {}
};

/// 内存池
/// @param Node 要分配的结点类型，必须定义有(隐式或显示的)析构函数
/// @param Synchronizer 线程互斥同步器
template <class Node, class Synchronizer>
class MemoryPool {
public:

    /// 列表类型
    typedef std::list<Node *> List;

    /// 表征元素数量的无符号数字类型
    typedef typename List::size_type size_type;

    /// 构造函数
    MemoryPool() {
        Init();
    }

    /// 析构函数
    ~MemoryPool() {
        Clear();
    }

    /// 从自由列表中分配一个新结点
    Node *Allocate() {
        Node *ret = nullptr;
        m_sync.lock();

        // 自由列表
        if (!m_free.empty() && m_free.front()->IsRecyclable()) {
            ret = m_free.front();
            m_free.pop_front();
        }
        // 静态空间
        else if (m_numStaticUsed < Node::STATIC_POOL_SIZE) {
            ret = m_static + m_numStaticUsed;
            m_numStaticUsed++;
        }
        // 动态空间
        else {
            if (m_nextAvailable == m_end) {
                NewChunk();
            }

            ret = m_nextAvailable;
            m_nextAvailable++;
        }

        m_sync.unlock();
        return ret;
    }

    /// 向分配器返还一个结点
    ///
    /// 注意：不会调用该结点的析构函数！！
    void DeAllocate(Node *node) {
        m_sync.lock();
        m_free.push_back(node);
        m_sync.unlock();
    }

    /// Clear the pool
    /// 
    /// This causes memory occupied by nodes allocated by the pool
    /// to be freed. Any nodes allocated from the pool will no longer
    /// be valid.
    void Clear() {
        while (m_header != (BlockHeader *) m_static) {
            byte *previousBegin = m_header->previousBegin;

            delete [] (Node *) m_header;

            m_header = (BlockHeader *) previousBegin;
        }

        Init();
    }

#ifdef _DEBUG
    /// 获取已分配的结点数目
    ///
    /// 仅在调试模式下定义。
    size_type GetTotalAllocated() const {
        return m_totalAllocated;
    }
#endif

private:

    // 初始化为从静态空间中分配
    void Init() {
        m_numStaticUsed = 0;

        m_header = (BlockHeader *) m_static;
        m_nextAvailable = nullptr;
        m_end = nullptr;

#ifdef _DEBUG
        m_totalAllocated = Node::STATIC_POOL_SIZE;
#endif
    }

    // 便捷定义
    typedef char byte;

    // 分配的动态空间头
    struct BlockHeader {
        byte *previousBegin;
        //-------------------------------------
        byte __PADDINGS[sizeof(Node) - sizeof(byte *)];
    };

    // 分配新的内存段
    void NewChunk() {
        Node *raw = new Node[Node::DYNAMIC_POOL_SIZE + 1];

        BlockHeader *header = (BlockHeader *) raw;
        header->previousBegin = (byte *) m_header;
        m_header = header;

        m_nextAvailable = raw + 1;
        m_end = raw + Node::DYNAMIC_POOL_SIZE + 1;

#ifdef _DEBUG
        m_totalAllocated += Node::DYNAMIC_POOL_SIZE + 1;
#endif
    }

private:

#ifdef _DEBUG
    size_type m_totalAllocated;
#endif

    Synchronizer m_sync;

    Node m_static[Node::STATIC_POOL_SIZE];

    // 已分配的静态数组元素数目
    size_type m_numStaticUsed;

    // Start of raw memory making up current pool
    BlockHeader *m_header;
    // First free byte in current pool
    Node *m_nextAvailable;
    // One past last available byte in current pool
    Node *m_end;

    List m_free;
};

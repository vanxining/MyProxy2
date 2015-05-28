
#include "ThreadPool.hpp"

//////////////////////////////////////////////////////////////////////////

/**
 * Improve Scalability With New Thread Pool APIs:
 * http://msdn.microsoft.com/en-us/magazine/cc16332.aspx
 *
 * Developing with Thread Pool Enhancements:
 * http://msdn.microsoft.com/en-us/library/cc308561.aspx
 *
 * Introduction to the Windows Threadpool:
 * http://blogs.msdn.com/b/harip/archive/2010/10/11/introduction-to-the-windows-threadpool-part-1.aspx
 * http://blogs.msdn.com/b/harip/archive/2010/10/12/introduction-to-the-windows-threadpool-part-2.aspx
 */

//////////////////////////////////////////////////////////////////////////

ThreadPool::ThreadPool() {
    m_handle = CreateThreadpool(NULL);

    if (IsOk()) {
        InitializeThreadpoolEnvironment(&m_env);
        SetThreadpoolCallbackPool(&m_env, m_handle);
    }
}

ThreadPool::~ThreadPool() {
    if (IsOk()) {
        DestroyThreadpoolEnvironment(&m_env);
        CloseThreadpool(m_handle);
        m_handle = nullptr;
    }
}

bool ThreadPool::CreateWork(PTP_SIMPLE_CALLBACK pfns, void *pv) {
    if (!IsOk()) {
        return false;
    }

    return TrySubmitThreadpoolCallback(pfns, pv, &m_env) != FALSE;
}

bool ThreadPool::SetThreadMinimum(int minimun) {
    if (!IsOk()) {
        return false;
    }

    return SetThreadpoolThreadMinimum(m_handle, minimun) == TRUE;
}

void ThreadPool::SetThreadMaximum(int maximun) {
    if (IsOk()) {
        SetThreadpoolThreadMaximum(m_handle, maximun);
    }
}

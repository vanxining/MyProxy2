
#include "DNSCache.hpp"
#include "Logger.hpp"

#include <mutex>
using namespace std;

//////////////////////////////////////////////////////////////////////////

double DNSCache::EXPIRATION = 60 * 60 * 1;
DNSCache::Cache DNSCache::ms_cache;
static mutex gs_loggerMutex;


const DNSCache::Entry *DNSCache::Resolve(const string &dname) {
    lock_guard<mutex> lock(gs_loggerMutex);

    auto it(ms_cache.find(dname));
    if (it != ms_cache.end()) {
        auto curr = time(nullptr);
        if (difftime(curr, it->second.ts) > EXPIRATION) {
            ms_cache.erase(it);

            return nullptr;
        }

        // 延长有效期：更新为当前时间戳
        it->second.ts = curr;

        return &(it->second);
    }

    return nullptr;
}

void DNSCache::Add(const std::string &dname, const AI &ai) {
    lock_guard<mutex> lock(gs_loggerMutex);

    auto it(ms_cache.find(dname));
    if (it != ms_cache.end()) {
        if (&(it->second.ai) == &ai) {
            it->second.ts = time(nullptr);
            return;
        }
    }

    ms_cache.emplace(dname, &ai);
}

bool DNSCache::Remove(const std::string &dname) {
    lock_guard<mutex> lock(gs_loggerMutex);

    auto it(ms_cache.find(dname));
    if (it != ms_cache.end()) {
        ms_cache.erase(it);
        return true;
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////

DNSCache::Entry::Entry(const AI *ai_other) {
    if (ai_other) {
        ai = *ai_other;
        ai.ai_addr = new sockaddr(*(ai_other->ai_addr));

        // 没有使用这两个属性值
        ai.ai_canonname = nullptr;
        ai.ai_next = nullptr;

        ts = time(nullptr);
    }
    else {
        memset(&ai, 0, sizeof(AI));
    }
}

DNSCache::Entry::~Entry() {
    if (IsOk()) {
        delete ai.ai_addr;
    }
}

bool DNSCache::Entry::IsOk() const {
    return ai.ai_addr != nullptr;
}

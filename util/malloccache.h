#ifndef MALLOCCACHE_H
#define MALLOCCACHE_H

// no-op the cache, sometimes useful for debugging memory issues
//#define MALLOCCACHE_PASSTHROUGH

template <size_t blockSize, size_t blockCount>
class MallocCache
{
public:
    MallocCache()
       : m_blocksCached(0)
    {
    }

    ~MallocCache()
    {
#ifndef MALLOCCACHE_PASSTHROUGH
        assert(m_blocksCached >= 0 && m_blocksCached <= blockCount);
        for (size_t i = 0; i < m_blocksCached; i++) {
            ::free(m_blocks[i]);
        }
#endif
    }

    void *allocate()
    {
#ifndef MALLOCCACHE_PASSTHROUGH
        assert(m_blocksCached >= 0 && m_blocksCached <= blockCount);
        if (m_blocksCached) {
            return m_blocks[--m_blocksCached];
        } else {
            return ::malloc(blockSize);
        }
#else
        return ::malloc(blockSize);
#endif
    }

    void free(void *allocation)
    {
#ifndef MALLOCCACHE_PASSTHROUGH
        assert(m_blocksCached >= 0 && m_blocksCached <= blockCount);
        if (m_blocksCached < blockCount) {
            m_blocks[m_blocksCached++] = allocation;
        } else {
            ::free(allocation);
        }
#else
        ::free(allocation);
#endif
    }

private:
    void *m_blocks[blockCount];
    size_t m_blocksCached;
};

#endif // MALLOCCACHE_H

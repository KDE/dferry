#ifndef MALLOCCACHE_H
#define MALLOCCACHE_H

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
        assert(m_blocksCached >= 0 && m_blocksCached <= blockCount);
        for (size_t i = 0; i < m_blocksCached; i++) {
            ::free(m_blocks[i]);
        }
    }

    void *allocate()
    {
        assert(m_blocksCached >= 0 && m_blocksCached <= blockCount);
        if (m_blocksCached) {
            return m_blocks[--m_blocksCached];
        } else {
            return ::malloc(blockSize);
        }
    }

    void free(void *allocation)
    {
        assert(m_blocksCached >= 0 && m_blocksCached <= blockCount);
        if (m_blocksCached < blockCount) {
            m_blocks[m_blocksCached++] = allocation;
        } else {
            ::free(allocation);
        }
    }

private:
    void *m_blocks[blockCount];
    size_t m_blocksCached;
};

#endif MALLOCCACHE_H

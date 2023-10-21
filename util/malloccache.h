#ifndef MALLOCCACHE_H
#define MALLOCCACHE_H

#include <cassert>
#include <cstdlib>

// no-op the cache, sometimes useful for debugging memory issues
//#define MALLOCCACHE_PASSTHROUGH

// On MinGW, we got crashes in multithreaded code due do an apparent problem with thread-local variable
// support in MinGW. It *should* be fixed in MinGW with GCC 13. In lower versions, disable MallocCache.
// It will still have uninitialized memory, but in passthrough mode it never accesses anything behind
// the this pointer, so it's effectively just a bunch of free functions that call malloc and free.
// https://github.com/msys2/MINGW-packages/issues/2519
// https://github.com/msys2/MINGW-packages/discussions/13259
#if defined(__GNUC__) && defined(__MINGW32__) && __GNUC__ < 13
#define MALLOCCACHE_PASSTHROUGH
#endif

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
        assert(m_blocksCached <= blockCount);
        for (size_t i = 0; i < m_blocksCached; i++) {
            ::free(m_blocks[i]);
        }
#endif
    }

    void *allocate()
    {
#ifndef MALLOCCACHE_PASSTHROUGH
        assert(m_blocksCached <= blockCount);
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
        assert(m_blocksCached <= blockCount);
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

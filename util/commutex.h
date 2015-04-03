/* When two objects on different threads talk to each other (uni- or bidirectionally), they will
 * have pointers to each other. Before sending something on the other side, the sender needs to
 * know:
 * - is there still an object alive at the memory address it knows?
 * - is it still the same object that it wants to talk to?
 * The latter is similar to the somewhat well-known the ABA problem.
 * So what we do is that the initiator of the connection creates a Commutex object held alive
 * by shared_ptrs. A shared_ptr copied from the original is sent to the receiver.
 * The Commutex synchronizes the two objects insofar that destruction of one end will prevent
 * calls forever (Commutex in "Broken" state), and an ongoing call will block other calls
 * through the same Commutex as well as destruction of the receiver.
 */

#ifndef COMMUTEX_H
#define COMMUTEX_H

#include <atomic>
#include <cassert>
#include <memory>

// Commutex: Mutex-like thing for communicating objects. Better names welcome.
class Commutex
{
public:
    enum State {
        Free = 0,
        Locked, // serves to delay destruction of one of the linked objects while the other is
              // calling methods / touching data on it
        Broken
    };

    enum TryLockResult {
        TransientFailure = 0, // state was Locked
        PermanentFailure, // state was Broken
        Success // state was Free and transitioned to Locked
    };

private:
    friend class CommutexPeer;

    TryLockResult tryLock()
    {
        State prevState = Free;
        if (m_state.compare_exchange_strong(prevState, Locked)) {
            return Success;
        }
        return prevState == Broken ? PermanentFailure : TransientFailure;
    }

    bool lock()
    {
        while (true) {
            TryLockResult result = tryLock();
            if (result == TransientFailure) {
                continue;
            }
            return result == Success;
        }
    }

    // return value is only informational - what are you going to do when unlocking fails because
    // the state is already Broken?
    bool unlock()
    {
        State prevState = Locked;
        if (m_state.compare_exchange_strong(prevState, Free)) {
            return true;
        }
        assert(prevState == Broken); // unlocking when already Free indicates wrong accounting
        return false;
    }

    bool tryUnlink()
    {
        State prevState = Free;
        bool wasFree = m_state.compare_exchange_strong(prevState, Broken);
        return wasFree || prevState == Broken;
    }

    void unlink()
    {
        while (!tryUnlink()) {
        }
    }

    void unlinkFromLocked()
    {
        // we don't have the data to check if the Locked state is "owned" by the calling thread
        State prevState = Locked;
        bool success = m_state.compare_exchange_strong(prevState, Broken);
        assert(success);
    }

    std::atomic<State> m_state;
};

class CommutexPeer
{
public:
    static std::pair<CommutexPeer, CommutexPeer> createLink()
    {
        std::shared_ptr<Commutex> commutex = std::make_shared<Commutex>();
        return std::make_pair(CommutexPeer(commutex), CommutexPeer(commutex));
    }

    CommutexPeer() = default; // state will be Broken, that's fine

    CommutexPeer(CommutexPeer &&other)
       : m_comm(std::move(other.m_comm))
    {}

    ~CommutexPeer()
    {
        unlink();
    }

    CommutexPeer &operator=(CommutexPeer &&other)
    {
        m_comm = std::move(other.m_comm);
        return *this;
    }

    CommutexPeer(const CommutexPeer &other) = delete;
    CommutexPeer &operator=(const CommutexPeer &other) = delete;

    Commutex::TryLockResult tryLock()
    {
        if (!m_comm) {
            return Commutex::PermanentFailure;
        }
        Commutex::TryLockResult ret = m_comm->tryLock();
        if (ret == Commutex::PermanentFailure) {
            m_comm.reset();
        }
        return ret;
    }

    bool lock()
    {
        if (!m_comm) {
            return false;
        }
        bool ret = m_comm->lock();
        if (!ret) {
            m_comm.reset();
        }
        return ret;
    }

    void unlock()
    {
        if (m_comm) {
            m_comm->unlock();
        }
    }

    // This might be useful when unlinking a set of somehow (accidentally?) inter-dependent commutexes.
    // In that case, keep calling tryUnlink() on all still unbroken ones until all are broken.
    bool tryUnlink()
    {
        if (!m_comm) {
            return true;
        }
        bool ret = m_comm->tryUnlink();
        if (ret) {
            m_comm.reset();
        }
        return ret;
    }

    void unlink()
    {
        if (m_comm) {
            m_comm->unlink();
            m_comm.reset();
        }
    }

    // This either succeeds immediately and unconditionally or the state wasn't Locked by user error
    // (it doesn't check if this CommutexPeer "owns" the Locked state)
    // So, this has the (unverifiable at this point) pre-condition that the calling thread "owns the lock".
    void unlinkFromLocked()
    {
        if (m_comm) {
            m_comm->unlinkFromLocked();
        }
    }

    // diagnostic use ONLY because it has no transactional semantics - also note that, since there
    // is no non-atomic read of an atomic variable, this might hide heisenbugs by causing spurious
    // memory barriers
    Commutex::State state() const
    {
        if (!m_comm) {
            return Commutex::Broken;
        }
        return m_comm->m_state;
    }

    // Only for identification purposes, to see which two CommutexPeers belong together if
    // there is an unsorted bunch of them somewhere.
    Commutex *id() const { return m_comm.get(); }

private:
    friend class Commutex;

    CommutexPeer(std::shared_ptr<Commutex> comm)
       : m_comm(comm)
    {}

    std::shared_ptr<Commutex> m_comm;
};

class CommutexLocker
{
public:
    CommutexLocker(CommutexPeer *cp)
       : m_peer(cp)
    {
        m_hasLock = m_peer->lock();
    }

    bool hasLock() const
    {
        return m_hasLock;
    }

    ~CommutexLocker()
    {
        // The check is not only an optimization - users of the class are likely to delete our
        // CommutexPeer when the Commutex is broken.
        if (m_hasLock) {
            m_peer->unlock();
        }
    }

    CommutexLocker(const CommutexLocker &other) = delete;
    CommutexLocker &operator=(const CommutexLocker &other) = delete;

private:
    CommutexPeer *m_peer;
    bool m_hasLock;
};

class CommutexUnlinker
{
public:
    CommutexUnlinker(CommutexPeer *cp, bool mustSucceed = true)
       : m_peer(cp)
    {
        m_tryLockResult = m_peer->tryLock();
        while (mustSucceed && m_tryLockResult == Commutex::TransientFailure) {
            m_tryLockResult = m_peer->tryLock();
        }
    }

    bool hasLock()
    {
        return m_tryLockResult == Commutex::Success;
    }

    // if the Commutex was already Broken or if we have a lock so our unlinkFromLocked() WILL succed
    bool willSucceed()
    {
        return m_tryLockResult != Commutex::TransientFailure;
    }

    // mainly used to prevent the destructor from accessing *m_peer, to fix lifetime issues with *m_peer.
    void unlinkNow()
    {
        assert(willSucceed());
        if (m_tryLockResult == Commutex::Success) {
            m_peer->unlinkFromLocked();
        }
        m_tryLockResult = Commutex::PermanentFailure; // aka it is already unlinked, which is the case now
    }

    ~CommutexUnlinker()
    {
        if (m_tryLockResult == Commutex::Success) {
            m_peer->unlinkFromLocked();
        }
    }

    CommutexUnlinker(const CommutexUnlinker &other) = delete;
    CommutexUnlinker &operator=(const CommutexUnlinker &other) = delete;

private:
    CommutexPeer *m_peer;
    Commutex::TryLockResult m_tryLockResult;
};

#endif // COMMUTEX_H
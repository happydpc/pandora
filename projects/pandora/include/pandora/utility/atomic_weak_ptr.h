#pragma once
#include <memory>
#include <mutex>
#include <atomic>

namespace pandora
{

// Locking placeholder until we get std::atomic_weak_ptr in C++20
template <typename T>
class atomic_weak_ptr
{
public:
    atomic_weak_ptr() = default;
    atomic_weak_ptr(const std::shared_ptr<T>& sharedPtr);

    void store(const std::shared_ptr<T>& sharedPtr);

    std::shared_ptr<T> lock();
    bool expired() const;
private:
    std::weak_ptr<T> m_ptr;
    tbb::mutex m_mutex;
};

template<typename T>
inline atomic_weak_ptr<T>::atomic_weak_ptr(const std::shared_ptr<T>& sharedPtr) :
    m_ptr(sharedPtr)
{
}

template<typename T>
void atomic_weak_ptr<T>::store(const std::shared_ptr<T>& sharedPtr)
{
    std::scoped_lock lock(m_mutex);
    std::atomic_thread_fence(std::memory_order_acquire);
    m_ptr = sharedPtr;
    std::atomic_thread_fence(std::memory_order_release);
}

template<typename T>
inline std::shared_ptr<T> atomic_weak_ptr<T>::lock()
{
    // Lock the mutex to ensure that we cannot store() while locking
    std::scoped_lock_read lock(m_mutex);
    std::atomic_thread_fence(std::memory_order_acquire);
    return m_ptr.lock();
}

template<typename T>
inline bool atomic_weak_ptr<T>::expired() const
{
    auto* mutThisPtr = const_cast<atomic_weak_ptr<T>*>(this);

    // Lock the mutex to ensure that we cannot store() while checking expired status
    std::scoped_lock_read lock(mutThisPtr->m_mutex);
    std::atomic_thread_fence(std::memory_order_acquire);
    return m_ptr.expired();
}


}
// RAII guard for s3w_mutex_t (C++ services). No exceptions: check locked().
#pragma once

#include "s3w_task.h"

namespace s3w {

class LockGuard {
public:
    explicit LockGuard(s3w_mutex_t m, uint32_t timeout_ms = UINT32_MAX) : m_(m), locked_(s3w_mutex_lock(m, timeout_ms)) {}
    ~LockGuard()
    {
        if (locked_) {
            s3w_mutex_unlock(m_);
        }
    }
    LockGuard(const LockGuard &) = delete;
    LockGuard &operator=(const LockGuard &) = delete;
    bool locked() const { return locked_; }

private:
    s3w_mutex_t m_;
    bool locked_;
};

} // namespace s3w

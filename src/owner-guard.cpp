#include "owner-guard.hpp"

namespace lt {

bool OwnerGuard::claim(const void *token)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (owner_ == nullptr)
        owner_ = token;
    return owner_ == token;
}

bool OwnerGuard::release(const void *token)
{
    std::lock_guard<std::mutex> lk(mtx_);
    if (owner_ == token)
        owner_ = nullptr;
    return owner_ == nullptr;
}

bool OwnerGuard::owned_by_other(const void *token)
{
    std::lock_guard<std::mutex> lk(mtx_);
    return owner_ != nullptr && owner_ != token;
}

bool OwnerGuard::has_owner()
{
    std::lock_guard<std::mutex> lk(mtx_);
    return owner_ != nullptr;
}

}

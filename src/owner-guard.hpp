#pragma once
#include <mutex>

namespace lt {

// First-wins single-owner guard. The token is an opaque pointer identifying the
// claimant (e.g. a per-instance data pointer). Ownership is granted only when
// the resource is unowned, so the first successful claimer keeps it until it
// releases. Used to keep a single filter owning the shared input stream and a
// single source owning the shared output stream.
class OwnerGuard {
public:
    // Take ownership iff currently unowned (or already held by token).
    // Returns true iff token owns the resource after the call.
    bool claim(const void *token);
    // Clear ownership, but only if token is the current holder (else no-op).
    void release(const void *token);
    // True iff some token other than `token` currently owns the resource.
    bool owned_by_other(const void *token);
    // True iff any token currently owns the resource.
    bool has_owner();

private:
    std::mutex mtx_;
    const void *owner_ = nullptr;
};

}

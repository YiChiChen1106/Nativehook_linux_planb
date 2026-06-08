#pragma once

#include <cstdint>
#include <memory>

namespace linux_native_hook_v1 {

class AddressHandler {
public:
    virtual ~AddressHandler() = default;

    virtual void AddAllocAddr(uint64_t addr) = 0;
    virtual bool CheckAddr(uint64_t addr) = 0;

protected:
    std::shared_ptr<AddressHandler> successor_ = nullptr;
};

}  // namespace linux_native_hook_v1

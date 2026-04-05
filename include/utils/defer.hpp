#pragma once
#include <functional>

namespace myblob::utils {

class Defer {
public:
    Defer(std::function<void()> func) : _func(std::move(func)) {}
    
    ~Defer() {
        _func();
    }
    
    Defer(const Defer& d) = delete;
    Defer& operator=(const Defer& d) = delete;

private:
    std::function<void()> _func;
};

}  // namespace myblob::utils

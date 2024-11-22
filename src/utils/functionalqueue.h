#pragma once

#include <functional>
#include <queue>

class FunctionQueue {
public:

    void push_function(std::function<void()>&& function)
    {
        funcs.push_back(std::move(function));
    }

    void flush() {
        for(auto& func : funcs) {
            func();
        }

        funcs.clear();
    }

private:
    std::deque<std::function<void()>> funcs;
};
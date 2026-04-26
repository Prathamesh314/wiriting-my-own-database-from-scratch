#pragma once

#include <utility>

namespace util {

    template <typename F>
    class Defer {
        F fn;
        bool active = true;
    public:
        explicit Defer(F f) : fn(std::move(f)) {}

        ~Defer() {
            if (active) fn();
        }

        void cancel() noexcept { active = false; }

        Defer(const Defer&) = delete;
        Defer& operator=(const Defer&) = delete;

        Defer(Defer&& other) noexcept
            : fn(std::move(other.fn)), active(other.active) {
            other.active = false;
        }
        Defer& operator=(Defer&&) = delete;
    };

    template <typename F>
    [[nodiscard]] Defer<F> make_defer(F f) {
        return Defer<F>(std::move(f));
    }

}

#define UTIL_DEFER_CONCAT_(a, b) a##b
#define UTIL_DEFER_NAME_(line)   UTIL_DEFER_CONCAT_(_util_defer_, line)

#define defer(code) \
    auto UTIL_DEFER_NAME_(__LINE__) = ::util::make_defer([&](){ code; })

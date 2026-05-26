#pragma once

#include "cib_swd_log.h"

#include <ch32fun.h>
#include <conc/concurrency.hpp>
#include <log_binary/catalog/encoder.hpp>

#include <cstdint>
#include <utility>

#ifdef INFO
#undef INFO
#endif

namespace firmware_log {
struct interrupt_guard {
    interrupt_guard() : mstatus{__get_MSTATUS()} { __disable_irq(); }
    ~interrupt_guard() { __set_MSTATUS(mstatus); }

    std::uint32_t mstatus;
};

struct concurrency_policy {
    template <typename = void, typename F, typename... Pred>
    static inline auto call_in_critical_section(F &&f, Pred &&...pred)
        -> decltype(std::forward<F>(f)()) {
        while (true) {
            [[maybe_unused]] interrupt_guard guard{};
            if ((... && std::forward<Pred>(pred)())) {
                return std::forward<F>(f)();
            }
        }
    }
};
} // namespace firmware_log

template <>
inline auto conc::injected_policy<> = firmware_log::concurrency_policy{};

template <>
inline auto logging::config<> =
    logging::binary::config{firmware_log::swd_binary_sink{}};

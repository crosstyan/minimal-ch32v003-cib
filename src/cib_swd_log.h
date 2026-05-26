#pragma once

#include <stdx/span.hpp>

#include <cstddef>
#include <cstdint>

namespace firmware_log {
void swd_log_init();
void swd_write_bytes(std::uint8_t const *data, std::size_t size);

struct swd_binary_sink {
    template <std::size_t N>
    auto operator()(stdx::span<std::uint32_t const, N> packet) const -> void {
        auto const *bytes =
            reinterpret_cast<std::uint8_t const *>(packet.data());
        swd_write_bytes(bytes, packet.size() * sizeof(std::uint32_t));
    }
};
} // namespace firmware_log

#include "cib_swd_log.h"

#include <ch32fun.h>

#include <cstddef>
#include <cstdint>

namespace {
constexpr std::size_t max_payload_bytes       = 7U;
constexpr std::uint32_t target_output_pending = 0x80U;

[[nodiscard]]
std::uint32_t pack_le(std::uint8_t const bytes[4]) {
	return static_cast<std::uint32_t>(bytes[0]) |
		   (static_cast<std::uint32_t>(bytes[1]) << 8U) |
		   (static_cast<std::uint32_t>(bytes[2]) << 16U) |
		   (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void wait_for_host_ack() {
	while ((*DMDATA0 & target_output_pending) != 0U) {
	}
}

void write_frame(std::uint8_t const *data, std::size_t size) {
	std::uint8_t dmdata0[4] = {
		static_cast<std::uint8_t>(target_output_pending | (size + 4U)), 0U, 0U,
		0U};
	std::uint8_t dmdata1[4] = {0U, 0U, 0U, 0U};

	for (std::size_t i = 0; i < size; ++i) {
		if (i < 3U) {
			dmdata0[i + 1U] = data[i];
		} else {
			dmdata1[i - 3U] = data[i];
		}
	}

	wait_for_host_ack();
	*DMDATA1 = pack_le(dmdata1);
	*DMDATA0 = pack_le(dmdata0);
}
} // namespace

namespace firmware_log {
void swd_log_init() {
	*DMDATA1 = 0U;
	*DMDATA0 = 0U;
}

void swd_write_bytes(std::uint8_t const *data, std::size_t size) {
	while (size > 0U) {
		std::size_t const frame_size =
			size > max_payload_bytes ? max_payload_bytes : size;
		write_frame(data, frame_size);
		data += frame_size;
		size -= frame_size;
	}
}
} // namespace firmware_log

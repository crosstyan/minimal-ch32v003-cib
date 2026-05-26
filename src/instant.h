#pragma once

#include "system_tick.h"

#include <stdint.h>

class Instant {
public:
	constexpr Instant() = default;
	constexpr explicit Instant(uint32_t timestamp) : timestamp_{timestamp} {}

	[[nodiscard]]
	static Instant now() { return Instant{system_millis()}; }

	[[nodiscard]]
	uint32_t elapsed_ms() const {
		return system_millis() - timestamp_;
	}

	[[nodiscard]]
	bool has_elapsed_ms(uint32_t ms) const {
		return elapsed_ms() >= ms;
	}

	void reset() { timestamp_ = system_millis(); }

private:
	uint32_t timestamp_{0};
};

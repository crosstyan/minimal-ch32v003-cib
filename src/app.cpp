#include "app.h"

#include <flow/flow.hpp>
#include <nexus/config.hpp>
#include <nexus/nexus.hpp>

#include "instant.h"
#include "system_tick.h"

#include <ch32fun.h>

#ifndef CH32V003_CIB_BINARY_LOG
#define CH32V003_CIB_BINARY_LOG 0
#endif

#if CH32V003_CIB_BINARY_LOG
#include "cib_log_config.h"
#endif

namespace {
constexpr uint32_t led_pin = PD6;
Instant last_blink;

[[nodiscard]] GPIO_TypeDef *gpio_of(uint32_t pin) {
  return reinterpret_cast<GPIO_TypeDef *>(GPIOA_BASE + 0x400U * (pin >> 4U));
}

void enable_gpio() {
  RCC->APB2PCENR |= RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA |
                    RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD;
}

void set_pin_output(uint32_t pin) {
  constexpr uint32_t mode = GPIO_Speed_10MHz | GPIO_CNF_OUT_PP;
  const uint32_t shift = 4U * (pin & 0xfU);
  auto *gpio = gpio_of(pin);
  gpio->CFGLR = (gpio->CFGLR & ~(0xfU << shift)) | (mode << shift);
}

[[nodiscard]] bool toggle_pin(uint32_t pin) {
  const uint32_t bit = 1U << (pin & 0xfU);
  auto *gpio = gpio_of(pin);
  const bool next_high = (gpio->OUTDR & bit) == 0U;
  gpio->BSHR = next_high ? bit : (bit << 16U);
  return next_high;
}

void log_led_level(bool high) {
#if CH32V003_CIB_BINARY_LOG
  if (high) {
    CIB_INFO("led high");
  } else {
    CIB_INFO("led low");
  }
#else
  std::ignore = high;
#endif
}

struct boot_flow : flow::service<"", flow::log_policies::none> {};
struct runtime_flow : flow::service<"", flow::log_policies::none> {};
struct loop_flow : flow::service<"", flow::log_policies::none> {};

struct board_component {
  static constexpr auto clock_init =
      flow::action<"ClockInit">([] { SystemInit(); });
  static constexpr auto tick_init =
      flow::action<"SysTickInit">([] { system_tick_init(); });
  static constexpr auto led_init = flow::action<"LedInit">([] {
    enable_gpio();
    set_pin_output(led_pin);
  });
  static constexpr auto blink = flow::action<"Blink">([] {
    if (!last_blink.has_elapsed_ms(250U)) {
      return;
    }

    const bool led_high = toggle_pin(led_pin);
    log_led_level(led_high);
    last_blink.reset();
  });

  static constexpr auto config = cib::config(
      cib::extend<boot_flow>(*clock_init), cib::extend<boot_flow>(*tick_init),
      cib::extend<runtime_flow>(*led_init), cib::extend<loop_flow>(*blink));
};

#if CH32V003_CIB_BINARY_LOG
struct logging_component {
  static constexpr auto swd_log_init =
      flow::action<"SwdLogInit">([] { firmware_log::swd_log_init(); });
  static constexpr auto boot_log =
      flow::action<"BootLog">([] { CIB_INFO("boot"); });

  static constexpr auto config =
      cib::config(cib::extend<boot_flow>(*swd_log_init),
                  cib::extend<runtime_flow>(*boot_log));
};
#endif

struct project {
  static constexpr auto config =
      cib::config(cib::exports<boot_flow, runtime_flow, loop_flow>,
                  // clang-format off
                    cib::components<
                        board_component
#if CH32V003_CIB_BINARY_LOG
                        , logging_component
#endif
                        >
                  // clang-format on
      );
};

using project_nexus = cib::nexus<project>;
} // namespace

[[noreturn]] void run_app() {
  project_nexus::service<boot_flow>();
  project_nexus::service<runtime_flow>();

  while (true) {
    project_nexus::service<loop_flow>();
  }
}

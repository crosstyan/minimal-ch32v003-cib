#include <flow/flow.hpp>
#include <nexus/config.hpp>
#include <nexus/nexus.hpp>

#include <ch32fun.h>

namespace {
constexpr uint32_t led_pin = PD6;

[[nodiscard]] GPIO_TypeDef *gpio_of(uint32_t pin) {
    return reinterpret_cast<GPIO_TypeDef *>(GPIOA_BASE + 0x400U * (pin >> 4U));
}

void enable_gpio() {
    RCC->APB2PCENR |=
        RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC |
        RCC_APB2Periph_GPIOD;
}

void set_pin_output(uint32_t pin) {
    constexpr uint32_t mode = GPIO_Speed_10MHz | GPIO_CNF_OUT_PP;
    const uint32_t shift = 4U * (pin & 0xfU);
    auto *gpio = gpio_of(pin);
    gpio->CFGLR = (gpio->CFGLR & ~(0xfU << shift)) | (mode << shift);
}

void write_pin(uint32_t pin, bool high) {
    gpio_of(pin)->BSHR = 1U << ((high ? 0U : 16U) + (pin & 0xfU));
}

void delay_ms(uint32_t ms) { DelaySysTick(ms * DELAY_MS_TIME); }

struct boot_flow : flow::service<"", flow::log_policies::none> {};
struct runtime_flow : flow::service<"", flow::log_policies::none> {};
struct loop_flow : flow::service<"", flow::log_policies::none> {};

struct board_component {
    static constexpr auto clock_init =
        flow::action<"ClockInit">([] { SystemInit(); });
    static constexpr auto led_init = flow::action<"LedInit">([] {
        enable_gpio();
        set_pin_output(led_pin);
    });
    static constexpr auto blink = flow::action<"Blink">([] {
        static bool on = false;
        write_pin(led_pin, !on);
        on = !on;
        delay_ms(250);
    });

    static constexpr auto config = cib::config(
        cib::extend<boot_flow>(*clock_init),
        cib::extend<runtime_flow>(*led_init),
        cib::extend<loop_flow>(*blink));
};

struct project {
    static constexpr auto config = cib::config(
        cib::exports<boot_flow, runtime_flow, loop_flow>,
        cib::components<board_component>);
};

using project_nexus = cib::nexus<project>;
} // namespace

extern "C" int main() {
    project_nexus::service<boot_flow>();
    project_nexus::service<runtime_flow>();

    while (true) {
        project_nexus::service<loop_flow>();
    }
}

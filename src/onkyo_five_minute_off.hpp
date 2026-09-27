#pragma once

#include <cstdint>

#include "w5500_onkyo.h"

// Core 0 only. The ADC level controls a 300-second timer. The timer also
// restarts after each completed power check, so persistent silence results
// in another check 300 seconds later.
class OnkyoFiveMinuteOff {
public:
    static constexpr std::uint32_t kSilenceMs = 300000u;

    void service(std::uint32_t now, std::uint32_t last_above_threshold_ms) {
        w5500_onkyo_task();

        // The input meter publishes a new timestamp for every block above
        // -60 dBFS. Do not require music to have played before starting.
        if (!monitor_started_ ||
            last_above_threshold_ms != last_observed_loud_ms_) {
            monitor_started_ = true;
            last_observed_loud_ms_ = last_above_threshold_ms;
            timer_start_ms_ = last_above_threshold_ms;
            step_ = Step::Idle;  // Audio returned: cancel pending standby.
        }
        if (now - timer_start_ms_ < kSilenceMs) {
            step_ = Step::Idle;
            return;
        }
        if (!w5500_onkyo_connected()) {
            step_ = Step::Idle;
            return;
        }

        switch (step_) {
        case Step::Idle:
            power_gen_ = w5500_onkyo_power_generation();
            if (w5500_onkyo_query_power()) {
                deadline_ms_ = now + 2000u;
                step_ = Step::WaitPower;
            }
            break;
        case Step::WaitPower:
            if (w5500_onkyo_power_generation() != power_gen_) {
                if (w5500_onkyo_power_is_on()) {
                    step_ = Step::SendStandby;
                } else {
                    restart_timer(now);  // Already off: try again in 5 min.
                }
            } else if (time_reached(now, deadline_ms_)) {
                step_ = Step::Idle;  // No reply: retry while silence lasts.
            }
            break;
        case Step::SendStandby:
            if (w5500_onkyo_standby()) {
                restart_timer(now); // Request queued: next check in 5 min.
            }
            break;
        }
    }

private:
    enum class Step { Idle, WaitPower, SendStandby };

    static bool time_reached(std::uint32_t now, std::uint32_t deadline) {
        return static_cast<std::int32_t>(now - deadline) >= 0;
    }

    void restart_timer(std::uint32_t now) {
        timer_start_ms_ = now;
        step_ = Step::Idle;
    }

    Step step_{Step::Idle};
    bool monitor_started_{false};
    std::uint32_t last_observed_loud_ms_{0};
    std::uint32_t timer_start_ms_{0};
    std::uint32_t deadline_ms_{0};
    std::uint32_t power_gen_{0};
};

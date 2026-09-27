#include <cstdio>
#include <cstdlib>
#include <cstdint>

#include "onkyo_five_minute_off.hpp"

namespace {
bool connected = true;
bool power_on = true;
bool reply = true;
std::uint32_t power_gen = 0;
int power_queries = 0;
int input_queries = 0;
int standby_count = 0;

void reset_receiver() {
    connected = true;
    power_on = true;
    reply = true;
    power_gen = 0;
    power_queries = input_queries = standby_count = 0;
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
}  // namespace

// Fake the real W5500 driver's receiver API. A query updates its response
// generation only when the simulated Onkyo actually answers.
extern "C" {
void w5500_onkyo_task(void) {}
bool w5500_onkyo_connected(void) { return connected; }
bool w5500_onkyo_query_power(void) {
    if (!connected) return false;
    ++power_queries;
    if (reply) ++power_gen;
    return true;
}
bool w5500_onkyo_query_input(void) {
    ++input_queries;
    return true;
}
bool w5500_onkyo_standby(void) {
    if (!connected) return false;
    ++standby_count;
    power_on = false;
    return true;
}
std::uint32_t w5500_onkyo_power_generation(void) { return power_gen; }
bool w5500_onkyo_power_is_on(void) { return power_on; }
}

int main() {
    reset_receiver();
    OnkyoFiveMinuteOff boot_silence;
    boot_silence.service(0, 0);
    boot_silence.service(299900, 0);
    require(power_queries == 0, "boot silence must wait 300 seconds");
    boot_silence.service(300000, 0);
    require(power_queries == 1 && standby_count == 0,
            "power must be queried before standby");
    boot_silence.service(300100, 0);
    boot_silence.service(300200, 0);
    require(standby_count == 1, "boot silence should switch off an ON Onkyo");
    require(input_queries == 0, "selected input must not be queried");
    boot_silence.service(600100, 0);
    require(power_queries == 1, "standby should restart the 300-second timer");
    boot_silence.service(600200, 0);
    require(power_queries == 2, "continuing silence starts another check");
    boot_silence.service(600300, 0);
    require(standby_count == 1, "already-OFF Onkyo must not get standby");
    power_on = true;
    boot_silence.service(900200, 0);
    require(power_queries == 2, "already-OFF result should restart timer");
    boot_silence.service(900300, 0);
    boot_silence.service(900400, 0);
    boot_silence.service(900500, 0);
    require(standby_count == 2, "later ON state triggers standby next cycle");

    reset_receiver();
    OnkyoFiveMinuteOff returned_audio;
    returned_audio.service(0, 0);
    returned_audio.service(299900, 299899); // One louder ADC block resets it.
    returned_audio.service(300000, 299899);
    require(power_queries == 0, "audio before expiry restarts silence timer");
    returned_audio.service(599898, 299899);
    require(power_queries == 0, "no query before a full 300 seconds");
    returned_audio.service(599899, 299899);
    require(power_queries == 1, "query 300 seconds after last loud block");
    returned_audio.service(600000, 599999);
    require(standby_count == 0, "new audio cancels pending power check");

    reset_receiver();
    OnkyoFiveMinuteOff no_reply;
    no_reply.service(0, 0);
    reply = false;
    no_reply.service(300000, 0);
    no_reply.service(300100, 0);
    no_reply.service(302000, 0);
    require(standby_count == 0, "no fresh power reply: no standby");
    reply = true;
    no_reply.service(302100, 0);
    no_reply.service(302200, 0);
    no_reply.service(302300, 0);
    require(standby_count == 1, "a fresh reply allows standby");

    reset_receiver();
    OnkyoFiveMinuteOff no_network;
    no_network.service(0, 0);
    connected = false;
    no_network.service(300000, 0);
    require(power_queries == 0, "disconnected receiver: no request");
    connected = true;
    no_network.service(300100, 0);
    no_network.service(300200, 0);
    no_network.service(300300, 0);
    require(standby_count == 1, "after reconnect, query and standby");

    reset_receiver();
    OnkyoFiveMinuteOff wraps;
    constexpr std::uint32_t start = 0xffffffffu - 1000u;
    wraps.service(start, start);
    wraps.service(start + 299999u, start);
    require(power_queries == 0, "clock wrap must preserve 300-second timer");
    wraps.service(start + 300000u, start);
    require(power_queries == 1, "clock wrap must still trigger power query");

    std::puts("PASS: -60 dB timer interface, boot silence, power query, "
              "other inputs, repeat checks, new audio, reconnect and wrap");
}

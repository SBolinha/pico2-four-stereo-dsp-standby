#include "w5500_onkyo.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "network_config.hpp"

#define W5500_SPI spi0

#define PIN_MISO 0
#define PIN_CS   1
#define PIN_SCK  2
#define PIN_MOSI 3
#define PIN_RST  4
#define PIN_INT  5

#define BSB_COMMON 0x00
#define BSB_S0_REG 0x01
#define BSB_S0_TX  0x02
#define BSB_S0_RX  0x03

#define REG_GAR      0x0001
#define REG_SUBR     0x0005
#define REG_SHAR     0x0009
#define REG_SIPR     0x000F
#define REG_PHYCFGR  0x002E
#define REG_VERSIONR 0x0039

#define Sn_MR          0x0000
#define Sn_CR          0x0001
#define Sn_IR          0x0002
#define Sn_SR          0x0003
#define Sn_PORT        0x0004
#define Sn_DIPR        0x000C
#define Sn_DPORT       0x0010
#define Sn_RXBUF_SIZE  0x001E
#define Sn_TXBUF_SIZE  0x001F
#define Sn_TX_FSR      0x0020
#define Sn_TX_WR       0x0024
#define Sn_RX_RSR      0x0026
#define Sn_RX_RD       0x0028

#define CMD_OPEN    0x01
#define CMD_CONNECT 0x04
#define CMD_CLOSE   0x10
#define CMD_SEND    0x20
#define CMD_RECV    0x40

#define SOCK_CLOSED      0x00
#define SOCK_INIT        0x13
#define SOCK_ESTABLISHED 0x17
#define SOCK_CLOSE_WAIT  0x1C

#define IR_TIMEOUT 0x08
#define IR_SENDOK  0x10

#define SOCKET_BUFFER_SIZE 2048
#define SOCKET_BUFFER_MASK 0x07FF

typedef enum {
    ST_DISABLED,
    ST_WAIT_LINK,
    ST_WAIT_CLOSE,
    ST_WAIT_OPEN,
    ST_WAIT_CONNECT,
    ST_WAIT_TX_SPACE,
    ST_WAIT_SEND,
    ST_ONLINE,
    ST_RETRY
} onkyo_state_t;

static onkyo_state_t state = ST_DISABLED;
static uint32_t deadline_ms;
static bool connected;
static bool power_is_on;
static uint32_t power_generation;

static uint8_t tx_packet[64];
static size_t tx_packet_len;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool expired(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static inline uint8_t control_byte(uint8_t block, bool write) {
    return (uint8_t)((block << 3) | (write ? 0x04 : 0x00));
}

static void w5500_write(uint8_t block, uint16_t address,
                        const uint8_t *data, size_t length) {
    uint8_t header[3] = {
        (uint8_t)(address >> 8),
        (uint8_t)(address & 0xff),
        control_byte(block, true)
    };

    gpio_put(PIN_CS, 0);
    spi_write_blocking(W5500_SPI, header, 3);
    if (length) spi_write_blocking(W5500_SPI, data, length);
    gpio_put(PIN_CS, 1);
}

static void w5500_read(uint8_t block, uint16_t address,
                       uint8_t *data, size_t length) {
    uint8_t header[3] = {
        (uint8_t)(address >> 8),
        (uint8_t)(address & 0xff),
        control_byte(block, false)
    };

    gpio_put(PIN_CS, 0);
    spi_write_blocking(W5500_SPI, header, 3);
    if (length) spi_read_blocking(W5500_SPI, 0x00, data, length);
    gpio_put(PIN_CS, 1);
}

static void write8(uint8_t block, uint16_t address, uint8_t value) {
    w5500_write(block, address, &value, 1);
}

static uint8_t read8(uint8_t block, uint16_t address) {
    uint8_t value = 0;
    w5500_read(block, address, &value, 1);
    return value;
}

static void write16(uint8_t block, uint16_t address, uint16_t value) {
    uint8_t b[2] = {
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xff)
    };
    w5500_write(block, address, b, 2);
}

static uint16_t read16(uint8_t block, uint16_t address) {
    uint8_t b[2];
    w5500_read(block, address, b, 2);
    return (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
}

static uint16_t read16_stable(uint8_t block, uint16_t address) {
    uint16_t a = read16(block, address);
    for (int i = 0; i < 5; ++i) {
        uint16_t b = read16(block, address);
        if (a == b) return a;
        a = b;
    }
    return a;
}

static void tx_buffer_write(uint16_t pointer, const uint8_t *data, size_t length) {
    uint16_t offset = pointer & SOCKET_BUFFER_MASK;
    size_t first = length;

    if ((size_t)offset + first > SOCKET_BUFFER_SIZE) {
        first = SOCKET_BUFFER_SIZE - offset;
    }

    w5500_write(BSB_S0_TX, offset, data, first);
    if (length > first) {
        w5500_write(BSB_S0_TX, 0, data + first, length - first);
    }
}

static void rx_buffer_read(uint16_t pointer, uint8_t *data, size_t length) {
    uint16_t offset = pointer & SOCKET_BUFFER_MASK;
    size_t first = length;

    if ((size_t)offset + first > SOCKET_BUFFER_SIZE) {
        first = SOCKET_BUFFER_SIZE - offset;
    }

    w5500_read(BSB_S0_RX, offset, data, first);
    if (length > first) {
        w5500_read(BSB_S0_RX, 0, data + first, length - first);
    }
}

static size_t build_command_packet(uint8_t *packet, const char *command) {
    const uint32_t data_size = (uint32_t)strlen(command);

    packet[0] = 'I';
    packet[1] = 'S';
    packet[2] = 'C';
    packet[3] = 'P';

    packet[4] = 0;
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 16;

    packet[8]  = (uint8_t)(data_size >> 24);
    packet[9]  = (uint8_t)(data_size >> 16);
    packet[10] = (uint8_t)(data_size >> 8);
    packet[11] = (uint8_t)data_size;

    packet[12] = 1;
    packet[13] = 0;
    packet[14] = 0;
    packet[15] = 0;

    memcpy(packet + 16, command, data_size);
    return 16 + data_size;
}

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           (uint32_t)p[3];
}

static void report_onkyo_response(const uint8_t *data, size_t length) {
    static const char pwr_standby[] = "!1PWR00";
    static const char pwr_on[]      = "!1PWR01";

    if (length < 16 || memcmp(data, "ISCP", 4) != 0) return;

    uint32_t header_size = read_be32(data + 4);
    uint32_t data_size   = read_be32(data + 8);
    if (header_size < 16 || header_size > length ||
        data_size > (uint32_t)(length - header_size)) return;

    const uint8_t *payload = data + header_size;
    size_t payload_len = (size_t)data_size;

    /* sizeof(...)-1 is intentional: compare the seven command bytes only,
       not the C string's trailing NUL against Onkyo's 0x1A EOF byte. */
    if (payload_len >= sizeof(pwr_standby) - 1 &&
        memcmp(payload, pwr_standby, sizeof(pwr_standby) - 1) == 0) {
        power_is_on = false;
        ++power_generation;
        return;
    }

    if (payload_len >= sizeof(pwr_on) - 1 &&
        memcmp(payload, pwr_on, sizeof(pwr_on) - 1) == 0) {
        power_is_on = true;
        ++power_generation;
        return;
    }

}

static void schedule_retry(uint32_t now) {
    connected = false;
    write8(BSB_S0_REG, Sn_CR, CMD_CLOSE);
    deadline_ms = now + 1500;
    state = ST_RETRY;
}

static void start_socket(uint32_t now) {
    connected = false;
    write8(BSB_S0_REG, Sn_CR, CMD_CLOSE);
    deadline_ms = now + 250;
    state = ST_WAIT_CLOSE;
}

bool w5500_onkyo_init(void) {
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    gpio_init(PIN_RST);
    gpio_set_dir(PIN_RST, GPIO_OUT);
    gpio_put(PIN_RST, 1);

    gpio_init(PIN_INT);
    gpio_set_dir(PIN_INT, GPIO_IN);
    gpio_pull_up(PIN_INT);

    spi_init(W5500_SPI, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    spi_set_format(W5500_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_put(PIN_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_RST, 1);
    sleep_ms(150);

    if (read8(BSB_COMMON, REG_VERSIONR) != 0x04) {
        state = ST_DISABLED;
        return false;
    }

    w5500_write(BSB_COMMON, REG_GAR,  network_config::gateway, 4);
    w5500_write(BSB_COMMON, REG_SUBR, network_config::mask, 4);
    w5500_write(BSB_COMMON, REG_SHAR, network_config::mac, 6);
    w5500_write(BSB_COMMON, REG_SIPR, network_config::device_ip, 4);

    tx_packet_len = 0;

    state = ST_WAIT_LINK;
    connected = false;
    return true;
}

bool w5500_onkyo_connected(void) {
    return connected;
}

static bool queue_command(const char *command) {
    if (!connected || state != ST_ONLINE) return false;
    tx_packet_len = build_command_packet(tx_packet, command);
    deadline_ms = now_ms() + 3000u;
    state = ST_WAIT_TX_SPACE;
    return true;
}

bool w5500_onkyo_standby(void) {
    return queue_command("!1PWR00\r");
}

bool w5500_onkyo_query_power(void) {
    return queue_command("!1PWRQSTN\r");
}

uint32_t w5500_onkyo_power_generation(void) {
    return power_generation;
}

bool w5500_onkyo_power_is_on(void) {
    return power_is_on;
}

void w5500_onkyo_task(void) {
    if (state == ST_DISABLED) return;

    uint32_t now = now_ms();
    bool link = (read8(BSB_COMMON, REG_PHYCFGR) & 0x01) != 0;

    if (!link) {
        if (state != ST_WAIT_LINK) {
            connected = false;
            write8(BSB_S0_REG, Sn_CR, CMD_CLOSE);
            state = ST_WAIT_LINK;
        }
        return;
    }

    switch (state) {
    case ST_WAIT_LINK:
        start_socket(now);
        break;

    case ST_WAIT_CLOSE:
        if (read8(BSB_S0_REG, Sn_CR) == 0 || expired(now, deadline_ms)) {
            write8(BSB_S0_REG, Sn_IR, 0xff);
            write8(BSB_S0_REG, Sn_RXBUF_SIZE, 2);
            write8(BSB_S0_REG, Sn_TXBUF_SIZE, 2);
            write8(BSB_S0_REG, Sn_MR, 0x01);
            write16(BSB_S0_REG, Sn_PORT, 50000);
            w5500_write(BSB_S0_REG, Sn_DIPR, network_config::receiver_ip, 4);
            write16(BSB_S0_REG, Sn_DPORT, network_config::receiver_port);
            write8(BSB_S0_REG, Sn_CR, CMD_OPEN);
            deadline_ms = now + 500;
            state = ST_WAIT_OPEN;
        }
        break;

    case ST_WAIT_OPEN:
        if (read8(BSB_S0_REG, Sn_CR) != 0) break;

        if (read8(BSB_S0_REG, Sn_SR) != SOCK_INIT) {
            if (expired(now, deadline_ms)) {
                schedule_retry(now);
            }
            break;
        }

        write8(BSB_S0_REG, Sn_CR, CMD_CONNECT);
        deadline_ms = now + 6000;
        state = ST_WAIT_CONNECT;
        break;

    case ST_WAIT_CONNECT: {
        uint8_t sr = read8(BSB_S0_REG, Sn_SR);

        if (sr == SOCK_ESTABLISHED) {
            connected = true;
            state = ST_ONLINE;
            break;
        }

        uint8_t ir = read8(BSB_S0_REG, Sn_IR);
        if (ir & IR_TIMEOUT) {
            write8(BSB_S0_REG, Sn_IR, IR_TIMEOUT);
            schedule_retry(now);
            break;
        }

        if (sr == SOCK_CLOSED) {
            schedule_retry(now);
            break;
        }

        if (expired(now, deadline_ms)) {
            schedule_retry(now);
        }
        break;
    }

    case ST_WAIT_TX_SPACE:
        if (read16_stable(BSB_S0_REG, Sn_TX_FSR) >= tx_packet_len) {
            uint16_t wr = read16(BSB_S0_REG, Sn_TX_WR);
            tx_buffer_write(wr, tx_packet, tx_packet_len);
            wr += (uint16_t)tx_packet_len;
            write16(BSB_S0_REG, Sn_TX_WR, wr);
            write8(BSB_S0_REG, Sn_IR, IR_SENDOK | IR_TIMEOUT);
            write8(BSB_S0_REG, Sn_CR, CMD_SEND);
            deadline_ms = now + 3000;
            state = ST_WAIT_SEND;
        } else if (expired(now, deadline_ms)) {
            schedule_retry(now);
        }
        break;

    case ST_WAIT_SEND: {
        uint8_t ir = read8(BSB_S0_REG, Sn_IR);

        if (ir & IR_SENDOK) {
            write8(BSB_S0_REG, Sn_IR, IR_SENDOK);
            state = ST_ONLINE;
            break;
        }

        if (ir & IR_TIMEOUT) {
            write8(BSB_S0_REG, Sn_IR, IR_TIMEOUT);
            schedule_retry(now);
            break;
        }

        if (expired(now, deadline_ms)) {
            schedule_retry(now);
        }
        break;
    }

    case ST_ONLINE: {
        uint8_t sr = read8(BSB_S0_REG, Sn_SR);

        if (sr == SOCK_CLOSED || sr == SOCK_CLOSE_WAIT) {
            schedule_retry(now);
            break;
        }

        uint16_t available = read16_stable(BSB_S0_REG, Sn_RX_RSR);
        if (available) {
            uint8_t buffer[256];
            if (available > sizeof(buffer)) available = sizeof(buffer);

            uint16_t rd = read16(BSB_S0_REG, Sn_RX_RD);
            rx_buffer_read(rd, buffer, available);
            rd += available;
            write16(BSB_S0_REG, Sn_RX_RD, rd);
            write8(BSB_S0_REG, Sn_CR, CMD_RECV);

            report_onkyo_response(buffer, available);
            state = ST_ONLINE;
            break;
        }

        break;
    }

    case ST_RETRY:
        if (expired(now, deadline_ms)) state = ST_WAIT_LINK;
        break;

    default:
        break;
    }
}

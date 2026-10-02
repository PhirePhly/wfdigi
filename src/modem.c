#include "wfdigi.h"

/* AX.25 address block is 7 bytes. Destination, source, and up to 8 digipeaters. */
#define AX25_ADDR 7u
#define AX25_MAX_ADDRS 10u
#define AX25_MIN 15u
#define AX25_MAX 330u

__sfr __at (PORT_SCC_A_CTRL) scc_a_ctrl;
__sfr __at (PORT_SCC_A_DATA) scc_a_data;
__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_SCC_B_DATA) scc_b_data;

#define TX_IDLE 0u
#define TX_WAIT_DCD 1u
#define TX_DELAY 2u
#define TX_DATA 3u
#define TX_WAIT_EMPTY 4u
#define TX_WAIT_EOM 5u
#define TX_TAIL 6u
/* SCC byte and CRC should finish well inside 40 ms. Three flags are 20 ms. */
#define TX_WAIT_TICKS 4u
#define TX_TAIL_TICKS 2u
#define TRACE_MAX 254u

static uint8_t rx_buf[2][AX25_MAX];
static uint8_t rx_fill;
static uint16_t rx_len;
static bool rx_overflow;
static volatile uint8_t rx_ready;
static volatile uint8_t rx_done;
static volatile uint16_t rx_done_len;

static uint8_t tx_buf[AX25_MAX];
static uint8_t tx_state;
static uint16_t tx_i;
static uint16_t tx_len;
static volatile bool keyed;

static char trace[TRACE_MAX];
static uint8_t trace_len;
static uint8_t trace_pos;
static volatile uint8_t dcd_now;
static volatile uint8_t sta_arm;
static volatile uint8_t rx_dropped;

static uint8_t radio_rr0(void)
{
    scc_a_ctrl = 0x00u;
    return scc_a_ctrl;
}

static uint8_t radio_rr1(void)
{
    scc_a_ctrl = 0x01u;
    return scc_a_ctrl;
}

static void radio_cmd(uint8_t cmd)
{
    scc_a_ctrl = cmd;
}

static void rx_reset(void)
{
    rx_len = 0u;
    rx_overflow = false;
}

static void rx_take(uint8_t data, uint8_t rr1)
{
    /* Half duplex: the receiver hears this station. Discard that audio. */
    if (keyed && g_config.fulldup == 0u) {
        if ((rr1 & (RR1_END_FRAME | RR1_OVERRUN)) != 0u) {
            rx_reset();
            radio_cmd(0x30u);
        }
        return;
    }
    if ((rr1 & RR1_OVERRUN) != 0u) {
        rx_reset();
        radio_cmd(0x30u);
        return;
    }
    if (!rx_overflow && rx_len < AX25_MAX) {
        rx_buf[rx_fill][rx_len] = data;
        ++rx_len;
    } else {
        rx_overflow = true;
    }
    if ((rr1 & RR1_END_FRAME) == 0u) {
        return;
    }
    if (!rx_overflow && (rr1 & RR1_CRC_ERR) == 0u &&
        (rr1 & RR1_RESIDUE) == RR1_RESIDUE_OK && rx_len >= 2u) {
        uint16_t n = (uint16_t)(rx_len - 2u);
        if (n >= AX25_MIN) {
            sta_arm = 1u;
            if (rx_ready == 0u) {
                rx_done = rx_fill;
                rx_done_len = n;
                rx_fill ^= 1u;
                rx_ready = 1u;
            } else if (rx_dropped != 255u) {
                ++rx_dropped;
            }
        }
    }
    rx_reset();
    radio_cmd(0x30u);
}

static void rx_service(bool special)
{
    bool need_reset = special;

    for (;;) {
        uint8_t rr1;
        uint8_t data;

        if ((radio_rr0() & RR0_RX_CHAR) == 0u) {
            break;
        }
        rr1 = radio_rr1();
        data = scc_a_data;
        if ((rr1 & (RR1_END_FRAME | RR1_CRC_ERR | RR1_OVERRUN)) != 0u) {
            need_reset = true;
        }
        rx_take(data, rr1);
    }
    if (need_reset) {
        radio_cmd(0x30u);
    }
    radio_cmd(0x38u);
}

static void scc_ius(uint8_t ctrl_write)
{
    scc_b_ctrl = ctrl_write;
}

void modem_isr_a_rx(void)
{
    rx_service(false);
}

void modem_isr_a_special(void)
{
    rx_service(true);
}

void modem_isr_a_ext(void)
{
    uint8_t latched = radio_rr0();

    radio_cmd(0x10u);
    dcd_now = (radio_rr0() & RR0_DCD) != 0u;
    if ((latched & RR0_ABORT) != 0u) {
        rx_reset();
    }
    radio_cmd(0x38u);
}

void modem_isr_a_tx(void)
{
    radio_cmd(0x28u);
    radio_cmd(0x38u);
}

void modem_isr_b_rx(void)
{
    scc_b_ctrl = 0x00u;
    if ((scc_b_ctrl & RR0_RX_CHAR) != 0u) {
        (void)scc_b_data;
    }
    scc_ius(0x38u);
}

void modem_isr_b_special(void)
{
    scc_b_ctrl = 0x00u;
    if ((scc_b_ctrl & RR0_RX_CHAR) != 0u) {
        (void)scc_b_data;
    }
    scc_ius(0x30u);
    scc_ius(0x38u);
}

void modem_isr_b_ext(void)
{
    uint8_t status;

    scc_b_ctrl = 0x00u;
    status = scc_b_ctrl;
    (void)status;
    timer_sync_edge();
    scc_b_ctrl = 0x10u; /* arm the next /SYNCB edge */
    scc_b_ctrl = 0x38u;
}

void modem_isr_b_tx(void)
{
    scc_ius(0x28u);
    scc_ius(0x38u);
}

bool modem_dcd(void)
{
    return dcd_now != 0u;
}

bool modem_keyed(void)
{
    return keyed;
}

void modem_quiesce(void)
{
    keyed = false;
    tx_state = TX_IDLE;
    trace_len = 0u;
    trace_pos = 0u;
    sta_arm = 0u;
    rx_dropped = 0u;
}

static bool trace_idle(void)
{
    return trace_pos >= trace_len;
}

static void trace_begin(void)
{
    trace_len = 0u;
    trace_pos = 0u;
}

static void trace_char(uint8_t byte)
{
    if (trace_len < TRACE_MAX) {
        trace[trace_len] = (char)byte;
        ++trace_len;
    }
}

static void trace_puts(const char *text)
{
    while (*text != '\0') {
        trace_char((uint8_t)*text);
        ++text;
    }
}

static void trace_drain(void)
{
    if (trace_idle()) {
        return;
    }
    if (serial_try_putc((uint8_t)trace[trace_pos])) {
        ++trace_pos;
    }
}

static void print_hex_byte(uint8_t value)
{
    uint8_t hi = (uint8_t)(value >> 4);
    uint8_t lo = (uint8_t)(value & 0x0Fu);

    trace_char((uint8_t)(hi < 10u ? '0' + hi : 'A' + (hi - 10u)));
    trace_char((uint8_t)(lo < 10u ? '0' + lo : 'A' + (lo - 10u)));
}

static void print_call(const uint8_t *raw)
{
    uint8_t i;
    uint8_t ssid;

    for (i = 0u; i < 6u; ++i) {
        uint8_t c = (uint8_t)((raw[i] >> 1) & 0x7Fu);
        if (c != ' ') {
            trace_char(c);
        }
    }
    ssid = (uint8_t)((raw[6] >> 1) & 0x0Fu);
    if (ssid != 0u) {
        trace_char('-');
        if (ssid >= 10u) {
            trace_char('1');
            ssid = (uint8_t)(ssid - 10u);
        }
        trace_char((uint8_t)('0' + ssid));
    }
}

static bool print_tnc2(const uint8_t *frame, uint16_t len)
{
    uint16_t at[AX25_MAX_ADDRS];
    uint8_t n = 0u;
    uint16_t i = 0u;
    uint8_t d;
    uint8_t last_h = 0u;
    bool saw_h = false;
    uint8_t ctl;

    while (n < AX25_MAX_ADDRS && (uint16_t)(i + AX25_ADDR) <= len) {
        at[n] = i;
        ++n;
        i = (uint16_t)(i + AX25_ADDR);
        if ((frame[i - 1u] & 0x01u) != 0u) {
            break;
        }
    }
    if (n < 2u || (frame[at[n - 1u] + 6u] & 0x01u) == 0u) {
        return false;
    }
    print_call(&frame[at[1]]);
    trace_char('>');
    print_call(&frame[at[0]]);
    for (d = 2u; d < n; ++d) {
        if ((frame[at[d] + 6u] & 0x80u) != 0u) {
            last_h = d;
            saw_h = true;
        }
    }
    for (d = 2u; d < n; ++d) {
        trace_char(',');
        print_call(&frame[at[d]]);
        if (saw_h && d == last_h) {
            trace_char('*');
        }
    }
    if (i >= len) {
        return true;
    }
    ctl = frame[i];
    ++i;
    trace_char(':');
    if (((ctl & 0x01u) == 0u) || ((ctl & 0xEFu) == 0x03u)) {
        if (i < len) {
            ++i;
        }
    }
    while (i < len) {
        uint8_t c = frame[i];
        ++i;
        if (c >= 0x20u && c <= 0x7Eu) {
            trace_char(c);
        }
    }
    return true;
}

static void log_frame(char kind, const uint8_t *frame, uint16_t len)
{
    uint16_t i;
    uint8_t n;
    uint8_t c;

    if (!trace_idle()) {
        return;
    }
    trace_begin();
    trace_puts("\r\n");
    trace_char((uint8_t)kind);
    trace_char(' ');
    if (!print_tnc2(frame, len)) {
        for (i = 0u; i < len; ++i) {
            if (i != 0u) {
                trace_char(' ');
            }
            print_hex_byte(frame[i]);
        }
    }
    trace_puts("\r\n");
    trace_puts(cli_prompt());
    n = cli_pending_len();
    for (c = 0u; c < n; ++c) {
        trace_char((uint8_t)cli_pending_char(c));
    }
}

static void service_rx(void)
{
    uint8_t idx;
    uint16_t n;

    if (rx_ready == 0u) {
        return;
    }
    idx = rx_done;
    n = rx_done_len;
    if (n > AX25_MAX) {
        n = AX25_MAX;
    }
    if (g_config.logging != 0u && trace_idle()) {
        log_frame('R', rx_buf[idx], n);
    } else if (g_config.logging != 0u) {
        serial_puts("!R");
    }
    rx_ready = 0u;
}

static bool tx_flag(uint8_t mask)
{
    uint8_t rr0;

    hardware_irq_off();
    rr0 = radio_rr0();
    hardware_irq_on();
    return (rr0 & mask) != 0u;
}

static void tx_drain_rx(void)
{
    uint8_t guard = 0u;

    while ((radio_rr0() & RR0_RX_CHAR) != 0u && guard < 8u) {
        (void)scc_a_data;
        ++guard;
    }
    radio_cmd(0x30u);
    rx_reset();
}

static bool mycall_unset(void)
{
    return g_config.mycall[0] == (uint8_t)'N' &&
           g_config.mycall[1] == (uint8_t)'0' &&
           g_config.mycall[2] == (uint8_t)'C' &&
           g_config.mycall[3] == (uint8_t)'A' &&
           g_config.mycall[4] == (uint8_t)'L' &&
           g_config.mycall[5] == (uint8_t)'L';
}

static void tx_key(void);

static void tx_kick(void)
{
    uint8_t kind;

    if (tx_state != TX_IDLE) {
        return;
    }
    if (!pktq_take(&kind, tx_buf, AX25_MAX, &tx_len)) {
        return;
    }
    (void)kind;
    if (mycall_unset()) {
        serial_puts("ERR - Set Callsign\r\n");
        while (pktq_take(&kind, tx_buf, AX25_MAX, &tx_len)) {
        }
        return;
    }
    tx_i = 0u;
    if (g_config.fulldup != 0u || dcd_now == 0u) {
        tx_key();
    } else {
        tx_state = TX_WAIT_DCD;
    }
}

static void tx_release(bool abort)
{
    hardware_irq_off();
    if (abort) {
        radio_cmd(0x18u);
    }
    hardware_ptt(false);
    keyed = false;
    tx_drain_rx();
    hardware_irq_on();
    tx_state = TX_IDLE;
}

static void tx_unkey(bool sent)
{
    tx_release(!sent);
    if (sent && g_config.logging != 0u) {
        log_frame('T', tx_buf, tx_len);
    }
    tx_kick();
}

/* The channel is already ours. Closing flags have gone out, so the next
 * queued frame starts at once. TXDELAY runs only from tx_key.
 */
static void tx_continue(void)
{
    uint8_t kind;

    if (g_config.logging != 0u) {
        log_frame('T', tx_buf, tx_len);
    }
    if (mycall_unset()) {
        serial_puts("ERR - Set Callsign\r\n");
        while (pktq_take(&kind, tx_buf, AX25_MAX, &tx_len)) {
        }
        tx_release(false);
        return;
    }
    if (!pktq_take(&kind, tx_buf, AX25_MAX, &tx_len)) {
        tx_release(false);
        return;
    }
    (void)kind;
    tx_i = 0u;
    tx_state = TX_DATA;
    timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
}

static void tx_key(void)
{
    hardware_irq_off();
    /* Sample the pin, not the shadow: carrier may have returned since the last edge. */
    if (g_config.fulldup == 0u && (radio_rr0() & RR0_DCD) != 0u) {
        dcd_now = 1u;
        hardware_irq_on();
        tx_state = TX_WAIT_DCD;
        return;
    }
    keyed = true;
    hardware_ptt(true);
    /* Flag idle is already selected, and the underrun latch is set, so the
     * SCC shifts 0x7E until the first data byte. A 10 ms tick is 12 bit
     * times at 1200 baud, which is 1.5 flags. Resetting the latch here would
     * send a CRC instead of those flags.
     */
    radio_cmd(0x10u);
    hardware_irq_on();
    hardware_watchdog_pet();
    tx_i = 0u;
    if (g_config.txdelay == 0u) {
        tx_state = TX_DATA;
        timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
    } else {
        tx_state = TX_DELAY;
        timer_set(TIMER_TXDELAY, g_config.txdelay);
    }
}

static void tx_service(void)
{
    if (keyed) {
        hardware_watchdog_pet();
    }
    switch (tx_state) {
    case TX_WAIT_DCD:
        if (dcd_now == 0u || g_config.fulldup != 0u) {
            tx_key();
        }
        break;
    case TX_DELAY:
        if (timer_expired(TIMER_TXDELAY)) {
            tx_state = TX_DATA;
            timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
        }
        break;
    case TX_DATA:
        if (tx_flag(RR0_TX_EMPTY)) {
            hardware_irq_off();
            if (tx_i == 0u) {
                radio_cmd(0x80u); /* reset Tx CRC, then load the byte, then arm CRC-on-underrun */
            }
            scc_a_data = tx_buf[tx_i];
            if (tx_i == 0u) {
                radio_cmd(0xC0u);
            }
            hardware_irq_on();
            ++tx_i;
            timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
            if (tx_i >= tx_len) {
                tx_state = TX_WAIT_EMPTY;
            }
        } else if (timer_expired(TIMER_TXWAIT)) {
            tx_unkey(false);
        }
        break;
    case TX_WAIT_EMPTY:
        if (tx_flag(RR0_TX_EMPTY)) {
            tx_state = TX_WAIT_EOM;
            timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
        } else if (timer_expired(TIMER_TXWAIT)) {
            tx_unkey(false);
        }
        break;
    case TX_WAIT_EOM:
        if (tx_flag(RR0_TX_EOM)) {
            tx_state = TX_TAIL;
            timer_set(TIMER_TXTAIL, TX_TAIL_TICKS);
        } else if (timer_expired(TIMER_TXWAIT)) {
            tx_unkey(false);
        }
        break;
    case TX_TAIL:
        if (timer_expired(TIMER_TXTAIL)) {
            tx_continue();
        }
        break;
    default:
        break;
    }
}

static void sta_service(void)
{
    uint8_t arm;

    hardware_irq_off();
    arm = sta_arm;
    sta_arm = 0u;
    hardware_irq_on();
    if (arm != 0u) {
        timer_set(TIMER_STA, STA_TICKS);
    }
}

static void drop_service(void)
{
    uint8_t n;

    hardware_irq_off();
    n = rx_dropped;
    rx_dropped = 0u;
    hardware_irq_on();
    while (n != 0u) {
        serial_puts("!R");
        --n;
    }
}

void modem_service(void)
{
    sta_service();
    trace_drain();
    service_rx();
    drop_service();
    tx_service();
}

bool modem_send(uint8_t kind, const uint8_t *frame, uint16_t len)
{
    if (frame == 0 || len < AX25_MIN || len > AX25_MAX) {
        return false;
    }
    if (!pktq_put(kind, frame, len)) {
        serial_puts("!Q");
        return false;
    }
    tx_kick();
    return true;
}

void modem_init(void)
{
    rx_fill = 0u;
    rx_reset();
    rx_ready = 0u;
    rx_done = 0u;
    rx_done_len = 0u;
    tx_state = TX_IDLE;
    tx_i = 0u;
    tx_len = 0u;
    keyed = false;
    pktq_init();
    trace_len = 0u;
    trace_pos = 0u;
    radio_cmd(0x30u);
    radio_cmd(0x10u);
    radio_cmd(0x10u);
    dcd_now = (radio_rr0() & RR0_DCD) != 0u;
    /* External status, and an interrupt on every received byte or special condition. */
    scc_a_ctrl = 0x01u;
    scc_a_ctrl = 0x11u;
    /* VIS was already set. Master interrupt enable turns the receiver loose. */
    scc_a_ctrl = 0x09u;
    scc_a_ctrl = 0x09u;
}

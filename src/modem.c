#include "wfdigi.h"

/* AX.25 address block is 7 bytes. Destination, source, and up to 8 digipeaters. */
#define AX25_ADDR 7u
#define AX25_MIN 15u
#define AX25_MAX 330u

__sfr __at (PORT_SCC_A_CTRL) scc_a_ctrl;
__sfr __at (PORT_SCC_A_DATA) scc_a_data;
__sfr __at (PORT_SCC_B_CTRL) scc_b_ctrl;
__sfr __at (PORT_SCC_B_DATA) scc_b_data;

typedef enum {
    TX_IDLE,
    TX_WAIT_DCD,
    TX_WAIT_SLOT,
    TX_DELAY,
    TX_DATA,
    TX_WAIT_EOM,
    TX_TAIL,
    TX_CAL
} TxState;
/* WR1: external/status and an interrupt on every received byte.
 * The transmit-empty interrupt is enabled only while a frame is being fed.
 */
#define WR1_HDLC 0x11u
#define WR1_HDLC_TX 0x13u
/* No progress for this long means the byte pump stalled. */
#define TX_WAIT_TICKS 4u
/* EOM is the start of the CRC. Sixteen CRC bits plus three flags is 33 ms. */
#define TX_TAIL_TICKS 4u
static uint8_t rx_buf[2][AX25_MAX];
static uint8_t rx_fill;
static uint16_t rx_len;
static bool rx_overflow;
static volatile uint8_t rx_ready;
static volatile uint8_t rx_done;
static volatile uint16_t rx_done_len;

static uint8_t tx_buf[AX25_MAX];
/* uint8_t so the main loop and the transmit ISR can share it without a torn read. */
static volatile uint8_t tx_state;
static volatile uint16_t tx_i;
static volatile uint16_t tx_len;
static volatile uint8_t tx_eom_watch;
static volatile uint8_t tx_eom_seen;
static uint16_t tx_mark;
static uint8_t tx_wait_armed;
static volatile bool keyed;

static volatile uint8_t dcd_now;
static volatile uint8_t sta_arm;
static volatile uint8_t rx_dropped;

// !R, !Q, and !S since this boot. Adverse Drop counters
static uint16_t drop_r; // Adverse drops due to Rx Overruns
static uint16_t drop_q; // Adverse drops due to Tx queue overflows
static uint16_t drop_s; // Adverse drops due to stale transmit frames

static void note_drop_r(void);
static void note_drop_s(void);

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

static void radio_wr1(uint8_t value)
{
    scc_a_ctrl = 0x01u;
    scc_a_ctrl = value;
}

static void rx_reset(void)
{
    rx_len = 0u;
    rx_overflow = false;
}

static void rx_take(uint8_t data, uint8_t rr1)
{
    /* Half duplex: the receiver hears this station. Discard that audio. */
    if (keyed && g_config.fullduplex == 0u) {
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
            telemetry_note_rx();
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
    /* Reset Ext/Status clears the latched EOM bit, so remember it here.
     * The latch also changes when the frame is armed; tx_eom_watch ignores that.
     */
    if (tx_eom_watch != 0u && (latched & RR0_TX_EOM) != 0u) {
        tx_eom_seen = 1u;
    }
    radio_cmd(0x38u);
}

/* The transmit buffer has just emptied. Load the next byte before the shift
 * register underruns. One byte is about 6.7 ms at 1200 baud.
 */
void modem_isr_a_tx(void)
{
    if (tx_state == TX_DATA && tx_i < tx_len) {
        scc_a_data = tx_buf[tx_i];
        ++tx_i;
        hardware_watchdog_pet();
        radio_cmd(0x38u);
        return;
    }
    if (tx_state == TX_DATA) {
        /* The last byte has moved into the shift register. CRC and flags follow. */
        tx_state = TX_WAIT_EOM;
        tx_eom_watch = 1u;
    }
    radio_wr1(WR1_HDLC);
    radio_cmd(0x28u);
    hardware_watchdog_pet();
    radio_cmd(0x38u);
}

void modem_isr_b_rx(void)
{
    uint8_t guard = 0u;

    scc_b_ctrl = 0x00u;
    while ((scc_b_ctrl & RR0_RX_CHAR) != 0u && guard < 4u) {
        serial_rx_push(scc_b_data);
        scc_b_ctrl = 0x00u;
        ++guard;
    }
    scc_ius(0x38u);
}

void modem_isr_b_special(void)
{
    scc_b_ctrl = 0x00u;
    if ((scc_b_ctrl & RR0_RX_CHAR) != 0u) {
        serial_rx_push(scc_b_data);
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
    tx_eom_watch = 0u;
    tx_eom_seen = 0u;
    tx_wait_armed = 0u;
    sta_arm = 0u;
    rx_dropped = 0u;
}

/* True when the source address contains a printable character other than space. */
static bool source_call_present(const uint8_t *frame, uint16_t len)
{
    uint8_t i;
    const uint8_t *src;

    if (len < (uint16_t)(AX25_ADDR + AX25_ADDR)) {
        return false;
    }
    /* The extension bit on the destination means this frame has no source. */
    if ((frame[6] & 0x01u) != 0u) {
        return false;
    }
    src = frame + AX25_ADDR;
    for (i = 0u; i < 6u; ++i) {
        uint8_t c = (uint8_t)(src[i] >> 1);

        if (c > (uint8_t)' ' && c <= (uint8_t)'~') {
            return true;
        }
    }
    return false;
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
    if (!source_call_present(rx_buf[idx], n)) {
        logging_frame('D', rx_buf[idx], n);
        rx_ready = 0u;
        return;
    }
    logging_frame('R', rx_buf[idx], n);
    digi_ingress(rx_buf[idx], n);
    rx_ready = 0u;
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

static void tx_key(void);
static void tx_persist(void);

/* Drop the transmit-empty interrupt. Caller has interrupts off. */
static void tx_pump_disarm(void)
{
    radio_wr1(WR1_HDLC);
    radio_cmd(0x28u);
    tx_eom_watch = 0u;
    tx_eom_seen = 0u;
    tx_wait_armed = 0u;
}

/* Load the first byte, then let modem_isr_a_tx feed the rest. The transmitter
 * is already shifting flags, so the buffer stays full until that flag finishes
 * and the empty interrupt is not missed.
 */
static void tx_start_data(void)
{
    hardware_irq_off();
    tx_eom_watch = 0u;
    tx_eom_seen = 0u;
    tx_wait_armed = 0u;
    tx_i = 0u;
    tx_state = TX_DATA;
    radio_wr1(WR1_HDLC_TX);
    radio_cmd(0x80u); /* reset Tx CRC, then load the byte, then arm CRC-on-underrun */
    scc_a_data = tx_buf[0];
    radio_cmd(0xC0u);
    tx_i = 1u;
    tx_mark = 1u;
    hardware_irq_on();
    timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
}

/* Take the next transmit frame that is still inside its 10 second window.
 * A frame whose expiry second has been reached is discarded and counted as !S.
 */
static bool tx_take(void)
{
    uint16_t expire;

    while (pktq_take(PKTQ_TX, tx_buf, AX25_MAX, &tx_len, &expire)) {
        if (!pktq_expired(expire)) {
            return true;
        }
        note_drop_s();
    }
    return false;
}

static void tx_kick(void)
{
    if (tx_state != TX_IDLE) {
        return;
    }
    if (!tx_take()) {
        return;
    }
    if (tx_interlock) {
        serial_puts("ERR - Set Callsign\r\n");
        // Flush any additional frames in the queue
        while (tx_take()) {
        }
        return;
    }
    tx_i = 0u;
    if (g_config.fullduplex != 0u || dcd_now == 0u) {
        tx_persist();
    } else {
        tx_state = TX_WAIT_DCD;
    }
}

static void tx_release(bool abort)
{
    hardware_irq_off();
    tx_state = TX_IDLE;
    tx_pump_disarm();
    if (abort) {
        radio_cmd(0x18u);
    }
    hardware_ptt(false);
    keyed = false;
    tx_drain_rx();
    hardware_irq_on();
}

static void tx_unkey(bool sent)
{
    tx_release(!sent);
    if (sent) {
        telemetry_note_tx();
        logging_frame('T', tx_buf, tx_len);
    }
    tx_kick();
}

/* The channel is already ours. Closing flags have gone out, so the next
 * queued frame starts at once. TXDELAY runs only from tx_key.
 */
static void tx_continue(void)
{
    telemetry_note_tx();
    logging_frame('T', tx_buf, tx_len);
    if (tx_interlock) {
        serial_puts("ERR - Set Callsign\r\n");
        while (tx_take()) {
        }
        tx_release(false);
        return;
    }
    if (!tx_take()) {
        tx_release(false);
        return;
    }
    tx_start_data();
}

static void tx_key(void)
{
    hardware_irq_off();
    /* Sample the pin, not the shadow: carrier may have returned since the last edge. */
    if (g_config.fullduplex == 0u && (radio_rr0() & RR0_DCD) != 0u) {
        dcd_now = 1u;
        hardware_irq_on();
        timer_set(TIMER_SLOTTIME, 0u);
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
    if (g_config.txdelay == 0u) {
        tx_start_data();
    } else {
        tx_i = 0u;
        tx_state = TX_DELAY;
        timer_set(TIMER_TXDELAY, g_config.txdelay);
    }
}

/* The channel is free. Key when the draw wins; otherwise wait one slot. */
static void tx_persist(void)
{
    if (g_config.fullduplex != 0u) {
        tx_key();
        return;
    }
    if (dcd_now != 0u) {
        timer_set(TIMER_SLOTTIME, 0u);
        tx_state = TX_WAIT_DCD;
        return;
    }
    prng_stir(timer_seconds());
    if (prng_u8() <= g_config.persist) {
        tx_key();
        return;
    }
    tx_state = TX_WAIT_SLOT;
    timer_set(TIMER_SLOTTIME, g_config.slottime);
}

static void tx_service(void)
{
    uint8_t state;

    if (keyed) {
        hardware_watchdog_pet();
    }
    hardware_irq_off();
    state = tx_state;
    hardware_irq_on();
    switch (state) {
    case TX_WAIT_DCD:
        if (dcd_now == 0u || g_config.fullduplex != 0u) {
            tx_persist();
        }
        break;
    case TX_WAIT_SLOT:
        if (g_config.fullduplex == 0u && dcd_now != 0u) {
            timer_set(TIMER_SLOTTIME, 0u);
            tx_state = TX_WAIT_DCD;
            break;
        }
        if (g_config.slottime == 0u || timer_expired(TIMER_SLOTTIME)) {
            tx_persist();
        }
        break;
    case TX_DELAY:
        if (timer_expired(TIMER_TXDELAY)) {
            tx_start_data();
        }
        break;
    case TX_DATA: {
        uint16_t sent;

        hardware_irq_off();
        sent = tx_i;
        state = tx_state;
        hardware_irq_on();
        if (state != TX_DATA) {
            break;
        }
        /* The ISR advances tx_i. No progress for 40 ms means the pump stalled. */
        if (sent != tx_mark) {
            tx_mark = sent;
            timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
        } else if (timer_expired(TIMER_TXWAIT)) {
            tx_unkey(false);
        }
        break;
    }
    case TX_WAIT_EOM:
        if (tx_wait_armed == 0u) {
            timer_set(TIMER_TXWAIT, TX_WAIT_TICKS);
            tx_wait_armed = 1u;
        }
        if (tx_eom_seen != 0u) {
            hardware_irq_off();
            tx_eom_watch = 0u;
            tx_eom_seen = 0u;
            tx_wait_armed = 0u;
            tx_state = TX_TAIL;
            hardware_irq_on();
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

static void count_drop(uint16_t *count)
{
    if (*count == 65535u) {
        serial_flush();
        firmware_reset();
    }
    *count = (uint16_t)(*count + 1u);
}

static void note_drop_r(void)
{
    serial_puts("!R");
    count_drop(&drop_r);
}

static void note_drop_q(void)
{
    serial_puts("!Q");
    count_drop(&drop_q);
}

static void note_drop_s(void)
{
    serial_puts("!S");
    count_drop(&drop_s);
}

static void drop_service(void)
{
    uint8_t n;

    hardware_irq_off();
    n = rx_dropped;
    rx_dropped = 0u;
    hardware_irq_on();
    while (n != 0u) {
        note_drop_r();
        --n;
    }
}

void modem_service(void)
{
    sta_service();
    service_rx();
    drop_service();
    tx_service();
}

bool modem_send(const uint8_t *frame, uint16_t len, uint16_t expires_in)
{
    if (frame == 0 || len < AX25_MIN || len > AX25_MAX) {
        return false;
    }
    if (!pktq_put(PKTQ_TX, frame, len, pktq_expire_in(expires_in))) {
        note_drop_q();
        return false;
    }
    tx_kick();
    return true;
}

bool modem_queue_viscous(const uint8_t *frame, uint16_t len, uint16_t expire)
{
    if (frame == 0 || len < AX25_MIN || len > AX25_MAX) {
        return false;
    }
    if (!pktq_put(PKTQ_VISCOUS, frame, len, expire)) {
        note_drop_q();
        return false;
    }
    return true;
}

void modem_cal_stop(void)
{
    if (tx_state != TX_CAL) {
        return;
    }
    hardware_irq_off();
    hardware_ptt(false);
    keyed = false;
    hardware_cal_restore();
    hardware_irq_on();
    tx_state = TX_IDLE;
    tx_kick();
}

void modem_calibrate(uint8_t tone, uint8_t seconds)
{
    if (tx_interlock) {
        serial_puts("ERR - Set Callsign\r\n");
        return;
    }
    if (tx_state != TX_IDLE || keyed) {
        serial_puts("Busy\r\n");
        return;
    }
    hardware_irq_off();
    hardware_cal_tone(tone);
    tx_state = TX_CAL;
    keyed = true;
    hardware_ptt(true);
    hardware_irq_on();
    hardware_watchdog_pet();
    timer_cal_start(seconds);
}

void modem_drop_counts(uint16_t *frame_drops, uint16_t *queue_drops, uint16_t *stale_drops)
{
    *frame_drops = drop_r;
    *queue_drops = drop_q;
    *stale_drops = drop_s;
}

void engine_stat(void)
{
    uint16_t received;
    uint16_t transmitted;

    telemetry_packet_counts(&received, &transmitted);
    serial_puts("TIME ");
    print_u16(timer_seconds());
    serial_puts("\r\nDUPEDB ");
    print_u16(dupe_count());
    serial_puts("\r\nRX ");
    print_u16(received);
    serial_puts("\r\nTX ");
    print_u16(transmitted);
    serial_puts("\r\n!R ");
    print_u16(drop_r);
    serial_puts("\r\n!Q ");
    print_u16(drop_q);
    serial_puts("\r\n!S ");
    print_u16(drop_s);
    serial_puts("\r\n");
}

void modem_init(void)
{
    drop_r = 0u;
    drop_q = 0u;
    drop_s = 0u;
    rx_fill = 0u;
    rx_reset();
    rx_ready = 0u;
    rx_done = 0u;
    rx_done_len = 0u;
    tx_state = TX_IDLE;
    tx_i = 0u;
    tx_len = 0u;
    tx_eom_watch = 0u;
    tx_eom_seen = 0u;
    tx_mark = 0u;
    tx_wait_armed = 0u;
    keyed = false;
    pktq_init();
    dupe_init();
    radio_cmd(0x30u);
    radio_cmd(0x10u);
    radio_cmd(0x10u);
    dcd_now = (radio_rr0() & RR0_DCD) != 0u;
    /* External status, and an interrupt on every received byte or special condition.
     * Transmit-empty interrupts stay off until tx_start_data.
     */
    radio_wr1(WR1_HDLC);
    /* VIS was already set. Master interrupt enable turns the receiver loose. */
    scc_a_ctrl = 0x09u;
    scc_a_ctrl = 0x09u;
}

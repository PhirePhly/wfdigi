#include "wfdigi.h"

#define LINE_MAX 80u

static char line[LINE_MAX];
static uint8_t line_len;
static bool overflow;
static bool ignore_lf;

static char prompt_buf[CALLSIGN_LEN + 6u];

const char *cli_prompt(void)
{
    uint8_t n = 0u;
    uint8_t i;
    uint8_t ssid = g_config.mycall_ssid;

    for (i = 0u; i < CALLSIGN_LEN; ++i) {
        if (g_config.mycall[i] == (uint8_t)' ') {
            break;
        }
        prompt_buf[n] = (char)g_config.mycall[i];
        ++n;
    }
    if (ssid != 0u) {
        prompt_buf[n] = '-';
        ++n;
        if (ssid >= 10u) {
            prompt_buf[n] = '1';
            ++n;
            ssid = (uint8_t)(ssid - 10u);
        }
        prompt_buf[n] = (char)('0' + ssid);
        ++n;
    }
    prompt_buf[n] = '>';
    ++n;
    prompt_buf[n] = ' ';
    ++n;
    prompt_buf[n] = '\0';
    return prompt_buf;
}

static void prompt(void)
{
    serial_puts(cli_prompt());
}

void cli_start(void)
{
    line_len = 0u;
    overflow = false;
    ignore_lf = false;
    prompt();
}

uint8_t cli_pending_len(void)
{
    return line_len;
}

char cli_pending_char(uint8_t index)
{
    return line[index];
}

void cli_redraw(void)
{
    uint8_t i;

    prompt();
    for (i = 0u; i < line_len; ++i) {
        serial_putc((uint8_t)line[i]);
    }
}

static void execute(void)
{
    if (overflow) {
        serial_puts("Too long?\r\n");
    } else if (line_len != 0u) {
        line[line_len] = '\0';
        config_command(line);
    }
    line_len = 0u;
    overflow = false;
    prompt();
}

void cli_input(uint8_t byte)
{
    if (byte == '\n') {
        if (ignore_lf) {
            ignore_lf = false;
            return;
        }
        ignore_lf = false;
        serial_puts("\r\n");
        execute();
        return;
    }
    ignore_lf = false;

    if (byte == '\r') {
        ignore_lf = true;
        serial_puts("\r\n");
        execute();
        return;
    }

    if (byte == 0x08u || byte == 0x7Fu) {
        if (!overflow && line_len != 0u) {
            --line_len;
            serial_puts("\b \b");
        }
        return;
    }

    if (byte < 0x20u || byte > 0x7Eu) {
        return;
    }

    if (line_len + 1u >= LINE_MAX) {
        overflow = true;
        return;
    }
    line[line_len] = (char)byte;
    ++line_len;
    serial_putc(byte);
}

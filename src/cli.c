#include "pk88.h"

#define LINE_MAX 80u

static char line[LINE_MAX];
static uint8_t line_len;
static bool overflow;
static bool ignore_lf;

static void prompt(void)
{
    serial_puts("WF> ");
}

void cli_start(void)
{
    line_len = 0u;
    overflow = false;
    ignore_lf = false;
    prompt();
}

static void execute(void)
{
    if (overflow) {
        serial_puts("?\r\n");
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

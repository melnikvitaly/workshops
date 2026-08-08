#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "ds1307.h"
#include "light_log.h"

// USART1 і I2C1 піднімає CubeMX у main.c.
static UART_HandleTypeDef *g_huart = NULL;
static I2C_HandleTypeDef  *g_hi2c  = NULL;

// Рядок, який користувач набирає в моніторі порту, збирається побайтово.
static char    g_line[CONSOLE_LINE_LEN];
static uint8_t g_len = 0;

#define ASCII_CR 0x0D
#define ASCII_LF 0x0A
#define ASCII_BS 0x08
#define ASCII_DEL 0x7F

/* Назви команд і довжина префікса тієї єдиної, що має аргументи. */
#define CMD_HELP  "help"
#define CMD_NOW   "now"
#define CMD_DUMP  "dump"
#define CMD_CLEAR "clear"
#define CMD_TIME  "time"
#define CMD_TIME_LEN (sizeof(CMD_TIME) - 1)   /* без кінцевого нуля */

/* Скільки чисел має команда "time": рік, місяць, число, години, хвилини, секунди. */
#define TIME_ARG_COUNT 6

/* Межі значень, які приймає DS1307 (рік — 00..99, тобто 2000..2099). */
#define YEAR_MAX   99
#define MONTH_MIN  1
#define MONTH_MAX  12
#define DATE_MIN   1
#define DATE_MAX   31
#define HOUR_MAX   23
#define MINUTE_MAX 59
#define SECOND_MAX 59

// --------------------------------------------------------------------------
// Вивід.
// --------------------------------------------------------------------------
void Console_Print(const char *text)
{
    if (g_huart == NULL || text == NULL) {
        return;
    }
    HAL_UART_Transmit(g_huart, (uint8_t *)text, (uint16_t)strlen(text), CONSOLE_TX_TIMEOUT);
}

void Console_Printf(const char *fmt, ...)
{
    if (g_huart == NULL) {
        return;
    }

    char buf[CONSOLE_TX_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    Console_Print(buf);
    Console_Print("\r\n");
}

// --------------------------------------------------------------------------
// Команди.
// --------------------------------------------------------------------------
static void print_help(void)
{
    Console_Printf("commands: help | now | dump | clear | time YY MM DD hh mm ss");
    Console_Printf("  example: time 26 8 8 14 30 0   -> 08:08:26 14:30:00");
}

static void print_now(void)
{
    RtcTime t = {0};
    uint16_t raw = LightLog_ReadLightRaw();

    if (DS1307_ReadTime(g_hi2c, &t) != HAL_OK) {
        Console_Printf("RTC: no answer at 0x%02X (check SDA/SCL, power)", DS1307_ADDR_7BIT);
        return;
    }

    char stamp[DS1307_STAMP_LEN];
    DS1307_FormatStamp(&t, stamp, sizeof(stamp));
    Console_Printf("now  %s  light=%u (%u%%)  records=%u",
                   stamp, (unsigned)raw, (unsigned)LightLog_Percent(raw),
                   (unsigned)LightLog_Count());
}

static void set_time(const char *args)
{
    unsigned year, month, date, hours, minutes, seconds;

    if (sscanf(args, "%u %u %u %u %u %u",
               &year, &month, &date, &hours, &minutes, &seconds) != TIME_ARG_COUNT) {
        Console_Printf("%s: expected %u numbers (YY MM DD hh mm ss)",
                       CMD_TIME, (unsigned)TIME_ARG_COUNT);
        return;
    }
    // DS1307 зберігає час у BCD і не перевіряє його сам: записане "місяць 19"
    // так і читатиметься назад, а журнал отримає безглузді мітки.
    if (year > YEAR_MAX || month < MONTH_MIN || month > MONTH_MAX ||
        date < DATE_MIN || date > DATE_MAX ||
        hours > HOUR_MAX || minutes > MINUTE_MAX || seconds > SECOND_MAX) {
        Console_Printf("time: value out of range");
        return;
    }

    RtcTime t = {
        .seconds = (uint8_t)seconds,
        .minutes = (uint8_t)minutes,
        .hours   = (uint8_t)hours,
        .day     = 1,                 /* день тижня в журналі не потрібен */
        .date    = (uint8_t)date,
        .month   = (uint8_t)month,
        .year    = (uint8_t)year,
    };

    if (DS1307_SetTime(g_hi2c, &t) != HAL_OK) {
        Console_Printf("RTC: write failed");
        return;
    }

    char stamp[DS1307_STAMP_LEN];
    DS1307_FormatStamp(&t, stamp, sizeof(stamp));
    Console_Printf("RTC set to %s", stamp);
}

static void handle_line(char *line)
{
    while (*line == ' ') {
        line++;   // пробіли на початку рядка користувача — не помилка
    }
    if (*line == '\0') {
        return;
    }

    if (strcmp(line, CMD_HELP) == 0) {
        print_help();
    } else if (strcmp(line, CMD_NOW) == 0) {
        print_now();
    } else if (strcmp(line, CMD_DUMP) == 0) {
        LightLog_Dump();
    } else if (strcmp(line, CMD_CLEAR) == 0) {
        LightLog_Clear();
    } else if (strncmp(line, CMD_TIME, CMD_TIME_LEN) == 0) {
        set_time(line + CMD_TIME_LEN);
    } else {
        Console_Printf("unknown command: %s (type '%s')", line, CMD_HELP);
    }
}

// --------------------------------------------------------------------------
// Init / Poll.
// --------------------------------------------------------------------------
void Console_Init(UART_HandleTypeDef *huart, I2C_HandleTypeDef *hi2c)
{
    g_huart = huart;
    g_hi2c  = hi2c;
    g_len   = 0;

    Console_Printf("");
    Console_Printf("=== workshop-4-3: light log with RTC timestamps ===");
    print_help();
}

void Console_Poll(void)
{
    if (g_huart == NULL) {
        return;
    }

    // Переповнення приймача (байт прийшов, поки попередній не забрали) гасить
    // RXNE, і без явного скидання ORE консоль замовкає назавжди.
    if (__HAL_UART_GET_FLAG(g_huart, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(g_huart);
    }

    uint8_t ch = 0;
    // Timeout = 0: забираємо лише те, що вже лежить у регістрі, і одразу
    // повертаємось у головний цикл — приймання не має його блокувати.
    while (HAL_UART_Receive(g_huart, &ch, 1, 0) == HAL_OK) {
        if (ch == ASCII_CR || ch == ASCII_LF) {
            g_line[g_len] = '\0';
            uint8_t had = g_len;
            g_len = 0;
            if (had > 0) {
                handle_line(g_line);
            }
        } else if (ch == ASCII_BS || ch == ASCII_DEL) {
            if (g_len > 0) {
                g_len--;
            }
        } else if (g_len < sizeof(g_line) - 1) {
            g_line[g_len++] = (char)ch;
        }
        // Довший за буфер рядок мовчки обрізається: команди тут короткі, а
        // скидати вже набране через одну зайву літеру — гірше.
    }
}

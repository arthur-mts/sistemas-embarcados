#include <string.h>
#include "i2cdev.h"
#include "hd44780.h"
#include <stdio.h>
#include "pcf8574.h"
#define LCD_ADDR 0x27
#define SDA_PIN  4
#define SCL_PIN  5
#define LCD_COLS 16
#define LCD_ROWS 2

static i2c_dev_t pcf8574;

static const uint8_t char_data[] = {
    0x04, 0x0e, 0x0e, 0x0e, 0x1f, 0x00, 0x04, 0x00,
    0x1f, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x1f, 0x00};

static esp_err_t write_lcd_data(const hd44780_t *lcd, uint8_t data)
{
    return pcf8574_port_write(&pcf8574, data);
}

hd44780_t lcd = {
    .write_cb = write_lcd_data,
    .font = HD44780_FONT_5X8,
    .lines = 2,
    .pins = {
        .rs = 0,
        .e = 2,
        .d4 = 4,
        .d5 = 5,
        .d6 = 6,
        .d7 = 7,
        .bl = 3}};


void config_display() {
    ESP_ERROR_CHECK(i2cdev_init());

    memset(&pcf8574, 0, sizeof(i2c_dev_t));
    ESP_ERROR_CHECK(pcf8574_init_desc(&pcf8574, 0x27, 0, SDA_PIN, SCL_PIN));

    ESP_ERROR_CHECK(hd44780_init(&lcd));

    hd44780_switch_backlight(&lcd, true);

    hd44780_upload_character(&lcd, 0, char_data);
    hd44780_upload_character(&lcd, 1, char_data + 8);
}

void write_on_lcd(char *string, int line)
{
    hd44780_gotoxy(&lcd, 0, line);
    hd44780_puts(&lcd, string);
}

void clear_lcd() {
    hd44780_clear(&lcd);
}
# Workshop 4-2. I²C: Синхронний послідовний інтерфейс із адресацією (TWI)


TBD - review all the code


Завдання
Ознайомитися з документацією на компоненти для практичного заняття
OLED  дисплей SSD1306
https://files.waveshare.com/upload/a/af/SSD1306-Revision_1.1.pdf

RTC DS1307
https://eu.mouser.com/datasheet/3/1014/1/DS1307.pdf

Memory AT24C32E
https://eu.mouser.com/datasheet/3/282/1/AT24C32E_I2C_Compatible__Two_Wire__Serial_EEPROM_32_Kbit__4_096x8__20006109B.pdf

Скласти план роботи з дисплеєм (послідовність даних які треба відправити на які адреси). Наприклад:
Display OFF (0xAE)
set multiplex (для висоти: 0x1F для 32, 0x3F для 64)
set display offset (0xD3, 0x00)
set start line (0x40)
addressing mode (0x20, 0x00/0x02 залежно від плану)
segment remap (0xA0/0xA1)
COM scan direction (0xC0/0xC8)
COM pins config (0xDA, значення залежить від 32/64)
contrast (0x81, value)
precharge (0xD9, value)
vcomh (0xDB, value)
charge pump / regulator enable (якщо є; у SSD1306 це 0x8D, 0x14)
Display ON (0xAF)
Опційно: намалювати блок-схему.
# Tasks 5.3 
Яка ширина доріжки живлення необхідна в таких випадках. Поясніть.



## Task 1
    Task: 
    Ви розробляєте плату розширення (HAT) для Raspberry Pi 4 Model B. Плата повинна подавати живлення 5V на контакти GPIO мікрокомп'ютера від зовнішнього джерела. Довжина доріжки: 25 мм

    **Solution:**
    - PI 4 - max power consumption 7-15 W (taking into account usb ports)
    - I = 15W/5V = 3 A
    - **Conductor width**: 1.54mm

"C:\Users\user\Pictures\Screenshots\Screenshot 2026-08-02 161125.png" - calculator

## Task 2

Ви проектуєте мініатюрний бездротовий датчик температури в кімнаті на базі ESP32-WROOM-32. Плата має багато елементів, тому доріжки мають бути тонкими, але витримувати пікові навантаження під час передачі даних. Довжина доріжки: 40 мм

**Solution:**
- Minimum current delivered by power supply: 500 mA (from https://www.alldatasheet.com/datasheet-pdf/download/1179101/ESPRESSIF/ESP-WROOM-32.html)
-  **Conductor width:**  0.14mm


./images/Task2.png
  
## Task 3

Ви розробляєте контролер для керування розумною світлодіодною стрічкою на базі діодів WS2812B (Neopixel), що буде працювати на вулиці за типового українського клімату круглий рік. До вашої плати підключається стрічка довжиною 2 метри зі щільністю 60 світлодіодів на метр. Довжина доріжки (від клеми живлення до конектора стрічки): 50 мм.

**Solution:**
- WS2912B - maximum of ~60 mA at 5V white light()
- LED_COUNT=60 * 2 = 120 
- Total current= 120*0.06 = 7.2A
- Also controller itself: 0.5A
- T -50, +50
- **Conductor width**:5.6590  mm
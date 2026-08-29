# Workshop 6-2


## Task

6.2 Вам потрібно створити систему з чотирьох задач із чітким розподілом обов'язків та пріоритетів.

Вимоги до задач:
1. Task "Sensor Reader" (Пріоритет: Середній / Normal)

Дія: Імітує опитування датчика. Виводить у консоль (UART): [Sensor] Data read...

Таймінг: Має виконуватися зі строгою періодичністю - рівно кожні 500 мс.

Підказка: Звичайний vTaskDelay тут використовувати заборонено.

2. Task "Event Trigger" (Пріоритет: Низький / Low)

Дія: Імітує виникнення непередбачуваної зовнішньої події.

Таймінг: Використовує звичайний vTaskDelay або osDelay, щоб "засинати" на 3 секунди.

Умова: Коли задача прокидається, вона повинна динамічно створити нову задачу - "Worker Task" - і знову заснути.

3. Task "Worker Task" (Пріоритет: Високий / High)

Дія: Ця задача створюється лише тоді, коли настає подія (Task 2). Вона має вивести в консоль: [Worker] Processing event...

Умова: Після виводу повідомлення ця задача виконала свою роботу і більше не потрібна. Вона обов'язково повинна правильно завершити своє існування, викликавши vTaskDelete(NULL) (або osThreadExit() / osThreadTerminate() у CMSIS). Виходити з функції задачі через return або дужку } без видалення — заборонено!

4. Task "Maintenance Mode" (Пріоритет: Найвищий / Realtime)

Дія: Імітує перехід пристрою в сервісний режим.

Таймінг: Прокидається раз на 10 секунд.

Умова:

За допомогою функції vTaskSuspend(handle) (або osThreadSuspend) ця задача повинна поставити "Sensor Reader" на паузу.

Вивести в консоль: [Maintenance] System paused for 2 seconds.

Зачекати 2 секунди.

Відновити роботу датчика за допомогою vTaskResume або osThreadResume.

## Solution

Реалізовано на **ESP-IDF для ESP32-C3** (PlatformIO, `framework = espidf`).
Планувальник FreeRTOS в ESP-IDF стартує до `app_main`, тому `vTaskStartScheduler()`
не викликається — усі чотири задачі створюються в
[`app_main()`](src/main.c#L119), а `app_main` після цього просто завершується.

### Розподіл задач

| Задача | Функція | Пріоритет | Механізм таймінгу |
|---|---|---|---|
| Sensor Reader | [`sensor_reader_task()`](src/main.c#L35) | 3 — Normal | [`xTaskDelayUntil()`](src/main.c#L44), рівно 500 мс |
| Event Trigger | [`event_trigger_task()`](src/main.c#L74) | 1 — Low | [`vTaskDelay()`](src/main.c#L77), 3 с |
| Worker Task | [`worker_task()`](src/main.c#L60) | 5 — High | створюється динамічно, живе один прохід |
| Maintenance Mode | [`maintenance_task()`](src/main.c#L94) | 7 — Realtime | [`vTaskDelay()`](src/main.c#L97), 10 с |

Пріоритети, розміри стеків і всі інтервали винесені в
[`src/Config.h`](src/Config.h#L4) — логіка в `main.c` не містить «магічних чисел».

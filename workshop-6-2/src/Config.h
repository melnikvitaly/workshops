#ifndef CONFIG_H
#define CONFIG_H

/* --- Пріоритети (0 = idle; чим більше число, тим вищий пріоритет) --------- */
#define PRIO_EVENT_TRIGGER      1U   /* Low      */
#define PRIO_SENSOR_READER      3U   /* Normal   */
#define PRIO_WORKER             5U   /* High     */
#define PRIO_MAINTENANCE        7U   /* Realtime */

/* --- Розміри стеків у байтах (xTaskCreate в ESP-IDF приймає байти) -------- */
#define STACK_SENSOR_READER     2560U
#define STACK_EVENT_TRIGGER     2560U
#define STACK_WORKER            2560U
#define STACK_MAINTENANCE       2560U

/* --- Таймінги, мс -------------------------------------------------------- */
#define SENSOR_PERIOD_MS        500U
#define EVENT_PERIOD_MS         3000U
#define MAINTENANCE_PERIOD_MS   10000U
#define MAINTENANCE_PAUSE_MS    2000U

#endif /* CONFIG_H */

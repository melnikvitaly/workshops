#pragma once
#include <driver/gpio.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class Button {
    using Callback = void (*)(void *, BaseType_t *);

    gpio_num_t pin_      = GPIO_NUM_NC;
    Callback   shortCb_  = nullptr;
    void      *shortCtx_ = nullptr;
    Callback   longCb_   = nullptr;
    void      *longCtx_  = nullptr;

    volatile int64_t pressStartUs_ = 0;

    static void IRAM_ATTR isr_(void *arg);

public:
    void onShortPress(Callback cb, void *ctx = nullptr) { shortCb_ = cb; shortCtx_ = ctx; }
    void onLongPress (Callback cb, void *ctx = nullptr) { longCb_  = cb; longCtx_  = ctx; }

    // C++17: `auto` as a non-type template parameter — deduces the type of Method
    // at the call site, so the caller writes onShortPress<&Foo::bar>(obj) without
    // spelling out the member-function-pointer type explicitly.
    template<auto Method, typename T>
    void onShortPress(T *obj) {
        shortCtx_ = obj;
        shortCb_  = [](void *ctx, BaseType_t *woken) {
            (static_cast<T *>(ctx)->*Method)(woken);
        };
    }

    // C++17: same `auto` non-type template parameter as above.
    template<auto Method, typename T>
    void onLongPress(T *obj) {
        longCtx_ = obj;
        longCb_  = [](void *ctx, BaseType_t *woken) {
            (static_cast<T *>(ctx)->*Method)(woken);
        };
    }

    void begin(gpio_num_t pin);
};

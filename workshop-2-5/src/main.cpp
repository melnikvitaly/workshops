#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <LoopTracker.h>
#include <Task1.h>

static Task task;

extern "C" void app_main()
{
    task.setup();
    task.dbg.print("Setup finished");
    LoopTracker loopTracker{task.dbg.Scoped("loop")};
    while (true)
    {
        loopTracker.loopStart();
        task.loop();
        loopTracker.loopEnd();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

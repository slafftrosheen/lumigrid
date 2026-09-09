#include "LumigridRuntime.hpp"

extern "C" void app_main(void) {
    static lg::LumigridRuntime runtime;
    runtime.init();
    runtime.start();
}

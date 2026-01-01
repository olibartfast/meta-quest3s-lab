#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include "core/application.hpp"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Quest3AR", __VA_ARGS__)

static quest3ar::Application* g_app = nullptr;

void handle_cmd(android_app* /*app*/, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (g_app) {
                g_app->initialize();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            if (g_app) {
                g_app->shutdown();
            }
            break;
        default:
            break;
    }
}

void android_main(struct android_app* state) {
    LOGI("android_main started");
    
    g_app = new quest3ar::Application(state->activity);
    
    state->onAppCmd = handle_cmd;
    
    int events;
    android_poll_source* source;
    
    while (true) {
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) {
                source->process(state, source);
            }
            
            if (state->destroyRequested) {
                delete g_app;
                g_app = nullptr;
                return;
            }
        }
        
        if (g_app) {
            g_app->run();
        }
    }
}
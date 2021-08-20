#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <memory>
#include <ctime>
#include <cstdlib>

#include "libretro.h"

#define FES_MAJOR_VERSION 1
#define FES_MINOR_VERSION 0
#define FES_PATCH_VERSION 0
#define FES_VERSION_STRING "1.0.0"

namespace {

struct system_t {
    static constexpr float fps = 60.f;
    static constexpr double sample_rate = 44100;
    static constexpr int audio_samples = (int)sample_rate / (int)fps / 10;
};

struct framebuffer_t {
    static constexpr retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_RGB565;
    using pixel_t = unsigned short;
    static constexpr size_t width = 256;
    static constexpr size_t height = 240;
    static constexpr size_t pitch = sizeof(pixel_t) * width;
    static constexpr float aspect_ratio = (float)width / (float)height;

    pixel_t data[framebuffer_t::width * framebuffer_t::height];

    void clear() {
        memset(data, 0, sizeof(data));
    }
};

struct core_t {
    framebuffer_t framebuffer;

#define CB_TYPE(NAME) retro_##NAME##_t
#define CB_VALUE(NAME) NAME##_cb

#define DEFINE_CB_SETTER(NAME) void set(CB_TYPE(NAME) cb) { CB_VALUE(NAME) = cb; }

#define DEFINE_CB_DEFAULT(NAME) CB_TYPE(NAME) CB_VALUE(NAME); \
    DEFINE_CB_SETTER(NAME) \
    template<typename ...Args> auto NAME(Args&&... arg) { \
        return (CB_VALUE(NAME) != nullptr) ? CB_VALUE(NAME)(std::forward<decltype(arg)>(arg)...) : std::invoke_result_t<CB_TYPE(NAME), Args...>{}; \
    }

#define DEFINE_CB(NAME) CB_TYPE(NAME) CB_VALUE(NAME); \
    DEFINE_CB_SETTER(NAME) \
    template<typename ...Args> auto NAME(Args&&... arg) { \
        if (CB_VALUE(NAME) != nullptr) CB_VALUE(NAME)(std::forward<decltype(arg)>(arg)...); \
    }

#define DEFINE_CB_ARG(NAME, ...) CB_TYPE(NAME) NAME##_cb; \
    DEFINE_CB_SETTER(NAME) \
    auto NAME() { \
        if (CB_VALUE(NAME) != nullptr) CB_VALUE(NAME)(__VA_ARGS__); \
    }

    DEFINE_CB_DEFAULT(environment);
    DEFINE_CB_ARG(video_refresh, framebuffer.data, framebuffer_t::width, framebuffer_t::height, framebuffer_t::pitch);
    DEFINE_CB(audio_sample);
    DEFINE_CB_DEFAULT(audio_sample_batch);
    DEFINE_CB(input_poll);
    DEFINE_CB(input_state);
    DEFINE_CB(log_printf);

#undef CB_TYPE
#undef CB_VALUE
#undef DEFINE_CB
#undef DEFINE_CB_DEFAULT
#undef DEFINE_CB_ARG
} core;

constexpr retro_system_info system_info = {
    "Fantasy Entertainment System",
    FES_VERSION_STRING,
    "fes",
    true,
    false
};

constexpr retro_system_av_info system_av_info = {
    {
        framebuffer_t::width,
        framebuffer_t::height,
        framebuffer_t::width,
        framebuffer_t::height,
        framebuffer_t::aspect_ratio
    },
    {
        system_t::fps,
        system_t::sample_rate
    }
};

void std_log_printf(retro_log_level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

};

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

// Cheats
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

// Load a cartridge
bool retro_load_game(const struct retro_game_info *info)
{
    return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) { return false; }

// Unload the cartridge
void retro_unload_game(void) {}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

// libretro unused api functions
void retro_set_controller_port_device(unsigned port, unsigned device) {}


void *retro_get_memory_data(unsigned id) { return NULL; }
size_t retro_get_memory_size(unsigned id){ return 0; }

// Serialisation methods
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { return false; }
bool retro_unserialize(const void *data, size_t size) { return false; }

// End of retrolib
void retro_deinit(void) {}

// libretro global setters
void retro_set_environment(retro_environment_t cb)
{
    ::core.set(cb);

    bool no_rom = true;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_rom);
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { ::core.set(cb);  }
void retro_set_video_refresh(retro_video_refresh_t cb) { ::core.set(cb); }
void retro_set_audio_sample(retro_audio_sample_t cb) { ::core.set(cb); }
void retro_set_input_poll(retro_input_poll_t cb) { ::core.set(cb); }
void retro_set_input_state(retro_input_state_t cb) { ::core.set(cb); }

void retro_init(void)
{
    /* set up some logging */
    struct retro_log_callback log;
    unsigned level = 4;

    std::srand(time(NULL));

    if (::core.environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        ::core.set(log.log);
    else
        ::core.set(::std_log_printf);

    // the performance level is guide to frontend to give an idea of how intensive this core is to run
    ::core.environment(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

    static framebuffer_t::pixel_t pixel = 0;
    for (int x = 0; x < framebuffer_t::width; ++x) {
        for (int y = 0; y < framebuffer_t::height; ++y) {
            if (pixel == 0xFFFF) pixel = 0;
            ::core.framebuffer.data[x + y * framebuffer_t::width] = pixel++;
        }
    }
}


/*
 * Tell libretro about this core, it's name, version and which rom files it supports.
 */
void retro_get_system_info(struct retro_system_info *info) {
    memcpy(info, &system_info, sizeof(system_info));
}

/*
 * Tell libretro about the AV system; the fps, sound sample rate and the
 * resolution of the display.
 */
void retro_get_system_av_info(struct retro_system_av_info *info) {
    memcpy(info, &system_av_info, sizeof(system_av_info));

    // the performance level is guide to frontend to give an idea of how intensive this core is to run
    int pixel_format = framebuffer_t::pixel_format;
    ::core.environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format);
}


// Reset the Vectrex
void retro_reset(void) {  }

// Run a single frames with out Vectrex emulation.
void retro_run(void)
{
    // 882 audio samples per frame (44.1kHz @ 50 fps)
    for (int i = 0; i < system_t::audio_samples; i++) {
        ::core.audio_sample(1, 1);
    }
#if 0
    for (auto& cell : ::core.framebuffer.data) {
        cell = std::rand();
    }
#endif

    //::core.log_printf(retro_log_level::RETRO_LOG_ERROR, "Hello World! (%d)\n", std::rand());
    ::core.input_poll();
    ::core.video_refresh();
}

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <memory>
#include <ctime>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <vector>

#include "libretro.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define FES_MAJOR_VERSION 1
#define FES_MINOR_VERSION 0
#define FES_PATCH_VERSION 0
#define FES_VERSION_STRING "1.0.0"

namespace {

struct system_t {
    static constexpr float fps = 60.f;
    static constexpr double sample_rate = 44100;
    static constexpr int audio_samples = (int)sample_rate / (int)fps;
};

unsigned char* image_data = nullptr;
stb_vorbis* vorbis = nullptr;
stb_vorbis_info vorbis_info;

struct framebuffer_t {
    static constexpr retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
    using pixel_t = unsigned int;
    static constexpr size_t width = 256;
    static constexpr size_t height = 240;
    static constexpr size_t pitch = sizeof(pixel_t) * width;
    static constexpr float aspect_ratio = (float)width / (float)height;

    pixel_t data[framebuffer_t::width * framebuffer_t::height];

    void clear(pixel_t pixel = 0) {
        memset(data, pixel, sizeof(data));
    }

    void point(int x, int y, pixel_t pixel = 0) {
        data[y * width + x] = pixel;
    }

    void blit(int x, int y, pixel_t pixel, size_t rect_width, size_t rect_height) {
        auto w = std::min(width - x, rect_width);
        auto h = std::min(height - x, rect_height);
        for (size_t i = 0; i < w; ++i) {
            for (size_t j = 0; j < h; ++j) {
                auto src = (j + y) * width + (i + x);
                data[src] = pixel;
            }
        }
    }

    void blit(int x, int y, pixel_t *pixels, size_t rect_width, size_t rect_height) {
        auto w = std::min(width - x, rect_width);
        auto h = std::min(height - x, rect_height);
        for (size_t i = 0; i < w; ++i) {
            for (size_t j = 0; j < h; ++j) {
                auto src = (j + y) * width + (i + x);
                auto dist = j * rect_width + i;
                data[src] = pixels[dist];
            }
        }
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
    DEFINE_CB_DEFAULT(input_state);
    DEFINE_CB(log_printf);

    bool use_audio = false;

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
    (void)level;
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

#define PI 3.14159265359

static void audio_callback(void)
{
#if 0
    static unsigned phase;
    for (unsigned i = 0; i < system_t::audio_samples; i++, phase++)
    {
        int16_t val = 0x800 * sinf(2.0f * PI * phase * 300.0f / 30000.0f);
        ::core.audio_sample(val, val);
    }

    phase %= 100;
#endif
    static std::vector<short> stream;
    if (stream.size() == 0) {
        stream.resize(static_cast<size_t>(vorbis_info.channels) * system_t::audio_samples);
    }
    if (stb_vorbis_get_frame_short_interleaved(vorbis, 2, (short*)stream.data(), stream.size()) > 0) {
        ::core.audio_sample_batch(stream.data(), system_t::audio_samples);

    } else {
        for (unsigned i = 0; i < system_t::audio_samples; i++) {
            ::core.audio_sample(1, 1);
        }
    }
}

static void audio_set_state(bool enable)
{
    (void)enable;
}

// Load a cartridge
bool retro_load_game(const struct retro_game_info *info)
{
    auto fmt = framebuffer_t::pixel_format;
    if (!::core.environment(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
    {
        ::core.log_printf(RETRO_LOG_INFO, "XRGB8888 is not supported.\n");
        return false;
    }

    struct retro_audio_callback audio_cb = { audio_callback, audio_set_state };
    ::core.use_audio = ::core.environment(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, &audio_cb);

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
    std::srand(time(NULL));

    struct retro_log_callback log;
    if (::core.environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        ::core.set(log.log);
    else
        ::core.set(::std_log_printf);

    // the performance level is guide to frontend to give an idea of how intensive this core is to run
    unsigned level = 4;
    ::core.environment(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

    ::core.framebuffer.clear(0xFFFFFFFF);

    int image_width = 0;
    int image_height = 0;
    int channels_in_file = 0;
    constexpr int desired_channels = 4;
    image_data = stbi_load("../assets/test.png", &image_width, &image_height, &channels_in_file, desired_channels);
    for (int x = 0; x < image_width; ++x) {
        for (int y = 0; y < image_height; ++y) {
            auto index = y * image_width * desired_channels + x * desired_channels;
            std::swap(image_data[index + 0], image_data[index + 2]);
            image_data[index + 3];
        }
    }
#if 0
    // Dawnbringer pallete 8
    constexpr ::framebuffer_t::pixel_t pallete[] = {
        0x000000,
        0x55415F,
        0x646964,
        0xD77355,
        0x508CD7,
        0x64B964,
        0xE6C86E,
        0xDCF5FF,
    };
    constexpr size_t k = 0;
    constexpr size_t v = 1;
    constexpr size_t a = 2;
    constexpr size_t r = 3;
    constexpr size_t g = 4;
    constexpr size_t b = 5;
    constexpr size_t y = 6;
    constexpr size_t w = 7;
    constexpr size_t sprite[8 * 8] = {
        1, 0, 0, 0, 0, 0, 0, 1,
        0, 2, 1, 1, 1, 1, 2, 0,
        0, 1, 3, 2, 2, 3, 1, 0,
        0, 1, 2, 4, 5, 2, 1, 0,
        0, 1, 2, 6, 7, 2, 1, 0,
        0, 1, 3, 2, 2, 3, 1, 0,
        0, 2, 1, 1, 1, 1, 2, 0,
        1, 0, 0, 0, 0, 0, 0, 1,
    };
    constexpr auto sprite_size = sizeof(sprite) / sizeof(sprite[0]);

    ::framebuffer_t::pixel_t pixels[sprite_size]{};
    for (int i = 0; i < sprite_size; ++i) {
        pixels[i] = pallete[sprite[i]];
    }

    ::core.framebuffer.blit(0, 0, pixels, 8, 8);
#else
    if (image_data) ::core.framebuffer.blit(0, 0, (::framebuffer_t::pixel_t*)image_data, image_width, image_height);
#endif

    int error = 0;
    stb_vorbis_alloc* alloc = nullptr;
    vorbis = stb_vorbis_open_filename("../assets/test.ogg", &error, alloc);
    vorbis_info = stb_vorbis_get_info(vorbis);
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
    if (!::core.use_audio) {
        audio_callback();
    }
    
#if 0
    for (auto& cell : ::core.framebuffer.data) {
        cell = std::rand();
    }
#endif

    //::core.log_printf(retro_log_level::RETRO_LOG_ERROR, "Hello World! (%d)\n", std::rand());
    ::core.input_poll();

    static int timer = static_cast<int>(system_t::fps);
    if (::core.input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP) && timer <= 0)
    {
        if (vorbis) {
            stb_vorbis_seek_start(vorbis);
            timer = static_cast<int>(system_t::fps);
        }
    }
    if (timer > 0) timer--;

    ::core.video_refresh();
}

#include <stdio.h>
#include <errno.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>


#include "riv.h"
#include "libretro.h"

riv_context g_riv;

#define PALETTE_SIZE 256

// Function to convert a pixel from XRGB8888 format to RGB332 format
uint8_t xrgb8888_to_rgb332(uint32_t xrgb) {
    // Extract the red, green, and blue components
    uint8_t red = (xrgb >> 16) & 0xFF;
    uint8_t green = (xrgb >> 8) & 0xFF;
    uint8_t blue = xrgb & 0xFF;

    // Convert to the RGB332 format
    uint8_t red_3bit = red >> 5;
    uint8_t green_3bit = green >> 5;
    uint8_t blue_2bit = blue >> 6;

    // Combine the components into a single byte
    return (red_3bit << 5) | (green_3bit << 2) | blue_2bit;
}

#define load_sym(V, S) do {\
	if (!((*(void**)&V) = dlsym(g_retro.handle, #S))) \
		riv_printf("Failed to load symbol '" #S "'': %s", dlerror()); \
	} while (0)
#define load_retro_sym(S) load_sym(g_retro.S, S)

static struct {
	void *handle;
	bool initialized;

	void (*retro_init)(void);
	void (*retro_deinit)(void);
	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info *info);
	void (*retro_get_system_av_info)(struct retro_system_av_info *info);
	void (*retro_set_controller_port_device)(unsigned port, unsigned device);
	void (*retro_reset)(void);
	void (*retro_run)(void);
	size_t (*retro_serialize_size)(void);
	bool (*retro_serialize)(void *data, size_t size);
	bool (*retro_unserialize)(const void *data, size_t size);
//	void retro_cheat_reset(void);
//	void retro_cheat_set(unsigned index, bool enabled, const char *code);
	bool (*retro_load_game)(const struct retro_game_info *game);
//	bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
	void (*retro_unload_game)(void);
//	unsigned retro_get_region(void);
//	void *retro_get_memory_data(unsigned id);
//	size_t retro_get_memory_size(unsigned id);
} g_retro;

static void core_log(enum retro_log_level level, const char *fmt, ...) {
	char buffer[4096] = {0};
	static const char * levelstr[] = { "dbg", "inf", "wrn", "err" };
	va_list va;

	va_start(va, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	if (level == 0)
		return;

	fprintf(stderr, "[%s] %s", levelstr[level], buffer);
	fflush(stderr);

	if (level == RETRO_LOG_ERROR)
		exit(EXIT_FAILURE);
}
static bool core_environment(unsigned cmd, void *data) {
	bool *bval;

	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
		struct retro_log_callback *cb = (struct retro_log_callback *)data;
		cb->log = core_log;
		break;
	}
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		bval = (bool*)data;
		*bval = true;
		break;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		const enum retro_pixel_format *fmt = (enum retro_pixel_format *)data;

		if (*fmt != RETRO_PIXEL_FORMAT_XRGB8888)
			return false;

		//return video_set_pixel_format(*fmt);
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = ".";
		return true;

	default:
		core_log(RETRO_LOG_DEBUG, "Unhandled env #%u", cmd);
		return false;
	}

	return true;
}

static void video_refresh(const void *data, unsigned width, unsigned height, unsigned pitch) {

    const uint32_t *inputBuffer = (const uint32_t *)data;
    uint8_t *outputBuffer = malloc(width * height); // Allocate memory for RGB332 format data

    if (!outputBuffer) {
        fprintf(stderr, "Failed to allocate memory for output buffer\n");
        return;
    }

    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            uint32_t pixel = inputBuffer[y * (pitch / 4) + x]; // Get pixel from input buffer
            outputBuffer[y * width + x] = xrgb8888_to_rgb332(pixel); // Convert and store in output buffer
        }
    }

	 // Update the riv_context structure
	g_riv.framebuffer = outputBuffer;

    // Update the framebuffer description
    g_riv.framebuffer_desc->width = width;
    g_riv.framebuffer_desc->height = height;
    g_riv.framebuffer_desc->pixel_format = RIV_PIXELFORMAT_PAL256;


    free(outputBuffer); // Free the output buffer after use
}
static void core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
	if (data)
		video_refresh(data, width, height, pitch);
}
static void core_load(const char *sofile) {
	void (*set_environment)(retro_environment_t) = NULL;
	void (*set_video_refresh)(retro_video_refresh_t) = NULL;
	void (*set_input_poll)(retro_input_poll_t) = NULL;
	void (*set_input_state)(retro_input_state_t) = NULL;
	void (*set_audio_sample)(retro_audio_sample_t) = NULL;
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;

	memset(&g_retro, 0, sizeof(g_retro));
	g_retro.handle = dlopen(sofile, RTLD_LAZY);

	if (!g_retro.handle)
		riv_printf("Failed to load core: %s", dlerror());

	dlerror();

	load_retro_sym(retro_init);
	load_retro_sym(retro_deinit);
	load_retro_sym(retro_api_version);
	load_retro_sym(retro_get_system_info);
	load_retro_sym(retro_get_system_av_info);
	load_retro_sym(retro_set_controller_port_device);
	load_retro_sym(retro_reset);
	load_retro_sym(retro_run);
	load_retro_sym(retro_load_game);
	load_retro_sym(retro_unload_game);
	load_retro_sym(retro_serialize_size);
	load_retro_sym(retro_serialize);
	load_retro_sym(retro_unserialize);

	load_sym(set_environment, retro_set_environment);
	load_sym(set_video_refresh, retro_set_video_refresh);
	load_sym(set_input_poll, retro_set_input_poll);
	load_sym(set_input_state, retro_set_input_state);
	load_sym(set_audio_sample, retro_set_audio_sample);
	load_sym(set_audio_sample_batch, retro_set_audio_sample_batch);

	set_environment(core_environment);
	set_video_refresh(core_video_refresh);
	//set_input_poll(core_input_poll);
	//set_input_state(core_input_state);
	//set_audio_sample(core_audio_sample);
	//set_audio_sample_batch(core_audio_sample_batch);

	g_retro.retro_init();
	g_retro.initialized = true;

	puts("Core loaded");
}


static void core_load_game(const char *filename) {
	struct retro_system_av_info av = {0};
	struct retro_system_info system = {0};
	struct retro_game_info info = { filename, 0 };
	FILE *file = fopen(filename, "rb");

	fseek(file, 0, SEEK_END);
	info.size = ftell(file);
	rewind(file);

	g_retro.retro_get_system_info(&system);
	if (!system.need_fullpath) {
		info.data = malloc(info.size);

		if (!info.data || !fread((void*)info.data, info.size, 1, file))
			riv_printf("error");
	}

	if (!g_retro.retro_load_game(&info))
		riv_printf("The core failed to load the content.");

	g_retro.retro_get_system_av_info(&av);
	return;
}


static void core_unload() {
	if (g_retro.initialized)
		g_retro.retro_deinit();

	if (g_retro.handle)
		dlclose(g_retro.handle);
}
static void cleanup_cb(riv_context *ctx) {
	core_unload();
    riv_shutdown(&g_riv);
}
static void frame_cb(riv_context *ctx) {
	g_retro.retro_run();
}
static void init_cb(riv_context *ctx) {
	g_riv = *ctx;
	core_load("/usr/lib/sameboy_libretro.so");
	printf("core loaded\n");
    core_load_game("/usr/lib/red.gb");
	printf("game loaded\n");
}

int main(int argc, char **argv) {
	//if (argc < 3)
	//	die("usage: %s <core> <game> [-s default-scale] [-l load-savestate] [-d save-savestate]", argv[0]);
  
	riv_framebuffer_desc gameboyFramebufferDesc = {
	    .width = 160,
	    .height = 144,
		.target_fps = 60,
		.pixel_format = RIV_PIXELFORMAT_PAL256
	};

    riv_run_desc gameboyRunDesc = {
        .init_cb = init_cb,
        .cleanup_cb = cleanup_cb,
        .frame_cb = frame_cb,
        .framebuffer_desc = gameboyFramebufferDesc,
        .argc = argc,
        .argv = argv
    };
	riv_run(&gameboyRunDesc);


    return 0;
}
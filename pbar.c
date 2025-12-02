#include <fcft/fcft.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

#include "protocols/fractional-scale.h"
#include "protocols/wlr-layer-shell.h"

struct wl_output_wrapper {
    struct wl_output* wl_output;
    uint32_t wl_name;
    const char* name;
    struct bar* bar;
};

struct wl_seat_wrapper {
    struct wl_seat* wl_seat;
    uint32_t wl_name;
    const char* name;
    struct bar* bar;
};

struct bar {
    const char* const version;

    const char* bg;
    const char* fg;
    const char* font;
    const char* output;
    const char* seat;
    bool bottom;
    uint32_t gap;

    struct wl_display* wl_display;
    struct wl_registry* wl_registry;

    struct wl_compositor* wl_compositor;
    uint32_t wl_compositor_name;
    struct wl_shm* wl_shm;
    uint32_t wl_shm_name;
    struct wp_fractional_scale_manager_v1* wp_fractional_scale_manager;
    uint32_t wp_fractional_scale_manager_name;
    struct zwlr_layer_shell_v1* zwlr_layer_shell;
    uint32_t zwlr_layer_shell_name;

    bool wl_output_available;
    struct wl_output_wrapper* wl_output_wrapper;
    struct wl_surface* wl_surface;
    struct wp_fractional_scale_v1* wp_fractional_scale;
    struct zwlr_layer_surface_v1* zwlr_layer_surface;

    bool wl_seat_available;
    struct wl_seat_wrapper* wl_seat_wrapper;
    struct wl_pointer* wl_pointer;

    uint32_t width, height, x, y, scale;
};

enum {
    NO_ERROR,
    INNER_ERROR,
    RUNTIME_ERROR,
};

static void quit(struct bar* bar, const int code, const char* restrict fmt, ...)
{
    if (fmt != NULL) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }

    if (bar->wl_seat_wrapper) {
        if (bar->wl_pointer) wl_pointer_release(bar->wl_pointer);
        wl_seat_release(bar->wl_seat_wrapper->wl_seat);
        free(bar->wl_seat_wrapper);
    }
    if (bar->wl_output_wrapper) {
        if (bar->zwlr_layer_surface) zwlr_layer_surface_v1_destroy(bar->zwlr_layer_surface);
        if (bar->wp_fractional_scale) wp_fractional_scale_v1_destroy(bar->wp_fractional_scale);
        if (bar->wl_surface) wl_surface_destroy(bar->wl_surface);
        wl_output_release(bar->wl_output_wrapper->wl_output);
        free(bar->wl_output_wrapper);
    }
    if (bar->zwlr_layer_shell) zwlr_layer_shell_v1_destroy(bar->zwlr_layer_shell);
    if (bar->wp_fractional_scale_manager) wp_fractional_scale_manager_v1_destroy(bar->wp_fractional_scale_manager);
    if (bar->wl_shm) wl_shm_release(bar->wl_shm);
    if (bar->wl_compositor) wl_compositor_destroy(bar->wl_compositor);
    if (bar->wl_registry) wl_registry_destroy(bar->wl_registry);
    if (bar->wl_display) wl_display_disconnect(bar->wl_display);
    fcft_fini();
    exit(code);
}

static void wp_fractional_scale_handle_preferred_scale(void* data, struct wp_fractional_scale_v1* wp_fractional_scale_v1, uint32_t scale)
{
    struct bar* bar = data;
    bar->scale = scale;
}
static const struct wp_fractional_scale_v1_listener wp_fractional_scale_listener = {
    .preferred_scale = wp_fractional_scale_handle_preferred_scale,
};

static void zwlr_layer_surface_handle_configure(void* data, struct zwlr_layer_surface_v1* zwlr_layer_surface, uint32_t serial, uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_ack_configure(zwlr_layer_surface, serial);
    struct bar* bar = data;
    bar->width = width;
}
static void zwlr_layer_surface_handle_closed(void* data, struct zwlr_layer_surface_v1* zwlr_layer_surface_v1) { }
static const struct zwlr_layer_surface_v1_listener zwlr_layer_surface_listener = {
    .configure = zwlr_layer_surface_handle_configure,
    .closed = zwlr_layer_surface_handle_closed,
};

static void wl_output_handle_name(void* data, struct wl_output* wl_output, const char* name)
{
    struct wl_output_wrapper* wl_output_wrapper = data;
    wl_output_wrapper->name = name;
}
static void wl_output_handle_done(void* data, struct wl_output* wl_output)
{
    struct wl_output_wrapper* wl_output_wrapper = data;
    struct bar* bar = wl_output_wrapper->bar;
    if (bar->wl_output_wrapper == NULL && (bar->output == NULL || strcmp(wl_output_wrapper->name, bar->output) == 0)) {
        bar->wl_output_wrapper = wl_output_wrapper;

        bar->height = fcft_from_name(1, (const char*[]) { bar->font }, "dpi=96")->height;
        bar->wl_surface = wl_compositor_create_surface(bar->wl_compositor);
        bar->wp_fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(bar->wp_fractional_scale_manager, bar->wl_surface);
        wp_fractional_scale_v1_add_listener(bar->wp_fractional_scale, &wp_fractional_scale_listener, bar);
        bar->zwlr_layer_surface = zwlr_layer_shell_v1_get_layer_surface(bar->zwlr_layer_shell, bar->wl_surface, bar->wl_output_wrapper->wl_output, ZWLR_LAYER_SHELL_V1_LAYER_TOP, "statusbar");
        zwlr_layer_surface_v1_add_listener(bar->zwlr_layer_surface, &zwlr_layer_surface_listener, bar);
        zwlr_layer_surface_v1_set_anchor(bar->zwlr_layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT | (bar->bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP));
        zwlr_layer_surface_v1_set_margin(bar->zwlr_layer_surface, bar->gap, bar->gap, bar->gap, bar->gap);
        zwlr_layer_surface_v1_set_exclusive_zone(bar->zwlr_layer_surface, bar->height);
        zwlr_layer_surface_v1_set_size(bar->zwlr_layer_surface, 0, bar->height);
        wl_surface_commit(bar->wl_surface);
        return;
    }
    if (wl_output_wrapper != bar->wl_output_wrapper) {
        wl_output_release(wl_output_wrapper->wl_output);
        free(wl_output_wrapper);
    } else {
        zwlr_layer_surface_v1_set_size(bar->zwlr_layer_surface, 0, bar->height);
    }
}
static void wl_output_handle_scale(void* data, struct wl_output* wl_output, int32_t factor) { }
static void wl_output_handle_geometry(void* data, struct wl_output* wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char* make, const char* model, int32_t transform) { }
static void wl_output_handle_mode(void* data, struct wl_output* wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) { }
static void wl_output_handle_description(void* data, struct wl_output* wl_output, const char* description) { }
static const struct wl_output_listener wl_output_listener = {
    .name = wl_output_handle_name,
    .geometry = wl_output_handle_geometry,
    .mode = wl_output_handle_mode,
    .done = wl_output_handle_done,
    .scale = wl_output_handle_scale,
    .description = wl_output_handle_description,
};

static void wl_pointer_handle_enter(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct bar* bar = data;
    bar->x = wl_fixed_to_double(surface_x);
    bar->y = wl_fixed_to_double(surface_y);
}
static void wl_pointer_handle_leave(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface)
{
    struct bar* bar = data;
    bar->x = UINT32_MAX;
    bar->y = UINT32_MAX;
}
static void wl_pointer_handle_motion(void* data, struct wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    struct bar* bar = data;
    bar->x = wl_fixed_to_double(surface_x);
    bar->y = wl_fixed_to_double(surface_y);
}
static void wl_pointer_handle_button(void* data, struct wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) { }
static void wl_pointer_handle_axis(void* data, struct wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) { }
static void wl_pointer_handle_frame(void* data, struct wl_pointer* wl_pointer) { }
static void wl_pointer_handle_axis_source(void* data, struct wl_pointer* wl_pointer, uint32_t axis_source) { }
static void wl_pointer_handle_axis_stop(void* data, struct wl_pointer* wl_pointer, uint32_t time, uint32_t axis) { }
static void wl_pointer_handle_axis_discrete(void* data, struct wl_pointer* wl_pointer, uint32_t axis, int32_t discrete) { }
static const struct wl_pointer_listener wl_pointer_listener = {
    .enter = wl_pointer_handle_enter,
    .leave = wl_pointer_handle_leave,
    .motion = wl_pointer_handle_motion,
    .button = wl_pointer_handle_button,
    .axis = wl_pointer_handle_axis,
    .frame = wl_pointer_handle_frame,
    .axis_source = wl_pointer_handle_axis_source,
    .axis_stop = wl_pointer_handle_axis_stop,
    .axis_discrete = wl_pointer_handle_axis_discrete,
};

static void wl_seat_handle_name(void* data, struct wl_seat* wl_seat, const char* name)
{
    struct wl_seat_wrapper* wl_seat_wrapper = data;
    wl_seat_wrapper->name = name;
}
static void wl_seat_handle_capabilities(void* data, struct wl_seat* wl_seat, uint32_t capabilities)
{
    struct wl_seat_wrapper* wl_seat_wrapper = data;
    struct bar* bar = wl_seat_wrapper->bar;
    if (bar->wl_seat_wrapper == NULL && (bar->seat == NULL || strcmp(wl_seat_wrapper->name, bar->output) == 0)) {
        bar->wl_seat_wrapper = wl_seat_wrapper;
    }
    if (wl_seat_wrapper != bar->wl_seat_wrapper) {
        wl_seat_release(wl_seat_wrapper->wl_seat);
        free(wl_seat_wrapper);
    } else {
        bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
        if (have_pointer && bar->wl_pointer == NULL) {
            bar->wl_pointer = wl_seat_get_pointer(bar->wl_seat_wrapper->wl_seat);
            wl_pointer_add_listener(bar->wl_pointer, &wl_pointer_listener, bar);
        } else if (!have_pointer && bar->wl_pointer != NULL) {
            wl_pointer_release(bar->wl_pointer);
            bar->wl_pointer = NULL;
            bar->x = UINT32_MAX;
            bar->y = UINT32_MAX;
        }
    }
}
static const struct wl_seat_listener wl_seat_listener = {
    .name = wl_seat_handle_name,
    .capabilities = wl_seat_handle_capabilities,
};

static void wl_registry_handle_global(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version)
{
    struct bar* bar = data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        bar->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 3);
        bar->wl_compositor_name = name;
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        bar->wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 2);
        bar->wl_shm_name = name;
    } else if (!strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
        bar->wp_fractional_scale_manager = wl_registry_bind(wl_registry, name, &wp_fractional_scale_manager_v1_interface, 1);
        bar->zwlr_layer_shell_name = name;
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        bar->zwlr_layer_shell = wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface, 3);
        bar->zwlr_layer_shell_name = name;
    } else if (!strcmp(interface, wl_output_interface.name)) {
        bar->wl_output_available = true;
        struct wl_output_wrapper* wl_output_wrapper = calloc(1, sizeof(struct wl_output_wrapper));
        wl_output_wrapper->wl_output = wl_registry_bind(wl_registry, name, &wl_output_interface, 4);
        wl_output_wrapper->wl_name = name;
        wl_output_wrapper->bar = bar;
        wl_output_add_listener(wl_output_wrapper->wl_output, &wl_output_listener, wl_output_wrapper);
    } else if (!strcmp(interface, wl_seat_interface.name)) {
        bar->wl_seat_available = true;
        struct wl_seat_wrapper* wl_seat_wrapper = calloc(1, sizeof(struct wl_seat_wrapper));
        wl_seat_wrapper->wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 5);
        wl_seat_wrapper->wl_name = name;
        wl_seat_wrapper->bar = bar;
        wl_seat_add_listener(wl_seat_wrapper->wl_seat, &wl_seat_listener, wl_seat_wrapper);
    }
}
static void wl_registry_handle_global_remove(void* data, struct wl_registry* wl_registry, uint32_t name)
{
    struct bar* bar = data;
    if (name == bar->wl_compositor_name) {
        quit(bar, INNER_ERROR, "Wayland compositor removed.\n");
    } else if (name == bar->wl_shm_name) {
        quit(bar, INNER_ERROR, "Wayland shared memory removed.\n");
    } else if (name == bar->wp_fractional_scale_manager_name) {
        quit(bar, INNER_ERROR, "Wayland fractional scale manager removed.\n");
    } else if (name == bar->zwlr_layer_shell_name) {
        quit(bar, INNER_ERROR, "Wayland layer shell removed.\n");
    } else if (bar->wl_output_wrapper != NULL && name == bar->wl_output_wrapper->wl_name) {
        quit(bar, INNER_ERROR, "Wayland output removed.\n");
    } else if (bar->wl_seat_wrapper != NULL && name == bar->wl_seat_wrapper->wl_name) {
        quit(bar, INNER_ERROR, "Wayland seat removed.\n");
    }
}
static const struct wl_registry_listener wl_registry_listener = {
    .global = wl_registry_handle_global,
    .global_remove = wl_registry_handle_global_remove,
};

static void pipe_init(struct bar* bar)
{
    struct stat stdin_stat;
    fstat(STDIN_FILENO, &stdin_stat);
    if (!S_ISFIFO(stdin_stat.st_mode)) {
        quit(bar, NO_ERROR,
            "pbar is a wayland statusbar that renders plain text from stdin and prints mouse event action to stdout.\n"
            "pbar version: %s\n"
            "pbar usage: producer | pbar [options] | consumer\n"
            "\n"
            "options are:\n"
            "        -B rrggbb[aa]   set default background color (000000ff)\n"
            "        -F rrggbb[aa]   set default foreground color (ffffffff)\n"
            "        -T font[:k=v]   set default font (monospace)\n"
            "        -o output       set wayland output\n"
            "        -s seat         set wayland seat\n"
            "        -b              place the bar at the bottom\n"
            "        -g gap          set margin gap (0)\n"
            "\n",
            bar->version);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
}

static void init(struct bar* bar)
{
    pipe_init(bar);

    fcft_init(FCFT_LOG_COLORIZE_AUTO, false, FCFT_LOG_CLASS_ERROR);
    if (!(fcft_capabilities() & FCFT_CAPABILITY_TEXT_RUN_SHAPING))
        quit(bar, INNER_ERROR, "fcft does not support text-run shaping.\n");

    bar->wl_display = wl_display_connect(NULL);
    if (bar->wl_display == NULL)
        quit(bar, INNER_ERROR, "failed to connect to wayland display.\n");
}

static void setup(struct bar* bar)
{
    bar->wl_registry = wl_display_get_registry(bar->wl_display);
    wl_registry_add_listener(bar->wl_registry, &wl_registry_listener, bar);

    wl_display_roundtrip(bar->wl_display); // wait for wayland registry handlers
    if (bar->wl_compositor == NULL) {
        quit(bar, INNER_ERROR, "failed to get wayland compositor.\n");
    } else if (bar->wl_shm == NULL) {
        quit(bar, INNER_ERROR, "failed to get wayland shared memory.\n");
    } else if (bar->wp_fractional_scale_manager == NULL) {
        quit(bar, INNER_ERROR, "failed to get wayland fractional scale manager.\n");
    } else if (bar->zwlr_layer_shell == NULL) {
        quit(bar, INNER_ERROR, "failed to get wayland layer shell.\n");
    } else if (!bar->wl_output_available) {
        quit(bar, INNER_ERROR, "failed to get any wayland output.\n");
    } else if (!bar->wl_seat_available) {
        quit(bar, INNER_ERROR, "failed to get any wayland seat.\n");
    }

    wl_display_roundtrip(bar->wl_display); // wait for wayland output & seat handlers
    if (bar->wl_output_wrapper == NULL) {
        quit(bar, INNER_ERROR, "failed to get the wayland output %s.\n", bar->output);
    } else if (bar->wl_seat_wrapper == NULL) {
        quit(bar, INNER_ERROR, "failed to get the wayland seat %s.\n", bar->seat);
    }

    wl_display_roundtrip(bar->wl_display); // wait for wayland output & seat handlers effects
}

static void loop(struct bar* bar)
{
    while (wl_display_dispatch(bar->wl_display) >= 0) { }
}

int main(int argc, char** argv)
{
    struct bar bar = {
        .version = "0.1",
        .bg = "000000ff",
        .fg = "ffffffff",
        .font = "monospace",
        .output = NULL,
        .seat = NULL,
        .bottom = false,
        .gap = 0,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-B") == 0) {
            if (++i < argc) bar.bg = argv[i];
        } else if (strcmp(argv[i], "-F") == 0) {
            if (++i < argc) bar.fg = argv[i];
        } else if (strcmp(argv[i], "-T") == 0) {
            if (++i < argc) bar.font = argv[i];
        } else if (strcmp(argv[i], "-o") == 0) {
            if (++i < argc) bar.output = argv[i];
        } else if (strcmp(argv[i], "-s") == 0) {
            if (++i < argc) bar.seat = argv[i];
        } else if (strcmp(argv[i], "-b") == 0) {
            bar.bottom = true;
        } else if (strcmp(argv[i], "-g") == 0) {
            if (++i < argc) bar.gap = strtoul(argv[i], NULL, 10);
        }
    }

    init(&bar);
    setup(&bar);
    loop(&bar);

    quit(&bar, NO_ERROR, NULL);
}

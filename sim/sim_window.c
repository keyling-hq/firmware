/* The OS-specific parts: window (Win32 on Windows, SDL2 elsewhere), keyboard, clock, folders. */
#include "lvgl.h"
#include "sim.h"

#ifdef _WIN32

#include <windows.h>

static HWND s_hwnd;

lv_display_t *sim_window_create(int width, int height, int zoom)
{
    lv_display_t *disp = lv_windows_create_display(L"Keyling UI", width, height, zoom * 100, false, true);
    if (disp) {
        s_hwnd = lv_windows_get_display_window_handle(disp);
    }
    return disp;
}

bool sim_window_open(void)
{
    return IsWindow(s_hwnd);
}

bool sim_window_focused(void)
{
    return GetForegroundWindow() == s_hwnd;
}

bool sim_key_down(int key)
{
    int vk = key == SIM_KEY_SPACE ? VK_SPACE : key == SIM_KEY_ESC ? VK_ESCAPE : key;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

uint32_t sim_clock_ms(void)
{
    return GetTickCount();
}

void sim_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

void sim_make_dir(const char *path)
{
    CreateDirectoryA(path, NULL);
}

#else

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if LV_USE_SDL

#include <SDL2/SDL.h>

lv_display_t *sim_window_create(int width, int height, int zoom)
{
    lv_display_t *disp = lv_sdl_window_create(width, height);
    if (disp) {
        lv_sdl_window_set_zoom(disp, (float)zoom);
        lv_sdl_window_set_title(disp, "Keyling UI");
    }
    return disp;
}

/* Closing the window ends the process (LV_SDL_DIRECT_EXIT). */
bool sim_window_open(void)
{
    return true;
}

bool sim_window_focused(void)
{
    return SDL_GetKeyboardFocus() != NULL;
}

bool sim_key_down(int key)
{
    SDL_Scancode code = key == SIM_KEY_SPACE           ? SDL_SCANCODE_SPACE
                        : key == SIM_KEY_ESC           ? SDL_SCANCODE_ESCAPE
                        : key >= '1' && key <= '9'     ? SDL_SCANCODE_1 + (key - '1')
                                                       : SDL_SCANCODE_A + (key - 'A');
    return SDL_GetKeyboardState(NULL)[code] != 0;
}

#else

lv_display_t *sim_window_create(int width, int height, int zoom)
{
    fprintf(stderr, "built without SDL2: only screenshots (--out DIR SCRIPT) are available\n");
    return NULL;
}

bool sim_window_open(void)
{
    return false;
}

bool sim_window_focused(void)
{
    return false;
}

bool sim_key_down(int key)
{
    return false;
}

#endif

uint32_t sim_clock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void sim_sleep_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

void sim_make_dir(const char *path)
{
    mkdir(path, 0755);
}

#endif

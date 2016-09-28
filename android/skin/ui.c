/* Copyright (C) 2006-2016 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include "android/skin/ui.h"

#include "android/skin/file.h"
#include "android/skin/image.h"
#include "android/skin/keyboard.h"
#include "android/skin/rect.h"
#include "android/skin/trackball.h"
#include "android/skin/window.h"

#include "android/utils/bufprint.h"
#include "android/utils/debug.h"
#include "android/utils/system.h"

#include <stdbool.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define  D(...)  do {  if (VERBOSE_CHECK(init)) dprint(__VA_ARGS__); } while (0)
#define  DE(...) do { if (VERBOSE_CHECK(keys)) dprint(__VA_ARGS__); } while (0)

struct SkinUI {
    SkinUIParams           ui_params;
    const SkinUIFuncs*     ui_funcs;

    SkinFile*              layout_file;
    SkinLayout*            layout;

    SkinKeyboard*          keyboard;

    SkinWindow*            window;

    bool                   show_trackball;
    SkinTrackBall*         trackball;

    int                    lcd_brightness;

    SkinImage*             onion;
    SkinRotation           onion_rotation;
    int                    onion_alpha;

};

static void _skin_ui_handle_key_command(void* opaque,
                                        SkinKeyCommand command,
                                        int  down);

static SkinLayout* skin_file_select_layout(SkinLayout* layouts,
        const char* layout_name) {
    if (!layout_name) return layouts;
    SkinLayout* currLayout = layouts;
    while (currLayout) {
        if (currLayout->name && !strcmp(currLayout->name, layout_name)) {
            return currLayout;
        }
        currLayout = currLayout->next;
    }
    return layouts;
}

SkinUI* skin_ui_create(SkinFile* layout_file,
                       const char* initial_orientation,
                       const SkinUIFuncs* ui_funcs,
                       const SkinUIParams* ui_params,
                       bool use_emugl_subwindow) {
    SkinUI* ui;

    ANEW0(ui);

    ui->layout_file = layout_file;
    ui->layout = skin_file_select_layout(layout_file->layouts, initial_orientation);

    ui->ui_funcs = ui_funcs;
    ui->ui_params = ui_params[0];

    ui->keyboard = skin_keyboard_create(ui->ui_params.keyboard_charmap,
                                        ui->ui_params.keyboard_raw_keys,
                                        ui_funcs->keyboard_flush);
    ui->window = NULL;

    skin_keyboard_enable(ui->keyboard, 1);
    skin_keyboard_on_command(ui->keyboard, _skin_ui_handle_key_command, ui);

    ui->window = skin_window_create(
            ui->layout, ui->ui_params.window_x, ui->ui_params.window_y,
            0, use_emugl_subwindow, ui->ui_funcs->window_funcs);
    if (!ui->window) {
        skin_ui_free(ui);
        return NULL;
    }

    if (ui->ui_params.enable_trackball) {
        ui->trackball = skin_trackball_create(ui->ui_funcs->trackball_params);
        skin_window_set_trackball(ui->window, ui->trackball);
    }

    ui->lcd_brightness = 128;  /* 50% */
    skin_window_set_lcd_brightness(ui->window, ui->lcd_brightness );

    if (ui->onion) {
        skin_window_set_onion(ui->window,
                              ui->onion,
                              ui->onion_rotation,
                              ui->onion_alpha);
    }

    skin_ui_reset_title(ui);

    skin_window_enable_touch(ui->window, ui->ui_params.enable_touch);
    skin_window_enable_dpad(ui->window, ui->ui_params.enable_dpad);
    skin_window_enable_qwerty(ui->window, ui->ui_params.enable_keyboard);
    skin_window_enable_trackball(ui->window, ui->ui_params.enable_trackball);

    return ui;
}

void skin_ui_free(SkinUI* ui) {
    if (ui->window) {
        skin_window_free(ui->window);
        ui->window = NULL;
    }
    if (ui->trackball) {
        skin_trackball_destroy(ui->trackball);
        ui->trackball = NULL;
    }
    if (ui->keyboard) {
        skin_keyboard_free(ui->keyboard);
        ui->keyboard = NULL;
    }

    skin_image_unref(&ui->onion);

    ui->layout = NULL;

    AFREE(ui);
}

void skin_ui_set_lcd_brightness(SkinUI* ui, int lcd_brightness) {
    ui->lcd_brightness = lcd_brightness;
    if (ui->window) {
        skin_window_set_lcd_brightness(ui->window, lcd_brightness);
    }
}

void skin_ui_set_scale(SkinUI* ui, double scale) {
    if (ui->window) {
        skin_window_set_scale(ui->window, scale);
    }
}

void skin_ui_reset_title(SkinUI* ui) {
    char  temp[128], *p=temp, *end = p + sizeof(temp);

    if (ui->window == NULL)
        return;

    if (ui->show_trackball) {
        SkinKeyBinding  bindings[SKIN_KEY_COMMAND_MAX_BINDINGS];

        int count = skin_keyset_get_bindings(skin_keyset_get_default(),
                                             SKIN_KEY_COMMAND_TOGGLE_TRACKBALL,
                                             bindings);
        if (count > 0) {
            int  nn;
            p = bufprint(p, end, "Press ");
            for (nn = 0; nn < count; nn++) {
                if (nn > 0) {
                    if (nn < count-1)
                        p = bufprint(p, end, ", ");
                    else
                        p = bufprint(p, end, " or ");
                }
                p = bufprint(p, end, "%s",
                             skin_key_pair_to_string(bindings[nn].sym,
                                                     bindings[nn].mod));
            }
            p = bufprint(p, end, " to leave trackball mode. ");
        }
    }

    p = bufprint(p, end, "%s", ui->ui_params.window_name);
    skin_window_set_title(ui->window, temp);
}

void skin_ui_set_onion(SkinUI* ui,
                       SkinImage* onion,
                       SkinRotation onion_rotation,
                       int onion_alpha) {
    if (onion) {
        skin_image_ref(onion);
    }
    skin_image_unref(&ui->onion);

    ui->onion = onion;
    ui->onion_rotation = onion_rotation;
    ui->onion_alpha = onion_alpha;

    if (ui->window) {
        skin_window_set_onion(ui->window,
                              onion,
                              onion_rotation,
                              onion_alpha);
    }
}

/* used to respond to a given keyboard command shortcut
 */
static void
_skin_ui_handle_key_command(void* opaque, SkinKeyCommand command, int  down)
{
    SkinUI* ui = opaque;

    static const struct { SkinKeyCommand  cmd; SkinKeyCode  kcode; }  keycodes[] =
    {
        { SKIN_KEY_COMMAND_BUTTON_CALL,        kKeyCodeCall },
        { SKIN_KEY_COMMAND_BUTTON_HOME,        kKeyCodeHome },
        { SKIN_KEY_COMMAND_BUTTON_HOMEPAGE,    kKeyCodeHomePage },
        { SKIN_KEY_COMMAND_BUTTON_BACK,        kKeyCodeBack },
        { SKIN_KEY_COMMAND_BUTTON_HANGUP,      kKeyCodeEndCall },
        { SKIN_KEY_COMMAND_BUTTON_POWER,       kKeyCodePower },
        { SKIN_KEY_COMMAND_BUTTON_SEARCH,      kKeyCodeSearch },
        { SKIN_KEY_COMMAND_BUTTON_MENU,        kKeyCodeMenu },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_UP,     kKeyCodeDpadUp },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_LEFT,   kKeyCodeDpadLeft },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_RIGHT,  kKeyCodeDpadRight },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_DOWN,   kKeyCodeDpadDown },
        { SKIN_KEY_COMMAND_BUTTON_DPAD_CENTER, kKeyCodeDpadCenter },
        { SKIN_KEY_COMMAND_BUTTON_VOLUME_UP,   kKeyCodeVolumeUp },
        { SKIN_KEY_COMMAND_BUTTON_VOLUME_DOWN, kKeyCodeVolumeDown },
        { SKIN_KEY_COMMAND_BUTTON_CAMERA,      kKeyCodeCamera },
        { SKIN_KEY_COMMAND_BUTTON_TV,          kKeyCodeTV },
        { SKIN_KEY_COMMAND_BUTTON_EPG,         kKeyCodeEPG },
        { SKIN_KEY_COMMAND_BUTTON_DVR,         kKeyCodeDVR },
        { SKIN_KEY_COMMAND_BUTTON_PREV,        kKeyCodePrevious },
        { SKIN_KEY_COMMAND_BUTTON_NEXT,        kKeyCodeNext },
        { SKIN_KEY_COMMAND_BUTTON_PLAY,        kKeyCodePlay },
        { SKIN_KEY_COMMAND_BUTTON_PLAYPAUSE,   kKeyCodePlaypause },
        { SKIN_KEY_COMMAND_BUTTON_PAUSE,       kKeyCodePause },
        { SKIN_KEY_COMMAND_BUTTON_STOP,        kKeyCodeStop },
        { SKIN_KEY_COMMAND_BUTTON_REWIND,      kKeyCodeRewind },
        { SKIN_KEY_COMMAND_BUTTON_FFWD,        kKeyCodeFastForward },
        { SKIN_KEY_COMMAND_BUTTON_BOOKMARKS,   kKeyCodeBookmarks },
        { SKIN_KEY_COMMAND_BUTTON_WINDOW,      kKeyCodeCycleWindows },
        { SKIN_KEY_COMMAND_BUTTON_CHANNELUP,   kKeyCodeChannelUp },
        { SKIN_KEY_COMMAND_BUTTON_CHANNELDOWN, kKeyCodeChannelDown },
        { SKIN_KEY_COMMAND_BUTTON_APPSWITCH,   kKeyCodeAppSwitch },
        { SKIN_KEY_COMMAND_NONE, 0 }
    };

    int nn;
    for (nn = 0; keycodes[nn].kcode != 0; nn++) {
        if (command == keycodes[nn].cmd) {
            unsigned  code = keycodes[nn].kcode;
            ui->ui_funcs->keyboard_event(NULL, code, down);
            return;
        }
    }

    // for the show-trackball command, handle down events to enable, and
    // up events to disable
    if (command == SKIN_KEY_COMMAND_SHOW_TRACKBALL) {
        ui->show_trackball = (down != 0);
        skin_window_show_trackball(ui->window, ui->show_trackball);
        return;
    }

    // only handle down events for the rest
    if (down == 0)
        return;

    switch (command)
    {
    case SKIN_KEY_COMMAND_TOGGLE_NETWORK:
        {
            bool enabled = ui->ui_funcs->network_toggle();
            D( "network is now %s", enabled ? "connected" : "disconnected");
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_FULLSCREEN:
        if (ui->window) {
            skin_window_toggle_fullscreen(ui->window);
        }
        break;

    case SKIN_KEY_COMMAND_TOGGLE_TRACKBALL:
        if (ui->ui_params.enable_trackball) {
            ui->show_trackball = !ui->show_trackball;
            skin_window_show_trackball(ui->window, ui->show_trackball);
            skin_ui_reset_title(ui);
        }
        break;

    case SKIN_KEY_COMMAND_ONION_ALPHA_UP:
    case SKIN_KEY_COMMAND_ONION_ALPHA_DOWN:
        if (ui->onion) {
            int alpha = ui->onion_alpha;

            if (command == SKIN_KEY_COMMAND_ONION_ALPHA_UP)
                alpha += 16;
            else
                alpha -= 16;

            if (alpha > 256)
                alpha = 256;
            else if (alpha < 0)
                alpha = 0;

            ui->onion_alpha = alpha;

            skin_window_set_onion(ui->window,
                                  ui->onion,
                                  ui->onion_rotation,
                                  ui->onion_alpha);
            skin_window_redraw(ui->window, NULL);
            //dprint( "onion alpha set to %d (%.f %%)", alpha, alpha/2.56 );
        }
        break;

    case SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV:
    case SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT:
        {
            SkinLayout* layout = NULL;

            if (command == SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT) {
                layout = ui->layout->next;
                if (layout == NULL)
                    layout = ui->layout_file->layouts;
            }
            else if (command == SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV) {
                layout = ui->layout_file->layouts;
                while (layout->next && layout->next != ui->layout)
                    layout = layout->next;
            }
            if (layout != NULL) {
                ui->layout = layout;
                if (VERBOSE_CHECK(rotation)) {
                    fprintf(stderr, "Applying new layout\n");
                }
                skin_window_reset(ui->window, layout);
                SkinRotation rotation = skin_layout_get_dpad_rotation(layout);

                if (ui->keyboard)
                    skin_keyboard_set_rotation(ui->keyboard, rotation);

                if (ui->trackball) {
                    skin_trackball_set_rotation(ui->trackball, rotation);
                    skin_window_set_trackball(ui->window, ui->trackball);
                    skin_window_show_trackball(ui->window, ui->show_trackball);
                }

                skin_window_set_lcd_brightness(ui->window, ui->lcd_brightness);

                ui->ui_funcs->framebuffer_invalidate();
            }
        }
        break;

    default:
        /* XXX: TODO ? */
        ;
    }
}

bool skin_ui_process_events(SkinUI* ui) {
    SkinEvent ev;

    // If a scrolled window is zoomed or resized while the scroll bars
    // are moved, Qt window scroll events are created as the window resizes.
    // They will be in the event queue behind the set-scale or set-zoom. Because
    // scroll events work by "moving" the GL sub-window when using host GPU and
    // finding its intersection with the Qt window, scroll events produced by a
    // resize should be ignored, since they may move the GL sub-window far enough
    // that it no longer intersects the Qt window at its current size.
    bool ignoreScroll = false;

    while(skin_event_poll(&ev)) {
        switch(ev.type) {
        case kEventForceRedraw:
            DE("EVENT: kEventVideoExpose\n");
            skin_window_redraw(ui->window, NULL);
            break;

        case kEventKeyDown:
            DE("EVENT: kEventKeyDown scancode=%d mod=0x%x\n",
               ev.u.key.keycode, ev.u.key.mod);
            skin_keyboard_process_event(ui->keyboard, &ev, 1);
            break;

        case kEventKeyUp:
            DE("EVENT: kEventKeyUp scancode=%d mod=0x%x\n",
               ev.u.key.keycode, ev.u.key.mod);
            skin_keyboard_process_event(ui->keyboard, &ev, 0);
            break;

        case kEventTextInput:
            DE("EVENT: kEventTextInput text=[%s] down=%s\n",
               ev.u.text.text, ev.u.text.down ? "true" : "false");
            skin_keyboard_process_event(ui->keyboard, &ev, ev.u.text.down);
            break;

        case kEventMouseMotion:
            DE("EVENT: kEventMouseMotion x=%d y=%d xrel=%d yrel=%d button=%d\n",
               ev.u.mouse.x, ev.u.mouse.y, ev.u.mouse.xrel, ev.u.mouse.yrel,
               ev.u.mouse.button);
            skin_window_process_event(ui->window, &ev);
            break;
        case kEventLayoutNext:
            DE("EVENT: kEventLayoutNext\n");
            if (VERBOSE_CHECK(rotation)) {
                fprintf(stderr, "Polled event: LayoutNext\n");
            }
            _skin_ui_handle_key_command(ui, SKIN_KEY_COMMAND_CHANGE_LAYOUT_NEXT, 1);
            break;
        case kEventLayoutPrev:
            DE("EVENT: kEventLayoutPrev\n");
            if (VERBOSE_CHECK(rotation)) {
                fprintf(stderr, "Polled event: LayoutPrev\n");
            }
            _skin_ui_handle_key_command(ui, SKIN_KEY_COMMAND_CHANGE_LAYOUT_PREV, 1);
            break;
        case kEventMouseButtonDown:
        case kEventMouseButtonUp:
            DE("EVENT: kEventMouseButton x=%d y=%d xrel=%d yrel=%d button=%d\n",
               ev.u.mouse.x, ev.u.mouse.y, ev.u.mouse.xrel, ev.u.mouse.yrel,
               ev.u.mouse.button);
            if (ev.u.mouse.button == kMouseButtonLeft ||
                ev.u.mouse.button == kMouseButtonSecondaryTouch ||
                ev.u.mouse.button == kMouseButtonRight ||
                ev.u.mouse.button == kMouseButtonWheelUp ||
                ev.u.mouse.button == kMouseButtonWheelDown ) {
                skin_window_process_event(ui->window, &ev);
            }
            break;

        case kEventScrollBarChanged:
            DE("EVENT: kEventScrollBarChanged x=%d xmax=%d y=%d ymax=%d ignored=%d\n",
               ev.u.scroll.x, ev.u.scroll.xmax, ev.u.scroll.y, ev.u.scroll.ymax, ignoreScroll);
            if (!ignoreScroll) {
                skin_window_scroll_updated(ui->window, ev.u.scroll.x, ev.u.scroll.xmax,
                                                       ev.u.scroll.y, ev.u.scroll.ymax);
            }
            break;

        case kEventSetScale:
            DE("EVENT: kEventSetScale scale=%f\n", ev.u.window.scale);
            ignoreScroll = true;
            skin_window_set_scale(ui->window, ev.u.window.scale);
            break;

        case kEventSetZoom:
            DE("EVENT: kEventSetZoom x=%d y=%d zoom=%f scroll_h=%d\n",
               ev.u.window.x, ev.u.window.y, ev.u.window.scale, ev.u.window.scroll_h);
            skin_window_set_zoom(ui->window, ev.u.window.scale, ev.u.window.x, ev.u.window.y,
                                             ev.u.window.scroll_h);
            break;

        case kEventQuit:
            DE("EVENT: kEventQuit\n");
            /* only save emulator config through clean exit */
            return true;

        case kEventWindowMoved:
            DE("EVENT: kEventWindowMoved x=%d y=%d\n", ev.u.window.x, ev.u.window.y);
            skin_window_position_changed(ui->window, ev.u.window.x, ev.u.window.y);
            break;

        case kEventScreenChanged:
            DE("EVENT: kEventScreenChanged\n");
            skin_window_process_event(ui->window, &ev);
            break;

        case kEventZoomedWindowResized:
            DE("EVENT: kEventZoomedWindowResized dx=%d dy=%d w=%d h=%d\n",
               ev.u.scroll.x, ev.u.scroll.y, ev.u.scroll.xmax, ev.u.scroll.ymax);
            skin_window_zoomed_window_resized(ui->window, ev.u.scroll.x, ev.u.scroll.y,
                                                          ev.u.scroll.xmax, ev.u.scroll.ymax,
                                                          ev.u.scroll.scroll_h);
            break;
        }
    }

    skin_keyboard_flush(ui->keyboard);
    return false;
}

void skin_ui_update_display(SkinUI* ui, int x, int y, int w, int h) {
    if (ui->window) {
        skin_window_update_display(ui->window, x, y, w, h);
    }
}

void skin_ui_update_gpu_frame(SkinUI* ui, int w, int h, const void* pixels) {
    if (ui->window) {
        skin_window_update_gpu_frame(ui->window, w, h, pixels);
    }
}

SkinLayout* skin_ui_get_current_layout(SkinUI* ui) {
    return ui->layout;
}

SkinKeyset* skin_ui_get_current_keyset(SkinUI* ui) {
    return skin_keyboard_get_keyset(ui->keyboard);
}

void skin_ui_set_name(SkinUI* ui, const char* name) {
    snprintf(ui->ui_params.window_name,
             sizeof(ui->ui_params.window_name),
             "%s",
             name);
    skin_ui_reset_title(ui);
}

bool skin_ui_is_trackball_active(SkinUI* ui) {
    return (ui && ui->ui_params.enable_trackball && ui->show_trackball);
}

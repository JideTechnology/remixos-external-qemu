/* Copyright (C) 2007-2008 The Android Open Source Project
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
#include "android/skin/keyboard.h"

#include "android/skin/charmap.h"
#include "android/skin/keycode.h"
#include "android/skin/keycode-buffer.h"
#include "android/utils/debug.h"
#include "android/utils/bufprint.h"
#include "android/utils/system.h"
#include "android/utils/utf8_utils.h"

#include <stdio.h>

#define  DEBUG  1

#if DEBUG
#  define  D(...)  VERBOSE_PRINT(keys,__VA_ARGS__)
#else
#  define  D(...)  ((void)0)
#endif

struct SkinKeyboard {
    const SkinCharmap*  charmap;
    SkinKeyset*         kset;
    char                enabled;
    char                raw_keys;

    SkinRotation        rotation;

    SkinKeyCommandFunc  command_func;
    void*               command_opaque;
    SkinKeyEventFunc    press_func;
    void*               press_opaque;

    SkinKeycodeBuffer   keycodes[1];
};

#define DEFAULT_ANDROID_CHARMAP  "qwerty2"

static bool skin_key_code_is_arrow(int code) {
    return code == kKeyCodeDpadLeft ||
           code == kKeyCodeDpadRight ||
           code == kKeyCodeDpadUp ||
           code == kKeyCodeDpadDown;
}

static void
skin_keyboard_cmd( SkinKeyboard*   keyboard,
                   SkinKeyCommand  command,
                   int             param )
{
    if (keyboard->command_func) {
        keyboard->command_func( keyboard->command_opaque, command, param );
    }
}

static SkinKeyCode
skin_keyboard_key_to_code(SkinKeyboard*  keyboard,
                          int            code,
                          int            mod,
                          int            down)
{
    SkinKeyCommand  command;

    D("key code=%d mod=%d str=%s",
      code,
      mod,
      skin_key_pair_to_string(code, mod));

    /* first, handle the arrow keys directly */
    if (skin_key_code_is_arrow(code)) {
        code = skin_keycode_rotate(code, -keyboard->rotation);
        D("handling arrow (code=%d mod=%d)", code, mod);
        if (!keyboard->raw_keys) {
            int  doCapL, doCapR, doAltL, doAltR;

            doCapL = mod & kKeyModLShift;
            doCapR = mod & kKeyModRShift;
            doAltL = mod & kKeyModLAlt;
            doAltR = mod & kKeyModRAlt;

            if (down) {
                if (doAltL) skin_keyboard_add_key_event(keyboard, kKeyCodeAltLeft, 1);
                if (doAltR) skin_keyboard_add_key_event(keyboard, kKeyCodeAltRight, 1);
                if (doCapL) skin_keyboard_add_key_event(keyboard, kKeyCodeCapLeft, 1);
                if (doCapR) skin_keyboard_add_key_event(keyboard, kKeyCodeCapRight, 1);
            }
            skin_keyboard_add_key_event(keyboard, code, down);

            if (!down) {
                if (doCapR) skin_keyboard_add_key_event(keyboard, kKeyCodeCapRight, 0);
                if (doCapL) skin_keyboard_add_key_event(keyboard, kKeyCodeCapLeft, 0);
                if (doAltR) skin_keyboard_add_key_event(keyboard, kKeyCodeAltRight, 0);
                if (doAltL) skin_keyboard_add_key_event(keyboard, kKeyCodeAltLeft, 0);
            }
            code = 0;
        }
        return code;
    }

    /* special case for keypad keys, ignore them here if numlock is on */
    if ((mod & kKeyModNumLock) != 0) {
        switch ((int)code) {
            case KEY_KP0:
            case KEY_KP1:
            case KEY_KP2:
            case KEY_KP3:
            case KEY_KP4:
            case KEY_KP5:
            case KEY_KP6:
            case KEY_KP7:
            case KEY_KP8:
            case KEY_KP9:
            case KEY_KPPLUS:
            case KEY_KPMINUS:
            case KEY_KPASTERISK:
            case KEY_KPSLASH:
            case KEY_KPEQUAL:
            case KEY_KPDOT:
            case KEY_KPENTER:
                return 0;
            default:
                ;
        }
    }

    /* now try all keyset combos */
    command = skin_keyset_get_command(keyboard->kset, code, mod);
    if (command != SKIN_KEY_COMMAND_NONE) {
        D("handling command %s from (sym=%d, mod=%d, str=%s)",
          skin_key_command_to_str(command),
          code,
          mod,
          skin_key_pair_to_string(code, mod));
        skin_keyboard_cmd(keyboard, command, down);
        return 0;
    }
    D("could not handle (code=%d, mod=%d, str=%s)", code, mod,
      skin_key_pair_to_string(code,mod));
    return -1;
}

/* this gets called only if the reverse unicode mapping didn't work
 * or wasn't used (when in raw keys mode)
 */

void
skin_keyboard_enable( SkinKeyboard*  keyboard,
                      int            enabled )
{
    keyboard->enabled = enabled;
    if (enabled) {
        skin_event_enable_unicode(!keyboard->raw_keys);
    }
}

static void
skin_keyboard_do_key_event( SkinKeyboard*   kb,
                            SkinKeyCode  code,
                            int             down )
{
    if (kb->press_func) {
        kb->press_func( kb->press_opaque, code, down );
    }
    skin_keyboard_add_key_event(kb, code, down);
}

void
skin_keyboard_process_event(SkinKeyboard*  kb, SkinEvent* ev, int  down)
{
    /* ignore key events if we're not enabled */
    if (!kb->enabled) {
        //printf( "ignoring key event\n");
        return;
    }

    if (ev->type == kEventTextInput) {
        if (!kb->raw_keys) {
            // TODO(digit): For each Unicode value in the input text.
            const uint8_t* text = ev->u.text.text;
            const uint8_t* end = text + sizeof(ev->u.text.text);
            while (text < end && *text) {
                uint32_t codepoint = 0;
                int len = android_utf8_decode(text, end - text, &codepoint);
                if (len < 0) {
                    break;
                }
                skin_keyboard_process_unicode_event(kb, codepoint, 1);
                skin_keyboard_process_unicode_event(kb, codepoint, 0);
                text += len;
            }
            skin_keyboard_flush(kb);
        }
    } else if (ev->type == kEventKeyDown || ev->type == kEventKeyUp) {
        int keycode = ev->u.key.keycode;
        int mod = ev->u.key.mod;
        // region @jide send key event directly to android
        if (keycode > 0) {
            skin_keyboard_do_key_event(kb, keycode, down);
            skin_keyboard_flush(kb);
            return;
        }
        // endregion
        /* first, try the keyboard-mode-independent keys */
        int code = skin_keyboard_key_to_code(kb, keycode, mod, down);
        if (code == 0) {
            return;
        }
        if ((int)code > 0) {
            skin_keyboard_do_key_event(kb, code, down);
            skin_keyboard_flush(kb);
            return;
        }

        /* Ctrl-K is used to switch between 'unicode' and 'raw' modes */
        if (keycode == kKeyCodeK) {
            if (mod == kKeyModLCtrl || mod == kKeyModRCtrl) {
                if (down) {
                    kb->raw_keys = !kb->raw_keys;
                    skin_event_enable_unicode(!kb->raw_keys);
                    D( "switching keyboard to %s mode", kb->raw_keys ? "raw" : "unicode" );
                }
                return;
            }
        }

        code = keycode;

        if ( !kb->raw_keys &&
            (code == kKeyCodeAltLeft  || code == kKeyCodeAltRight ||
             code == kKeyCodeCapLeft  || code == kKeyCodeCapRight ||
             code == kKeyCodeSym) ) {
            return;
        }

        if (code == KEY_APPSWITCH   || code == KEY_PLAY       ||
            code == KEY_BACK        || code == KEY_POWER      ||
            code == KEY_BACKSPACE   || code == KEY_SOFT1      ||
            code == KEY_CENTER      || code == KEY_REWIND     ||
            code == KEY_ENTER       || code == KEY_VOLUMEDOWN ||
            code == KEY_FASTFORWARD || code == KEY_VOLUMEUP   ||
            code == KEY_HOME                                     )
        {
            skin_keyboard_do_key_event(kb, code, down);
            skin_keyboard_flush(kb);
            return;
        }
        D("ignoring keycode %d", keycode);
    }
}

void
skin_keyboard_set_keyset( SkinKeyboard*  keyboard, SkinKeyset*  kset )
{
    if (kset == NULL)
        return;
    if (keyboard->kset && keyboard->kset != skin_keyset_get_default()) {
        skin_keyset_free(keyboard->kset);
    }
    keyboard->kset = kset;
}


SkinKeyset*
skin_keyboard_get_keyset( SkinKeyboard* keyboard ) {
    return keyboard->kset;
}

void
skin_keyboard_set_rotation( SkinKeyboard*     keyboard,
                            SkinRotation      rotation )
{
    keyboard->rotation = (rotation & 3);
}

void
skin_keyboard_on_command( SkinKeyboard*  keyboard, SkinKeyCommandFunc  cmd_func, void*  cmd_opaque )
{
    keyboard->command_func   = cmd_func;
    keyboard->command_opaque = cmd_opaque;
}

void
skin_keyboard_on_key_press( SkinKeyboard*  keyboard, SkinKeyEventFunc  press_func, void*  press_opaque )
{
    keyboard->press_func   = press_func;
    keyboard->press_opaque = press_opaque;
}

void
skin_keyboard_add_key_event( SkinKeyboard*  kb,
                             unsigned       code,
                             unsigned       down )
{
    skin_keycode_buffer_add(kb->keycodes, code, down);
}


void
skin_keyboard_flush( SkinKeyboard*  kb )
{
    skin_keycode_buffer_flush(kb->keycodes);
}


int
skin_keyboard_process_unicode_event( SkinKeyboard*  kb,  unsigned int  unicode, int  down )
{
    return skin_charmap_reverse_map_unicode(kb->charmap, unicode, down,
                                            kb->keycodes);
}


static SkinKeyboard*
skin_keyboard_create_from_charmap_name(const char* charmap_name,
                                       int  use_raw_keys,
                                       SkinKeyCodeFlushFunc keycode_flush)
{
    SkinKeyboard*  kb;

    ANEW0(kb);

    kb->charmap = skin_charmap_get_by_name(charmap_name);
    if (!kb->charmap) {
        // Charmap name was not found. Default to "qwerty2" */
        kb->charmap = skin_charmap_get_by_name(DEFAULT_ANDROID_CHARMAP);
        fprintf(stderr, "### warning, skin requires unknown '%s' charmap, reverting to '%s'\n",
                charmap_name, kb->charmap->name );
    }
    kb->raw_keys = use_raw_keys;
    kb->enabled  = 0;

    /* add default keyset */
    if (skin_keyset_get_default()) {
        kb->kset = skin_keyset_get_default();
    } else {
        kb->kset = skin_keyset_new_from_text(
                skin_keyset_get_default_text());
    }
    skin_keycode_buffer_init(kb->keycodes, keycode_flush);
    return kb;
}

SkinKeyboard*
skin_keyboard_create(const char* kcm_file_path,
                     int use_raw_keys,
                     SkinKeyCodeFlushFunc keycode_flush)
{
    const char* charmap_name = DEFAULT_ANDROID_CHARMAP;
    char        cmap_buff[SKIN_CHARMAP_NAME_SIZE];

    if (kcm_file_path != NULL) {
        kcm_extract_charmap_name(kcm_file_path, cmap_buff, sizeof cmap_buff);
        charmap_name = cmap_buff;
    }
    return skin_keyboard_create_from_charmap_name(
            charmap_name, use_raw_keys, keycode_flush);
}

void
skin_keyboard_free( SkinKeyboard*  keyboard )
{
    if (keyboard) {
        AFREE(keyboard);
    }
}

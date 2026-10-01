#include "input_pad.h"
#include "../log.h"

#include <Limelight.h>

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <string.h>
#include <stdatomic.h>
#include <unistd.h>

static int32_t s_pad = -1;
static atomic_bool s_quit;
static uint64_t s_combo_start_us;
static int s_announced;

/* Menu-mode state (edges + D-pad auto-repeat). */
static unsigned s_menu_prev;
static uint64_t s_menu_rpt_start_us;
static uint64_t s_menu_rpt_last_us;

/* Touchpad-as-mouse POC state. */
#define TOUCH_MOUSE_DIVISOR 2

static int s_mouse_have_touch;
static uint8_t s_mouse_finger;
static uint16_t s_mouse_x;
static uint16_t s_mouse_y;
static int s_mouse_accum_x;
static int s_mouse_accum_y;
static int s_mouse_left_down;

static uint64_t now_us(void) {
    // sceKernelGetProcessTime is in us per scene docs.
    extern uint64_t sceKernelGetProcessTime(void);
    return sceKernelGetProcessTime();
}

// PS4 stick: 0..255 with center ~128. Moonlight expects -32768..32767.
static short stick_to_short(uint8_t v) {
    int centered = ((int)v - 128) * 256;
    if (centered < -32768) centered = -32768;
    if (centered > 32767) centered = 32767;
    return (short)centered;
}

static short stick_to_short_inverted(uint8_t v) {
    int inverted = -(int)stick_to_short(v);
    if (inverted > 32767) inverted = 32767;
    return (short)inverted;
}

int input_init(void) {
    atomic_store(&s_quit, false);
    s_combo_start_us = 0;
    s_announced = 0;

    int rc = scePadInit();
    if (rc < 0) {
        LOGE("input: scePadInit failed: 0x%08x", rc);
        return -1;
    }

    int32_t userId = 0;
    sceUserServiceGetInitialUser(&userId);
    s_pad = scePadOpen(userId, ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
    if (s_pad < 0) {
        LOGE("input: scePadOpen failed: 0x%08x", s_pad);
        return -1;
    }

    OrbisPadInformation info;
    memset(&info, 0, sizeof(info));
    int info_rc = scePadGetControllerInformation(s_pad, &info);
    if (info_rc == 0) {
        LOGI("input: controller touch resolution=%ux%u density=%.3f connected=%d",
             (unsigned)info.touchResolutionX,
             (unsigned)info.touchResolutionY,
             info.touchpadDensity,
             info.connected);
    } else {
        LOGW("input: scePadGetControllerInformation failed: 0x%08x", info_rc);
    }

    LOGI("input: pad open (handle=%d, user=%d)", s_pad, userId);
    return 0;
}

void input_shutdown(void) {
    if (s_pad >= 0) {
        scePadClose(s_pad);
        s_pad = -1;
    }
}

void input_request_quit(void) {
    atomic_store(&s_quit, true);
}

void input_reset(void) {
    atomic_store(&s_quit, false);
    s_combo_start_us = 0;
    s_announced = 0;
    s_menu_prev = 0;
    s_menu_rpt_start_us = 0;
    s_menu_rpt_last_us = 0;

    s_mouse_have_touch = 0;
    s_mouse_finger = 0;
    s_mouse_x = 0;
    s_mouse_y = 0;
    s_mouse_accum_x = 0;
    s_mouse_accum_y = 0;
    s_mouse_left_down = 0;
}

static unsigned pad_to_menu_mask(const OrbisPadData *pad) {
    unsigned h = 0;
    if (pad->buttons & ORBIS_PAD_BUTTON_UP)        h |= MENU_BTN_UP;
    if (pad->buttons & ORBIS_PAD_BUTTON_DOWN)      h |= MENU_BTN_DOWN;
    if (pad->buttons & ORBIS_PAD_BUTTON_LEFT)      h |= MENU_BTN_LEFT;
    if (pad->buttons & ORBIS_PAD_BUTTON_RIGHT)     h |= MENU_BTN_RIGHT;
    if (pad->buttons & ORBIS_PAD_BUTTON_CROSS)     h |= MENU_BTN_CROSS;
    if (pad->buttons & ORBIS_PAD_BUTTON_CIRCLE)    h |= MENU_BTN_CIRCLE;
    if (pad->buttons & ORBIS_PAD_BUTTON_SQUARE)    h |= MENU_BTN_SQUARE;
    if (pad->buttons & ORBIS_PAD_BUTTON_TRIANGLE)  h |= MENU_BTN_TRIANGLE;
    if (pad->buttons & ORBIS_PAD_BUTTON_L1)        h |= MENU_BTN_L1;
    if (pad->buttons & ORBIS_PAD_BUTTON_R1)        h |= MENU_BTN_R1;
    if (pad->buttons & ORBIS_PAD_BUTTON_OPTIONS)   h |= MENU_BTN_OPTIONS;
    return h;
}

void input_menu_absorb(void) {
    if (s_pad < 0)
        return;
    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0)
        return;
    s_menu_prev = pad_to_menu_mask(&pad);
    /* Touchpad is not in menu mask but OPTIONS is: already covered. */
    s_menu_rpt_start_us = 0;
    s_menu_rpt_last_us = 0;
}

void input_menu_wait_release(unsigned mask, int timeout_ms) {
    if (s_pad < 0 || !mask)
        return;
    for (int i = 0; i < timeout_ms; i++) {
        OrbisPadData pad;
        memset(&pad, 0, sizeof(pad));
        if (scePadReadState(s_pad, &pad) == 0) {
            unsigned h = pad_to_menu_mask(&pad);
            if (pad.buttons & ORBIS_PAD_BUTTON_TOUCH_PAD)
                h |= MENU_BTN_OPTIONS; /* treat touchpad as "held" */
            if ((h & mask) == 0) {
                s_menu_prev = h;
                return;
            }
            s_menu_prev = h; /* keep absorbing while held */
        }
        sceKernelUsleep(1000);
    }
    input_menu_absorb();
}

int input_menu_poll(unsigned *pressed, unsigned *held) {
    *pressed = 0;
    *held = 0;
    if (s_pad < 0)
        return -1;

    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0)
        return -1;

    unsigned h = pad_to_menu_mask(&pad);
    unsigned edges = h & ~s_menu_prev;

    /* Auto-repeat D-pad only: 350 ms delay, then every 90 ms. */
    const unsigned dpad = MENU_BTN_UP | MENU_BTN_DOWN | MENU_BTN_LEFT | MENU_BTN_RIGHT;
    uint64_t now = now_us();
    if (h & dpad) {
        if (edges & dpad) {
            s_menu_rpt_start_us = now;
            s_menu_rpt_last_us = now;
        } else if (now - s_menu_rpt_start_us > 350000ull &&
                   now - s_menu_rpt_last_us > 90000ull) {
            edges |= h & dpad;
            s_menu_rpt_last_us = now;
        }
    } else {
        s_menu_rpt_start_us = 0;
    }

    s_menu_prev = h;
    *pressed = edges;
    *held = h;
    return 0;
}

bool input_should_quit(void) {
    return atomic_load(&s_quit);
}

static short mouse_delta_to_short(int v) {
    if (v < -32768)
        return -32768;
    if (v > 32767)
        return 32767;
    return (short)v;
}

static void input_touchpad_mouse(const OrbisPadData *pad) {
    /*
     * OPTIONS+TOUCHPAD is reserved for the local quit combo.
     * Never emit a mouse click while OPTIONS is held.
     */
    int want_left =
        (pad->buttons & ORBIS_PAD_BUTTON_TOUCH_PAD) &&
        !(pad->buttons & ORBIS_PAD_BUTTON_OPTIONS);

    if (want_left != s_mouse_left_down) {
        int rc = LiSendMouseButtonEvent(
            want_left ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE,
            BUTTON_LEFT);

        if (rc != 0)
            LOGW("input: mouse left event failed rc=%d", rc);

        s_mouse_left_down = want_left;
    }

    /*
     * Exactly one finger controls the mouse.
     * Zero or two fingers reset the relative-motion baseline.
     */
    if (pad->touch.fingers != 1) {
        s_mouse_have_touch = 0;
        s_mouse_accum_x = 0;
        s_mouse_accum_y = 0;
        return;
    }

    const OrbisPadTouch *touch = &pad->touch.touch[0];

    /*
     * First sample after finger-down (or a changed finger ID) only
     * establishes the origin. This prevents cursor jumps.
     */
    if (!s_mouse_have_touch || touch->finger != s_mouse_finger) {
        s_mouse_have_touch = 1;
        s_mouse_finger = touch->finger;
        s_mouse_x = touch->x;
        s_mouse_y = touch->y;
        s_mouse_accum_x = 0;
        s_mouse_accum_y = 0;
        return;
    }

    int dx = (int)touch->x - (int)s_mouse_x;
    int dy = (int)touch->y - (int)s_mouse_y;

    s_mouse_x = touch->x;
    s_mouse_y = touch->y;

    /*
     * Preserve fractional movement across polls instead of simply
     * throwing away small movements when sensitivity is divided.
     */
    s_mouse_accum_x += dx;
    s_mouse_accum_y += dy;

    int out_x = s_mouse_accum_x / TOUCH_MOUSE_DIVISOR;
    int out_y = s_mouse_accum_y / TOUCH_MOUSE_DIVISOR;

    s_mouse_accum_x -= out_x * TOUCH_MOUSE_DIVISOR;
    s_mouse_accum_y -= out_y * TOUCH_MOUSE_DIVISOR;

    if (out_x || out_y) {
        int rc = LiSendMouseMoveEvent(
            mouse_delta_to_short(out_x),
            mouse_delta_to_short(out_y));

        if (rc != 0)
            LOGW("input: mouse move failed rc=%d", rc);
    }
}

bool input_poll(void) {
    if (s_pad < 0)
        return input_should_quit();

    OrbisPadData pad;
    memset(&pad, 0, sizeof(pad));
    if (scePadReadState(s_pad, &pad) < 0)
        return input_should_quit();

    if (!s_announced) {
        // DualShock 4: PS type, rumble + touchpad + gyro.
        LiSendControllerArrivalEvent(
            0, 0x1, LI_CTYPE_PS,
            0xFFFFFFFF, // generic supported buttons
            LI_CCAP_RUMBLE | LI_CCAP_GYRO | LI_CCAP_TOUCHPAD | LI_CCAP_ANALOG_TRIGGERS);
        LiSendControllerBatteryEvent(0, LI_BATTERY_STATE_FULL, 100);
        s_announced = 1;
    }

    int buttons = 0;
    if (pad.buttons & ORBIS_PAD_BUTTON_UP)        buttons |= UP_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_DOWN)      buttons |= DOWN_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_LEFT)      buttons |= LEFT_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_RIGHT)     buttons |= RIGHT_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_CROSS)     buttons |= A_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_CIRCLE)    buttons |= B_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_SQUARE)    buttons |= X_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_TRIANGLE)  buttons |= Y_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_L1)        buttons |= LB_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_R1)        buttons |= RB_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_L3)        buttons |= LS_CLK_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_R3)        buttons |= RS_CLK_FLAG;
    if (pad.buttons & ORBIS_PAD_BUTTON_OPTIONS)   buttons |= PLAY_FLAG;

    /*
     * Touchpad click is reserved for mouse-left in this feature branch.
     * Do not also send TOUCHPAD_FLAG or the host would receive two actions.
     */

    // Y axis inverted: PS4 grows downward, Moonlight upward.
    short lx = stick_to_short(pad.leftStick.x);
    short ly = stick_to_short_inverted(pad.leftStick.y);
    short rx = stick_to_short(pad.rightStick.x);
    short ry = stick_to_short_inverted(pad.rightStick.y);

    LiSendMultiControllerEvent(0, 0x1, buttons,
                               pad.analogButtons.l2, pad.analogButtons.r2,
                               lx, ly, rx, ry);

    input_touchpad_mouse(&pad);

    // Quit combo: OPTIONS + TOUCHPAD for ~1 s.
    const int combo = ORBIS_PAD_BUTTON_OPTIONS | ORBIS_PAD_BUTTON_TOUCH_PAD;
    if ((pad.buttons & combo) == combo) {
        uint64_t now = now_us();
        if (!s_combo_start_us)
            s_combo_start_us = now;
        else if (now - s_combo_start_us > 1000000ull) {
            LOGI("input: quit combo detected");
            input_request_quit();
        }
    } else {
        s_combo_start_us = 0;
    }

    return input_should_quit();
}

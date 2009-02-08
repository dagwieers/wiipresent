/*
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Library General Public License as published by
the Free Software Foundation; version 2 only

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Library General Public License for more details.

You should have received a copy of the GNU Library General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
Copyright 2009 Dag Wieers <dag@wieers.com>
*/

// $Id$

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include "wiimote_api.h"

static char *displayname = NULL;
static Display *display = NULL;
static Window window = 0;
wiimote_t wmote;

static void FakeKeycode(int keycode, int modifiers){
    if ( modifiers & ControlMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), True, 0);

    if ( modifiers & Mod1Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), True, 0);

    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keycode), True, 0);

    XSync(display, False);

    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keycode), False, 0);

    if ( modifiers & ControlMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), False, 0);

    if ( modifiers & Mod1Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), False, 0);

}

void exit_clean(int sig) {
    wiimote_disconnect(&wmote);
    printf("Exiting on signal %d.\n", sig);
    exit(0);
}

void rumble(wiimote_t *wmote, int secs) {
    wiimote_update(wmote);
    wmote->rumble = 1;
    wiimote_update(wmote);
    usleep(secs * 1000);
    wmote->rumble = 0;
}

int main() {
    int debug = False;
    int length = 1 * 60;
    char btaddress[] = "00:1B:7A:F9:D5:70";
    wmote = (wiimote_t) WIIMOTE_INIT;

    setvbuf(stdout, NULL, _IONBF, 0);

    // Wait for 1+2
    printf("Please press 1+2 on the wiimote with address %s...", btaddress);
    wiimote_connect(&wmote, btaddress);
    printf("\nIt's alive, Jim!\n");

    signal(SIGINT, exit_clean);
    signal(SIGHUP, exit_clean);
    signal(SIGQUIT, exit_clean);

    // Obtain the X11 display.
//    if (displayname == NULL)
//        displayname = getenv("DISPLAY");

//    if (displayname == NULL)
//        displayname = ":0.0";

    display = XOpenDisplay(0);
    if (display == NULL) {
        fprintf(stderr, "wiipresent: can't open display `%s'.\n", displayname);
        return -1;
    }

    // Get the root window for the current display.
    int revert;

    rumble(&wmote, 200);

    time_t start = 0, now = 0, duration = 0;
    int phase = 0, oldphase = 0;
    uint16_t keys = 0;

    start = time(NULL);

    while (wiimote_is_open(&wmote)) {
        // Find the window which has the current keyboard focus.
        XGetInputFocus(display, &window, &revert);

        if (wiimote_pending(&wmote) == 0) {
            usleep(10000);
        }

        if (wiimote_update(&wmote) < 0) {
            printf("Lost connection.");
            wiimote_disconnect(&wmote);
            break;
        };

        // Change leds only when phase changes
        now = time(NULL);
        duration = now - start;
        phase = (int) floorf( ( (float) duration * 5.0 / (float) length)) % 5;
        if (phase != oldphase) {
            printf("%ld minutes passed, %ld minutes left. (phase=%d)\n", duration / 60, (length - duration) / 60, phase);
            // Shift the leds
            wmote.led.bits = pow(2, phase) - 1;

            // Rumble slightly longer at the end (exponentially)
            rumble(&wmote, 100 * exp(phase + 1) / 10);

            switch (phase)  {
                case 0:
                    printf("Sorry, times is up !\n");
                    break;
                case 4:
                    printf("Hurry up ! Maybe questions ?\n");
                    break;
            }
            oldphase = phase;
        }

//        printf("%f - %f - %f - %ld - %ld - %ld - %d\n", ((float) duration * 5.0 / (float) length), (float) duration, (float) length, start, now, duration, phase);

        // Filter out recurring key-events for the same key-press
        if (keys == wmote.keys.bits) {
            continue;
        } else {
            keys = wmote.keys.bits;
        }

        if (wmote.keys.home) {
            printf("Exit on user request.\n");
            wiimote_disconnect(&wmote);
        } else if (wmote.keys.a) {
            if (debug) printf("[A] ");
        } else if (wmote.keys.b) {
            if (debug) printf("[B] ");
        } else if (wmote.keys.one) {
            if (debug) printf("[1] ");
//          wmote.rumble = 1;
//            FakeKeycode(XF86XK_ScreenSaver, 0);
              XActivateScreenSaver(display);
        } else if (wmote.keys.two) {
            if (debug) printf("[2] ");
//          wmote.rumble = 0;
        } else if (wmote.keys.plus) {
            if (debug) printf("[+] ");
            FakeKeycode(XK_Right, ControlMask | Mod1Mask);
        } else if (wmote.keys.minus) {
            if (debug) printf("[-] ");
            FakeKeycode(XK_Left, ControlMask | Mod1Mask);
        } else if (wmote.keys.up) {
            if (debug) printf("[up] ");
            FakeKeycode(XK_F9, 0);
        } else if (wmote.keys.down) {
            if (debug) printf("[down] ");
            FakeKeycode(XK_Escape, 0);
        } else if (wmote.keys.left) {
            if (debug) printf("[left] ");
            FakeKeycode(XK_Page_Up, 0);
        } else if (wmote.keys.right) {
            if (debug) printf("[right] ");
            FakeKeycode(XK_Page_Down, 0);
        }
    }
    XCloseDisplay(display);

    return 0;
}

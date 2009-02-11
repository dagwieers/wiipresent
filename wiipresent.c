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

#include <getopt.h>
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

static char NAME[] = "wiipresent";
static char VERSION[] = "0.5";

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

void MovePointer(Display *display, int xpos, int ypos, int relative) {
    if (relative)
        XTestFakeRelativeMotionEvent(display, xpos, ypos, 0);
    else
        XTestFakeMotionEvent(display, -1, xpos, ypos, 0);
}

void ClickMouse(Display *display, int button, int release) {
    XTestFakeButtonEvent(display, button, release, 0);
    XSync(display, False);
}

void exit_clean(int sig) {
    wiimote_disconnect(&wmote);
    printf("Exiting on signal %d.\n", sig);
    exit(0);
}

void rumble(wiimote_t *wmote, int msecs) {
    wiimote_update(wmote);
    wmote->rumble = 1;
    wiimote_update(wmote);
    usleep(msecs * 1000);
    wmote->rumble = 0;
}

int main(int argc, char **argv) {
    int debug = False;
    int length = 50 * 60;
    char *btaddress = NULL;
    wmote = (wiimote_t) WIIMOTE_INIT;

    int c;

    // Make stdout unbuffered
    setvbuf(stdout, NULL, _IONBF, 0);

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"bluetooth", 1, 0, 'b'},
            {"display", 1, 0, 'd'},
            {"help", 0, 0, 'h'},
            {"length", 1, 0, 'l'},
            {"version", 0, 0, 'v'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "b:d:hl:v", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'b':
                btaddress = optarg;
                continue;
            case 'd':
                displayname = optarg;
                continue;
            case 'h':
                printf("Nintendo Wiimote presentation controller\n\
\n\
%s options:\n\
  -b, --bluetooth=btaddress      Wiimote bluetooth address (use hcitool scan)\n\
  -d, --display=name             X display to use\n\
  -l, --length=minutes           presentation length in minutes\n\
\n\
  -h, --help                     display this help and exit\n\
  -v, --version                  output version information and exit\n\
\n\
Report bugs to <dag@wieers.com>.\n", NAME);
                exit(0);
            case 'l':
                length = atoi(optarg) * 60;
                continue;
            case 'v':
                printf("%s %s\n\
Copyright (C) 2009 Dag Wieërs\n\
This is open source software.  You may redistribute copies of it under the terms of\n\
the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n\
There is NO WARRANTY, to the extent permitted by law.\n\
\n\
Written by Dag Wieers <dag@wieers.com>.\n", NAME, VERSION);
                exit(0);
            default:
                printf ("?? getopt returned character code 0%o ??\n", c);
        }

        if (optind < argc) {
            printf ("non-option ARGV-elements: ");
            while (optind < argc)
                printf ("%s ", argv[optind++]);
            printf ("\n");
        }
    }

    // Wait for 1+2
    if (btaddress == NULL) {
//        printf("Please press 1+2 on a wiimote in the viscinity...");
//        wiimote_connect(&wmote, btaddress);
        printf("Sorry, you need to provide a bluetooth address using -b/--bluetooth.\n");
        exit(1);
    } else {
        printf("Please press 1+2 on the wiimote with address %s...", btaddress);
        wiimote_connect(&wmote, btaddress);
    }

    signal(SIGINT, exit_clean);
    signal(SIGHUP, exit_clean);
    signal(SIGQUIT, exit_clean);

    printf("\nIt's alive, Jim!\n");

    printf("Presentation length is %dmin divided in 5 slots of %dmin.\n", length/60, length/60/5);

    // Obtain the X11 display.
    if (displayname == NULL)
        displayname = getenv("DISPLAY");

    if (displayname == NULL)
        displayname = ":0.0";

    display = XOpenDisplay(displayname);
    if (display == NULL) {
        fprintf(stderr, "%s: can't open display `%s'.\n", NAME, displayname);
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
                    printf("Sorry, time is up !\n");
                    break;
                case 4:
                    printf("Hurry up ! Maybe questions ?\n");
                    break;
            }
            oldphase = phase;
        }

        // Check battery
        if (wmote.battery < 5) {
            printf("Bettery low (%d%%), please replace batteries !\n", wmote.battery);
        }

//        printf("%f - %f - %f - %ld - %ld - %ld - %d\n", ((float) duration * 5.0 / (float) length), (float) duration, (float) length, start, now, duration, phase);

        // Inside the mouse functionality
        if (wmote.keys.a) {
            wmote.mode.acc = 1;

            // Tilt method
            MovePointer(display, wmote.tilt.x / 4, wmote.tilt.y / 4, 1);

            // Block repeating keys
            if (keys == wmote.keys.bits) {
                continue;
            }

            // Left mouse button events
            if (wmote.keys.minus) {
                ClickMouse(display, 1, 1);
            } else if (keys & WIIMOTE_KEY_MINUS) {
                ClickMouse(display, 1, 0);
            }

            // Right mouse button events
            if (wmote.keys.plus) {
                ClickMouse(display, 3, 1);
            } else if (keys & WIIMOTE_KEY_PLUS) {
                ClickMouse(display, 3, 0);
            }

        } else {
            wmote.mode.acc = 0;

            // Block repeating keys
            if (keys == wmote.keys.bits) {
                continue;
            }

            // Disconnect the device
            if (wmote.keys.home) {
                printf("Exit on user request.\n");
                wiimote_disconnect(&wmote);
            }

            if (wmote.keys.b) {
                if (debug) printf("[B] ");
            }

            // Blank screen
            if (wmote.keys.one) {
                XActivateScreenSaver(display);
            }

            if (wmote.keys.two) {
                if (debug) printf("[2] ");
            }

            // Goto to previous workspace
            if (wmote.keys.plus) {
                FakeKeycode(XK_Right, ControlMask | Mod1Mask);
            }

            // Goto to next workspace
            if (wmote.keys.minus) {
                FakeKeycode(XK_Left, ControlMask | Mod1Mask);
            }

            // Fullscreen
            if (wmote.keys.up) {
                FakeKeycode(XK_F9, 0);
            }

            if (wmote.keys.down) {
                FakeKeycode(XK_Escape, 0);
            }

            // Next slide
            if (wmote.keys.left) {
                FakeKeycode(XK_Page_Up, 0);
            }

            // Previous slide
            if (wmote.keys.right) {
                FakeKeycode(XK_Page_Down, 0);
            }

            // Save the keys state for next run
            keys = wmote.keys.bits;
        }

    }
    XCloseDisplay(display);

    return 0;
}

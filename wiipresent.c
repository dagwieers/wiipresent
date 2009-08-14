/*
This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 only

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
Copyright 2009 Dag Wieers <dag@wieers.com>
*/

// $Id$

#define _GNU_SOURCE

#include <getopt.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/XF86keysym.h>
#include <X11/keysym.h> // Needed for older X releases

#include "wiimote_api.h"

Status XQueryCommand(Display *display, Window window, char **name);

static char NAME[] = "wiipresent";
static char VERSION[] = "0.7.2svn";

static char *displayname = NULL;
static Display *display = NULL;
static Window window = 0;
wiimote_t wmote;

int verbose = 0;

// Screensaver variables
int timeout_return = 0;
int interval_return = 0;
int prefer_blanking_return = 0;
int allow_exposures_return = 0;

static void XKeyPress(int keycode, int modifiers) {
    if ( modifiers & ControlMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), True, 0);

    if ( modifiers & Mod1Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), True, 0);

    if ( modifiers & Mod2Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_R), True, 0);

    if ( modifiers & ShiftMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L), True, 0);

    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keycode), True, 0);

    XSync(display, False);
}

static void XKeyRelease(int keycode, int modifiers) {
    XTestFakeKeyEvent(display, XKeysymToKeycode(display, keycode), False, 0);

    if ( modifiers & ShiftMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Shift_L), False, 0);

    if ( modifiers & Mod2Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_R), False, 0);

    if ( modifiers & Mod1Mask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Alt_L), False, 0);

    if ( modifiers & ControlMask )
        XTestFakeKeyEvent(display, XKeysymToKeycode(display, XK_Control_L), False, 0);

    XSync(display, False);
}

static void XKeycode(int keycode, int modifiers) {
    XKeyPress(keycode, modifiers);
    XKeyRelease(keycode, modifiers);
}

void XMovePointer(Display *display, int xpos, int ypos, int relative) {
    if (relative)
        XTestFakeRelativeMotionEvent(display, xpos, ypos, 0);
    else
        XTestFakeMotionEvent(display, -1, xpos, ypos, 0);
}

void XClickMouse(Display *display, int button, int release) {
    XTestFakeButtonEvent(display, button, release, 0);
    XSync(display, False);
}

Status XFetchProperty (register Display *display, Window window, int property, char **name) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long leftover;
    unsigned char *data = NULL;
    if (XGetWindowProperty(display, window, property, 0L, (long) BUFSIZ,
            False, XA_STRING, &actual_type, &actual_format,
            &nitems, &leftover, &data) != Success) {
        *name = NULL;
        return 0;
    }
    if ( (actual_type == XA_STRING) && (actual_format == 8) ) {
        // Cut of paths if any
        *name = basename((char *) data);
        return 1;
    }
    if (data) XFree((char *) data);
    *name = NULL;
    return 0;
}

static int IgnoreDeadWindow(Display *display, XErrorEvent *error) {
   if (error->error_code == BadWindow)
      if (verbose >= 1) printf("Received BadWindow for window 0x%08x.\n", (int) error->resourceid);
   return 0;
}

int try_classhint(Display *display, Window window, char **name) {
    XClassHint xclasshint;
    if (XGetClassHint(display, window, &xclasshint) != 0) {
        *name = xclasshint.res_class;
        XFree(xclasshint.res_name);

        // FIXME: Free when not NULL
        if (*name != NULL && strcmp(*name, ""))
            return 1;
    }
    return 0;
}

int try_property(Display *display, Window window, int property, char **name) {
    if (XFetchProperty(display, window, XA_WM_COMMAND, name) != 0) {
        // FIXME: Free when not NULL
        if (*name != NULL && strcmp(*name, ""))
            return 1;
    }
    return 0;
}

// Implemented for some apps that return a 'weird' parent window id (eg. qiv)
int try_guessed(Display *display, Window window, char **name) {
    Window guessed = window - window % 0x100000 + 1;
    if (window == guessed)
        return 0;

    if (XQueryCommand(display, guessed, name) != 0) {
        // FIXME: Free when not NULL
        if (*name != NULL && strcmp(*name, ""))
            return 1;
    }
    return 0;
}

int try_parent(Display *display, Window window, char **name) {
    Window root_window;
    Window parent_window;
    Window *children_window;
    unsigned int nchildrens;

    if (XQueryTree(display, window, &root_window, &parent_window, &children_window, &nchildrens) != 0) {
        if (parent_window) {
            if (XQueryCommand(display, parent_window, name) != 0) {
                if (verbose >= 3) fprintf(stderr, "Found application %s using parent window. (0x%08x)\n", *name, (unsigned int) parent_window);
                return 1;
            }
        }
    }
    return 0;
}

int try_children(Display *display, Window window, char **name) {
    Window root_window;
    Window parent_window;
    Window *children_window;
    unsigned int nchildrens;

    if (XQueryTree(display, window, &root_window, &parent_window, &children_window, &nchildrens) != 0) {
        if (nchildrens > 0) {
            int i;
            for (i = 0; i < nchildrens; i++) {
                fprintf(stderr, "Found child %d as window 0x%08x.\n", i, (unsigned int) children_window[i]);
                if (XQueryCommand(display, children_window[i], name) != 0) {
                    if (verbose >= 3) fprintf(stderr, "Found application %s using child window (0x%08x).\n", *name, (unsigned int) parent_window);
                    return 1;
                }
            }
        }

    }
    return 0;
}

Status XQueryCommand(Display *display, Window window, char **name) {

    // If root-window, use previous name
    if (window == 0x01 ) {
        if (verbose >= 2) fprintf(stderr, "Use previous name %s for root-window.\n", *name);
        return 1;
    }

    // Free name as we look for a new one
//    if (name != NULL) XFree(name);

    // Low window ids are guaranteed to be wrong
    if (window < 0xff ) return 0;

    if (verbose >= 3) fprintf(stderr, "Working with window (0x%08x).\n", (unsigned int) window);

    if (try_classhint(display, window, name)) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%08x) using XGetClassHint.\n", *name, (unsigned int) window);
    } else if (try_property(display, window, XA_WM_NAME, name)) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%08x) using XA_WM_NAME.\n", *name, (unsigned int) window);
    } else if (try_property(display, window, XA_WM_COMMAND, name)) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%08x) using XA_WM_COMMAND.\n", *name, (unsigned int) window);
    } else if (try_property(display, window, XA_WM_ICON_NAME, name)) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%08x) using XA_WM_ICON_NAME.\n", *name, (unsigned int) window);
    } else if (try_property(display, window, XA_WM_CLASS, name)) {
        if (verbose >= 3) fprintf(stderr, "Found application %s (0x%08x) using XA_WM_CLASS.\n", *name, (unsigned int) window);
    } else if (try_guessed(display, window, name)) {
        if (verbose >= 2) fprintf(stderr, "Found application %s using guessed parent window (0x%08x).\n", *name, (unsigned int) (window - window % 0x100000 + 1));
    } else if (try_parent(display, window, name)) {
        ;
//    } else if (try_children(display, window, name)) {
//        ;
    } else {
        return 0;
    }
    return 1;
}

void exit_clean(int sig) {
    wiimote_disconnect(&wmote);
    XSetScreenSaver(display, timeout_return, interval_return, prefer_blanking_return, allow_exposures_return);
    XCloseDisplay(display);
    switch(sig) {
        case(0):
        case(2):
            exit(0);
        default:
            printf("Exiting on signal %d.\n", sig);
            exit(sig);
    }
}

void rumble(wiimote_t *wmote, int msecs) {
    wmote->rumble = 1;
    wiimote_update(wmote);
    usleep(msecs * 1000);
    wmote->rumble = 0;
}

int main(int argc, char **argv) {
    int length = 0;
    int totaddr = 20;
    int numaddr = 0;
    char *btaddresses[totaddr];
    int infrared = False;
    int tilt = True;
    int reconnect = False;
    wmote = (wiimote_t) WIIMOTE_INIT;

    int c;
    int i = 0;

    // Make stdout unbuffered
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    while (1) {
        int option_index = 0;
        static struct option long_options[] = {
            {"bluetooth", 1, 0, 'b'},
            {"display", 1, 0, 'd'},
            {"help", 0, 0, 'h'},
            {"ir", 0, 0, 'i'},
            {"infrared", 0, 0, 'i'},
            {"length", 1, 0, 'l'},
            {"reconnect", 1, 0, 'r'},
            {"tilt", 0, 0, 't'},
            {"verbose", 0, 0, 'v'},
            {"version", 0, 0, 'V'},
            {0, 0, 0, 0}
        };

        c = getopt_long (argc, argv, "b:d:hil:rtvV", long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'b':
                btaddresses[numaddr] = optarg;
                numaddr++;
                continue;
            case 'd':
                displayname = optarg;
                continue;
            case 'h':
                printf("Nintendo Wiimote presentation controller\n\
\n\
%s options:\n\
  -b, --bluetooth=btaddress      wiimote bluetooth address (use hcitool scan)\n\
  -d, --display=name             X display to use\n\
  -i, --infrared                 use infrared sensor to move mouse pointer\n\
  -l, --length=minutes           presentation length in minutes\n\
  -r, --reconnect                on disconnect, wait for reconnect\n\
  -t, --tilt                     use tilt sensors to move mouse pointer\n\
\n\
  -h, --help                     display this help and exit\n\
  -v, --verbose                  increase verbosity\n\
      --version                  output version information and exit\n\
\n\
Report bugs to <dag@wieers.com>.\n", NAME);
                exit(0);
            case 'i':
                infrared = True;
                tilt = False;
                continue;
            case 'l':
                length = atoi(optarg) * 60;
                continue;
            case 'r':
                reconnect = True;
                continue;
            case 't':
                tilt = True;
                infrared = False;
                continue;
            case 'v':
                verbose += 1;
                continue;
            case 'V':
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

    // Check bluetooth address
    // FIXME: Allow for wiimote scanning
    if (numaddr == 0) {
        fprintf(stderr, "%s: One bluetooth address (-b/--bluetooth) is mandatory.\n", NAME);
        return 1;
    }

    for(i=0; i<numaddr; i++) {
        if (strlen(btaddresses[i]) != 17) {
            fprintf(stderr, "%s: Bluetooth address %s has incorrect length.\n", NAME, btaddresses[i]);
            return 1;
        }
    }

    // Obtain the X11 display.
    if (displayname == NULL)
        displayname = getenv("DISPLAY");

    if (displayname == NULL)
        displayname = ":0.0";

    // Reconnect loop
    do {
        display = XOpenDisplay(displayname);
        if (display == NULL) {
            fprintf(stderr, "%s: Cannot open display `%s'.\n", NAME, displayname);
            return -1;
        }
        XSetErrorHandler(IgnoreDeadWindow);

        // Wait for 1+2
        if (numaddr == 0) {
    //        printf("Please press 1+2 on a wiimote in the viscinity...");
            fprintf(stderr, "%s: Sorry, you need to provide at least one  bluetooth address using -b/--bluetooth.\n", NAME);
            exit(1);
        } else {
            printf("Please press 1+2 on the wiimote with address %s...", btaddresses[0]);
            wiimote_connect(&wmote, btaddresses[0]);
            printf("\n");
        }

        signal(SIGINT, exit_clean);
        signal(SIGHUP, exit_clean);
        signal(SIGQUIT, exit_clean);

        if (tilt)
            fprintf(stderr, "Mouse movement controlled by tilting wiimote.\n");
        else if (infrared)
            fprintf(stderr, "Mouse movement controlled by infrared using sensor bar. (EXPERIMENTAL)\n");
        else
            fprintf(stderr, "Mouse movement disabled.\n");

        if (length) fprintf(stderr, "Presentation length is %dmin divided in 5 slots of %dmin.\n", length/60, length/60/5);

        // Disable screensaver
        XGetScreenSaver(display, &timeout_return, &interval_return, &prefer_blanking_return, &allow_exposures_return);
        XSetScreenSaver(display, 0, 0, 1, 0);

        // Get the root window for the current display.
        int revert;

        time_t start = 0, now = 0, duration = 0;
        int phase = 0, oldphase = 0;
        uint16_t keys = 0;

        int oldbattery = 0;
        Window oldwindow = window;
        int fileviewtoggle = False;
        int fullscreentoggle = False;
        int playtoggle = False;
        int screensavertoggle = False;
        int leftmousebutton = False;
        int rightmousebutton = False;

        int mousemode = False;

        char *name = NULL;
        XGetInputFocus(display, &window, &revert);
        XQueryCommand(display, window, &name);
        oldwindow = window;

        rumble(&wmote, 200);

        start = time(NULL);

        while (wiimote_is_open(&wmote)) {

            // Find the window which has the current keyboard focus.
            XGetInputFocus(display, &window, &revert);

            // Handle focus changes
            if (window != oldwindow) {
                if (XQueryCommand(display, window, &name) != 0) {
                    if (verbose >= 2) fprintf(stderr, "Focus on application %s (0x%08x)\n", name, (unsigned int) window);
                } else {
                    name = strdup("(unknown)");
                    fprintf(stderr, "%s: Unable to find application name for window 0x%08x\n", NAME, (unsigned int) window);
                }
                oldwindow = window;
            }

            // FIXME: We should reconnect at our own convenience
            if (wiimote_update(&wmote) < 0) {
                printf("Lost connection.");
                exit_clean(0);
            }

            if (wiimote_pending(&wmote) == 0) {
                usleep(10000);
            }

            // Check battery change
            // FIXME: Battery level does not seem to get updated ??
            if (wmote.battery < oldbattery) {
                if (wmote.battery < 5)
                    printf("Battery low (%d%%), please replace batteries !\n", wmote.battery);
                else
                    printf("Battery level is now %d%%.\n", wmote.battery);
                oldbattery = wmote.battery;
            }

            // Change leds only when phase changes
            if (length) {
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
            }

//            printf("%f - %f - %f - %ld - %ld - %ld - %d\n", ((float) duration * 5.0 / (float) length), (float) duration, (float) length, start, now, duration, phase);

            // Inside the mouse functionality
            if (mousemode) {
                // Tilt method
                if (tilt) {
                    XMovePointer(display, wmote.tilt.x / 4, wmote.tilt.y / 4, 1);

                // Infrared method
                } else if (infrared) {

//                    XMovePointer(display, (int) absx * width, (int) absy * height, 0);

                }
            }

            // Block repeating keys
            if (keys == wmote.keys.bits) {
                continue;
            }

            // WINDOW MODE
            if (wmote.keys.b) {
                if (wmote.keys.a) {
                    mousemode = ! mousemode;
                    if (mousemode) {
                        if (verbose >= 1) fprintf(stderr, "Entering mouse-mode. ");
                        if (tilt) {
                            if (verbose >= 1) fprintf(stderr, "Tilt sensors enabled.\n");
                        } else if (infrared) {
                            if (verbose >= 1) fprintf(stderr, "Infrared camera enabled. (NOT IMPLEMENTED YET)\n");
                            wmote.mode.ir = 1;
                        }
                        // Infrared only works if acceleration sensors are enabled.
                        wmote.mode.acc = 1;
                    } else {
                        if (verbose >= 1) fprintf(stderr, "Leaving mouse-mode. ");
                        if (tilt) {
                            if (verbose >= 1) fprintf(stderr, "Tilt sensors disabled.\n");
                        } else if (infrared) {
                            if (verbose >= 1) fprintf(stderr, "Infrared camera disabled.\n");
                            wmote.mode.acc = 0;
                        }
                        // Infrared only works if acceleration sensors are enabled.
                        wmote.mode.acc = 0;
                    }

                }

                // Scroll up
                if (wmote.keys.up) {
                    if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Page_Up, ShiftMask);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_i, 0); // Change input source
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Page_Up, ShiftMask);
                    }
                }

                // Scroll down
                if (wmote.keys.down) {
                    if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Page_Down, ShiftMask);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Page_Down, ShiftMask);
                    }
                }

                // FIXME: We have to keep Alt pressed if we want to browse between apps
                if (wmote.keys.left) {
                    XKeycode(XK_Tab, Mod1Mask | ShiftMask);
                }

                if (wmote.keys.right) {
                    XKeycode(XK_Tab, Mod1Mask);
                }

                // Previous workspace
                if (wmote.keys.minus) {
                    XKeycode(XK_Left, ControlMask | Mod1Mask);
                }

                // Next workspace
                if (wmote.keys.plus) {
                    XKeycode(XK_Right, ControlMask | Mod1Mask);
                }

                if (wmote.keys.two) {
                    // Mute audio
                    if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_m, 0);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_m, ControlMask);
                    } else {
                        XKeycode(XF86XK_AudioMute, 0);
                    }

                    // Blank screen
                    if (screensavertoggle) {
                        XForceScreenSaver(display, ScreenSaverReset);
                    } else {
                        XActivateScreenSaver(display);
                    }

                    screensavertoggle = ! screensavertoggle;
                }

                // Save the keys state for next run
                keys = wmote.keys.bits;
                continue;
            }

            // MOUSE MODE
            if (mousemode) {
                // Left mouse button events
                if (wmote.keys.minus || wmote.keys.a) {
                    if (! leftmousebutton) {
                        if (verbose >= 3) fprintf(stderr, "Mouse left button pressed.\n");
                        XClickMouse(display, 1, 1);
                        leftmousebutton = ! leftmousebutton;
                    }
                } else {
                    if (leftmousebutton) {
                        if (verbose >= 3) fprintf(stderr, "Mouse left button released.\n");
                        XClickMouse(display, 1, 0);
                        leftmousebutton = ! leftmousebutton;
                    }
                }

                // Right mouse button events
                if (wmote.keys.plus) {
                    if (! rightmousebutton) {
                        if (verbose >= 3) fprintf(stderr, "Mouse right button pressed.\n");
                        XClickMouse(display, 3, 1);
                        rightmousebutton = ! rightmousebutton;
                    }
                } else {
                    if (rightmousebutton) {
                        if (verbose >= 3) fprintf(stderr, "Mouse right button released.\n");
                        XClickMouse(display, 3, 0);
                        rightmousebutton = ! rightmousebutton;
                    }
                }

            // APPLICATION MODE
            } else {

                // Go home/back
                if (wmote.keys.home) {
                    if (strcasestr(name, "acroread") == name) {
                        XKeycode(XK_Home, 0);
                    } else if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_Home, 0);
                    } else if (strcasestr(name, "evince") == name) {
                        XKeycode(XK_Home, ControlMask);
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Home, 0);
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_Home, ControlMask);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_Home, 0);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_BackSpace, ShiftMask);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        XKeycode(XK_Home, 0);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_Home, 0);
                    } else if (strcasestr(name, "xpdf") == name) {
                        XKeycode(XK_Home, ControlMask);
                    } else if (strcasestr(name, "yelp") == name) {
                        XKeycode(XK_Home, 0);
                    } else {
                        if (verbose) fprintf(stderr, "No home-key support for application %s.\n", name);
                    }
                }

                // Next slide/page, play/pause, enter
                if (wmote.keys.a) {
                    if (strcasestr(name, "acroread") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "evince") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Return, 0);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Return, 0);
                    } else if (strcasestr(name, "gqview") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "gxine") == name) {
                        XKeycode(XK_space, 0);
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_p, 0);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_Return, ShiftMask);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_Return, 0);
                    } else if (strcasestr(name, "qiv") == name) {
//                        XKeycode(XK_space, 0);
                        XKeycode(XK_m, 0);  // Maximize
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_space, ControlMask); 
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_p, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_Return, 0); // Channel info
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_space, 0);
                    } else if (strcasestr(name, "xpdf") == name) {
                        XKeycode(XK_n, 0);
                    } else if (strcasestr(name, "xmms") == name) {
                        if ( (playtoggle = ! playtoggle ) )
                            XKeycode(XK_x, 0);
                        else
                            XKeycode(XK_c, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Return, 0);
                    } else if (strcasestr(name, "yelp") == name) {
                        XKeycode(XK_Return, 0);
                    } else {
//                        if (verbose) fprintf(stderr, "No A-key support for application %s.\n", name);
                        XKeycode(XK_Return, 0);
                    }
                }

                // Fullscreen
                if (wmote.keys.one) {
                    if (strcasestr(name, "acroread") == name) {
                        XKeycode(XK_L, ControlMask);
                    } else if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_F11, 0);
                    } else if (strcasestr(name, "evince") == name) {
                        XKeycode(XK_F5, 0);
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_F11, 0);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_F11, 0);
                    } else if (strcasestr(name, "gqview") == name) {
                        XKeycode(XK_F, 0);
                    } else if (strcasestr(name, "gxine") == name) {
                        XKeycode(XK_f, ControlMask);
                    } else if (strcasestr(name, "kpdf") == name) {
                        if ( (fullscreentoggle = ! fullscreentoggle ) )
                            XKeycode(XK_Escape, 0);
                        else
                            XKeycode(XK_p, ControlMask | ShiftMask);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_F12, 0);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_f, 0);
                    } else if (strcasestr(name, "nautilus") == name) {
                        if ( (fullscreentoggle = ! fullscreentoggle ) )
                            XKeycode(XK_F5, Mod1Mask);
                        else
                            XKeycode(XK_F10, Mod1Mask);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        if ( (fullscreentoggle = ! fullscreentoggle ) )
                            XKeycode(XK_Escape, 0);
                        else
                            XKeycode(XK_F9, 0);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_F11, 0);
                    } else if (strcasestr(name, "qiv") == name) {
                        XKeycode(XK_f, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_F11, 0);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_f, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_f, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_f, 0);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_f, 0);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_d, ControlMask);
                    } else if (strcasestr(name, "xpdf") == name) {
                        XKeycode(XK_F, Mod1Mask);
                    } else {
                        if (verbose) fprintf(stderr, "No one-key support for application %s.\n", name);
                    }
                }

                // change aspect ratio
                if (wmote.keys.two) {
                    if (strcasestr(name, "gxine") == name) {
                        XKeycode(XK_a, 0);
                    } else if (strcasestr(name, "nautilus") == name) {
                        if ( (fileviewtoggle = ! fileviewtoggle) )
                            XKeycode(XK_1, ControlMask | ShiftMask);
                        else
                            XKeycode(XK_2, ControlMask | ShiftMask);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_u, ControlMask); // Toggle random
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_a, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_a, 0);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_a, 0);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_s, 0); // Toggle random
                    } else {
                        if (verbose) fprintf(stderr, "No two-key support for application %s.\n", name);
                    }
                }

                // Scroll up, volume up, rotate
                if (wmote.keys.up) {
                    if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_r, ControlMask);
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Tab, ShiftMask);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "gqview") == name) {
                        XKeycode(XK_bracketright, 0); // FIXME: This does not work
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_0, ShiftMask);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_Up, ControlMask);
                    } else if (strcasestr(name, "pidgin") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        XKeycode(XK_Page_Up, Mod1Mask);
                    } else if (strcasestr(name, "qiv") == name) {
                        XKeycode(XK_k, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Up, ControlMask);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_KP_Add, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_Up, ControlMask);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_V, ShiftMask);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "yelp") == name) {
                        XKeycode(XK_Tab, ShiftMask);
                    } else {
//                        if (verbose) fprintf(stderr, "No up-key for application %s.\n", name);
                        XKeycode(XK_Up, 0);
                    }
                }

                // Scroll down, volume down, rotate back, cursor down
                if (wmote.keys.down) {
                    if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_r, ShiftMask | ControlMask); // FIXME: No key in eog for rotating counter clockwise ?
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Tab, 0);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "gqview") == name) {
                        XKeycode(XK_bracketleft, 0); // FIXME: This does not work
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_9, ShiftMask);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        XKeycode(XK_Page_Down, Mod1Mask);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_Down, ControlMask);
                    } else if (strcasestr(name, "pidgin") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "qiv") == name) {
                        XKeycode(XK_l, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Down, ControlMask);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_KP_Subtract, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_Down, ControlMask);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_v, 0);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "yelp") == name) {
                        XKeycode(XK_Tab, 0);
                    } else {
//                        if (verbose) fprintf(stderr, "No down-key support for application %s.\n", name);
                        XKeycode(XK_Down, 0);
                    }
                }

                // Next tab/slide/song/channel, skip firward, cursor right
                if (wmote.keys.right) {
                    if (strcasestr(name, "acroread") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "evince") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Page_Down, ControlMask);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "gxine") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_F6, ControlMask);
                    } else if (strcasestr(name, "pan") == name) {
                        XKeycode(XK_v, 0);
                    } else if (strcasestr(name, "pidgin") == name) {
                        XKeycode(XK_Tab, ControlMask);
                    } else if (strcasestr(name, "gqview") == name) {
                        XKeycode(XK_Page_Down, 0);
                    } else if (strcasestr(name, "qiv") == name) {
                        XKeycode(XK_space, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Right, Mod1Mask);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_Right, Mod1Mask);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_Right, ControlMask);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_b, 0);
                    } else if (strcasestr(name, "xpdf") == name) {
                        XKeycode(XK_n, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Right, 0);
                    } else if (strcasestr(name, "yelp") == name) {
                        XKeycode(XK_Right, Mod1Mask);
                    } else {
//                        if (verbose) fprintf(stderr, "No right-key support for application %s.\n", name);
                        XKeycode(XK_Right, 0);
                    }
                }

                // Previous tab/slide/song/channel, skip backward
                if (wmote.keys.left) {
                    if (strcasestr(name, "acroread") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "eog") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "evince") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_Page_Up, ControlMask);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "gqview") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "gxine") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "kpresenter") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "openoffice") == name ||
                               strcasestr(name, "soffice") == name) {
                        XKeycode(XK_Page_Up, 0);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_F6, ControlMask | ShiftMask);
                    } else if (strcasestr(name, "pan") == name) {
                        XKeycode(XK_n, ControlMask);
                    } else if (strcasestr(name, "pidgin") == name) {
                        XKeycode(XK_Tab, ControlMask | ShiftMask);
                    } else if (strcasestr(name, "qiv") == name) {
                        XKeycode(XK_BackSpace, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Left, Mod1Mask);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_Left, Mod1Mask);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_Left, ControlMask);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_z, 0);
                    } else if (strcasestr(name, "xpdf") == name) {
                        XKeycode(XK_p, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_Left, 0);
                    } else if (strcasestr(name, "yelp") == name) {
                        XKeycode(XK_Left, Mod1Mask);
                    } else {
//                        if (verbose) fprintf(stderr, "No left-key support for application %s.\n", name);
                        XKeycode(XK_Left, 0);
                    }
                }

                // Zoom out, volume down
                if (wmote.keys.minus) {
                    if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_minus, ControlMask);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_minus, ControlMask);
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_minus, ControlMask);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_9, ShiftMask);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_minus, ControlMask);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_minus, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Down, ControlMask);
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_KP_Subtract, 0);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_Down, ControlMask);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_v, 0);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_Down, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_KP_Subtract, ShiftMask);
                    } else {
                        XKeycode(XF86XK_AudioLowerVolume, 0);
                    }
                }

                // Zoom in, volume up
                if (wmote.keys.plus) {
                    if (strcasestr(name, "firefox") == name) {
                        XKeycode(XK_plus, ShiftMask | ControlMask);
                    } else if (strcasestr(name, "gnome-terminal") == name) {
                        XKeycode(XK_plus, ShiftMask | ControlMask);
                    } else if (strcasestr(name, "kpdf") == name) {
                        XKeycode(XK_plus, ShiftMask | ControlMask);
                    } else if (strcasestr(name, "mplayer") == name) {
                        XKeycode(XK_0, ShiftMask);
                    } else if (strcasestr(name, "nautilus") == name) {
                        XKeycode(XK_plus, ShiftMask | ControlMask);
                    } else if (strcasestr(name, "opera") == name) {
                        XKeycode(XK_plus, 0);
                    } else if (strcasestr(name, "rhythmbox") == name) {
                        XKeycode(XK_Up, ShiftMask | ControlMask);
                    } else if (strcasestr(name, "totem") == name) {
                        XKeycode(XK_Up, 0); 
                    } else if (strcasestr(name, "tvtime") == name) {
                        XKeycode(XK_KP_Add, 0);
                    } else if (strcasestr(name, "vlc") == name) {
                        XKeycode(XK_Up, ControlMask);
                    } else if (strcasestr(name, "xine") == name) {
                        XKeycode(XK_V, ShiftMask);
                    } else if (strcasestr(name, "xmms") == name) {
                        XKeycode(XK_Up, 0);
                    } else if (strcasestr(name, "xterm") == name) {
                        XKeycode(XK_KP_Add, ShiftMask);
                    } else {
                        XKeycode(XF86XK_AudioRaiseVolume, 0);
                    }
                }

            }

            // Save the keys state for next run
            keys = wmote.keys.bits;
        }
        XCloseDisplay(display);

    } while(reconnect);

    return 0;
}

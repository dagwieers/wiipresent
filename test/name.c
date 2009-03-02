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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

static Display *display = NULL;
static Window window = 0;

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
        *name = (char *) data;
        return 1;
    }
    if (data) XFree((char *)data);
    *name = NULL;
    return 0;
}

void lowercase(char *string) {
    int i;
    for(i=0; string[i] != '\0'; i++)
        if (isupper(string[i]))
            string[i] = tolower(string[i]);
}

Status XQueryCommand(Display *display, Window window, char **name) {
    Window root_window;
    Window parent_window;
    Window *children_window;
    unsigned int nchildrens;

    // Try getting the command
    if (XFetchProperty(display, window, XA_WM_COMMAND, name) == 0) {
        // Try XClassHint next
        XClassHint xclasshint;
        if (XGetClassHint(display, window, &xclasshint) != 0) {
            *name = xclasshint.res_class;
            XFree(xclasshint.res_name);
        // Try parent window
        } else if (XQueryTree(display, window, &root_window, &parent_window, &children_window, &nchildrens) != 0) {
            if (XQueryCommand(display, parent_window, name) == 0)
                return 0;
        } else
            return 0;
    }
    lowercase(*name);
    return 1;
}

int main(int argc, char **argv) {
    // Make stdout unbuffered
    setvbuf(stdout, NULL, _IONBF, 0);

    display = XOpenDisplay(":0.0");
    if (display == NULL) {
        fprintf(stderr, "Can't open display.\n");
        return -1;
    }

    // Get the root window for the current display.
    int revert;
    char *name = NULL;
    Window oldwindow;

    while (1) {
        oldwindow = window;

        // Find the window which has the current keyboard focus.
        XGetInputFocus(display, &window, &revert);

        if (window == oldwindow)
            continue;

        if (XQueryCommand(display, window, &name) != 0)
            printf("Entering %s (%ld)\n", name, window);
        else
            printf("Entering unknown %s (%ld)\n", name, window);

        XFree(name);
        usleep(10000);
    }
    XCloseDisplay(display);

    return 0;
}

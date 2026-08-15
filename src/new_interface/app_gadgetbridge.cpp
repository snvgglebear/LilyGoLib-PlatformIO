#include "app_gadgetbridge.h"

#include <stdio.h>

#include "gadgetbridge_ble/gb_link.h"
#include "gadgetbridge_ble/gb_platform.h"

/// Generous fixed size: today's callers are the home screen (notification
/// toast + battery + call overlay) and the alarms screen, i.e. a handful.
/// Bumping this is free; there is no dynamic allocation to worry about.
#define MAX_GB_LISTENERS 8

static gb_listener_t s_listeners[MAX_GB_LISTENERS];
static int s_listener_count = 0;

static void dispatch(GbStateChange change)
{
    for (int i = 0; i < s_listener_count; i++) {
        s_listeners[i](change);
    }
}

void app_gb_init()
{
    gb_platform::begin();
    gb_app.begin(dispatch);
    printf("[app_gb] ready as \"%s\"\n", gb_link_device_name());
}

void app_gb_poll()
{
    gb_app.poll();
}

bool app_gb_add_listener(gb_listener_t fn)
{
    if (s_listener_count >= MAX_GB_LISTENERS) {
        return false;
    }
    s_listeners[s_listener_count++] = fn;
    return true;
}

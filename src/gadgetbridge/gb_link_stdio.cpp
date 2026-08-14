/**
 * @file      gb_link_stdio.cpp
 * @license   MIT
 * @brief     stdin/stdout transport, for the native/SDL2 emulator build.
 *
 * Stands in for the BLE link (gb_ble.cpp) when there is no radio: lines typed
 * or piped into stdin are fed through exactly the same framing and decoding
 * path the phone's writes take, and the watch's replies are printed on stdout.
 * That makes the whole protocol exercisable on the desktop:
 *
 *     pio run -e emulator_watch_ultra -t exec
 *     ...then type, or pipe in:
 *     {"t":"notify","id":1,"src":"Signal","title":"Ada","body":"On my way"}
 *     {"t":"call","cmd":"incoming","name":"Grace Hopper","number":"+15551234567"}
 *     {"t":"musicinfo","artist":"Boards of Canada","track":"Dawn Chorus","dur":257}
 */
#ifndef ARDUINO

#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include "gb_link.h"

namespace
{

GbProtocolHandler *s_handler = nullptr;

GbLineAssembler s_assembler([](const std::string & line) {
    if (s_handler && !gb_protocol_dispatch(line, *s_handler)) {
        printf("[gb] dropping malformed line: %s\n", line.c_str());
    }
});

} // namespace

void gb_link_begin(GbProtocolHandler &handler)
{
    s_handler = &handler;
    printf("[gb] emulator link ready -- paste phone -> watch messages on stdin\n");
}

void gb_link_poll()
{
    // Non-blocking: the UI has to keep running whether or not anything is typed.
    struct pollfd stdin_poll = {STDIN_FILENO, POLLIN, 0};
    while (poll(&stdin_poll, 1, 0) > 0 && (stdin_poll.revents & POLLIN)) {
        uint8_t buffer[256];
        ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
        s_assembler.feed(buffer, static_cast<size_t>(count));
    }
}

bool gb_link_send(const std::string &json)
{
    printf("%s\n", json.c_str());
    fflush(stdout);
    return true;
}

bool gb_link_connected()
{
    return true;                    // no pairing to do; pretend the phone is there
}

const char *gb_link_device_name()
{
    return "T-Watch Ultra (emulator)";
}

void gb_link_set_battery_level(int percent)
{
    (void)percent;                  // no Battery Service without a GATT server
}

#endif // !ARDUINO

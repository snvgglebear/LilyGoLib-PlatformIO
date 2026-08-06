FROM mcr.microsoft.com/devcontainers/python:1-3.12-bookworm

# build-essential + libsdl2-dev: needed for the `env_emulator` native targets
# (emulator_lora_pager / emulator_twatchs3 / emulator_watch_ultra), which
# compile and link against SDL2 on the host instead of an ESP32 toolchain.
# libusb-1.0-0-dev: needed by esptool/pyserial for flashing over USB.
# Drop the base image's broken yarn apt source (unverifiable signing key) —
# not needed here and otherwise breaks `apt-get update`.
RUN rm -f /etc/apt/sources.list.d/yarn.list

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
        build-essential \
        libsdl2-dev \
        libusb-1.0-0-dev \
        udev \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir -U platformio

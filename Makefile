DEVKITPRO ?= /opt/devkitpro
CC := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc
NXTOOLS := $(DEVKITPRO)/tools/bin
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu17 -O2 -g -Wall -Wextra -Werror -ffunction-sections $(ARCH) -D__SWITCH__ -I$(DEVKITPRO)/libnx/include
VERSION := $(shell sed -n 's/^\#define RELAY_VERSION "\(.*\)"/\1/p' relay/common/relay_protocol.h)
HOST_CC ?= cc
HOST_CFLAGS := -std=c17 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all test
all: build/ldn-relay-v$(VERSION).nro

build:
	mkdir -p build

build/relay-main.o: relay/switch/main.c relay/switch/ble_link.inc relay/common/relay_protocol.h relay/common/relay_codec.h Makefile | build
	$(CC) $(CFLAGS) -Irelay/common -c $< -o $@

build/relay-codec.o: relay/common/relay_codec.c relay/common/relay_codec.h Makefile | build
	$(CC) $(CFLAGS) -Irelay/common -c $< -o $@

build/ldn-relay.elf: build/relay-main.o build/relay-codec.o
	$(CC) $(ARCH) -specs=$(DEVKITPRO)/libnx/switch.specs -L$(DEVKITPRO)/libnx/lib -Wl,-Map,build/ldn-relay.map $^ -lnx -o $@

build/ldn-relay.nacp: Makefile relay/common/relay_protocol.h | build
	$(NXTOOLS)/nacptool --create 'LDN Relay' 'Personal research' '$(VERSION)' $@

build/ldn-relay-v$(VERSION).nro: build/ldn-relay.elf build/ldn-relay.nacp
	$(NXTOOLS)/elf2nro $< $@ --nacp=build/ldn-relay.nacp --icon=$(DEVKITPRO)/libnx/default_icon.jpg

build/relay-codec-test: relay/common/relay_codec.c relay/tests/codec_test.c relay/common/relay_codec.h Makefile | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/common/relay_codec.c relay/tests/codec_test.c -o $@

test: build/relay-codec-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-codec-test

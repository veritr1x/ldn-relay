DEVKITPRO ?= /opt/devkitpro
CC := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc
NXTOOLS := $(DEVKITPRO)/tools/bin
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -std=gnu17 -O2 -g -ffile-prefix-map=$(CURDIR)=. -fdebug-prefix-map=$(CURDIR)=. -Wall -Wextra -Werror -ffunction-sections $(ARCH) -D__SWITCH__ -I$(DEVKITPRO)/libnx/include
VERSION := $(shell sed -n 's/^\#define RELAY_VERSION "\(.*\)"/\1/p' relay/common/relay_protocol.h)
HOST_CC ?= cc
ifeq ($(shell uname -s),Linux)
AUTH_TEST_LIBS ?= -lcrypto
endif
HOST_CFLAGS := -std=c17 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all test
all: build/ldn-relay-v$(VERSION).nro

build:
	mkdir -p build

build/relay-main.o: relay/switch/main.c relay/common/relay_approval.h relay/common/relay_recovery.h relay/common/relay_host.h relay/common/relay_compact.h relay/switch/ble_link.inc relay/switch/stream_link.inc relay/common/relay_stream_batch.h relay/switch/usb_link.inc relay/common/relay_usb.h relay/switch/oneway.inc relay/common/relay_oneway.h relay/switch/benchmark.inc relay/common/relay_stream.h relay/common/relay_queue.h relay/common/relay_protocol.h relay/common/relay_codec.h relay/common/relay_batch.h Makefile | build
	$(CC) $(CFLAGS) -Irelay/common -c $< -o $@

build/relay-codec.o: relay/common/relay_codec.c relay/common/relay_codec.h relay/common/relay_batch.h Makefile | build
	$(CC) $(CFLAGS) -Irelay/common -c $< -o $@

build/relay-stream.o: relay/common/relay_stream.c relay/common/relay_stream.h relay/common/relay_codec.h | build
	$(CC) $(CFLAGS) -Irelay/common -c $< -o $@

build/relay-auth.o: relay/common/relay_auth.c relay/common/relay_auth.h | build
	$(CC) $(CFLAGS) -Irelay/common -c $< -o $@

build/ldn-relay.elf: build/relay-main.o build/relay-codec.o build/relay-stream.o
	$(CC) $(ARCH) -specs=$(DEVKITPRO)/libnx/switch.specs -L$(DEVKITPRO)/libnx/lib -Wl,-Map,build/ldn-relay.map $^ -lnx -o $@

build/ldn-relay.nacp: Makefile relay/common/relay_protocol.h | build
	$(NXTOOLS)/nacptool --create 'LDN Relay' 'veritrix' '$(VERSION)' $@

build/ldn-relay-v$(VERSION).nro: build/ldn-relay.elf build/ldn-relay.nacp assets/icon.jpg
	$(NXTOOLS)/elf2nro $< $@ --nacp=build/ldn-relay.nacp --icon=assets/icon.jpg

build/relay-codec-test: relay/common/relay_codec.c relay/tests/codec_test.c relay/common/relay_codec.h relay/common/relay_batch.h Makefile | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/common/relay_codec.c relay/tests/codec_test.c -o $@


build/relay-stream-test: relay/common/relay_notify_gate.h relay/common/relay_recovery.h relay/common/relay_stream.c relay/common/relay_admission.h relay/common/relay_codec.c relay/tests/stream_test.c relay/common/relay_stream.h relay/common/relay_codec.h relay/common/relay_queue.h relay/common/relay_stream_batch.h | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/common/relay_stream.c relay/common/relay_codec.c relay/tests/stream_test.c -o $@

build/relay-usb-test: relay/tests/usb_test.c relay/common/relay_usb.h relay/common/relay_codec.c | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/common/relay_codec.c relay/tests/usb_test.c -o $@

build/relay-compact-test: relay/tests/compact_test.c relay/common/relay_compact.h relay/common/relay_protocol.h | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/tests/compact_test.c -o $@

test: build/relay-approval-test build/relay-auth-test build/relay-host-test build/relay-oneway-test build/relay-stream-test build/relay-codec-test build/relay-usb-test build/relay-compact-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-approval-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-auth-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-host-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-oneway-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-compact-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-codec-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-stream-test
	UBSAN_OPTIONS=halt_on_error=1 build/relay-usb-test

# Host-only deterministic BLE laboratory; no console or Bluetooth access.
.PHONY: ble-lab ble-lab-test
build/librelay-lab.dylib: relay/sim/stream_bridge.c relay/common/relay_stream.c relay/common/relay_codec.c relay/common/relay_stream.h relay/common/relay_codec.h relay/common/relay_batch.h relay/common/relay_protocol.h | build
	$(HOST_CC) -std=c17 -O2 -g -Wall -Wextra -Werror -fPIC -shared -Irelay/common relay/sim/stream_bridge.c relay/common/relay_stream.c relay/common/relay_codec.c -o $@

ble-lab: build/librelay-lab.dylib
	python3 relay/sim/ble_lab.py $(LAB_ARGS)

ble-lab-test: build/librelay-lab.dylib
	python3 relay/sim/test_lab.py

build/relay-oneway-test: relay/tests/oneway_test.c relay/common/relay_oneway.h relay/common/relay_codec.c | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/tests/oneway_test.c relay/common/relay_codec.c -o $@

build/relay-host-test: relay/tests/host_test.c relay/common/relay_host.h relay/common/relay_codec.c | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common $< relay/common/relay_codec.c -o $@

build/relay-auth-test: relay/tests/auth_test.c relay/common/relay_stream.c relay/common/relay_auth.c relay/common/relay_auth.h relay/common/relay_codec.c | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/tests/auth_test.c relay/common/relay_stream.c relay/common/relay_auth.c relay/common/relay_codec.c $(AUTH_TEST_LIBS) -o $@

# Current local-approval transport; authentication tests above cover historical v4.
build/relay-approval-test: relay/tests/approval_test.c relay/common/relay_approval.h relay/common/relay_stream.c relay/common/relay_codec.c | build
	$(HOST_CC) $(HOST_CFLAGS) -Irelay/common relay/tests/approval_test.c relay/common/relay_stream.c relay/common/relay_codec.c -o $@

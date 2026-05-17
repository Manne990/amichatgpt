APP_NAME := AmiChatGPT
VERSION := $(shell sed -n '1p' VERSION)

BUILD_DIR := build
DIST_DIR := dist
PACKAGE_DIR := $(DIST_DIR)/$(APP_NAME)-$(VERSION)
ADF_IMAGE := $(DIST_DIR)/$(APP_NAME)-$(VERSION).adf

HOST_CC ?= cc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

AMIGA_CC ?= m68k-amigaos-gcc
AMIGA_CFLAGS ?= -m68000 -Os -Wall -Wextra
AMIGA_LDFLAGS ?= -noixemul

LHA ?= $(shell command -v lha 2>/dev/null)
XDFTOOL ?= $(shell command -v xdftool 2>/dev/null)

HOST_BIN := $(BUILD_DIR)/host/$(APP_NAME)-host
HOST_OUTPUT := $(BUILD_DIR)/host/output.txt
AMIGA_BIN := $(BUILD_DIR)/amiga/$(APP_NAME)

.PHONY: all host-build host-test test amiga package package-dir archive adf clean

all: host-test

host-build: $(HOST_BIN)

$(HOST_BIN): src/main.c VERSION | $(BUILD_DIR)/host
	$(HOST_CC) $(HOST_CFLAGS) -DAMICHATGPT_VERSION=\"$(VERSION)\" -o $@ src/main.c

host-test: host-build
	$(HOST_BIN) > $(HOST_OUTPUT)
	grep -q "AmiChatGPT $(VERSION)" $(HOST_OUTPUT)
	grep -q "ChatGPT64 bridge" $(HOST_OUTPUT)
	grep -q "Workbench 3.x" $(HOST_OUTPUT)

test: host-test
	python3 -m unittest discover -s tests

amiga: $(AMIGA_BIN)

$(AMIGA_BIN): src/main.c VERSION | $(BUILD_DIR)/amiga
	@command -v $(AMIGA_CC) >/dev/null 2>&1 || { echo "Missing $(AMIGA_CC). Use the Docker build in README.md or install an Amiga m68k cross-compiler."; exit 127; }
	$(AMIGA_CC) $(AMIGA_CFLAGS) -DAMICHATGPT_VERSION=\"$(VERSION)\" -o $@ src/main.c $(AMIGA_LDFLAGS)

package: package-dir archive adf

package-dir: $(PACKAGE_DIR)/$(APP_NAME)

$(PACKAGE_DIR)/$(APP_NAME): $(AMIGA_BIN) packaging/README.txt VERSION | $(DIST_DIR)
	rm -rf $(PACKAGE_DIR)
	mkdir -p $(PACKAGE_DIR)
	cp $(AMIGA_BIN) $(PACKAGE_DIR)/$(APP_NAME)
	cp packaging/README.txt $(PACKAGE_DIR)/README.txt
	cp VERSION $(PACKAGE_DIR)/VERSION
	printf "HOST=192.168.1.50\nPORT=6464\nWIDTH=72\n" > $(PACKAGE_DIR)/AmiChatGPT.conf

archive: $(PACKAGE_DIR)/$(APP_NAME)
	@if [ -n "$(LHA)" ]; then \
		rm -f "$(DIST_DIR)/$(APP_NAME)-$(VERSION).lha"; \
		(cd "$(DIST_DIR)" && "$(LHA)" a "$(APP_NAME)-$(VERSION).lha" "$(APP_NAME)-$(VERSION)" >/dev/null); \
		echo "Wrote $(DIST_DIR)/$(APP_NAME)-$(VERSION).lha"; \
	else \
		rm -f "$(DIST_DIR)/$(APP_NAME)-$(VERSION).tar.gz"; \
		tar -czf "$(DIST_DIR)/$(APP_NAME)-$(VERSION).tar.gz" -C "$(DIST_DIR)" "$(APP_NAME)-$(VERSION)"; \
		echo "lha not found; wrote $(DIST_DIR)/$(APP_NAME)-$(VERSION).tar.gz"; \
	fi

adf: $(ADF_IMAGE)

$(ADF_IMAGE): $(PACKAGE_DIR)/$(APP_NAME)
	@if [ -n "$(XDFTOOL)" ]; then \
		rm -f "$(ADF_IMAGE)"; \
		"$(XDFTOOL)" -f "$(ADF_IMAGE)" format "$(APP_NAME)" \
			+ write "$(PACKAGE_DIR)/$(APP_NAME)" \
			+ write "$(PACKAGE_DIR)/README.txt" \
			+ write "$(PACKAGE_DIR)/VERSION" \
			+ write "$(PACKAGE_DIR)/AmiChatGPT.conf" \
			+ list; \
		echo "Wrote $(ADF_IMAGE)"; \
	else \
		echo "xdftool not found; skipped ADF image"; \
	fi

$(BUILD_DIR)/host $(BUILD_DIR)/amiga $(DIST_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

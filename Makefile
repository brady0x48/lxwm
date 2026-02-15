CC = cc
XFT_CFLAGS := $(shell pkg-config --cflags xft 2>/dev/null)
XFT_LIBS := $(shell pkg-config --libs xft 2>/dev/null)

CFLAGS = -std=c11 -Wall -Wextra -O2 $(XFT_CFLAGS)
LDFLAGS = -lX11 -lXinerama $(XFT_LIBS)
CPPFLAGS = -Iinclude

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
XSESSIONS_DIR ?= /usr/share/xsessions
DESKTOP_FILE ?= lxwm.desktop
CONFIG_DEST_REL ?= .config/lxwm

SRC = src/main.c \
	src/wm.c \
	src/utils.c \
	src/tree.c \
	src/focus.c \
	src/decor.c \
	src/bar.c \
	src/layout.c \
	src/clients.c \
	src/workspace.c \
	src/spawn.c \
	src/config.c \
	src/keypresses.c \
	src/monitors.c \
	src/x11.c

all: lxwm lxwm-msg

lxwm: $(SRC) include/lxwm.h include/wm_internal.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o lxwm $(SRC) $(LDFLAGS)

lxwm-msg: src/lxwm-msg.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o lxwm-msg src/lxwm-msg.c

install: lxwm lxwm-msg install-bin install-xsession install-user-config

install-bin:
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 lxwm "$(DESTDIR)$(BINDIR)/lxwm"
	install -m 755 lxwm-msg "$(DESTDIR)$(BINDIR)/lxwm-msg"

install-xsession:
	install -d "$(DESTDIR)$(XSESSIONS_DIR)"
	{ \
		printf '%s\n' '[Desktop Entry]'; \
		printf '%s\n' 'Name=lxwm'; \
		printf '%s\n' 'Comment=Light X Window Manager'; \
		printf '%s\n' 'Exec=lxwm'; \
		printf '%s\n' 'TryExec=lxwm'; \
		printf '%s\n' 'Type=Application'; \
		printf '%s\n' 'DesktopNames=lxwm'; \
		printf '%s\n' 'X-GNOME-WMName=lxwm'; \
	} > "$(DESTDIR)$(XSESSIONS_DIR)/$(DESKTOP_FILE)"

install-user-config:
	@if [ -n "$(DESTDIR)" ]; then \
		echo "Skipping user config install because DESTDIR is set."; \
	else \
		target_home="$$HOME"; \
		if [ -n "$$SUDO_USER" ]; then \
			sudo_home="$$(getent passwd "$$SUDO_USER" | cut -d: -f6)"; \
			if [ -n "$$sudo_home" ]; then target_home="$$sudo_home"; fi; \
		fi; \
		if [ -n "$$target_home" ]; then \
			mkdir -p "$$target_home/$(CONFIG_DEST_REL)"; \
			if [ ! -f "$$target_home/$(CONFIG_DEST_REL)/config" ]; then \
				install -m 644 config.example "$$target_home/$(CONFIG_DEST_REL)/config"; \
				echo "Installed default config: $$target_home/$(CONFIG_DEST_REL)/config"; \
			else \
				echo "Keeping existing config: $$target_home/$(CONFIG_DEST_REL)/config"; \
			fi; \
		else \
			echo "Could not determine target home for user config install."; \
		fi; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/lxwm"
	rm -f "$(DESTDIR)$(BINDIR)/lxwm-msg"
	rm -f "$(DESTDIR)$(XSESSIONS_DIR)/$(DESKTOP_FILE)"
	@echo "User config is not removed automatically."

clean:
	rm -f lxwm lxwm-msg

.PHONY: all install install-bin install-xsession install-user-config uninstall clean

.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
RWMCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND) $(CPPFLAGS)
RWMDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput $(LUA_PKG) $(XLIBS)
RWMCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(RWMCPPFLAGS) $(RWMDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

all: rwm
rwm: rwm.c luaconf.c luaconf.h default_config.h client.h ime.h util.h config.mk cursor-shape-v1-protocol.h \
	ext-image-copy-capture-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h
	$(CC) rwm.c luaconf.c $(RWMCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
ext-image-copy-capture-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@

# The default Lua config is embedded in the binary (rwm -g prints it).
# Written as a byte array so that it has no string length limit.
default_config.h: config.def.lua
	( echo '/* generated from config.def.lua, do not edit */'; \
	  echo 'static const char default_config_lua[] = {'; \
	  od -An -v -tu1 config.def.lua | sed -e 's/^ *//' -e 's/  */,/g' -e 's/$$/,/'; \
	  echo '0 };' ) > $@

# Unit tests for the Lua config loader (no wlroots needed)
TESTPKGS = $(LUA_PKG) wayland-server xkbcommon libinput
test: default_config.h
	$(CC) -g -Wall -Wextra -Wno-unused-parameter tests/test_luaconf.c luaconf.c \
		`$(PKG_CONFIG) --cflags --libs $(TESTPKGS)` -lm -o tests/test_luaconf
	./tests/test_luaconf

clean:
	rm -f rwm *.o *-protocol.h default_config.h tests/test_luaconf

dist: clean
	mkdir -p rwm-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md client.h config.def.lua \
		config.mk protocols rwm.1 rwm.c luaconf.c luaconf.h tests ime.h util.h rwm.desktop \
		rwm-$(VERSION)
	tar -caf rwm-$(VERSION).tar.gz rwm-$(VERSION)
	rm -rf rwm-$(VERSION)

install: rwm
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/rwm
	cp -f rwm $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/rwm
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f rwm.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/rwm.1
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f rwm.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/rwm.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/rwm.desktop
	mkdir -p $(DESTDIR)$(DATADIR)/rwm
	cp -f config.def.lua $(DESTDIR)$(DATADIR)/rwm/config.lua
	chmod 644 $(DESTDIR)$(DATADIR)/rwm/config.lua
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/rwm $(DESTDIR)$(MANDIR)/man1/rwm.1 \
		$(DESTDIR)$(DATADIR)/wayland-sessions/rwm.desktop \
		$(DESTDIR)$(DATADIR)/rwm/config.lua

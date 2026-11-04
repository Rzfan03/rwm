# rwm

A small Wayland compositor built on [wlroots], forked from
[dwl](https://codeberg.org/dwl/dwl) ("dwm for Wayland"). It keeps dwl's small, readable
C core and adds Lua configuration, multi-monitor setup, animations, gaps and extra
layouts.

[rwm-upstream]: https://github.com/Rzfan03/rwm

## Features

- **Lua config** (`~/.config/rwm/config.lua`), no recompiling to change settings
  - live reload: `Mod+Shift+r` or `pkill -USR1 rwm`
  - typos and bad values are reported (log + notification); a broken file never
    leaves you without a working session
  - `rwm -t` checks a config, `rwm -g` prints the default one
- **Monitors made easy**: mode (`"2560x1440@144"`, `"highres"`), scale, rotation,
  VRR, enable/disable, and positions that can be relative (`"right-of:eDP-1"` with
  `align`), matched by name or description, re-resolved on hotplug
- **Animations**: windows glide on layout changes, new windows zoom and fade in,
  closing windows fade out, workspace switches slide; duration and easing curve
  are configurable and frame-synced
- **Gaps**: inner/outer, smart gaps, adjustable at runtime
- **Layouts**: tile, float, monocle, grid, spiral, dwindle, centered master,
  bottom stack, deck, columns
- Window rules, autostart, environment variables, keybindings (keys, mouse,
  scroll wheel) and libinput settings, all in the same file

## Dependencies

- wlroots-0.20 or newer
- wayland, wayland-protocols, libinput, xkbcommon, pkg-config
- **Lua 5.4** (5.3 and LuaJIT also work: set `LUA_PKG` in `config.mk`)
- Optional, for XWayland: xcb, xcb-icccm and the `Xwayland` binary (uncomment the
  `XWAYLAND` / `XLIBS` lines in `config.mk`)

## Build and install

    make
    sudo make install
    make test       # unit tests for the config loader, no wlroots needed
    make clean      # remove build artifacts

The default keybinds are in `man rwm`; all the Lua options are in `config.def.lua`.

## Run

Try it nested inside another Wayland or X11 session first:

    mkdir -p ~/.config/rwm && rwm -g > ~/.config/rwm/config.lua
    ./rwm -s foot

The default modifier is `ALT` so it does not clash with the host compositor while
testing. For real use, change `local mod = "ALT"` to `"SUPER"` at the top of the
config.

## A taste of the config

```lua
local mod = "SUPER"
return {
  gaps       = { inner = 10, outer = 14 },
  animations = { duration = 200, curve = "ease-out-back" },
  monitors = {
    { name = "eDP-1",    scale = 1.5, position = "0x0" },
    { name = "HDMI-A-1", mode = "2560x1440@144",
      position = "right-of:eDP-1", align = "center", vrr = true },
  },
  rules = { { app_id = "pavucontrol", floating = true } },
  autostart = { "waybar" },
}
```

Everything else keeps its default. See `config.def.lua` or `man rwm` for all options.

## Not (yet) done

Pointer/touchpad settings, cursor theme, tag count and log level need a restart to
change; everything else reloads live.

## Credits

rwm is a fork of [dwl] by Simon Janak and the dwl contributors, which began by
extending [TinyWL], the example compositor by the [sway]/[wlroots] developers,
and borrows ideas from [dwm] and sway.

Original copyright notices are preserved in `LICENSE`, `LICENSE.dwm`,
`LICENSE.sway` and `LICENSE.tinywl`. `CHANGELOG.md` is the upstream dwl changelog.

Maintained by [Rzfan03](https://github.com/Rzfan03).

[dwl]: https://codeberg.org/dwl/dwl
[TinyWL]: https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl
[sway]: https://github.com/swaywm/sway
[dwm]: https://dwm.suckless.org
[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots

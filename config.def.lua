-- rwm configuration (Lua)
--
-- Copy this file to ~/.config/rwm/config.lua and edit it.
-- Print it again at any time with:  rwm -g
-- Reload it without restarting:     press mod+Shift+r  (or run `pkill -USR1 rwm`)
--
-- The file must `return` a table. Every key is optional: whatever you leave
-- out keeps the value from this default file. Lists (binds, monitors, rules,
-- mouse, scroll, autostart) REPLACE the default list when you provide them.
-- Unknown keys are reported as warnings, so typos do not go unnoticed.

---------------------------------------------------------------------------
-- Variables (plain Lua, use anything you like)
---------------------------------------------------------------------------
-- "ALT" is safe while testing nested inside another compositor.
-- Switch to "SUPER" (the windows key) once rwm is your real session.
local mod      = "ALT"
local terminal = "foot"
local launcher = "wmenu-run"

---------------------------------------------------------------------------
-- Keybindings:  { "MODS", "key", "action", argument }
--   MODS     any of SHIFT CTRL ALT SUPER joined with + or |  ("" for none)
--   key      an xkbcommon keysym name: j, Return, space, comma, F1, ...
--   action   see the list at the bottom of this file
---------------------------------------------------------------------------
local binds = {
  { mod,                "p",      "spawn",            launcher },
  { mod .. "|SHIFT",    "Return", "spawn",            terminal },

  { mod,                "j",      "focusstack",       1 },
  { mod,                "k",      "focusstack",       -1 },
  { mod,                "i",      "incnmaster",       1 },
  { mod,                "d",      "incnmaster",       -1 },
  { mod,                "h",      "setmfact",         -0.05 },
  { mod,                "l",      "setmfact",         0.05 },
  { mod,                "Return", "zoom" },
  { mod,                "Tab",    "view" },           -- back to previous tags
  { mod .. "|SHIFT",    "c",      "killclient" },

  { mod,                "t",      "setlayout",        "tile" },
  { mod,                "f",      "setlayout",        "float" },
  { mod,                "m",      "setlayout",        "monocle" },
  { mod,                "g",      "setlayout",        "grid" },
  { mod,                "s",      "setlayout",        "spiral" },
  { mod,                "w",      "setlayout",        "dwindle" },
  { mod,                "u",      "setlayout",        "centered" },
  { mod,                "b",      "setlayout",        "bstack" },
  { mod,                "y",      "setlayout",        "deck" },
  { mod,                "c",      "setlayout",        "columns" },
  { mod,                "n",      "cyclelayout",      1 },
  { mod .. "|SHIFT",    "n",      "cyclelayout",      -1 },
  { mod,                "space",  "setlayout" },      -- toggle last layout
  { mod .. "|SHIFT",    "space",  "togglefloating" },
  { mod,                "e",      "togglefullscreen" },

  { mod,                "0",      "view",             "all" },
  { mod .. "|SHIFT",    "0",      "tag",              "all" },

  { mod,                "comma",  "focusmon",         "left" },
  { mod,                "period", "focusmon",         "right" },
  { mod .. "|SHIFT",    "comma",  "tagmon",           "left" },
  { mod .. "|SHIFT",    "period", "tagmon",           "right" },

  -- gaps
  { mod,                "minus",  "gaps",             -2 },
  { mod,                "equal",  "gaps",             2 },
  { mod .. "|SHIFT",    "g",      "togglegaps" },

  -- config
  { mod .. "|SHIFT",    "r",      "reload" },
  { mod .. "|SHIFT",    "q",      "quit" },

  -- emergency exit and VT switching (do not remove if you don't know what a VT is)
  { "CTRL|ALT",         "BackSpace", "quit" },
}

-- Tags 1..9: view / toggleview / move window / toggle window tag
for i = 1, 9 do
  local key = tostring(i)
  binds[#binds + 1] = { mod,                     key, "view",      i }
  binds[#binds + 1] = { mod .. "|CTRL",          key, "toggleview", i }
  binds[#binds + 1] = { mod .. "|SHIFT",         key, "tag",       i }
  binds[#binds + 1] = { mod .. "|CTRL|SHIFT",    key, "toggletag", i }
end

for n = 1, 12 do
  binds[#binds + 1] = { "CTRL|ALT", "F" .. n, "chvt", n }
end

---------------------------------------------------------------------------
-- The configuration table
---------------------------------------------------------------------------
return {

  general = {
    sloppyfocus = true,        -- focus follows the mouse
    snap        = 32,          -- px: floating windows snap to screen edges
    tags        = 9,           -- number of tags (1-31); changing needs a restart
    log_level   = "error",     -- "silent" | "error" | "info" | "debug"
    bypass_surface_visibility = false, -- idle inhibitors work even if hidden
  },

  appearance = {
    border_width = 2,
    colors = {                 -- "#rrggbb" or "#rrggbbaa"
      root          = "#222222",
      border        = "#444444",
      focus         = "#005577",
      urgent        = "#ff0000",
      fullscreen_bg = "#000000",
    },
  },

  gaps = {
    inner = 8,                 -- px between windows
    outer = 8,                 -- px between windows and the screen edge
    smart = false,             -- true: no gaps when only one window is tiled
  },

  animations = {
    enabled  = true,
    duration = 180,            -- milliseconds
    -- "linear" | "ease-out-cubic" | "ease-in-out" | "ease-out-back" | "ease-out-expo"
    curve    = "ease-out-cubic",
    move     = true,           -- windows glide when the layout changes
    open     = true,           -- new windows zoom in
    fade     = true,           -- new windows also fade in
    close              = true, -- closing windows fade out
    workspace          = true, -- workspace switch slides + fades
    workspace_distance = 8,    -- % of monitor width the slide travels
  },

  keyboard = {
    layout       = "",         -- e.g. "us", "de", "us,ru"
    variant      = "",
    model        = "",
    options      = "",         -- e.g. "ctrl:nocaps,grp:alt_shift_toggle"
    rules        = "",
    repeat_rate  = 25,
    repeat_delay = 600,
  },

  -- Pointer / touchpad (applied to every pointer device that supports it)
  input = {
    tap                  = true,
    tap_drag             = true,
    drag_lock            = true,
    natural_scroll       = false,
    disable_while_typing = true,
    left_handed          = false,
    middle_emulation     = false,
    scroll_method        = "2fg",          -- "none" | "2fg" | "edge" | "button"
    click_method         = "button_areas", -- "none" | "button_areas" | "clickfinger"
    send_events          = "enabled",      -- "enabled" | "disabled" | "disabled_on_external_mouse"
    accel_profile        = "adaptive",     -- "flat" | "adaptive"
    accel_speed          = 0.0,            -- -1.0 .. 1.0
    tap_button_map       = "lrm",          -- "lrm" | "lmr"
  },

  cursor = {
    theme = "",                -- "" = default theme
    size  = 24,
  },

  ---------------------------------------------------------------------------
  -- Monitors. The first matching entry wins; "*" matches everything.
  --   name      output name (see `wlr-randr`), exact match, or "*"
  --   desc      substring of the monitor description (alternative to name)
  --   enabled   false turns the output off
  --   mode      "preferred" | "highres" | "highrr" | "1920x1080" | "1920x1080@144"
  --   scale     1, 1.25, 1.5, 2, ...
  --   transform "normal" | "90" | "180" | "270" | "flipped" | "flipped-90" ...
  --   position  "auto" | "1920x0" | {1920, 0}
  --             | "right-of:NAME" | "left-of:NAME" | "above:NAME" | "below:NAME"
  --   align     with relative positions: "start" | "center" | "end"
  --   vrr       true = adaptive sync (FreeSync / G-Sync compatible)
  --   layout    "tile" | "float" | "monocle" | "grid" | "spiral" | "dwindle"
  --             | "centered" | "bstack" | "deck" | "columns"   (initial layout)
  --   mfact, nmaster   initial master area settings
  --
  -- A keyed form is also accepted:
  --   monitors = { ["HDMI-A-1"] = { mode = "1920x1080@60", position = "right-of:eDP-1" } }
  --
  -- Examples (uncomment and adjust):
  --   { name = "eDP-1",    scale = 1.5, position = "0x0" },
  --   { name = "HDMI-A-1", mode = "2560x1440@144", position = "right-of:eDP-1", align = "center" },
  --   { name = "DP-2",     position = "above:HDMI-A-1", enabled = true },
  ---------------------------------------------------------------------------
  monitors = {
    { name = "*", scale = 1, layout = "tile", mfact = 0.55, nmaster = 1 },
  },

  ---------------------------------------------------------------------------
  -- Window rules. All matching rules apply, later ones override earlier ones.
  --   app_id    substring of the window app id (see `rwm -d` log or `wlrctl`)
  --   title     substring of the window title
  --   tags      tag number or list of numbers, e.g. 9 or {2, 3}
  --   floating  true / false
  --   monitor   monitor name ("HDMI-A-1") or 1-based index
  --
  -- Examples:
  --   { app_id = "pavucontrol", floating = true },
  --   { app_id = "firefox",     tags = 2 },
  --   { app_id = "mpv",         floating = true, monitor = "HDMI-A-1" },
  ---------------------------------------------------------------------------
  rules = {},

  binds = binds,

  -- Mouse buttons: { "MODS", "left|right|middle|side|extra", "move|resize|<action>", arg }
  mouse = {
    { mod, "left",   "move" },
    { mod, "middle", "togglefloating" },
    { mod, "right",  "resize" },
  },

  -- Scroll wheel: { "MODS", "up|down|left|right", "action", arg }
  -- Example: { mod, "up", "spawn", "pactl set-sink-volume @DEFAULT_SINK@ +5%" },
  scroll = {},

  -- Started once when rwm starts. A string runs through `sh -c`,
  -- a table is an argument vector (no shell).
  -- Examples:
  --   "waybar",
  --   { "swaybg", "-i", "/path/to/wallpaper.png" },
  autostart = {},

  -- Environment variables for rwm and everything it starts.
  -- Example: env = { QT_QPA_PLATFORM = "wayland", MOZ_ENABLE_WAYLAND = "1" },
  env = {},
}

--[[ Actions
  spawn <string|{argv}>      run a command
  focusstack <+1|-1>         focus next / previous window
  incnmaster <+1|-1>         more / fewer windows in the master area
  setmfact <delta>           resize the master area (e.g. 0.05, -0.05;
                             a value above 1 sets it absolutely: 1.55 = 0.55)
  zoom                       swap focused window with master
  view [tag|"all"]           show a tag (no argument: previous tags)
  toggleview <tag>           add/remove a tag from the view
  tag <tag|"all">            move the focused window to a tag
  toggletag <tag>            add/remove a tag on the focused window
  killclient (close)         close the focused window
  setlayout [name]           "tile" "float" "monocle" "grid" "spiral" "dwindle"
                             "centered" "bstack" "deck" "columns" (none: toggle)
  cyclelayout <+1|-1>        next / previous layout in that list
  togglefloating (float)     float / tile the focused window
  togglefullscreen (fullscreen)
  focusmon <left|right|up|down>
  tagmon <left|right|up|down>   send the focused window to another monitor
  gaps <delta>               grow/shrink the gaps by delta px (can be negative)
  togglegaps                 turn gaps on/off
  reload                     reload this file
  chvt <n>                   switch to virtual terminal n
  quit                       exit rwm
  move / resize              mouse binds only
]]

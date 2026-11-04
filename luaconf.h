/*
 * luaconf.h - Lua configuration loader for rwm.
 *
 * This module only depends on Lua, xkbcommon, libinput and wayland-server's
 * generated enums, never on wlroots, so it can be built and tested on its own.
 */
#ifndef LUACONF_H
#define LUACONF_H

#include <stddef.h>
#include <stdint.h>

enum { CFG_ARG_NONE, CFG_ARG_NUM, CFG_ARG_STR, CFG_ARG_ARGV };
enum { CFG_POS_AUTO, CFG_POS_ABS, CFG_POS_REL };
enum { CFG_REL_RIGHT, CFG_REL_LEFT, CFG_REL_ABOVE, CFG_REL_BELOW };
enum { CFG_ALIGN_START, CFG_ALIGN_CENTER, CFG_ALIGN_END };
enum { CFG_MODE_PREFERRED, CFG_MODE_EXPLICIT, CFG_MODE_HIGHRES, CFG_MODE_HIGHRR };
enum { CFG_CURVE_LINEAR, CFG_CURVE_OUT_CUBIC, CFG_CURVE_IN_OUT,
	CFG_CURVE_OUT_BACK, CFG_CURVE_OUT_EXPO };

/* Modifier bits, identical to enum wlr_keyboard_modifier */
#define CFG_MOD_SHIFT 1u
#define CFG_MOD_CAPS  2u
#define CFG_MOD_CTRL  4u
#define CFG_MOD_ALT   8u
#define CFG_MOD_MOD2  16u
#define CFG_MOD_MOD3  32u
#define CFG_MOD_LOGO  64u
#define CFG_MOD_MOD5  128u

#define CFG_MAX_TAGS 31

typedef struct {
	uint32_t mod;
	uint32_t code;      /* keysym, button code, or axis direction */
	char *action;
	int argtype;
	double num;
	char *str;
	char **argv;        /* NULL-terminated when argtype == CFG_ARG_ARGV */
	int argc;
} CfgBind;

typedef struct {
	char *name;         /* exact output name, NULL = any */
	char *desc;         /* substring of the output description, or NULL */
	int enabled;
	int mode_kind;
	int mode_w, mode_h;
	int mode_mhz;       /* 0 = highest refresh for the resolution */
	float scale;
	int transform;      /* enum wl_output_transform */
	int pos_kind;
	int x, y;
	int rel_dir, align;
	char *rel_ref;
	char *layout;       /* NULL = default */
	float mfact;        /* 0 = default */
	int nmaster;        /* -1 = default */
	int vrr;            /* -1 = leave alone, 0 off, 1 on */
} CfgMonitor;

typedef struct {
	char *id, *title;
	uint32_t tags;
	int floating;
	int monitor;        /* 0-based index, -1 = none */
	char *monitor_name;
} CfgRule;

typedef struct {
	char *sh;           /* run through sh -c, or NULL */
	char **argv;        /* NULL-terminated, or NULL */
	int argc;
} CfgCmd;

typedef struct {
	char *name, *value;
} CfgEnv;

typedef struct {
	/* general */
	int sloppyfocus, bypass_surface_visibility;
	unsigned int snap;
	int tagcount;
	int log_level;      /* 0 silent, 1 error, 2 info, 3 debug */

	/* appearance */
	unsigned int borderpx;
	float rootcolor[4], bordercolor[4], focuscolor[4], urgentcolor[4],
		fullscreen_bg[4];
	int gap_inner, gap_outer, smartgaps;

	/* animations */
	int anim_enabled, anim_duration, anim_curve, anim_move, anim_open, anim_fade,
		anim_close, anim_workspace, anim_ws_dist;

	/* keyboard */
	char *xkb_rules, *xkb_model, *xkb_layout, *xkb_variant, *xkb_options;
	int repeat_rate, repeat_delay;

	/* input */
	int tap, tap_drag, drag_lock, natural_scroll, dwt, left_handed,
		middle_emulation;
	int scroll_method, click_method, accel_profile, tap_map;
	uint32_t send_events;
	double accel_speed;

	/* cursor */
	char *cursor_theme;
	int cursor_size;

	/* lists */
	CfgMonitor *mons;   int nmons;
	CfgRule *rules;     int nrules;
	CfgBind *keys;      int nkeys;
	CfgBind *buttons;   int nbuttons;
	CfgBind *axes;      int naxes;
	CfgCmd *autostart;  int nautostart;
	CfgEnv *env;        int nenv;

	char warnings[4096];
} Config;

/* Returns the path of the config file to use, or NULL when none exists.
 * Order: $RWM_CONFIG, $XDG_CONFIG_HOME/rwm/config.lua,
 * ~/.config/rwm/config.lua, /etc/rwm/config.lua. Caller frees. */
char *cfg_find_path(void);

/* Load the built-in default (defsrc), then overlay the file at `path` if
 * non-NULL. On failure returns NULL and writes a message to err. Non-fatal
 * problems (unknown keys, bad values) are collected in Config.warnings. */
Config *cfg_load(const char *path, const char *defsrc, const char *version,
		char *err, size_t errlen);
void cfg_free(Config *c);

#endif

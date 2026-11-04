/*
 * luaconf.c - Lua configuration loader for rwm. See luaconf.h.
 *
 * The config file is a Lua script that returns a table. The built-in default
 * config is evaluated first, then the user's file is evaluated on top of the
 * same Config: scalar keys that are present overwrite, list keys that are
 * present replace the whole list.
 */
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>
#include <wayland-server-protocol.h>
#include <linux/input-event-codes.h>

#include "luaconf.h"

#if LUA_VERSION_NUM < 502
#define lua_rawlen lua_objlen
#endif

typedef struct {
	lua_State *L;
	Config *c;
	char sect[96];
} P;

typedef struct {
	const char *name;
	int val;
} Enum;

/* ---------------------------------------------------------------- helpers */

static void *
xcalloc(size_t n, size_t sz)
{
	void *p = calloc(n ? n : 1, sz ? sz : 1);
	if (!p)
		abort();
	return p;
}

static void *
xrealloc(void *old, size_t sz)
{
	void *p = realloc(old, sz ? sz : 1);
	if (!p)
		abort();
	return p;
}

static char *
xstrdup(const char *s)
{
	char *r = strdup(s);
	if (!r)
		abort();
	return r;
}

static void
pwarn(P *p, const char *fmt, ...)
{
	char msg[400];
	va_list ap;
	size_t used = strlen(p->c->warnings);

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
	snprintf(p->c->warnings + used, sizeof(p->c->warnings) - used,
			"%s: %s\n", p->sect, msg);
}

static void
setsect(P *p, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(p->sect, sizeof(p->sect), fmt, ap);
	va_end(ap);
}

/* replace *dst by a copy of s; empty or NULL becomes NULL */
static void
setstr(char **dst, const char *s)
{
	free(*dst);
	*dst = (s && *s) ? xstrdup(s) : NULL;
}

static int
o_bool(P *p, int t, const char *k, int *out)
{
	int r = 0;

	lua_getfield(p->L, t, k);
	if (lua_isboolean(p->L, -1)) {
		*out = lua_toboolean(p->L, -1);
		r = 1;
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'%s' must be true or false", k);
	}
	lua_pop(p->L, 1);
	return r;
}

static int
o_num(P *p, int t, const char *k, double *out, double min, double max)
{
	int r = 0;
	double v;

	lua_getfield(p->L, t, k);
	if (lua_type(p->L, -1) == LUA_TNUMBER) {
		v = lua_tonumber(p->L, -1);
		if (v < min || v > max) {
			pwarn(p, "'%s' must be between %g and %g (got %g)", k, min, max, v);
			v = v < min ? min : max;
		}
		*out = v;
		r = 1;
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'%s' must be a number", k);
	}
	lua_pop(p->L, 1);
	return r;
}

static int
o_int(P *p, int t, const char *k, int *out, int min, int max)
{
	double v;
	if (!o_num(p, t, k, &v, min, max))
		return 0;
	*out = (int)lround(v);
	return 1;
}

static int
o_str(P *p, int t, const char *k, char **dst)
{
	int r = 0;

	lua_getfield(p->L, t, k);
	if (lua_type(p->L, -1) == LUA_TSTRING) {
		setstr(dst, lua_tostring(p->L, -1));
		r = 1;
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'%s' must be a string", k);
	}
	lua_pop(p->L, 1);
	return r;
}

static int
find_enum(const Enum *e, const char *s, int *out)
{
	for (; e->name; e++)
		if (!strcasecmp(s, e->name)) {
			*out = e->val;
			return 1;
		}
	return 0;
}

static void
warn_enum(P *p, const char *k, const char *got, const Enum *e)
{
	char list[256] = "";
	size_t n = 0;

	for (; e->name && n < sizeof(list) - 24; e++)
		n += (size_t)snprintf(list + n, sizeof(list) - n, "%s\"%s\"",
				n ? ", " : "", e->name);
	pwarn(p, "'%s' has invalid value \"%s\" (use one of: %s)", k, got, list);
}

static int
o_enum(P *p, int t, const char *k, const Enum *e, int *out)
{
	int r = 0;
	const char *s;

	lua_getfield(p->L, t, k);
	if (lua_type(p->L, -1) == LUA_TSTRING) {
		s = lua_tostring(p->L, -1);
		if (find_enum(e, s, out))
			r = 1;
		else
			warn_enum(p, k, s, e);
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'%s' must be a string", k);
	}
	lua_pop(p->L, 1);
	return r;
}

static int
parse_color(const char *s, float out[4])
{
	size_t n, i;
	unsigned long v;

	if (*s == '#')
		s++;
	else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	n = strlen(s);
	if (n != 6 && n != 8)
		return -1;
	for (i = 0; i < n; i++)
		if (!isxdigit((unsigned char)s[i]))
			return -1;
	v = strtoul(s, NULL, 16);
	if (n == 6)
		v = (v << 8) | 0xff;
	out[0] = (float)((v >> 24) & 0xff) / 255.0f;
	out[1] = (float)((v >> 16) & 0xff) / 255.0f;
	out[2] = (float)((v >> 8) & 0xff) / 255.0f;
	out[3] = (float)(v & 0xff) / 255.0f;
	return 0;
}

static int
o_color(P *p, int t, const char *k, float out[4])
{
	int r = 0;
	float tmp[4];
	unsigned long v;

	lua_getfield(p->L, t, k);
	if (lua_type(p->L, -1) == LUA_TSTRING) {
		if (parse_color(lua_tostring(p->L, -1), tmp) == 0) {
			memcpy(out, tmp, sizeof(tmp));
			r = 1;
		} else {
			pwarn(p, "'%s' is not a color, use \"#rrggbb\" or \"#rrggbbaa\"", k);
		}
	} else if (lua_type(p->L, -1) == LUA_TNUMBER) {
		v = (unsigned long)lua_tonumber(p->L, -1);
		out[0] = (float)((v >> 24) & 0xff) / 255.0f;
		out[1] = (float)((v >> 16) & 0xff) / 255.0f;
		out[2] = (float)((v >> 8) & 0xff) / 255.0f;
		out[3] = (float)(v & 0xff) / 255.0f;
		r = 1;
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'%s' must be a color string", k);
	}
	lua_pop(p->L, 1);
	return r;
}

static void
check_keys(P *p, int t, const char *const *allowed)
{
	const char *k;
	const char *const *a;
	int ok;

	lua_pushnil(p->L);
	while (lua_next(p->L, t)) {
		if (lua_type(p->L, -2) == LUA_TSTRING) {
			k = lua_tostring(p->L, -2);
			ok = 0;
			for (a = allowed; *a; a++)
				if (!strcmp(*a, k))
					ok = 1;
			if (!ok)
				pwarn(p, "unknown key '%s'", k);
		}
		lua_pop(p->L, 1);
	}
}

/* Pushes root[name] if it's a table and returns its absolute index, else 0 */
static int
get_table(P *p, int root, const char *name, const char *sect)
{
	lua_getfield(p->L, root, name);
	if (lua_isnil(p->L, -1)) {
		lua_pop(p->L, 1);
		return 0;
	}
	setsect(p, "%s", sect);
	if (!lua_istable(p->L, -1)) {
		pwarn(p, "must be a table");
		lua_pop(p->L, 1);
		return 0;
	}
	return lua_gettop(p->L);
}

/* ------------------------------------------------------------ enum tables */

static const Enum E_LOG[] = {
	{"silent", 0}, {"error", 1}, {"info", 2}, {"debug", 3}, {NULL, 0}};
static const Enum E_CURVE[] = {
	{"linear", CFG_CURVE_LINEAR}, {"ease-out-cubic", CFG_CURVE_OUT_CUBIC},
	{"ease-in-out", CFG_CURVE_IN_OUT}, {"ease-out-back", CFG_CURVE_OUT_BACK},
	{"ease-out-expo", CFG_CURVE_OUT_EXPO}, {NULL, 0}};
static const Enum E_SCROLL[] = {
	{"none", LIBINPUT_CONFIG_SCROLL_NO_SCROLL},
	{"2fg", LIBINPUT_CONFIG_SCROLL_2FG},
	{"edge", LIBINPUT_CONFIG_SCROLL_EDGE},
	{"button", LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN}, {NULL, 0}};
static const Enum E_CLICK[] = {
	{"none", LIBINPUT_CONFIG_CLICK_METHOD_NONE},
	{"button_areas", LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS},
	{"clickfinger", LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER}, {NULL, 0}};
static const Enum E_SENDEV[] = {
	{"enabled", LIBINPUT_CONFIG_SEND_EVENTS_ENABLED},
	{"disabled", LIBINPUT_CONFIG_SEND_EVENTS_DISABLED},
	{"disabled_on_external_mouse",
		LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE}, {NULL, 0}};
static const Enum E_ACCEL[] = {
	{"flat", LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT},
	{"adaptive", LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE}, {NULL, 0}};
static const Enum E_TAPMAP[] = {
	{"lrm", LIBINPUT_CONFIG_TAP_MAP_LRM},
	{"lmr", LIBINPUT_CONFIG_TAP_MAP_LMR}, {NULL, 0}};
static const Enum E_TRANSFORM[] = {
	{"normal", WL_OUTPUT_TRANSFORM_NORMAL}, {"0", WL_OUTPUT_TRANSFORM_NORMAL},
	{"90", WL_OUTPUT_TRANSFORM_90}, {"180", WL_OUTPUT_TRANSFORM_180},
	{"270", WL_OUTPUT_TRANSFORM_270},
	{"flipped", WL_OUTPUT_TRANSFORM_FLIPPED},
	{"flipped-90", WL_OUTPUT_TRANSFORM_FLIPPED_90},
	{"flipped-180", WL_OUTPUT_TRANSFORM_FLIPPED_180},
	{"flipped-270", WL_OUTPUT_TRANSFORM_FLIPPED_270}, {NULL, 0}};
static const Enum E_ALIGN[] = {
	{"start", CFG_ALIGN_START}, {"center", CFG_ALIGN_CENTER},
	{"end", CFG_ALIGN_END}, {NULL, 0}};
static const Enum E_LAYOUT[] = {
	{"tile", 0}, {"float", 1}, {"monocle", 2}, {"grid", 3},
	{"spiral", 4}, {"dwindle", 5}, {"centered", 6}, {"bstack", 7}, {"deck", 8},
	{"columns", 9}, {NULL, 0}};
static const Enum E_BUTTON[] = {
	{"left", BTN_LEFT}, {"right", BTN_RIGHT}, {"middle", BTN_MIDDLE},
	{"side", BTN_SIDE}, {"extra", BTN_EXTRA}, {"forward", BTN_FORWARD},
	{"back", BTN_BACK}, {NULL, 0}};
static const Enum E_AXIS[] = {
	{"up", 0}, {"right", 1}, {"down", 2}, {"left", 3}, {NULL, 0}};
static const Enum E_MODS[] = {
	{"shift", CFG_MOD_SHIFT}, {"ctrl", CFG_MOD_CTRL}, {"control", CFG_MOD_CTRL},
	{"alt", CFG_MOD_ALT}, {"mod1", CFG_MOD_ALT}, {"super", CFG_MOD_LOGO},
	{"logo", CFG_MOD_LOGO}, {"win", CFG_MOD_LOGO}, {"mod4", CFG_MOD_LOGO},
	{"mod2", CFG_MOD_MOD2}, {"mod3", CFG_MOD_MOD3}, {"mod5", CFG_MOD_MOD5},
	{"none", 0}, {NULL, 0}};

/* --------------------------------------------------------------- sections */

static void
parse_general(P *p, int t)
{
	static const char *const keys[] = {"sloppyfocus", "snap", "tags",
		"log_level", "bypass_surface_visibility", NULL};
	Config *c = p->c;
	int v;

	check_keys(p, t, keys);
	o_bool(p, t, "sloppyfocus", &c->sloppyfocus);
	o_bool(p, t, "bypass_surface_visibility", &c->bypass_surface_visibility);
	if (o_int(p, t, "snap", &v, 0, 10000))
		c->snap = (unsigned int)v;
	o_int(p, t, "tags", &c->tagcount, 1, CFG_MAX_TAGS);
	o_enum(p, t, "log_level", E_LOG, &c->log_level);
}

static void
parse_appearance(P *p, int t)
{
	static const char *const keys[] = {"border_width", "colors", NULL};
	static const char *const ckeys[] = {"root", "border", "focus", "urgent",
		"fullscreen_bg", NULL};
	Config *c = p->c;
	int v, top, ct;

	check_keys(p, t, keys);
	if (o_int(p, t, "border_width", &v, 0, 100))
		c->borderpx = (unsigned int)v;

	top = lua_gettop(p->L);
	if ((ct = get_table(p, t, "colors", "appearance.colors"))) {
		check_keys(p, ct, ckeys);
		o_color(p, ct, "root", c->rootcolor);
		o_color(p, ct, "border", c->bordercolor);
		o_color(p, ct, "focus", c->focuscolor);
		o_color(p, ct, "urgent", c->urgentcolor);
		o_color(p, ct, "fullscreen_bg", c->fullscreen_bg);
	}
	lua_settop(p->L, top);
}

static void
parse_gaps(P *p, int t)
{
	static const char *const keys[] = {"inner", "outer", "smart", NULL};
	Config *c = p->c;

	check_keys(p, t, keys);
	o_int(p, t, "inner", &c->gap_inner, 0, 1000);
	o_int(p, t, "outer", &c->gap_outer, 0, 1000);
	o_bool(p, t, "smart", &c->smartgaps);
}

static void
parse_animations(P *p, int t)
{
	static const char *const keys[] = {"enabled", "duration", "curve", "move",
		"open", "fade", "close", "workspace", "workspace_distance", NULL};
	Config *c = p->c;

	check_keys(p, t, keys);
	o_bool(p, t, "enabled", &c->anim_enabled);
	o_int(p, t, "duration", &c->anim_duration, 0, 5000);
	o_enum(p, t, "curve", E_CURVE, &c->anim_curve);
	o_bool(p, t, "move", &c->anim_move);
	o_bool(p, t, "open", &c->anim_open);
	o_bool(p, t, "fade", &c->anim_fade);
	o_bool(p, t, "close", &c->anim_close);
	o_bool(p, t, "workspace", &c->anim_workspace);
	o_int(p, t, "workspace_distance", &c->anim_ws_dist, 0, 50);
}

static void
parse_keyboard(P *p, int t)
{
	static const char *const keys[] = {"layout", "variant", "model", "options",
		"rules", "repeat_rate", "repeat_delay", NULL};
	Config *c = p->c;

	check_keys(p, t, keys);
	o_str(p, t, "layout", &c->xkb_layout);
	o_str(p, t, "variant", &c->xkb_variant);
	o_str(p, t, "model", &c->xkb_model);
	o_str(p, t, "options", &c->xkb_options);
	o_str(p, t, "rules", &c->xkb_rules);
	o_int(p, t, "repeat_rate", &c->repeat_rate, 0, 1000);
	o_int(p, t, "repeat_delay", &c->repeat_delay, 0, 10000);
}

static void
parse_input(P *p, int t)
{
	static const char *const keys[] = {"tap", "tap_drag", "drag_lock",
		"natural_scroll", "disable_while_typing", "left_handed",
		"middle_emulation", "scroll_method", "click_method", "send_events",
		"accel_profile", "accel_speed", "tap_button_map", NULL};
	Config *c = p->c;
	int v;

	check_keys(p, t, keys);
	o_bool(p, t, "tap", &c->tap);
	o_bool(p, t, "tap_drag", &c->tap_drag);
	o_bool(p, t, "drag_lock", &c->drag_lock);
	o_bool(p, t, "natural_scroll", &c->natural_scroll);
	o_bool(p, t, "disable_while_typing", &c->dwt);
	o_bool(p, t, "left_handed", &c->left_handed);
	o_bool(p, t, "middle_emulation", &c->middle_emulation);
	o_enum(p, t, "scroll_method", E_SCROLL, &c->scroll_method);
	o_enum(p, t, "click_method", E_CLICK, &c->click_method);
	if (o_enum(p, t, "send_events", E_SENDEV, &v))
		c->send_events = (uint32_t)v;
	o_enum(p, t, "accel_profile", E_ACCEL, &c->accel_profile);
	o_num(p, t, "accel_speed", &c->accel_speed, -1.0, 1.0);
	o_enum(p, t, "tap_button_map", E_TAPMAP, &c->tap_map);
}

static void
parse_cursor(P *p, int t)
{
	static const char *const keys[] = {"theme", "size", NULL};
	Config *c = p->c;

	check_keys(p, t, keys);
	o_str(p, t, "theme", &c->cursor_theme);
	o_int(p, t, "size", &c->cursor_size, 1, 256);
}

/* ------------------------------------------------------------------ binds */

static int
parse_mods(P *p, const char *s, uint32_t *out)
{
	char tok[32];
	size_t n;
	int v;

	*out = 0;
	while (*s) {
		while (*s && strchr("|+, \t", *s))
			s++;
		n = 0;
		while (*s && !strchr("|+, \t", *s)) {
			if (n < sizeof(tok) - 1)
				tok[n++] = *s;
			s++;
		}
		tok[n] = '\0';
		if (!n)
			continue;
		if (!find_enum(E_MODS, tok, &v)) {
			pwarn(p, "unknown modifier \"%s\" (use SHIFT, CTRL, ALT, SUPER)", tok);
			return -1;
		}
		*out |= (uint32_t)v;
	}
	return 0;
}

static void
free_bind(CfgBind *b)
{
	int i;

	free(b->action);
	free(b->str);
	for (i = 0; i < b->argc; i++)
		free(b->argv[i]);
	free(b->argv);
}

static void
free_binds(CfgBind **list, int *n)
{
	int i;

	for (i = 0; i < *n; i++)
		free_bind(&(*list)[i]);
	free(*list);
	*list = NULL;
	*n = 0;
}

/* Reads the list of argument strings of the table at index t.
 * Returns a NULL-terminated vector, or NULL if empty / invalid. */
static char **
read_argv(P *p, int t, int *argc)
{
	int i, n = (int)lua_rawlen(p->L, t);
	char **v;

	*argc = 0;
	if (n < 1)
		return NULL;
	v = xcalloc((size_t)n + 1, sizeof(*v));
	for (i = 1; i <= n; i++) {
		lua_rawgeti(p->L, t, i);
		if (lua_type(p->L, -1) != LUA_TSTRING && lua_type(p->L, -1) != LUA_TNUMBER) {
			pwarn(p, "argument %d must be a string", i);
			lua_pop(p->L, 1);
			while (i-- > 1)
				free(v[i - 1]);
			free(v);
			return NULL;
		}
		v[i - 1] = xstrdup(lua_tostring(p->L, -1));
		lua_pop(p->L, 1);
	}
	*argc = n;
	return v;
}

enum { KIND_KEY, KIND_BUTTON, KIND_AXIS };

static void
parse_binds(P *p, int t, const char *name, int kind, CfgBind **list, int *n)
{
	static const char *const fields[] = {"mods", "key", "action", "arg"};
	int i, count = (int)lua_rawlen(p->L, t), f, e, top;
	CfgBind b;
	const char *s;
	int code;

	free_binds(list, n);
	for (i = 1; i <= count; i++) {
		top = lua_gettop(p->L);
		setsect(p, "%s[%d]", name, i);
		lua_rawgeti(p->L, t, i);
		e = lua_gettop(p->L);
		if (!lua_istable(p->L, e)) {
			pwarn(p, "entry must be a table like { \"MOD\", \"key\", \"action\", arg }");
			lua_settop(p->L, top);
			continue;
		}
		/* fetch mods, key, action, arg (positional or named) onto the stack */
		for (f = 0; f < 4; f++) {
			lua_rawgeti(p->L, e, f + 1);
			if (lua_isnil(p->L, -1)) {
				lua_pop(p->L, 1);
				lua_getfield(p->L, e, fields[f]);
			}
		}
		memset(&b, 0, sizeof(b));

		if (lua_isnil(p->L, e + 1)) {
			b.mod = 0;
		} else if (lua_type(p->L, e + 1) != LUA_TSTRING) {
			pwarn(p, "modifiers must be a string like \"SUPER|SHIFT\"");
			lua_settop(p->L, top);
			continue;
		} else if (parse_mods(p, lua_tostring(p->L, e + 1), &b.mod) < 0) {
			lua_settop(p->L, top);
			continue;
		}

		if (lua_type(p->L, e + 2) != LUA_TSTRING
				&& lua_type(p->L, e + 2) != LUA_TNUMBER) {
			pwarn(p, "missing key name");
			lua_settop(p->L, top);
			continue;
		}
		s = lua_tostring(p->L, e + 2);
		if (kind == KIND_KEY) {
			xkb_keysym_t sym = xkb_keysym_from_name(s, XKB_KEYSYM_NO_FLAGS);
			if (sym == XKB_KEY_NoSymbol)
				sym = xkb_keysym_from_name(s, XKB_KEYSYM_CASE_INSENSITIVE);
			if (sym == XKB_KEY_NoSymbol) {
				pwarn(p, "unknown key \"%s\"", s);
				lua_settop(p->L, top);
				continue;
			}
			b.code = sym;
		} else {
			const Enum *tab = kind == KIND_BUTTON ? E_BUTTON : E_AXIS;
			if (!find_enum(tab, s, &code)) {
				warn_enum(p, kind == KIND_BUTTON ? "button" : "direction", s, tab);
				lua_settop(p->L, top);
				continue;
			}
			b.code = (uint32_t)code;
		}

		if (lua_type(p->L, e + 3) != LUA_TSTRING) {
			pwarn(p, "missing action name");
			lua_settop(p->L, top);
			continue;
		}
		b.action = xstrdup(lua_tostring(p->L, e + 3));

		switch (lua_type(p->L, e + 4)) {
		case LUA_TNUMBER:
			b.argtype = CFG_ARG_NUM;
			b.num = lua_tonumber(p->L, e + 4);
			break;
		case LUA_TBOOLEAN:
			b.argtype = CFG_ARG_NUM;
			b.num = lua_toboolean(p->L, e + 4) ? 1 : 0;
			break;
		case LUA_TSTRING:
			b.argtype = CFG_ARG_STR;
			b.str = xstrdup(lua_tostring(p->L, e + 4));
			break;
		case LUA_TTABLE:
			b.argv = read_argv(p, e + 4, &b.argc);
			if (b.argv)
				b.argtype = CFG_ARG_ARGV;
			break;
		default:
			break;
		}

		*list = xrealloc(*list, (size_t)(*n + 1) * sizeof(**list));
		(*list)[(*n)++] = b;
		lua_settop(p->L, top);
	}
}

/* ---------------------------------------------------------------- monitors */

static void
free_monitor(CfgMonitor *m)
{
	free(m->name);
	free(m->desc);
	free(m->rel_ref);
	free(m->layout);
}

static void
free_monitors(Config *c)
{
	int i;

	for (i = 0; i < c->nmons; i++)
		free_monitor(&c->mons[i]);
	free(c->mons);
	c->mons = NULL;
	c->nmons = 0;
}

static void
parse_mode(P *p, const char *s, CfgMonitor *m)
{
	int w, h, n;
	double hz;

	if (!strcasecmp(s, "preferred")) {
		m->mode_kind = CFG_MODE_PREFERRED;
	} else if (!strcasecmp(s, "highres")) {
		m->mode_kind = CFG_MODE_HIGHRES;
	} else if (!strcasecmp(s, "highrr")) {
		m->mode_kind = CFG_MODE_HIGHRR;
	} else if ((n = sscanf(s, "%dx%d@%lf", &w, &h, &hz)) >= 2 && w > 0 && h > 0) {
		m->mode_kind = CFG_MODE_EXPLICIT;
		m->mode_w = w;
		m->mode_h = h;
		m->mode_mhz = n == 3 ? (int)lround(hz * 1000.0) : 0;
	} else {
		pwarn(p, "invalid mode \"%s\" (use \"preferred\", \"highres\", "
				"\"highrr\", \"1920x1080\" or \"1920x1080@60\")", s);
	}
}

static void
parse_position(P *p, int t, CfgMonitor *m)
{
	static const struct { const char *prefix; int dir; } rel[] = {
		{"right-of:", CFG_REL_RIGHT}, {"left-of:", CFG_REL_LEFT},
		{"above:", CFG_REL_ABOVE}, {"below:", CFG_REL_BELOW}};
	const char *s;
	size_t i;
	int x, y, top = lua_gettop(p->L), hasxy = 0;
	double dx = 0, dy = 0;

	/* separate x / y fields */
	if (o_num(p, t, "x", &dx, -100000, 100000))
		hasxy = 1;
	if (o_num(p, t, "y", &dy, -100000, 100000))
		hasxy = 1;
	if (hasxy) {
		m->pos_kind = CFG_POS_ABS;
		m->x = (int)lround(dx);
		m->y = (int)lround(dy);
	}

	lua_getfield(p->L, t, "position");
	if (lua_type(p->L, -1) == LUA_TSTRING) {
		s = lua_tostring(p->L, -1);
		if (!strcasecmp(s, "auto")) {
			m->pos_kind = CFG_POS_AUTO;
		} else if (sscanf(s, "%dx%d", &x, &y) == 2 || sscanf(s, "%d,%d", &x, &y) == 2) {
			m->pos_kind = CFG_POS_ABS;
			m->x = x;
			m->y = y;
		} else {
			for (i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
				size_t n = strlen(rel[i].prefix);
				if (!strncasecmp(s, rel[i].prefix, n) && s[n]) {
					m->pos_kind = CFG_POS_REL;
					m->rel_dir = rel[i].dir;
					setstr(&m->rel_ref, s + n);
					break;
				}
			}
			if (i == sizeof(rel) / sizeof(rel[0]))
				pwarn(p, "invalid position \"%s\" (use \"auto\", \"1920x0\", "
						"\"right-of:NAME\", \"left-of:NAME\", \"above:NAME\", "
						"\"below:NAME\")", s);
		}
	} else if (lua_istable(p->L, -1)) {
		lua_rawgeti(p->L, -1, 1);
		lua_rawgeti(p->L, -2, 2);
		if (lua_type(p->L, -2) == LUA_TNUMBER && lua_type(p->L, -1) == LUA_TNUMBER) {
			m->pos_kind = CFG_POS_ABS;
			m->x = (int)lround(lua_tonumber(p->L, -2));
			m->y = (int)lround(lua_tonumber(p->L, -1));
		} else {
			pwarn(p, "position table must be { x, y }");
		}
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'position' must be a string or { x, y }");
	}
	lua_settop(p->L, top);
}

static void
parse_monitor_entry(P *p, int t, const char *keyname, CfgMonitor *m)
{
	static const char *const keys[] = {"name", "desc", "enabled", "mode",
		"scale", "transform", "position", "x", "y", "align", "vrr", "layout",
		"mfact", "nmaster", NULL};
	double d;
	int v, top = lua_gettop(p->L);
	char *name = NULL;

	check_keys(p, t, keys);
	m->enabled = 1;
	m->scale = 1.0f;
	m->nmaster = -1;
	m->vrr = -1;

	if (keyname)
		name = xstrdup(keyname);
	o_str(p, t, "name", &name);
	if (name && !strcmp(name, "*")) {
		free(name);
		name = NULL;
	}
	m->name = name;
	o_str(p, t, "desc", &m->desc);
	o_bool(p, t, "enabled", &m->enabled);

	lua_getfield(p->L, t, "mode");
	if (lua_type(p->L, -1) == LUA_TSTRING)
		parse_mode(p, lua_tostring(p->L, -1), m);
	else if (!lua_isnil(p->L, -1))
		pwarn(p, "'mode' must be a string");
	lua_pop(p->L, 1);

	if (o_num(p, t, "scale", &d, 0.1, 10.0))
		m->scale = (float)d;

	lua_getfield(p->L, t, "transform");
	if (lua_type(p->L, -1) == LUA_TNUMBER) {
		char tmp[16];
		snprintf(tmp, sizeof(tmp), "%d", (int)lround(lua_tonumber(p->L, -1)));
		if (!find_enum(E_TRANSFORM, tmp, &m->transform))
			pwarn(p, "'transform' must be 0, 90, 180 or 270 when numeric");
	} else if (lua_type(p->L, -1) == LUA_TSTRING) {
		const char *s = lua_tostring(p->L, -1);
		if (!find_enum(E_TRANSFORM, s, &m->transform))
			warn_enum(p, "transform", s, E_TRANSFORM);
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'transform' must be a string");
	}
	lua_pop(p->L, 1);

	parse_position(p, t, m);
	o_enum(p, t, "align", E_ALIGN, &m->align);
	o_bool(p, t, "vrr", &m->vrr);

	lua_getfield(p->L, t, "layout");
	if (lua_type(p->L, -1) == LUA_TSTRING) {
		const char *s = lua_tostring(p->L, -1);
		if (find_enum(E_LAYOUT, s, &v))
			setstr(&m->layout, s);
		else
			warn_enum(p, "layout", s, E_LAYOUT);
	} else if (!lua_isnil(p->L, -1)) {
		pwarn(p, "'layout' must be a string");
	}
	lua_pop(p->L, 1);

	if (o_num(p, t, "mfact", &d, 0.1, 0.9))
		m->mfact = (float)d;
	o_int(p, t, "nmaster", &m->nmaster, 0, 100);
	lua_settop(p->L, top);
}

static int
is_wild(const CfgMonitor *m)
{
	return !m->name && !m->desc;
}

static void
parse_monitors(P *p, int t)
{
	Config *c = p->c;
	CfgMonitor m, *sorted;
	int top, i, n = 0, ei;
	const char *keyname;

	free_monitors(c);
	lua_pushnil(p->L);
	while (lua_next(p->L, t)) {
		top = lua_gettop(p->L); /* value at top, key below */
		keyname = NULL;
		if (lua_type(p->L, top - 1) == LUA_TSTRING)
			keyname = lua_tostring(p->L, top - 1);
		ei = ++n;
		if (keyname)
			setsect(p, "monitors[\"%s\"]", keyname);
		else
			setsect(p, "monitors[%d]", ei);
		if (!lua_istable(p->L, top)) {
			pwarn(p, "entry must be a table");
		} else {
			memset(&m, 0, sizeof(m));
			parse_monitor_entry(p, top, keyname, &m);
			c->mons = xrealloc(c->mons, (size_t)(c->nmons + 1) * sizeof(*c->mons));
			c->mons[c->nmons++] = m;
		}
		lua_settop(p->L, top);
		lua_pop(p->L, 1); /* pop value, keep key for lua_next */
	}

	/* Wildcard entries go last so specific ones win; order is otherwise kept */
	if (c->nmons > 1) {
		sorted = xcalloc((size_t)c->nmons, sizeof(*sorted));
		n = 0;
		for (i = 0; i < c->nmons; i++)
			if (!is_wild(&c->mons[i]))
				sorted[n++] = c->mons[i];
		for (i = 0; i < c->nmons; i++)
			if (is_wild(&c->mons[i]))
				sorted[n++] = c->mons[i];
		free(c->mons);
		c->mons = sorted;
	}
}

/* ------------------------------------------------------------------ rules */

static void
free_rules(Config *c)
{
	int i;

	for (i = 0; i < c->nrules; i++) {
		free(c->rules[i].id);
		free(c->rules[i].title);
		free(c->rules[i].monitor_name);
	}
	free(c->rules);
	c->rules = NULL;
	c->nrules = 0;
}

static void
parse_rules(P *p, int t)
{
	static const char *const keys[] = {"app_id", "title", "tags", "floating",
		"monitor", NULL};
	Config *c = p->c;
	int i, n = (int)lua_rawlen(p->L, t), e, top, j, tag;
	CfgRule r;

	free_rules(c);
	for (i = 1; i <= n; i++) {
		top = lua_gettop(p->L);
		setsect(p, "rules[%d]", i);
		lua_rawgeti(p->L, t, i);
		e = lua_gettop(p->L);
		if (!lua_istable(p->L, e)) {
			pwarn(p, "entry must be a table");
			lua_settop(p->L, top);
			continue;
		}
		check_keys(p, e, keys);
		memset(&r, 0, sizeof(r));
		r.monitor = -1;
		o_str(p, e, "app_id", &r.id);
		o_str(p, e, "title", &r.title);
		o_bool(p, e, "floating", &r.floating);

		lua_getfield(p->L, e, "tags");
		if (lua_type(p->L, -1) == LUA_TNUMBER) {
			tag = (int)lround(lua_tonumber(p->L, -1));
			if (tag >= 1 && tag <= CFG_MAX_TAGS)
				r.tags |= 1u << (tag - 1);
			else
				pwarn(p, "tag %d is out of range (1-%d)", tag, CFG_MAX_TAGS);
		} else if (lua_istable(p->L, -1)) {
			int tt = lua_gettop(p->L), tn = (int)lua_rawlen(p->L, tt);
			for (j = 1; j <= tn; j++) {
				lua_rawgeti(p->L, tt, j);
				tag = (int)lround(lua_tonumber(p->L, -1));
				if (lua_type(p->L, -1) == LUA_TNUMBER && tag >= 1 && tag <= CFG_MAX_TAGS)
					r.tags |= 1u << (tag - 1);
				else
					pwarn(p, "tags[%d] must be a number from 1 to %d", j, CFG_MAX_TAGS);
				lua_pop(p->L, 1);
			}
		} else if (!lua_isnil(p->L, -1)) {
			pwarn(p, "'tags' must be a number or a list of numbers");
		}
		lua_pop(p->L, 1);

		lua_getfield(p->L, e, "monitor");
		if (lua_type(p->L, -1) == LUA_TNUMBER) {
			r.monitor = (int)lround(lua_tonumber(p->L, -1)) - 1;
			if (r.monitor < 0) {
				pwarn(p, "'monitor' index starts at 1");
				r.monitor = -1;
			}
		} else if (lua_type(p->L, -1) == LUA_TSTRING) {
			setstr(&r.monitor_name, lua_tostring(p->L, -1));
		} else if (!lua_isnil(p->L, -1)) {
			pwarn(p, "'monitor' must be a name or a number");
		}
		lua_pop(p->L, 1);

		if (!r.id && !r.title)
			pwarn(p, "rule has neither 'app_id' nor 'title' and matches every window");
		c->rules = xrealloc(c->rules, (size_t)(c->nrules + 1) * sizeof(*c->rules));
		c->rules[c->nrules++] = r;
		lua_settop(p->L, top);
	}
}

/* ------------------------------------------------------- autostart and env */

static void
free_autostart(Config *c)
{
	int i, j;

	for (i = 0; i < c->nautostart; i++) {
		free(c->autostart[i].sh);
		for (j = 0; j < c->autostart[i].argc; j++)
			free(c->autostart[i].argv[j]);
		free(c->autostart[i].argv);
	}
	free(c->autostart);
	c->autostart = NULL;
	c->nautostart = 0;
}

static void
parse_autostart(P *p, int t)
{
	Config *c = p->c;
	int i, n = (int)lua_rawlen(p->L, t), top, e;
	CfgCmd cmd;

	free_autostart(c);
	for (i = 1; i <= n; i++) {
		top = lua_gettop(p->L);
		setsect(p, "autostart[%d]", i);
		lua_rawgeti(p->L, t, i);
		e = lua_gettop(p->L);
		memset(&cmd, 0, sizeof(cmd));
		if (lua_type(p->L, e) == LUA_TSTRING) {
			cmd.sh = xstrdup(lua_tostring(p->L, e));
		} else if (lua_istable(p->L, e)) {
			cmd.argv = read_argv(p, e, &cmd.argc);
			if (!cmd.argv) {
				pwarn(p, "argument list is empty or invalid");
				lua_settop(p->L, top);
				continue;
			}
		} else {
			pwarn(p, "must be a command string or a list of arguments");
			lua_settop(p->L, top);
			continue;
		}
		c->autostart = xrealloc(c->autostart,
				(size_t)(c->nautostart + 1) * sizeof(*c->autostart));
		c->autostart[c->nautostart++] = cmd;
		lua_settop(p->L, top);
	}
}

static void
free_env(Config *c)
{
	int i;

	for (i = 0; i < c->nenv; i++) {
		free(c->env[i].name);
		free(c->env[i].value);
	}
	free(c->env);
	c->env = NULL;
	c->nenv = 0;
}

static void
parse_env(P *p, int t)
{
	Config *c = p->c;
	int top;

	free_env(c);
	setsect(p, "env");
	lua_pushnil(p->L);
	while (lua_next(p->L, t)) {
		top = lua_gettop(p->L);
		if (lua_type(p->L, top - 1) != LUA_TSTRING
				|| (lua_type(p->L, top) != LUA_TSTRING
					&& lua_type(p->L, top) != LUA_TNUMBER)) {
			pwarn(p, "entries must look like NAME = \"value\"");
		} else {
			c->env = xrealloc(c->env, (size_t)(c->nenv + 1) * sizeof(*c->env));
			c->env[c->nenv].name = xstrdup(lua_tostring(p->L, top - 1));
			c->env[c->nenv].value = xstrdup(lua_tostring(p->L, top));
			c->nenv++;
		}
		lua_pop(p->L, 1);
	}
}

/* ------------------------------------------------------------------- root */

static void
parse_root(P *p, int root)
{
	static const char *const keys[] = {"general", "appearance", "gaps",
		"animations", "keyboard", "input", "cursor", "monitors", "rules",
		"binds", "mouse", "scroll", "autostart", "env", NULL};
	static const struct {
		const char *name;
		void (*fn)(P *, int);
	} sections[] = {
		{"general", parse_general}, {"appearance", parse_appearance},
		{"gaps", parse_gaps}, {"animations", parse_animations},
		{"keyboard", parse_keyboard}, {"input", parse_input},
		{"cursor", parse_cursor}, {"monitors", parse_monitors},
		{"rules", parse_rules}, {"autostart", parse_autostart},
		{"env", parse_env},
	};
	size_t i;
	int t, top = lua_gettop(p->L);

	setsect(p, "config");
	check_keys(p, root, keys);

	for (i = 0; i < sizeof(sections) / sizeof(sections[0]); i++) {
		if ((t = get_table(p, root, sections[i].name, sections[i].name)))
			sections[i].fn(p, t);
		lua_settop(p->L, top);
	}
	if ((t = get_table(p, root, "binds", "binds")))
		parse_binds(p, t, "binds", KIND_KEY, &p->c->keys, &p->c->nkeys);
	lua_settop(p->L, top);
	if ((t = get_table(p, root, "mouse", "mouse")))
		parse_binds(p, t, "mouse", KIND_BUTTON, &p->c->buttons, &p->c->nbuttons);
	lua_settop(p->L, top);
	if ((t = get_table(p, root, "scroll", "scroll")))
		parse_binds(p, t, "scroll", KIND_AXIS, &p->c->axes, &p->c->naxes);
	lua_settop(p->L, top);
}

/* ------------------------------------------------------------- public API */

static void
cfg_defaults(Config *c)
{
	static const float root[4] = {0.133f, 0.133f, 0.133f, 1.0f};
	static const float border[4] = {0.267f, 0.267f, 0.267f, 1.0f};
	static const float focus[4] = {0.0f, 0.333f, 0.467f, 1.0f};
	static const float urgent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
	static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};

	c->sloppyfocus = 1;
	c->snap = 32;
	c->tagcount = 9;
	c->log_level = 1;
	c->borderpx = 1;
	memcpy(c->rootcolor, root, sizeof(root));
	memcpy(c->bordercolor, border, sizeof(border));
	memcpy(c->focuscolor, focus, sizeof(focus));
	memcpy(c->urgentcolor, urgent, sizeof(urgent));
	memcpy(c->fullscreen_bg, black, sizeof(black));
	c->anim_duration = 180;
	c->anim_ws_dist = 8;
	c->anim_curve = CFG_CURVE_OUT_CUBIC;
	c->repeat_rate = 25;
	c->repeat_delay = 600;
	c->tap = c->tap_drag = c->drag_lock = c->dwt = 1;
	c->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
	c->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	c->send_events = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	c->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
	c->tap_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
	c->cursor_size = 24;
}

void
cfg_free(Config *c)
{
	if (!c)
		return;
	free(c->xkb_rules);
	free(c->xkb_model);
	free(c->xkb_layout);
	free(c->xkb_variant);
	free(c->xkb_options);
	free(c->cursor_theme);
	free_monitors(c);
	free_rules(c);
	free_binds(&c->keys, &c->nkeys);
	free_binds(&c->buttons, &c->nbuttons);
	free_binds(&c->axes, &c->naxes);
	free_autostart(c);
	free_env(c);
	free(c);
}

/* Evaluate one chunk (file or built-in source) and apply it. 0 on success. */
static int
run_chunk(Config *c, const char *path, const char *src, const char *version,
		char *err, size_t errlen)
{
	lua_State *L = luaL_newstate();
	P p = {.L = L, .c = c};
	const char *slash;
	char dir[1024];
	int rc;

	if (!L) {
		snprintf(err, errlen, "cannot create Lua state");
		return -1;
	}
	luaL_openlibs(L);

	/* global table `rwm` with a few helpers, and require() next to the file */
	lua_newtable(L);
	lua_pushstring(L, version ? version : "");
	lua_setfield(L, -2, "version");
	if (path) {
		slash = strrchr(path, '/');
		if (slash) {
			snprintf(dir, sizeof(dir), "%.*s", (int)(slash - path), path);
			lua_pushstring(L, dir);
			lua_setfield(L, -2, "config_dir");
			lua_getglobal(L, "package");
			if (lua_istable(L, -1)) {
				lua_getfield(L, -1, "path");
				lua_pushfstring(L, "%s/?.lua;%s", dir,
						lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
				lua_setfield(L, -3, "path");
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
	}
	lua_setglobal(L, "rwm");

	rc = path ? luaL_loadfile(L, path)
		: luaL_loadbuffer(L, src, strlen(src), "=default config");
	if (rc != 0 || lua_pcall(L, 0, 1, 0) != 0) {
		snprintf(err, errlen, "%s", lua_tostring(L, -1));
		lua_close(L);
		return -1;
	}
	if (!lua_istable(L, -1)) {
		snprintf(err, errlen, "%s: the config must end with `return { ... }`",
				path ? path : "default config");
		lua_close(L);
		return -1;
	}
	parse_root(&p, lua_gettop(L));
	lua_close(L);
	return 0;
}

Config *
cfg_load(const char *path, const char *defsrc, const char *version,
		char *err, size_t errlen)
{
	Config *c = xcalloc(1, sizeof(*c));

	cfg_defaults(c);
	if (defsrc && run_chunk(c, NULL, defsrc, version, err, errlen) < 0) {
		cfg_free(c);
		return NULL;
	}
	if (path && run_chunk(c, path, NULL, version, err, errlen) < 0) {
		cfg_free(c);
		return NULL;
	}
	return c;
}

char *
cfg_find_path(void)
{
	const char *env, *xdg, *home;
	char buf[1024];

	if ((env = getenv("RWM_CONFIG")) && *env)
		return xstrdup(env);
	if ((xdg = getenv("XDG_CONFIG_HOME")) && *xdg) {
		snprintf(buf, sizeof(buf), "%s/rwm/config.lua", xdg);
		if (access(buf, R_OK) == 0)
			return xstrdup(buf);
	}
	if ((home = getenv("HOME")) && *home) {
		snprintf(buf, sizeof(buf), "%s/.config/rwm/config.lua", home);
		if (access(buf, R_OK) == 0)
			return xstrdup(buf);
	}
	if (access("/etc/rwm/config.lua", R_OK) == 0)
		return xstrdup("/etc/rwm/config.lua");
	return NULL;
}

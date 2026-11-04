/* Standalone tests for luaconf.c (no wlroots needed). Run with: make test */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../luaconf.h"
#include "../default_config.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
	fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static char *
write_tmp(const char *body)
{
	static char path[64];
	FILE *f;
	snprintf(path, sizeof(path), "/tmp/rwm-test-%d.lua", (int)getpid());
	f = fopen(path, "w");
	fputs(body, f);
	fclose(f);
	return path;
}

static const CfgBind *
findbind(const Config *c, const char *action, int argnum)
{
	int i;
	for (i = 0; i < c->nkeys; i++)
		if (!strcmp(c->keys[i].action, action)
				&& (argnum < 0 || (c->keys[i].argtype == CFG_ARG_NUM
					&& (int)c->keys[i].num == argnum)))
			return &c->keys[i];
	return NULL;
}

int
main(void)
{
	char err[512] = "";
	Config *c;
	const CfgBind *b;
	const char *user;

	/* ---- 1. built-in default only */
	c = cfg_load(NULL, default_config_lua, "test", err, sizeof(err));
	if (!c) { fprintf(stderr, "default failed: %s\n", err); return 1; }
	CHECK(c->warnings[0] == '\0');
	if (c->warnings[0]) fputs(c->warnings, stderr);
	CHECK(c->tagcount == 9);
	CHECK(c->borderpx == 2);
	CHECK(c->gap_inner == 8 && c->gap_outer == 8 && !c->smartgaps);
	CHECK(c->anim_enabled && c->anim_duration == 180 && c->anim_open && c->anim_fade);
	CHECK(c->anim_curve == CFG_CURVE_OUT_CUBIC);
	CHECK(fabsf(c->focuscolor[2] - 0.4667f) < 0.01f);
	CHECK(c->nmons == 1 && c->mons[0].name == NULL && c->mons[0].nmaster == 1);
	CHECK(c->nkeys > 60);                 /* 4*9 tag binds + chvt + misc */
	CHECK(c->nbuttons == 3 && c->naxes == 0);
	b = findbind(c, "view", 3);
	CHECK(b && b->mod == CFG_MOD_ALT);
	b = findbind(c, "spawn", -1);
	CHECK(b && b->argtype == CFG_ARG_STR && !strcmp(b->str, "wmenu-run"));
	b = findbind(c, "setmfact", -1);
	CHECK(b && fabs(b->num + 0.05) < 1e-9);
	b = findbind(c, "reload", -1);
	CHECK(b && b->mod == (CFG_MOD_ALT | CFG_MOD_SHIFT));
	cfg_free(c);

	/* ---- 2. user config overlay with deliberate mistakes */
	user =
		"local m = 'SUPER'\n"
		"return {\n"
		"  gaps = { inner = 20, outer = 'big', smarrt = true },\n"
		"  appearance = { border_width = 4, colors = { focus = '#ff8800' } },\n"
		"  animations = { enabled = false, curve = 'bouncy' },\n"
		"  keyboard = { layout = 'us,ru', options = 'grp:alt_shift_toggle' },\n"
		"  input = { natural_scroll = true, accel_speed = 5 },\n"
		"  monitors = {\n"
		"    { name = '*', scale = 1 },\n"
		"    { name = 'eDP-1', scale = 1.5, position = '0x0', mode = '1920x1080@60' },\n"
		"    ['HDMI-A-1'] = { mode = '2560x1440@143.97', position = 'right-of:eDP-1',\n"
		"                     align = 'center', vrr = true, layout = 'grid' },\n"
		"    { desc = 'Dell', transform = 90, enabled = false },\n"
		"  },\n"
		"  rules = {\n"
		"    { app_id = 'firefox', tags = {2, 3}, monitor = 'HDMI-A-1' },\n"
		"    { title = 'Picture', floating = true, monitor = 2 },\n"
		"  },\n"
		"  binds = {\n"
		"    { m, 'q', 'killclient' },\n"
		"    { m .. '+SHIFT', 'Return', 'spawn', { 'foot', '-e', 'htop' } },\n"
		"    { 'HYPER', 'x', 'quit' },\n"
		"    { m, 'notakey', 'quit' },\n"
		"    { mods = 'ALT', key = 'F1', action = 'chvt', arg = 1 },\n"
		"  },\n"
		"  mouse = { { m, 'left', 'move' }, { m, 'nobutton', 'move' } },\n"
		"  scroll = { { m, 'up', 'gaps', 2 } },\n"
		"  autostart = { 'waybar', { 'swaybg', '-i', '/x.png' } },\n"
		"  env = { QT_QPA_PLATFORM = 'wayland', N = 3 },\n"
		"  typo_section = {},\n"
		"}\n";
	c = cfg_load(write_tmp(user), default_config_lua, "test", err, sizeof(err));
	if (!c) { fprintf(stderr, "user failed: %s\n", err); return 1; }
	fputs("--- expected warnings follow ---\n", stderr);
	fputs(c->warnings, stderr);
	fputs("--- end warnings ---\n", stderr);
	CHECK(strstr(c->warnings, "'outer' must be a number"));
	CHECK(strstr(c->warnings, "unknown key 'smarrt'"));
	CHECK(strstr(c->warnings, "\"bouncy\""));
	CHECK(strstr(c->warnings, "accel_speed"));         /* out of range */
	CHECK(strstr(c->warnings, "unknown modifier \"HYPER\""));
	CHECK(strstr(c->warnings, "unknown key \"notakey\""));
	CHECK(strstr(c->warnings, "nobutton") || strstr(c->warnings, "invalid value"));
	CHECK(strstr(c->warnings, "unknown key 'typo_section'"));

	CHECK(c->gap_inner == 20 && c->gap_outer == 8);     /* bad outer keeps default */
	CHECK(c->borderpx == 4);
	CHECK(fabsf(c->focuscolor[0] - 1.0f) < 0.01f && fabsf(c->focuscolor[1] - 0.533f) < 0.01f);
	CHECK(fabsf(c->bordercolor[0] - 0.267f) < 0.01f);   /* untouched default */
	CHECK(!c->anim_enabled);
	CHECK(c->anim_curve == CFG_CURVE_OUT_CUBIC);        /* bad curve ignored */
	CHECK(c->xkb_layout && !strcmp(c->xkb_layout, "us,ru"));
	CHECK(c->xkb_variant == NULL);
	CHECK(c->natural_scroll);
	CHECK(fabs(c->accel_speed - 1.0) < 1e-9);           /* clamped */

	/* monitors: specific first, wildcard last */
	CHECK(c->nmons == 4);
	CHECK(c->mons[c->nmons - 1].name == NULL && c->mons[c->nmons - 1].desc == NULL);
	{
		int i, seen_edp = 0, seen_hdmi = 0, seen_desc = 0;
		for (i = 0; i < c->nmons; i++) {
			CfgMonitor *m = &c->mons[i];
			if (m->name && !strcmp(m->name, "eDP-1")) {
				seen_edp = 1;
				CHECK(fabsf(m->scale - 1.5f) < 1e-6);
				CHECK(m->pos_kind == CFG_POS_ABS && m->x == 0 && m->y == 0);
				CHECK(m->mode_kind == CFG_MODE_EXPLICIT && m->mode_w == 1920
						&& m->mode_h == 1080 && m->mode_mhz == 60000);
			}
			if (m->name && !strcmp(m->name, "HDMI-A-1")) {
				seen_hdmi = 1;
				CHECK(m->pos_kind == CFG_POS_REL && m->rel_dir == CFG_REL_RIGHT);
				CHECK(m->rel_ref && !strcmp(m->rel_ref, "eDP-1"));
				CHECK(m->align == CFG_ALIGN_CENTER && m->vrr == 1);
				CHECK(m->mode_mhz == 143970);
				CHECK(m->layout && !strcmp(m->layout, "grid"));
			}
			if (m->desc && !strcmp(m->desc, "Dell")) {
				seen_desc = 1;
				CHECK(!m->enabled && m->transform == 1);
			}
		}
		CHECK(seen_edp && seen_hdmi && seen_desc);
	}

	/* rules */
	CHECK(c->nrules == 2);
	CHECK(c->rules[0].tags == ((1u << 1) | (1u << 2)));
	CHECK(c->rules[0].monitor_name && !strcmp(c->rules[0].monitor_name, "HDMI-A-1"));
	CHECK(c->rules[1].floating && c->rules[1].monitor == 1);

	/* binds replace the default list (2 valid + 1 named-form = 3) */
	CHECK(c->nkeys == 3);
	b = findbind(c, "spawn", -1);
	CHECK(b && b->argtype == CFG_ARG_ARGV && b->argc == 3 && !strcmp(b->argv[2], "htop")
			&& b->argv[3] == NULL);
	CHECK(b && b->mod == (CFG_MOD_LOGO | CFG_MOD_SHIFT));
	b = findbind(c, "chvt", 1);
	CHECK(b && b->mod == CFG_MOD_ALT);
	CHECK(c->nbuttons == 1 && c->buttons[0].code == 0x110);
	CHECK(c->naxes == 1 && c->axes[0].code == 0);

	CHECK(c->nautostart == 2 && c->autostart[0].sh && c->autostart[1].argc == 3);
	CHECK(c->nenv == 2);
	cfg_free(c);

	/* ---- 3. hard errors are reported, not crashed on */
	c = cfg_load(write_tmp("return {"), default_config_lua, "t", err, sizeof(err));
	CHECK(c == NULL && strstr(err, "rwm-test"));
	c = cfg_load(write_tmp("local x = 1"), default_config_lua, "t", err, sizeof(err));
	CHECK(c == NULL && strstr(err, "return"));
	c = cfg_load(write_tmp("error('boom')"), default_config_lua, "t", err, sizeof(err));
	CHECK(c == NULL && strstr(err, "boom"));
	c = cfg_load("/nonexistent/config.lua", default_config_lua, "t", err, sizeof(err));
	CHECK(c == NULL);

	/* ---- 4. require() next to the config file works */
	{
		FILE *f = fopen("/tmp/rwm_helper_mod.lua", "w");
		fputs("return { gap = 33 }\n", f);
		fclose(f);
		c = cfg_load(write_tmp("local h = require('rwm_helper_mod')\n"
				"return { gaps = { inner = h.gap } }"),
				default_config_lua, "t", err, sizeof(err));
		CHECK(c && c->gap_inner == 33);
		cfg_free(c);
	}

	unlink(write_tmp(""));
	printf(failures ? "%d FAILED\n" : "all luaconf tests passed\n", failures);
	return failures != 0;
}

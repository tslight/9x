#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#if defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/ioctl.h>
#include <machine/apmvar.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#include "config.h"

#define MAXCLIENTS  512
#define MAXCMDS     4096
#define MINSIZE     20
#define DESK_COLORS 4
#define SWEEP_TIMEOUT 2
#define LENGTH(x)   (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define CLAMP(x,lo,hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define USED(x)     ((void)(x))
#define RGB16(v)    ((unsigned short)(((v) & 0xFF) * 0x101))

typedef struct {
	unsigned int  width;
	unsigned int  hot[2];
	unsigned char mask[64];
	unsigned char fore[64];
} Cursordata;
static Cursordata bigarrow = {
	16, {0, 0},
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F,
	  0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x1F, 0xFF, 0x3F,
	  0xFF, 0x7F, 0xFF, 0xFF, 0xFF, 0x7F, 0xFF, 0x3F,
	  0xCF, 0x1F, 0x8F, 0x0F, 0x07, 0x07, 0x03, 0x02 },
	{ 0x00, 0x00, 0xFE, 0x7F, 0xFE, 0x3F, 0xFE, 0x0F,
	  0xFE, 0x07, 0xFE, 0x07, 0xFE, 0x0F, 0xFE, 0x1F,
	  0xFE, 0x3F, 0xFE, 0x7F, 0xFE, 0x3F, 0xCE, 0x1F,
	  0x86, 0x0F, 0x06, 0x07, 0x02, 0x02, 0x00, 0x00 }
};
static Cursordata sweepdata = {
	16, {7, 7},
	{0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03,
	 0xC0, 0x03, 0xC0, 0x03, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x03, 0xC0, 0x03,
	 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03},
	{0x00, 0x00, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
	 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0xFE, 0x7F,
	 0xFE, 0x7F, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
	 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00}
};
static Cursordata boxdata = {
	16, {7, 7},
	{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	 0xFF, 0xFF, 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8,
	 0x1F, 0xF8, 0x1F, 0xF8, 0x1F, 0xF8, 0xFF, 0xFF,
	 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
	{0x00, 0x00, 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F,
	 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70,
	 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70, 0x0E, 0x70,
	 0xFE, 0x7F, 0xFE, 0x7F, 0xFE, 0x7F, 0x00, 0x00}
};
enum {
	BorderUnknown = 0,
	BorderN, BorderNNE, BorderENE, BorderE,
	BorderESE, BorderSSE, BorderS, BorderSSW,
	BorderWSW, BorderW, BorderWNW, BorderNNW,
	NBorder
};
enum { TileNone = 0, TileN, TileS, TileE, TileW, TileNW, TileNE, TileSW, TileSE, TileMax };
static const int border2tile[NBorder] = {
	[BorderN] = TileN, [BorderS] = TileS, [BorderE] = TileE, [BorderW] = TileW,
	[BorderNNW] = TileNW, [BorderWNW] = TileNW,
	[BorderNNE] = TileNE, [BorderENE] = TileNE,
	[BorderSSW] = TileSW, [BorderWSW] = TileSW,
	[BorderSSE] = TileSE, [BorderESE] = TileSE,
};
typedef struct Client Client;
struct Client {
	Window  win;
	Window  frame;
	int     x, y;
	unsigned int dx, dy;
	unsigned int odx, ody;
	int     ox, oy;
	int     tiled;
	int     fullscreen;
	int     proto;
	int     reparenting;
	int     virt;
	char   *label;
	Client *next;
};
enum { Pdelete = 1, Ptakefocus = 2 };
enum { StateRemove, StateAdd, StateToggle };

static Display      *dpy;
static int           screen;
static Window        root;
static unsigned int  sw, sh, barw, barh;
static XftFont      *xftfont;
static Cursor        c_arrow, c_sweep, c_box, c_border[NBorder];
static Atom          wm_protocols, wm_delete, wm_take_focus, wm_state;
static Atom          net_supported, net_supporting, net_client_list, net_active;
static Atom          net_num_desks, net_cur_desk, net_wm_desk, net_workarea;
static Atom          net_wm_name, net_wm_state, net_wm_state_fs, net_frame_ext;
static Atom          net_wm_type, net_wm_type_dock, utf8_string;
static Client       *clients, *current;
static unsigned long col_active, col_inactive, col_red;
static volatile sig_atomic_t running = 1;
static Window        swout[4], wmcheck, barwin, last_click_win;
static time_t        sweep_pending, bar_deadline;
static int           sweep_x, sweep_y, curdesk, bar_batt = -1, bar_onac;
static unsigned int  sweep_dx, sweep_dy;
static Time          last_click_time;
static Client       *deskfocus[NDESKS];
static Pixmap        barpix;
static GC            bargc;
static XftDraw      *bardraw;
static XftColor      bar_fg, bar_bg, bar_sel, bar_self, bar_tab;
static XftColor      bar_run, bar_exit, bar_desk[DESK_COLORS];

static Client       *bar_tabs[MAXCLIENTS];
static int           bar_tab_x[MAXCLIENTS], bar_tab_w[MAXCLIENTS], bar_ntabs;
static int           bar_run_x, bar_run_w, bar_desk_w, bar_status_x, bar_exit_x, bar_exit_w;
static int           bar_desk_x[NDESKS];
static int           launch_visible, launch_ncmds, launch_sel = -1, launch_scroll;
static int           launch_filterlen, launch_nfiltered, launch_nitems;
static char         *launch_cmds[MAXCMDS];
static char          launch_filter[256];
static int           launch_filtered[MAXCMDS], launch_item_x[MAXCMDS], launch_item_w[MAXCMDS];
static void bar_redraw(void);

static void
raisebar(void)
{
	if(!(current && current->fullscreen))
		XRaiseWindow(dpy, barwin);
}

static unsigned long
getcolor(unsigned long rgb)
{
	XColor c = {0, RGB16(rgb >> 16), RGB16(rgb >> 8), RGB16(rgb), DoRed | DoGreen | DoBlue, 0};
	return XAllocColor(dpy, DefaultColormap(dpy, screen), &c) ? c.pixel : WhitePixel(dpy, screen);
}

static XftColor
getxftcolor(unsigned long rgb)
{
	XRenderColor rc = {RGB16(rgb >> 16), RGB16(rgb >> 8), RGB16(rgb), 0xFFFF};
	XftColor c = {0};
	if(!XftColorAllocValue(dpy, DefaultVisual(dpy, screen), DefaultColormap(dpy, screen), &rc, &c))
		c.pixel = WhitePixel(dpy, screen);
	return c;
}

static Cursor
makecursor(Cursordata *d)
{
	XColor bl = {0}, wh = {0}, dummy;
	Colormap cmap = DefaultColormap(dpy, screen);
	if(!XAllocNamedColor(dpy, cmap, "black", &bl, &dummy))
		bl.pixel = BlackPixel(dpy, screen);
	if(!XAllocNamedColor(dpy, cmap, "white", &wh, &dummy))
		wh.pixel = WhitePixel(dpy, screen);
	Pixmap f = XCreatePixmapFromBitmapData(dpy, root, (char *)d->fore, d->width, d->width, 1, 0, 1);
	Pixmap m = XCreatePixmapFromBitmapData(dpy, root, (char *)d->mask, d->width, d->width, 1, 0, 1);
	Cursor cur = XCreatePixmapCursor(dpy, f, m, &bl, &wh, d->hot[0], d->hot[1]);
	XFreePixmap(dpy, f);
	XFreePixmap(dpy, m);
	return cur;
}

static int
xft_textwidth(const char *s, int len)
{
	XGlyphInfo ext;
	XftTextExtentsUtf8(dpy, xftfont, (const FcChar8 *)s, len, &ext);
	return ext.xOff;
}

static int
handler(Display *d, XErrorEvent *e)
{
	USED(d);
	USED(e);
	return 0;
}

static void
sigchld(int sig)
{
	USED(sig);
	while(waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static void
sigterm(int sig)
{
	USED(sig);
	running = 0;
}

static void
spawn(const char *cmd)
{
	pid_t p = fork();
	if(p < 0)
		return;
	if(p == 0){
		if(fork() == 0){
			setsid();
			close(ConnectionNumber(dpy));
			execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
			_exit(1);
		}
		_exit(0);
	}
	wait(NULL);
}

#if defined(__OpenBSD__) || defined(__NetBSD__)
static int apmfd = -1;
static void
initbattery(void)
{
	apmfd = open("/dev/apm", O_RDONLY);
}
static void
readbattery(void)
{
	struct apm_power_info ai;
	if(apmfd < 0 || ioctl(apmfd, APM_IOC_GETPOWER, &ai) < 0){
		bar_batt = -1;
		return;
	}
	bar_batt = ai.battery_life <= 100 ? (int)ai.battery_life : -1;
	bar_onac = ai.ac_state == APM_AC_ON;
}
static void
closebattery(void)
{
	if(apmfd >= 0){
		close(apmfd);
		apmfd = -1;
	}
}
#elif defined(__FreeBSD__)
static void initbattery(void) {}
static void
readbattery(void)
{
	int life, ac;
	size_t len;
	len = sizeof life;
	if(sysctlbyname("hw.acpi.battery.life", &life, &len, NULL, 0) < 0){
		bar_batt = -1;
		return;
	}
	bar_batt = life <= 100 ? life : -1;
	len = sizeof ac;
	if(sysctlbyname("hw.acpi.acline", &ac, &len, NULL, 0) < 0)
		ac = 0;
	bar_onac = ac;
}
static void closebattery(void) {}
#elif defined(__linux__)
static void initbattery(void) {}
static int
readsysfs(const char *path)
{
	FILE *f;
	int n = -1;
	if((f = fopen(path, "r")) != NULL){
		if(fscanf(f, "%d", &n) != 1)
			n = -1;
		fclose(f);
	}
	return n;
}
static void
readbattery(void)
{
	int n;
	n = readsysfs("/sys/class/power_supply/BAT0/capacity");
	bar_batt = (n >= 0 && n <= 100) ? n : -1;
	n = readsysfs("/sys/class/power_supply/AC/online");
	if(n < 0)
		n = readsysfs("/sys/class/power_supply/ACAD/online");
	bar_onac = n > 0;
}
static void closebattery(void) {}
#else
static void initbattery(void) {}
static void readbattery(void) { bar_batt = -1; }
static void closebattery(void) {}
#endif

static int
cmdcmp(const void *a, const void *b)
{
	return strcmp(*(char **)a, *(char **)b);
}

static int
tabcmp(const void *a, const void *b)
{
	Window wa = (*(Client **)a)->win;
	Window wb = (*(Client **)b)->win;
	return (wa > wb) - (wa < wb);
}

static void
scan_path(void)
{
	char *path, *p, *dir, fullpath[PATH_MAX];
	DIR *d;
	struct dirent *ent;
	struct stat st;
	int i, j;

	path = getenv("PATH");
	if(!path)
		return;
	path = strdup(path);
	if(!path)
		return;

	for(dir = path; dir; dir = p){
		p = strchr(dir, ':');
		if(p)
			*p++ = '\0';
		if(*dir == '\0')
			continue;
		d = opendir(dir);
		if(!d)
			continue;
		while((ent = readdir(d)) != NULL){
			if(ent->d_name[0] == '.')
				continue;
			if(launch_ncmds >= MAXCMDS)
				break;
			snprintf(fullpath, sizeof fullpath, "%s/%s", dir, ent->d_name);
			if(stat(fullpath, &st) != 0)
				continue;
			if(!S_ISREG(st.st_mode))
				continue;
			if(!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
				continue;
			launch_cmds[launch_ncmds] = strdup(ent->d_name);
			if(launch_cmds[launch_ncmds])
				launch_ncmds++;
		}
		closedir(d);
	}
	free(path);
	qsort(launch_cmds, (size_t)launch_ncmds, sizeof(char *), cmdcmp);
	for(i = 0, j = 0; i < launch_ncmds; i++){
		if(j > 0 && strcmp(launch_cmds[j-1], launch_cmds[i]) == 0)
			free(launch_cmds[i]);
		else
			launch_cmds[j++] = launch_cmds[i];
	}
	launch_ncmds = j;
}

static void
bar_drawbtn(int x, int w, const char *s, int len, int sel, XftColor *bg)
{
	int ty = BAR_PAD + xftfont->ascent;
	if(w <= 0)
		return;
	XftDrawRect(bardraw, sel ? &bar_sel : bg, x, 0, (unsigned int)w, barh);
	XftDrawStringUtf8(bardraw, sel ? &bar_self : &bar_fg, xftfont,
		x + BAR_BTN_PAD, ty, (const FcChar8 *)s, len);
}

static void
launcher_draw(void)
{
	int i, x, ty, tw, maxw, idx, nlen, sel;
	const char *name;
	XGlyphInfo ext;

	XSetForeground(dpy, bargc, bar_bg.pixel);
	XFillRectangle(dpy, barpix, bargc, 0, 0, sw, barh);

	ty = BAR_PAD + xftfont->ascent;
	x = BAR_GAP;

	XftDrawRect(bardraw, &bar_tab, x, 0, LAUNCH_FILTER_W, barh);
	XftDrawStringUtf8(bardraw, &bar_fg, xftfont,
		x + BAR_BTN_PAD, ty, (const FcChar8 *)launch_filter, launch_filterlen);
	XftTextExtentsUtf8(dpy, xftfont, (const FcChar8 *)launch_filter, launch_filterlen, &ext);
	XSetForeground(dpy, bargc, bar_fg.pixel);
	XFillRectangle(dpy, barpix, bargc, x + BAR_BTN_PAD + ext.xOff, BAR_PAD, 2, barh - 2*BAR_PAD);
	x += LAUNCH_FILTER_W + BAR_GAP;

	maxw = (int)sw - x - BAR_GAP;
	launch_nitems = 0;
	for(i = launch_scroll; i < launch_nfiltered && x < (int)sw - BAR_GAP && launch_nitems < MAXCMDS; i++){
		idx = launch_filtered[i];
		name = launch_cmds[idx];
		nlen = (int)strlen(name);
		XftTextExtentsUtf8(dpy, xftfont, (const FcChar8 *)name, nlen, &ext);
		tw = ext.xOff + 2*BAR_BTN_PAD;
		if(tw > maxw)
			tw = maxw;
		if(tw <= 0)
			continue;
		launch_item_x[launch_nitems] = x;
		launch_item_w[launch_nitems] = tw;
		launch_nitems++;
		sel = (i == launch_sel);
		XftDrawRect(bardraw, sel ? &bar_sel : &bar_tab, x, 0, (unsigned int)tw, barh);
		XftDrawStringUtf8(bardraw, sel ? &bar_self : &bar_fg, xftfont,
			x + BAR_BTN_PAD, ty, (const FcChar8 *)name, nlen);
		x += tw + BAR_GAP;
	}

	XCopyArea(dpy, barpix, barwin, bargc, 0, 0, sw, barh, 0, 0);
}

static int
launcher_hittest(int x)
{
	for(int i = 0; i < launch_nitems; i++)
		if(x >= launch_item_x[i] && x < launch_item_x[i] + launch_item_w[i])
			return launch_scroll + i;
	return -1;
}

static int
match(const char *s, const char *sub)
{
	size_t i, j, sublen;
	if(!sub[0]) return 1;
	sublen = strlen(sub);
	for(i = 0; s[i]; i++){
		for(j = 0; j < sublen && s[i+j]; j++)
			if(tolower((unsigned char)s[i+j]) != tolower((unsigned char)sub[j]))
				break;
		if(j == sublen) return 1;
	}
	return 0;
}

static void
launcher_filter(void)
{
	launch_nfiltered = 0;
	for(int i = 0; i < launch_ncmds && launch_nfiltered < MAXCMDS; i++)
		if(launch_filterlen == 0 || match(launch_cmds[i], launch_filter))
			launch_filtered[launch_nfiltered++] = i;
	if(launch_nfiltered == 0)
		launch_sel = -1;
	else if(launch_sel >= launch_nfiltered)
		launch_sel = launch_nfiltered - 1;
	else if(launch_sel < 0)
		launch_sel = 0;
	launch_scroll = 0;
}

static void
launcher_show(void)
{
	if(launch_visible || launch_ncmds == 0)
		return;
	launch_filter[0] = '\0';
	launch_filterlen = 0;
	launch_sel = 0;
	launcher_filter();
	if(XGrabKeyboard(dpy, root, True, GrabModeAsync, GrabModeAsync, CurrentTime) != GrabSuccess)
		return;
	if(XGrabPointer(dpy, root, True,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None,
		CurrentTime) != GrabSuccess){
		XUngrabKeyboard(dpy, CurrentTime);
		return;
	}
	launch_visible = 1;
	launcher_draw();
}

static void
launcher_hide(void)
{
	if(!launch_visible)
		return;
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	launch_visible = 0;
	bar_redraw();
}

static void
launcher_key(XKeyEvent *e)
{
	char buf[32];
	KeySym ks;
	const char *cmd;
	int n;

	n = XLookupString(e, buf, sizeof(buf) - 1, &ks, NULL);

	if(ks == XK_Escape){
		launcher_hide();
		return;
	}
	if(ks == XK_Return || ks == XK_KP_Enter){
		if(launch_sel >= 0 && launch_sel < launch_nfiltered){
			cmd = launch_cmds[launch_filtered[launch_sel]];
			launcher_hide();
			spawn(cmd);
		}
		return;
	}
	if(ks == XK_BackSpace){
		if(launch_filterlen > 0){
			launch_filter[--launch_filterlen] = '\0';
			launcher_filter();
			launcher_draw();
		}
		return;
	}
	if(ks == XK_Left || ks == XK_Up || (ks == XK_Tab && (e->state & ShiftMask))){
		if(launch_sel > 0){
			launch_sel--;
			if(launch_sel < launch_scroll)
				launch_scroll = launch_sel;
			launcher_draw();
		}
		return;
	}
	if(ks == XK_Right || ks == XK_Down || (ks == XK_Tab && !(e->state & ShiftMask))){
		if(launch_sel < launch_nfiltered - 1){
			launch_sel++;
			if(launch_sel >= launch_scroll + launch_nitems)
				launch_scroll++;
			launcher_draw();
		}
		return;
	}

	if(n == 1 && buf[0] >= ' ' && buf[0] < 127
	&& launch_filterlen < (int)sizeof(launch_filter) - 1){
		launch_filter[launch_filterlen++] = buf[0];
		launch_filter[launch_filterlen] = '\0';
		launcher_filter();
		launcher_draw();
	}
}

static void
bar_drawtabs(int x, int tabarea, int rightw)
{
	Client *c;
	const char *name;
	char trunc[256];
	int i, tw, tabx, nlen, drawn, ty, maxw, dotw, sel;

	bar_ntabs = 0;
	for(c = clients; c && bar_ntabs < MAXCLIENTS; c = c->next)
		if(c->virt == curdesk)
			bar_tabs[bar_ntabs++] = c;

	if(bar_ntabs <= 0 || tabarea <= 0)
		return;
	qsort(bar_tabs, (size_t)bar_ntabs, sizeof(Client *), tabcmp);

	ty = BAR_PAD + xftfont->ascent;
	dotw = xft_textwidth("..", 2);
	drawn = 0;
	tw = (tabarea - (bar_ntabs - 1) * BAR_GAP) / bar_ntabs;
	if(tw < 30) tw = 30;
	tabx = x;

	for(i = 0; i < bar_ntabs && tabx < (int)barw - rightw; i++){
		c = bar_tabs[i];
		name = c->label ? c->label : "(unnamed)";
		nlen = (int)strlen(name);
		if(nlen > 250)
			nlen = 250;

		maxw = tw - 2 * BAR_BTN_PAD;
		if(maxw > 0 && xft_textwidth(name, nlen) > maxw){
			while(nlen > 0 && xft_textwidth(name, nlen) + dotw > maxw)
				nlen--;
			if(nlen > 0){
				memcpy(trunc, name, (size_t)nlen);
				trunc[nlen] = '.';
				trunc[nlen+1] = '.';
				trunc[nlen+2] = '\0';
				name = trunc;
				nlen += 2;
			} else {
				name = "..";
				nlen = 2;
			}
		}

		bar_tab_x[drawn] = tabx;
		bar_tab_w[drawn] = tw;
		bar_tabs[drawn] = c;
		drawn++;

		sel = (c == current);
		XftDrawRect(bardraw, sel ? &bar_sel : &bar_tab,
			tabx, 0, (unsigned int)tw, barh);
		XftDrawStringUtf8(bardraw, sel ? &bar_self : &bar_fg, xftfont,
			tabx + BAR_BTN_PAD, ty, (const FcChar8 *)name, nlen);
		tabx += tw + BAR_GAP;
	}
	bar_ntabs = drawn;
}

static void
bar_redraw(void)
{
	Client *dc;
	time_t now;
	struct tm *t;
	char tbuf[64], bbuf[32], dbuf[2];
	int x, blen, tlen, rightw, tabarea, statusw, i;
	int wincnt[NDESKS] = {0};

	now = time(NULL);
	t = localtime(&now);
	if(t)
		strftime(tbuf, sizeof tbuf, TIMEFMT, t);
	else
		strcpy(tbuf, "??:??");

	if(bar_batt >= 0)
		snprintf(bbuf, sizeof bbuf,
			bar_onac ? "%d%% " : "!%d%% ", bar_batt);
	else
		bbuf[0] = '\0';

	blen = (int)strlen(bbuf);
	tlen = (int)strlen(tbuf);

	XSetForeground(dpy, bargc, bar_bg.pixel);
	XFillRectangle(dpy, barpix, bargc, 0, 0, barw, barh);

	x = BAR_PAD;
	bar_run_x = x;
	bar_run_w = xft_textwidth("Run", 3) + 2 * BAR_BTN_PAD;
	bar_drawbtn(x, bar_run_w, "Run", 3, 0, &bar_run);
	x += bar_run_w + BAR_GAP * 2;

	bar_desk_w = xft_textwidth("0", 1) + 2 * BAR_BTN_PAD;
	bar_exit_w = xft_textwidth("Exit", 4) + 2 * BAR_BTN_PAD;
	statusw = xft_textwidth(bbuf, blen) + xft_textwidth(tbuf, tlen) + BAR_GAP * 2;
	rightw = (bar_desk_w + BAR_GAP) * NDESKS + statusw + bar_exit_w + BAR_PAD;
	tabarea = (int)barw - x - rightw;

	bar_drawtabs(x, tabarea, rightw);

	x = (int)barw - rightw + BAR_PAD;
	for(dc = clients; dc; dc = dc->next)
		if(dc->virt >= 0 && dc->virt < NDESKS)
			wincnt[dc->virt]++;
	for(i = 0; i < NDESKS; i++){
		dbuf[0] = (char)('1' + i);
		dbuf[1] = '\0';
		bar_desk_x[i] = x;
		bar_drawbtn(x, bar_desk_w, dbuf, 1, i == curdesk,
			&bar_desk[MIN(wincnt[i], DESK_COLORS - 1)]);
		x += bar_desk_w + BAR_GAP;
	}

	bar_status_x = x;
	if(bbuf[0]){
		XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, BAR_PAD + xftfont->ascent,
			(const FcChar8 *)bbuf, blen);
		x += xft_textwidth(bbuf, blen);
	}
	XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, BAR_PAD + xftfont->ascent,
		(const FcChar8 *)tbuf, tlen);
	x += xft_textwidth(tbuf, tlen) + BAR_GAP * 2;

	bar_exit_x = x;
	bar_drawbtn(x, bar_exit_w, "Exit", 4, 0, &bar_exit);

	XCopyArea(dpy, barpix, barwin, bargc, 0, 0, barw, barh, 0, 0);
}

static int
borderorient(Client *c, int x, int y)
{
	int fw, fh;

	fw = (int)c->dx + 2 * BORDER;
	fh = (int)c->dy + 2 * BORDER;

	if(x <= BORDER){
		if(y <= CORNER) return BorderWNW;
		if(y >= fh - CORNER) return BorderWSW;
		return BorderW;
	}
	if(x >= fw - BORDER){
		if(y <= CORNER) return BorderENE;
		if(y >= fh - CORNER) return BorderESE;
		return BorderE;
	}
	if(y <= BORDER){
		if(x <= CORNER) return BorderNNW;
		if(x >= fw - CORNER) return BorderNNE;
		return BorderN;
	}
	if(y >= fh - BORDER){
		if(x <= CORNER) return BorderSSW;
		if(x >= fw - CORNER) return BorderSSE;
		return BorderS;
	}
	return BorderUnknown;
}

static Client *
winclient(Window w)
{
	Client *c;

	for(c = clients; c; c = c->next)
		if(c->win == w)
			return c;
	return NULL;
}

static Client *
frameclient(Window w)
{
	Client *c;

	for(c = clients; c; c = c->next)
		if(c->frame == w)
			return c;
	return NULL;
}

static void
getname(Client *c)
{
	Atom type;
	int fmt;
	unsigned long nitems, after;
	unsigned char *data = NULL;
	char *fetched = NULL;

	free(c->label);
	c->label = NULL;

	if(XGetWindowProperty(dpy, c->win, net_wm_name, 0, 512, False,
		utf8_string, &type, &fmt, &nitems, &after, &data) == Success
	&& type == utf8_string && data && nitems > 0){
		c->label = strdup((char *)data);
		XFree(data);
		return;
	}
	if(data)
		XFree(data);
	if(XFetchName(dpy, c->win, &fetched) && fetched){
		c->label = strdup(fetched);
		XFree(fetched);
	}
}

static void
getproto(Client *c)
{
	Atom *protos;
	int n;

	c->proto = 0;
	if(XGetWMProtocols(dpy, c->win, &protos, &n)){
		for(int i = 0; i < n; i++){
			if(protos[i] == wm_delete)
				c->proto |= Pdelete;
			else if(protos[i] == wm_take_focus)
				c->proto |= Ptakefocus;
		}
		XFree(protos);
	}
}

static void
sendcmessage(Window w, Atom proto, Atom data)
{
	XEvent ev = {0};

	ev.xclient.type = ClientMessage;
	ev.xclient.window = w;
	ev.xclient.message_type = proto;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long)data;
	ev.xclient.data.l[1] = CurrentTime;
	XSendEvent(dpy, w, False, 0, &ev);
}

static void
setwmstate(Client *c, long state)
{
	long data[2] = { state, (long)None };

	XChangeProperty(dpy, c->win, wm_state, wm_state, 32,
		PropModeReplace, (unsigned char *)data, 2);
}

static void
sendconfig(Client *c)
{
	XConfigureEvent ce = {0};

	ce.type = ConfigureNotify;
	ce.event = c->win;
	ce.window = c->win;
	if(c->fullscreen){
		ce.x = 0;
		ce.y = 0;
		ce.width = (int)sw;
		ce.height = (int)sh;
	} else {
		ce.x = c->x;
		ce.y = c->y;
		ce.width = (int)c->dx;
		ce.height = (int)c->dy;
	}
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

static void
ewmh_set(Window w, Atom prop, Atom type, int fmt, void *data, int n)
{
	XChangeProperty(dpy, w, prop, type, fmt, PropModeReplace, data, n);
}

static void
ewmh_updateclients(void)
{
	Window wins[MAXCLIENTS];
	Client *c;
	int n = 0;
	for(c = clients; c && n < MAXCLIENTS; c = c->next)
		wins[n++] = c->win;
	ewmh_set(root, net_client_list, XA_WINDOW, 32, wins, n);
}

static void
ewmh_setup(void)
{
	Atom supported[] = {
		net_supported, net_supporting, net_client_list, net_active,
		net_num_desks, net_cur_desk, net_wm_desk, net_workarea,
		net_wm_name, net_wm_state, net_wm_state_fs, net_frame_ext,
		net_wm_type, net_wm_type_dock
	};
	long workarea[4] = { 0, barh, sw, sh - barh };
	wmcheck = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
	ewmh_set(wmcheck, net_supporting, XA_WINDOW, 32, &wmcheck, 1);
	ewmh_set(wmcheck, net_wm_name, utf8_string, 8, "9x", 2);
	ewmh_set(root, net_supporting, XA_WINDOW, 32, &wmcheck, 1);
	ewmh_set(root, net_supported, XA_ATOM, 32, supported, LENGTH(supported));
	ewmh_set(root, net_num_desks, XA_CARDINAL, 32, &(long){NDESKS}, 1);
	ewmh_set(root, net_cur_desk, XA_CARDINAL, 32, &(long){0}, 1);
	ewmh_set(root, net_workarea, XA_CARDINAL, 32, workarea, 4);
	ewmh_set(root, net_client_list, XA_WINDOW, 32, NULL, 0);
}

static void
setborder(Client *c, int focused)
{
	XSetWindowBackground(dpy, c->frame,
		focused ? col_active : col_inactive);
	XClearWindow(dpy, c->frame);
}

static void
grabbuttons(Client *c, int focused)
{
	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if(!focused)
		XGrabButton(dpy, Button1, AnyModifier, c->win, False,
			ButtonPressMask, GrabModeSync, GrabModeSync,
			None, None);
}

static void
focus(Client *c)
{
	if(current && current != c){
		setborder(current, 0);
		grabbuttons(current, 0);
	}
	current = c;
	if(c){
		setborder(c, 1);
		grabbuttons(c, 1);
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		if(c->proto & Ptakefocus)
			sendcmessage(c->win, wm_protocols, wm_take_focus);
		XRaiseWindow(dpy, c->frame);
		ewmh_set(root, net_active, XA_WINDOW, 32, &c->win, 1);
	} else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		ewmh_set(root, net_active, XA_WINDOW, 32, &(Window){None}, 1);
	}
	raisebar();
	bar_redraw();
}

static void
promote(Client *c)
{
	Client **pp;

	if(!c || c == clients)
		return;
	for(pp = &clients; *pp; pp = &(*pp)->next)
		if(*pp == c){
			*pp = c->next;
			break;
		}
	c->next = clients;
	clients = c;
}

static void
applylayout(Client *c)
{
	if(c->fullscreen){
		XMoveResizeWindow(dpy, c->frame, 0, 0, sw, sh);
		XMoveResizeWindow(dpy, c->win, 0, 0, sw, sh);
	} else {
		XMoveResizeWindow(dpy, c->frame,
			c->x - BORDER, c->y - BORDER,
			c->dx + 2 * BORDER, c->dy + 2 * BORDER);
		XMoveResizeWindow(dpy, c->win, BORDER, BORDER, c->dx, c->dy);
	}
	sendconfig(c);
}

static Client *
topclient(void)
{
	Client *c;
	for(c = clients; c; c = c->next)
		if(c->virt == curdesk)
			return c;
	return NULL;
}

static void
switch_to(int n)
{
	Client *c;

	if(n < 0 || n >= NDESKS || n == curdesk)
		return;
	deskfocus[curdesk] = current;
	curdesk = n;
	ewmh_set(root, net_cur_desk, XA_CARDINAL, 32, &(long){n}, 1);
	for(c = clients; c; c = c->next){
		if(c->virt == curdesk)
			XMapWindow(dpy, c->frame);
		else
			XUnmapWindow(dpy, c->frame);
	}
	focus(deskfocus[curdesk]);
}

static void
sendtodesktop(Client *c, int n)
{
	if(!c || n < 0 || n >= NDESKS || n == curdesk)
		return;
	c->virt = n;
	ewmh_set(c->win, net_wm_desk, XA_CARDINAL, 32, &(long){n}, 1);
	XUnmapWindow(dpy, c->frame);
	if(current == c)
		focus(topclient());
	deskfocus[n] = c;
	switch_to(n);
}

static void
clampframe(int *x, int *y, unsigned int *dx, unsigned int *dy)
{
	*dx = MAX(*dx, MINSIZE + 2*BORDER);
	*dy = MAX(*dy, MINSIZE + 2*BORDER);
	*x = CLAMP(*x, 0, (int)(sw - *dx));
	*y = CLAMP(*y, (int)barh, (int)(sh - *dy));
}

static int
getprop(Window w, Atom prop, Atom type, unsigned char **ret)
{
	Atom rtype;
	int fmt;
	unsigned long n, after;
	if(XGetWindowProperty(dpy, w, prop, 0, 32, False, type,
		&rtype, &fmt, &n, &after, ret) != Success || !*ret)
		return 0;
	return (int)n;
}

static int
isdock(Window w)
{
	unsigned char *data;
	int n, dock = 0;

	if(!(n = getprop(w, net_wm_type, XA_ATOM, &data)))
		return 0;
	for(Atom *t = (Atom *)data; n--; t++)
		if(*t == net_wm_type_dock)
			dock = 1;
	XFree(data);
	return dock;
}

static void
clampgeom(Client *c)
{
	int x = c->x - BORDER, y = c->y - BORDER;
	unsigned int dx = c->dx + 2*BORDER, dy = c->dy + 2*BORDER;

	clampframe(&x, &y, &dx, &dy);
	c->x = x + BORDER;
	c->y = y + BORDER;
	c->dx = dx - 2*BORDER;
	c->dy = dy - 2*BORDER;
}

static Client *
manage(Window w)
{
	Client *c;
	XWindowAttributes wa;
	XSetWindowAttributes sa;
	XSizeHints hints = {0};
	long supplied;

	if(!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect)
		return NULL;
	if(isdock(w))
		return NULL;
	c = winclient(w);
	if(c)
		return c;

	c = calloc(1, sizeof *c);
	if(!c)
		return NULL;

	c->win = w;
	c->virt = curdesk;

	if(sweep_pending && time(NULL) - sweep_pending < SWEEP_TIMEOUT){
		c->x = sweep_x;
		c->y = sweep_y;
		c->dx = sweep_dx;
		c->dy = sweep_dy;
		sweep_pending = 0;
		clampgeom(c);
	} else {
		c->dx = (unsigned int)wa.width;
		c->dy = (unsigned int)wa.height;
		c->x = wa.x;
		c->y = wa.y;

		if(!(XGetWMNormalHints(dpy, w, &hints, &supplied)
		&& (hints.flags & (USPosition | PPosition))
		&& c->x > 0 && c->y > 0)){
			c->x = (int)(sw - c->dx) / 2;
			c->y = (int)barh + (int)(sh - barh - c->dy) / 2;
		}
		clampgeom(c);
	}

	getname(c);
	getproto(c);

	sa.override_redirect = True;
	sa.background_pixel = col_inactive;
	sa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
		| ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

	c->frame = XCreateWindow(dpy, root,
		c->x - BORDER, c->y - BORDER,
		c->dx + 2 * BORDER, c->dy + 2 * BORDER,
		0, CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWEventMask, &sa);

	c->reparenting = 1;
	XAddToSaveSet(dpy, w);
	XReparentWindow(dpy, w, c->frame, BORDER, BORDER);
	XResizeWindow(dpy, w, c->dx, c->dy);
	XSetWindowBorderWidth(dpy, w, 0);
	XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
	sendconfig(c);
	XMapWindow(dpy, w);
	XMapWindow(dpy, c->frame);

	c->next = clients;
	clients = c;
	setwmstate(c, NormalState);
	ewmh_set(c->win, net_frame_ext, XA_CARDINAL, 32, (long[]){BORDER,BORDER,BORDER,BORDER}, 4);
	ewmh_set(c->win, net_wm_desk, XA_CARDINAL, 32, &(long){c->virt}, 1);
	ewmh_updateclients();
	focus(c);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
		(int)c->dx / 2, (int)c->dy / 2);
	return c;
}

static void
unmanage(Client *c)
{
	Client **pp, *old = c;

	for(pp = &clients; *pp; pp = &(*pp)->next)
		if(*pp == c){
			*pp = c->next;
			break;
		}
	for(int i = 0; i < NDESKS; i++)
		if(deskfocus[i] == c)
			deskfocus[i] = NULL;
	bar_ntabs = 0;
	ewmh_updateclients();
	if(current == c){
		current = NULL;
		focus(topclient());
	} else
		bar_redraw();
	free(old->label);
	free(old);
}

static void
closeclient(Client *c)
{
	if(!c)
		return;
	if(c->proto & Pdelete)
		sendcmessage(c->win, wm_protocols, wm_delete);
	else
		XKillClient(dpy, c->win);
}

static void
clearsnap(Client *c)
{
	c->tiled = TileNone;
	c->fullscreen = 0;
}

static void
savegeom(Client *c)
{
	if(!c->tiled && !c->fullscreen){
		c->ox = c->x;
		c->oy = c->y;
		c->odx = c->dx;
		c->ody = c->dy;
	}
}

static void
restoregeom(Client *c)
{
	c->x = c->ox;
	c->y = c->oy;
	c->dx = c->odx;
	c->dy = c->ody;
}

static int
edgezone(int x, int y)
{
	int at_left = x < EDGE_SNAP;
	int at_right = x >= (int)sw - EDGE_SNAP;
	int at_top = y < (int)barh + EDGE_SNAP;
	int at_bottom = y >= (int)sh - EDGE_SNAP;

	if(at_top && at_left) return TileNW;
	if(at_top && at_right) return TileNE;
	if(at_bottom && at_left) return TileSW;
	if(at_bottom && at_right) return TileSE;
	if(at_top) return TileN;
	if(at_bottom) return TileS;
	if(at_left) return TileW;
	if(at_right) return TileE;
	return TileNone;
}

static void
tilegeom(int dir, int *nx, int *ny, unsigned int *ndx, unsigned int *ndy)
{
	unsigned int hw = sw / 2 - 2 * BORDER;
	unsigned int hh = (sh - barh) / 2 - 2 * BORDER;
	int mx = (int)sw / 2 + BORDER;
	int my = (int)barh + (int)(sh - barh) / 2 + BORDER;

	*nx = BORDER;
	*ny = (int)barh + BORDER;
	*ndx = sw - 2 * BORDER;
	*ndy = sh - barh - 2 * BORDER;

	switch(dir){
	case TileN:  *ndy = hh; break;
	case TileS:  *ny = my; *ndy = hh; break;
	case TileW:  *ndx = hw; break;
	case TileE:  *nx = mx; *ndx = hw; break;
	case TileNW: *ndx = hw; *ndy = hh; break;
	case TileNE: *nx = mx; *ndx = hw; *ndy = hh; break;
	case TileSW: *ny = my; *ndx = hw; *ndy = hh; break;
	case TileSE: *nx = mx; *ny = my; *ndx = hw; *ndy = hh; break;
	}
}

static void
tile(Client *c, int dir)
{
	int nx, ny;
	unsigned int ndx, ndy;

	if(!c || c->fullscreen)
		return;
	if(c->tiled == dir){
		restoregeom(c);
		c->tiled = TileNone;
	} else {
		savegeom(c);
		tilegeom(dir, &nx, &ny, &ndx, &ndy);
		c->x = nx;
		c->y = ny;
		c->dx = ndx;
		c->dy = ndy;
		c->tiled = dir;
	}
	applylayout(c);
	raisebar();
}

static void
outline_show(int x, int y, unsigned int w, unsigned int h)
{
	XMoveResizeWindow(dpy, swout[0], x, y, MAX(w, 1), BORDER);
	XMoveResizeWindow(dpy, swout[1], x, y+(int)h-BORDER, MAX(w, 1), BORDER);
	XMoveResizeWindow(dpy, swout[2], x, y, BORDER, MAX(h, 1));
	XMoveResizeWindow(dpy, swout[3], x+(int)w-BORDER, y, BORDER, MAX(h, 1));
	for(int i = 0; i < 4; i++)
		XMapRaised(dpy, swout[i]);
}

static void
outline_hide(void)
{
	for(int i = 0; i < 4; i++)
		XUnmapWindow(dpy, swout[i]);
}

static Window
make_outline_bar(void)
{
	XSetWindowAttributes sa;

	sa.override_redirect = True;
	sa.background_pixel = col_red;
	return XCreateWindow(dpy, root, 0, 0, 1, 1, 0,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel, &sa);
}

static int
dosweep(int have_origin, int sx, int sy,
	int *rx, int *ry, unsigned int *rdx, unsigned int *rdy)
{
	XEvent ev;
	int bx = 0, by = 0, drawn = 0, done = 0;
	unsigned int bdx = 0, bdy = 0;

	if(XGrabPointer(dpy, root, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_sweep,
		CurrentTime) != GrabSuccess)
		return 0;

	XSync(dpy, False);
	while(XCheckMaskEvent(dpy, ButtonReleaseMask|ButtonPressMask, &ev))
		;

	if(!have_origin){
		while(!done){
			XMaskEvent(dpy, ButtonPressMask|ButtonReleaseMask
				|PointerMotionMask, &ev);
			if(ev.type == ButtonPress){
				sx = ev.xbutton.x_root;
				sy = ev.xbutton.y_root;
				done = 1;
			}
		}
		done = 0;
	}
	while(!done){
		XMaskEvent(dpy, ButtonPressMask|ButtonReleaseMask
			|PointerMotionMask, &ev);
		switch(ev.type){
		case MotionNotify: {
			int x1 = MIN(sx, ev.xmotion.x_root);
			int y1 = MIN(sy, ev.xmotion.y_root);
			int x2 = MAX(sx, ev.xmotion.x_root);
			int y2 = MAX(sy, ev.xmotion.y_root);

			bx = x1; by = y1;
			bdx = (unsigned int)(x2 - x1);
			bdy = (unsigned int)(y2 - y1);
			clampframe(&bx, &by, &bdx, &bdy);
			if(bdx >= 2*BORDER && bdy >= 2*BORDER){
				outline_show(bx, by, bdx, bdy);
				drawn = 1;
			} else if(drawn){
				outline_hide();
				drawn = 0;
			}
			XFlush(dpy);
			break;
		}
		case ButtonRelease:
			done = 1;
			break;
		case ButtonPress:
			break;
		}
	}

	if(drawn)
		outline_hide();
	XUngrabPointer(dpy, CurrentTime);

	if(bdx < 2*BORDER + MINSIZE || bdy < 2*BORDER + MINSIZE)
		return 0;

	*rx = bx;
	*ry = by;
	*rdx = bdx;
	*rdy = bdy;
	return 1;
}

static void
setsweep(int bx, int by, unsigned int bdx, unsigned int bdy)
{
	clampframe(&bx, &by, &bdx, &bdy);
	sweep_x = bx + BORDER;
	sweep_y = by + BORDER;
	sweep_dx = bdx - 2 * BORDER;
	sweep_dy = bdy - 2 * BORDER;
	sweep_pending = time(NULL);
}

static void
reshapeclient(Client *c)
{
	int bx, by;
	unsigned int bdx, bdy;

	if(!c)
		return;
	if(!dosweep(0, 0, 0, &bx, &by, &bdx, &bdy))
		return;
	c->x = bx + BORDER;
	c->y = by + BORDER;
	c->dx = bdx - 2 * BORDER;
	c->dy = bdy - 2 * BORDER;
	clampgeom(c);
	clearsnap(c);
	applylayout(c);
	raisebar();
}

static void
pullclient(Client *c, int bl, XButtonEvent *start)
{
	XEvent ev;
	int ox = c->x, oy = c->y, cx = start->x_root, cy = start->y_root;
	int bx = ox - BORDER, by = oy - BORDER;
	unsigned int odx = c->dx, ody = c->dy;
	unsigned int bdx = odx + 2*BORDER, bdy = ody + 2*BORDER;

	if(XGrabPointer(dpy, root, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root,
		c_border[bl], CurrentTime) != GrabSuccess)
		return;

	outline_show(bx, by, bdx, bdy);

	for(;;){
		XMaskEvent(dpy, ButtonPressMask|ButtonReleaseMask
			|PointerMotionMask, &ev);
		if(ev.type == MotionNotify){
			int ddx = ev.xmotion.x_root - cx;
			int ddy = ev.xmotion.y_root - cy;
			int nx = ox, ny = oy;
			int ndx = (int)odx, ndy = (int)ody;

			clearsnap(c);

			switch(bl){
			case BorderN:
				ny = oy+ddy; ndy = (int)ody-ddy; break;
			case BorderS:
				ndy = (int)ody+ddy; break;
			case BorderE:
				ndx = (int)odx+ddx; break;
			case BorderW:
				nx = ox+ddx; ndx = (int)odx-ddx; break;
			case BorderNNW: case BorderWNW:
				nx = ox+ddx; ndx = (int)odx-ddx;
				ny = oy+ddy; ndy = (int)ody-ddy; break;
			case BorderNNE: case BorderENE:
				ndx = (int)odx+ddx;
				ny = oy+ddy; ndy = (int)ody-ddy; break;
			case BorderSSE: case BorderESE:
				ndx = (int)odx+ddx;
				ndy = (int)ody+ddy; break;
			case BorderSSW: case BorderWSW:
				nx = ox+ddx; ndx = (int)odx-ddx;
				ndy = (int)ody+ddy; break;
			}
			bx = nx - BORDER; by = ny - BORDER;
			bdx = ndx > 0 ? (unsigned int)ndx + 2*BORDER : 2*BORDER;
			bdy = ndy > 0 ? (unsigned int)ndy + 2*BORDER : 2*BORDER;
			clampframe(&bx, &by, &bdx, &bdy);
			outline_show(bx, by, bdx, bdy);
			XFlush(dpy);
		} else if(ev.type == ButtonPress){
			outline_hide();
			break;
		} else if(ev.type == ButtonRelease){
			outline_hide();
			c->x = bx + BORDER;
			c->y = by + BORDER;
			c->dx = bdx - 2*BORDER;
			c->dy = bdy - 2*BORDER;
			applylayout(c);
			raisebar();
			break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
}

static void
moveclient(Client *c, XButtonEvent *start)
{
	XEvent ev;
	int ox, oy, mx, my, bx, by, zone, tx, ty;
	unsigned int bdx, bdy, tdx, tdy;

	if(!c)
		return;
	ox = c->x; oy = c->y;
	mx = start->x_root; my = start->y_root;
	bx = ox - BORDER; by = oy - BORDER;
	bdx = c->dx + 2*BORDER; bdy = c->dy + 2*BORDER;
	zone = TileNone;

	if(XGrabPointer(dpy, root, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_box,
		CurrentTime) != GrabSuccess)
		return;

	outline_show(bx, by, bdx, bdy);

	for(;;){
		XMaskEvent(dpy, ButtonPressMask|ButtonReleaseMask
			|PointerMotionMask, &ev);
		if(ev.type == MotionNotify){
			bx = ox + (ev.xmotion.x_root - mx) - BORDER;
			by = oy + (ev.xmotion.y_root - my) - BORDER;
			clampframe(&bx, &by, &bdx, &bdy);
			zone = edgezone(ev.xmotion.x_root, ev.xmotion.y_root);
			if(zone != TileNone){
				tilegeom(zone, &tx, &ty, &tdx, &tdy);
				outline_show(tx - BORDER, ty - BORDER, tdx + 2*BORDER, tdy + 2*BORDER);
			} else {
				outline_show(bx, by, bdx, bdy);
			}
			XFlush(dpy);
		} else if(ev.type == ButtonRelease){
			outline_hide();
			if(zone != TileNone){
				tile(c, zone);
			} else {
				clearsnap(c);
				c->x = bx + BORDER;
				c->y = by + BORDER;
				XMoveWindow(dpy, c->frame, bx, by);
				sendconfig(c);
				raisebar();
			}
			break;
		} else if(ev.type == ButtonPress){
			outline_hide();
			break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
}

static void
sweepnew(void)
{
	int bx, by;
	unsigned int bdx, bdy;

	if(!dosweep(0, 0, 0, &bx, &by, &bdx, &bdy))
		return;
	setsweep(bx, by, bdx, bdy);
	spawn(TERM);
}

static Client *
bar_hittest(int x)
{
	for(int i = 0; i < bar_ntabs; i++)
		if(x >= bar_tab_x[i] && x < bar_tab_x[i] + bar_tab_w[i])
			return bar_tabs[i];
	return NULL;
}

static void
buttonpress(XButtonEvent *e)
{
	Client *c;
	const char *cmd;
	int bl, i, px, py, old_fx, old_fy, idx, bx, by;
	unsigned int btn, bdx, bdy;

	btn = e->button;
	if(btn == Button1 && (e->state & ControlMask))
		btn = Button3;
	else if(btn == Button1 && (e->state & Mod1Mask))
		btn = Button2;

	if(launch_visible){
		if(e->window == barwin){
			idx = launcher_hittest(e->x);
			if(idx >= 0 && idx < launch_nfiltered){
				cmd = launch_cmds[launch_filtered[idx]];
				launcher_hide();
				if(btn == Button3 && dosweep(0, 0, 0, &bx, &by, &bdx, &bdy))
					setsweep(bx, by, bdx, bdy);
				spawn(cmd);
				return;
			}
			if(e->x < LAUNCH_FILTER_W + BAR_GAP + BAR_GAP)
				return;
		}
		launcher_hide();
		return;
	}

	if(e->window == barwin){
		if(e->x >= bar_run_x && e->x < bar_run_x + bar_run_w){
			launcher_show();
			return;
		}
		for(i = 0; i < NDESKS; i++){
			if(e->x >= bar_desk_x[i] && e->x < bar_desk_x[i] + bar_desk_w){
				if(btn == Button1)
					switch_to(i);
				else if(btn == Button2 && current && i != curdesk){
					current->virt = i;
					ewmh_set(current->win, net_wm_desk, XA_CARDINAL, 32, &(long){i}, 1);
					XUnmapWindow(dpy, current->frame);
					deskfocus[i] = current;
					ewmh_updateclients();
					focus(topclient());
				} else if(btn == Button3)
					sendtodesktop(current, i);
				return;
			}
		}
		if(e->x >= bar_exit_x && e->x < bar_exit_x + bar_exit_w){
			if(btn == Button1)
				running = 0;
			return;
		}
		if(e->x >= bar_status_x && e->x < bar_exit_x){
			sweepnew();
			return;
		}
		c = bar_hittest(e->x);
		if(c){
			switch(btn){
			case Button1: tile(c, TileMax); break;
			case Button2: closeclient(c); break;
			case Button3: reshapeclient(c); break;
			}
			return;
		}
		if(btn == Button4)
			switch_to((curdesk + NDESKS - 1) % NDESKS);
		else if(btn == Button5)
			switch_to((curdesk + 1) % NDESKS);
		else
			sweepnew();
		return;
	}

	if(e->window == root){
		if(btn == Button4)
			switch_to((curdesk + NDESKS - 1) % NDESKS);
		else if(btn == Button5)
			switch_to((curdesk + 1) % NDESKS);
		else
			sweepnew();
		return;
	}

	c = winclient(e->window);
	if(c){
		promote(c);
		focus(c);
		XAllowEvents(dpy, ReplayPointer, e->time);
		return;
	}

	c = frameclient(e->window);
	if(!c)
		return;
	bl = borderorient(c, e->x, e->y);
	if(bl != BorderUnknown){
		if(btn == Button1){
			if(e->window == last_click_win
			&& e->time - last_click_time < DBLCLICK_MS){
				px = e->x_root;
				py = e->y_root;
				old_fx = c->x - BORDER;
				old_fy = c->y - BORDER;
				last_click_time = 0;
				last_click_win = None;
				tile(c, border2tile[bl]);
				XWarpPointer(dpy, None, root, 0, 0, 0, 0,
					px - old_fx + (c->x - BORDER),
					py - old_fy + (c->y - BORDER));
				return;
			}
			last_click_time = e->time;
			last_click_win = e->window;
			pullclient(c, bl, e);
		} else if(btn == Button2){
			closeclient(c);
		} else if(btn == Button3){
			moveclient(c, e);
		}
		return;
	}
	promote(c);
	focus(c);
}

static void
motionnotify(XMotionEvent *e)
{
	Client *c;
	int bl, idx;

	if(e->window == barwin){
		if(launch_visible){
			idx = launcher_hittest(e->x);
			if(idx >= 0 && idx != launch_sel){
				launch_sel = idx;
				launcher_draw();
			}
		} else if(e->x >= bar_run_x && e->x < bar_run_x + bar_run_w){
			launcher_show();
		} else if((c = bar_hittest(e->x)) && c != current)
			focus(c);
		return;
	}
	c = frameclient(e->window);
	if(!c)
		return;
	bl = borderorient(c, e->x, e->y);
	if(bl == BorderUnknown)
		XUndefineCursor(dpy, c->frame);
	else
		XDefineCursor(dpy, c->frame, c_border[bl]);
}

static void
keypress(XKeyEvent *e)
{
	if(launch_visible)
		launcher_key(e);
}

static void
configreq(XConfigureRequestEvent *e)
{
	Client *c = winclient(e->window);

	if(c){
		if(c->fullscreen || c->tiled){
			sendconfig(c);
			return;
		}
		if(e->value_mask & CWX) c->x = e->x;
		if(e->value_mask & CWY) c->y = e->y;
		if(e->value_mask & CWWidth) c->dx = (unsigned int)e->width;
		if(e->value_mask & CWHeight) c->dy = (unsigned int)e->height;
		clampgeom(c);
		applylayout(c);
		return;
	}
	XConfigureWindow(dpy, e->window, (unsigned int)e->value_mask, &(XWindowChanges){
		.x = e->x, .y = e->y, .width = e->width, .height = e->height,
		.border_width = e->border_width, .sibling = e->above, .stack_mode = e->detail
	});
}

static void
unmapnotify(XUnmapEvent *e)
{
	Client *c = winclient(e->window);

	if(!c)
		return;
	if(c->reparenting){
		c->reparenting = 0;
		return;
	}
	XReparentWindow(dpy, c->win, root, c->x, c->y);
	XRemoveFromSaveSet(dpy, c->win);
	XDestroyWindow(dpy, c->frame);
	unmanage(c);
}

static void
destroynotify(XDestroyWindowEvent *e)
{
	Client *c = winclient(e->window);

	if(!c)
		return;
	XDestroyWindow(dpy, c->frame);
	unmanage(c);
}

static void
propertynotify(XPropertyEvent *e)
{
	Client *c = winclient(e->window);

	if(!c)
		return;
	if(e->atom == XA_WM_NAME || e->atom == net_wm_name){
		getname(c);
		bar_redraw();
	}
}

static void
clientmessage(XClientMessageEvent *e)
{
	Client *c;

	if(e->message_type == net_cur_desk){
		if(e->data.l[0] >= 0 && e->data.l[0] < NDESKS)
			switch_to((int)e->data.l[0]);
		return;
	}
	c = winclient(e->window);
	if(!c)
		return;
	if(e->message_type == net_active){
		if(c->virt != curdesk)
			switch_to(c->virt);
		focus(c);
	} else if(e->message_type == net_wm_state){
		int fs;
		if((Atom)e->data.l[1] != net_wm_state_fs && (Atom)e->data.l[2] != net_wm_state_fs)
			return;
		fs = e->data.l[0] == StateAdd || (e->data.l[0] == StateToggle && !c->fullscreen);
		if(fs == c->fullscreen)
			return;
		if(fs)
			savegeom(c);
		c->fullscreen = fs;
		if(fs){
			ewmh_set(c->win, net_wm_state, XA_ATOM, 32, &net_wm_state_fs, 1);
			ewmh_set(c->win, net_frame_ext, XA_CARDINAL, 32, (long[]){0,0,0,0}, 4);
		} else {
			XDeleteProperty(dpy, c->win, net_wm_state);
			ewmh_set(c->win, net_frame_ext, XA_CARDINAL, 32, (long[]){BORDER,BORDER,BORDER,BORDER}, 4);
		}
		applylayout(c);
		focus(c);
	}
}

static void
scan(void)
{
	unsigned int n;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if(!XQueryTree(dpy, root, &d1, &d2, &wins, &n))
		return;
	for(unsigned int i = 0; i < n; i++)
		if(XGetWindowAttributes(dpy, wins[i], &wa)
		&& !wa.override_redirect && wa.map_state == IsViewable)
			manage(wins[i]);
	if(wins) XFree(wins);
}

static void
setup_bar(void)
{
	XSetWindowAttributes wa;

	bar_fg = getxftcolor(COL_BAR_FG);
	bar_bg = getxftcolor(COL_BAR_BG);
	bar_sel = getxftcolor(COL_BAR_SEL);
	bar_self = getxftcolor(COL_BAR_SELF);
	bar_tab = getxftcolor(COL_BAR_TAB);
	bar_run = getxftcolor(COL_BAR_RUN);
	bar_exit = getxftcolor(COL_BAR_EXIT);
	bar_desk[0] = getxftcolor(COL_BAR_DESK0);
	bar_desk[1] = getxftcolor(COL_BAR_DESK1);
	bar_desk[2] = getxftcolor(COL_BAR_DESK2);
	bar_desk[3] = getxftcolor(COL_BAR_DESK3);

	barw = sw;
	barh = (unsigned int)(xftfont->ascent + xftfont->descent) + BAR_PAD * 2;

	wa.override_redirect = True;
	wa.background_pixel = bar_bg.pixel;
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	barwin = XCreateWindow(dpy, root,
		0, 0, barw, barh, 0,
		DefaultDepth(dpy, screen),
		InputOutput, DefaultVisual(dpy, screen),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

	bargc = XCreateGC(dpy, barwin, 0, NULL);
	barpix = XCreatePixmap(dpy, barwin, barw, barh,
		(unsigned int)DefaultDepth(dpy, screen));
	bardraw = XftDrawCreate(dpy, barpix,
		DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));
	if(!bardraw)
		errx(1, "XftDrawCreate failed");

	scan_path();

	XMapRaised(dpy, barwin);
	initbattery();
	readbattery();
	bar_redraw();
	bar_deadline = time(NULL) + BAR_REFRESH;
}

static void
setup(void)
{
	XSetWindowAttributes wa;

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = (unsigned int)DisplayWidth(dpy, screen);
	sh = (unsigned int)DisplayHeight(dpy, screen);

	XSetErrorHandler(handler);

	wm_protocols  = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wm_delete     = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	wm_state      = XInternAtom(dpy, "WM_STATE", False);
	net_supported   = XInternAtom(dpy, "_NET_SUPPORTED", False);
	net_supporting  = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
	net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	net_active      = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	net_num_desks   = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	net_cur_desk    = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
	net_wm_desk     = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
	net_workarea    = XInternAtom(dpy, "_NET_WORKAREA", False);
	net_wm_name     = XInternAtom(dpy, "_NET_WM_NAME", False);
	net_wm_state    = XInternAtom(dpy, "_NET_WM_STATE", False);
	net_wm_state_fs = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	net_frame_ext   = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
	net_wm_type     = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	net_wm_type_dock   = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	utf8_string     = XInternAtom(dpy, "UTF8_STRING", False);

	col_active   = getcolor(COL_ACTIVE);
	col_inactive = getcolor(COL_INACTIVE);
	col_red      = getcolor(COL_SWEEP_BD);

	xftfont = XftFontOpenName(dpy, screen, XFTFONT);
	if(!xftfont)
		errx(1, "XftFontOpenName failed for %s", XFTFONT);

	c_arrow = makecursor(&bigarrow);
	c_sweep = makecursor(&sweepdata);
	c_box   = makecursor(&boxdata);

	c_border[BorderN]   = XCreateFontCursor(dpy, XC_top_side);
	c_border[BorderE]   = XCreateFontCursor(dpy, XC_right_side);
	c_border[BorderS]   = XCreateFontCursor(dpy, XC_bottom_side);
	c_border[BorderW]   = XCreateFontCursor(dpy, XC_left_side);
	c_border[BorderNNE] = c_border[BorderENE] = XCreateFontCursor(dpy, XC_top_right_corner);
	c_border[BorderSSE] = c_border[BorderESE] = XCreateFontCursor(dpy, XC_bottom_right_corner);
	c_border[BorderSSW] = c_border[BorderWSW] = XCreateFontCursor(dpy, XC_bottom_left_corner);
	c_border[BorderNNW] = c_border[BorderWNW] = XCreateFontCursor(dpy, XC_top_left_corner);

	for(int i = 0; i < 4; i++)
		swout[i] = make_outline_bar();

	wa.cursor = c_arrow;
	wa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
		| ButtonPressMask | PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWCursor | CWEventMask, &wa);
	XSetWindowBackground(dpy, root, getcolor(COL_ROOT_BG));
	XClearWindow(dpy, root);

	signal(SIGCHLD, sigchld);
	signal(SIGTERM, sigterm);
	signal(SIGINT, sigterm);
	signal(SIGHUP, sigterm);

	setup_bar();
	ewmh_setup();
}

static void
cleanup(void)
{
	Client *c, *next;

	for(c = clients; c; c = next){
		next = c->next;
		XReparentWindow(dpy, c->win, root, c->x, c->y);
		XRemoveFromSaveSet(dpy, c->win);
		XDestroyWindow(dpy, c->frame);
		free(c->label);
		free(c);
	}
	XDeleteProperty(dpy, root, net_supported);
	XDeleteProperty(dpy, root, net_supporting);
	XDeleteProperty(dpy, root, net_client_list);
	XDestroyWindow(dpy, wmcheck);
	closebattery();
	for(int i = 0; i < launch_ncmds; i++)
		free(launch_cmds[i]);

	for(int i = 0; i < 4; i++)
		XDestroyWindow(dpy, swout[i]);
	XftDrawDestroy(bardraw);
	XFreePixmap(dpy, barpix);
	XFreeGC(dpy, bargc);
	XDestroyWindow(dpy, barwin);
	XftFontClose(dpy, xftfont);
	XFreeCursor(dpy, c_arrow);
	XFreeCursor(dpy, c_sweep);
	XFreeCursor(dpy, c_box);
	for(int i = BorderN; i < NBorder; i++)
		if(c_border[i] && c_border[i] != c_border[i-1])
			XFreeCursor(dpy, c_border[i]);

	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XCloseDisplay(dpy);
}

static void
run(void)
{
	XEvent ev;
	int xfd;
	fd_set rd;
	struct timeval tv;
	time_t now;

	xfd = ConnectionNumber(dpy);

	while(running){
		now = time(NULL);
		if(now >= bar_deadline){
			readbattery();
			bar_redraw();
			bar_deadline = now + BAR_REFRESH;
		}

		while(XPending(dpy)){
			XNextEvent(dpy, &ev);
			switch(ev.type){
			case KeyPress:
				keypress(&ev.xkey); break;
			case ButtonPress:
				buttonpress(&ev.xbutton); break;
			case MapRequest:
				manage(ev.xmaprequest.window); break;
			case ConfigureRequest:
				configreq(&ev.xconfigurerequest); break;
			case UnmapNotify:
				unmapnotify(&ev.xunmap); break;
			case DestroyNotify:
				destroynotify(&ev.xdestroywindow); break;
			case PropertyNotify:
				propertynotify(&ev.xproperty); break;
			case ClientMessage:
				clientmessage(&ev.xclient); break;
			case MotionNotify:
				motionnotify(&ev.xmotion); break;
			case Expose:
				if(ev.xexpose.count == 0 && ev.xexpose.window == barwin)
					XCopyArea(dpy, barpix, barwin, bargc, 0, 0, barw, barh, 0, 0);
				break;
			}
		}

		FD_ZERO(&rd);
		FD_SET(xfd, &rd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		select(xfd + 1, &rd, NULL, NULL, &tv);
	}
}

int
main(int argc, char *argv[])
{
	if(argc == 2 && strcmp(argv[1], "-v") == 0){
		fprintf(stderr, "9x " VERSION "\n");
		return 0;
	}
	if(argc != 1){
		fprintf(stderr, "usage: 9x [-v]\n");
		return 1;
	}
	if(!(dpy = XOpenDisplay(NULL))){
		fprintf(stderr, "9x: cannot open display\n");
		return 1;
	}
	setup();
	scan();
	run();
	cleanup();
	return 0;
}

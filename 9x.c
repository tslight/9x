/*
 * 9x more scum & rats than rio. Just say NEIN.
 *
 * Copyright (c) 2026 Toby Slight <0xff.art>
 */

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <machine/apmvar.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#include "config.h"

#define MAXCLIENTS  512
#define MAXCMDS     2048
#define MINSIZE     20

#define LENGTH(x)   (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))

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

typedef struct Client Client;
struct Client {
	Window  win;
	Window  frame;
	int     x, y;
	unsigned int dx, dy;
	unsigned int odx, ody;
	int     ox, oy;
	int     tiled;
	int     prev_tiled;
	int     fullscreen;
	int     proto;
	int     reparenting;
	int     virt;
	char   *label;
	Client *next;
};

enum { Pdelete = 1, Ptakefocus = 2 };

static Display      *dpy;
static int           screen;
static Window        root;
static unsigned int  sw, sh;
static XftFont      *xftfont;
static Cursor        c_arrow, c_sweep, c_box;
static Cursor        c_border[NBorder];
static Atom          wm_protocols, wm_delete, wm_take_focus, wm_state;
static Atom          net_wm_name, utf8_string;
static Client       *clients;
static Client       *current;
static unsigned long col_active, col_inactive;
static unsigned long col_red;
static volatile sig_atomic_t running = 1;

static Window        swN, swS, swE, swW;

static int           sweep_pending;
static int           sweep_x, sweep_y;
static unsigned int  sweep_dx, sweep_dy;

static Time          last_click_time;
static Window        last_click_win;

static int           curdesk;
static Client       *deskfocus[NDESKS];

static Window        barwin;
static Pixmap        barpix;
static GC            bargc;
static XftDraw      *bardraw;
static XftColor      bar_fg, bar_bg, bar_sel, bar_self, bar_tab;
static XftColor      bar_run, bar_exit, bar_desk[NDESKS];
static unsigned long col_bar_bd;
static unsigned int  barw, barh;
static int           bar_batt = -1;
static int           bar_onac;
static time_t        bar_deadline;

static Client       *bar_tabs[MAXCLIENTS];
static int           bar_tab_x[MAXCLIENTS];
static int           bar_tab_w[MAXCLIENTS];
static int           bar_ntabs;

static int           bar_run_x, bar_run_w;
static int           bar_desk_x[NDESKS], bar_desk_w;
static int           bar_status_x;
static int           bar_exit_x, bar_exit_w;

static int           launch_visible;
static char         *launch_cmds[MAXCMDS];
static int           launch_ncmds;
static int           launch_sel = -1;
static int           launch_scroll;
static char          launch_filter[256];
static int           launch_filterlen;
static int           launch_filtered[MAXCMDS];
static int           launch_nfiltered;
static int           launch_item_x[MAXCMDS];
static int           launch_item_w[MAXCMDS];
static int           launch_nitems;

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
	XColor c;

	c.red   = (unsigned short)(((rgb >> 16) & 0xFF) * 0x101);
	c.green = (unsigned short)(((rgb >> 8) & 0xFF) * 0x101);
	c.blue  = (unsigned short)((rgb & 0xFF) * 0x101);
	c.flags = DoRed | DoGreen | DoBlue;
	if(!XAllocColor(dpy, DefaultColormap(dpy, screen), &c))
		return WhitePixel(dpy, screen);
	return c.pixel;
}

static XftColor
getxftcolor(unsigned long rgb)
{
	XRenderColor rc;
	XftColor c;

	rc.red   = (unsigned short)(((rgb >> 16) & 0xFF) * 0x101);
	rc.green = (unsigned short)(((rgb >> 8) & 0xFF) * 0x101);
	rc.blue  = (unsigned short)((rgb & 0xFF) * 0x101);
	rc.alpha = 0xFFFF;
	XftColorAllocValue(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &rc, &c);
	return c;
}

static Cursor
makecursor(Cursordata *d)
{
	Pixmap f, m;
	Cursor cur;
	XColor bl, wh, dummy;

	XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
		"black", &bl, &dummy);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
		"white", &wh, &dummy);
	f = XCreatePixmapFromBitmapData(dpy, root, (char *)d->fore,
		d->width, d->width, 1, 0, 1);
	m = XCreatePixmapFromBitmapData(dpy, root, (char *)d->mask,
		d->width, d->width, 1, 0, 1);
	cur = XCreatePixmapCursor(dpy, f, m, &bl, &wh,
		d->hot[0], d->hot[1]);
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
	(void)d;
	(void)e;
	return 0;
}

static void
sigchld(int sig)
{
	(void)sig;
	while(waitpid(-1, NULL, WNOHANG) > 0)
		;
}

static void
sigterm(int sig)
{
	(void)sig;
	running = 0;
}

static void
spawn(const char *cmd)
{
	pid_t p;

	p = fork();
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

#ifdef __OpenBSD__
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

static void
scan_path(void)
{
	char *path, *p, *dir;
	DIR *d;
	struct dirent *ent;
	int i;

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
			for(i = 0; i < launch_ncmds; i++)
				if(strcmp(launch_cmds[i], ent->d_name) == 0)
					break;
			if(i < launch_ncmds)
				continue;
			launch_cmds[launch_ncmds] = strdup(ent->d_name);
			if(launch_cmds[launch_ncmds])
				launch_ncmds++;
		}
		closedir(d);
	}
	free(path);
	qsort(launch_cmds, (size_t)launch_ncmds, sizeof(char *), cmdcmp);
}

static void
bar_drawbtn(int x, int w, const char *s, int len, int sel, XftColor *bg)
{
	int ty = BAR_PAD + xftfont->ascent;
	if(w <= 0)
		return;
	XftDrawRect(bardraw, sel ? &bar_sel : bg, x, 0, (unsigned int)w, barh);
	XSetForeground(dpy, bargc, col_bar_bd);
	XDrawRectangle(dpy, barpix, bargc, x, 0, (unsigned int)(w - 1), barh - 1);
	XftDrawStringUtf8(bardraw, sel ? &bar_self : &bar_fg, xftfont,
		x + BAR_BTN_PAD, ty, (const FcChar8 *)s, len);
}

static void
launcher_draw(void)
{
	int i, x, ty, tw, maxw;
	const char *name;
	int nlen;
	XGlyphInfo ext;

	XSetForeground(dpy, bargc, bar_bg.pixel);
	XFillRectangle(dpy, barpix, bargc, 0, 0, sw, barh);

	ty = BAR_PAD + xftfont->ascent;
	x = BAR_GAP;

	XftDrawRect(bardraw, &bar_tab, x, 0, 200, barh);
	XSetForeground(dpy, bargc, col_bar_bd);
	XDrawRectangle(dpy, barpix, bargc, x, 0, 199, barh - 1);
	XftDrawStringUtf8(bardraw, &bar_fg, xftfont,
		x + BAR_BTN_PAD, ty, (const FcChar8 *)launch_filter, launch_filterlen);
	XftTextExtentsUtf8(dpy, xftfont, (const FcChar8 *)launch_filter, launch_filterlen, &ext);
	XSetForeground(dpy, bargc, bar_fg.pixel);
	XFillRectangle(dpy, barpix, bargc, x + BAR_BTN_PAD + ext.xOff, BAR_PAD, 2, barh - 2*BAR_PAD);
	x += 200 + BAR_GAP;

	maxw = (int)sw - x - BAR_GAP;
	launch_nitems = 0;
	for(i = launch_scroll; i < launch_nfiltered && x < (int)sw - BAR_GAP && launch_nitems < MAXCMDS; i++){
		int idx = launch_filtered[i];
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
		if(i == launch_sel){
			XftDrawRect(bardraw, &bar_sel, x, 0, (unsigned int)tw, barh);
			XftDrawStringUtf8(bardraw, &bar_self, xftfont,
				x + BAR_BTN_PAD, ty, (const FcChar8 *)name, nlen);
		} else {
			XftDrawRect(bardraw, &bar_tab, x, 0, (unsigned int)tw, barh);
			XftDrawStringUtf8(bardraw, &bar_fg, xftfont,
				x + BAR_BTN_PAD, ty, (const FcChar8 *)name, nlen);
		}
		XSetForeground(dpy, bargc, col_bar_bd);
		XDrawRectangle(dpy, barpix, bargc, x, 0, (unsigned int)(tw - 1), barh - 1);
		x += tw + BAR_GAP;
	}

	XCopyArea(dpy, barpix, barwin, bargc, 0, 0, sw, barh, 0, 0);
}

static int
launcher_hittest(int x)
{
	int i;
	for(i = 0; i < launch_nitems; i++)
		if(x >= launch_item_x[i] && x < launch_item_x[i] + launch_item_w[i])
			return launch_scroll + i;
	return -1;
}

static int
cistrstr(const char *h, const char *n)
{
	size_t i, j, nlen;
	if(!n[0]) return 1;
	nlen = strlen(n);
	for(i = 0; h[i]; i++){
		for(j = 0; j < nlen && h[i+j]; j++)
			if(tolower((unsigned char)h[i+j]) != tolower((unsigned char)n[j]))
				break;
		if(j == nlen) return 1;
	}
	return 0;
}

static void
launcher_filter(void)
{
	int i;
	launch_nfiltered = 0;
	for(i = 0; i < launch_ncmds && launch_nfiltered < MAXCMDS; i++){
		if(launch_filterlen == 0 || cistrstr(launch_cmds[i], launch_filter)){
			launch_filtered[launch_nfiltered++] = i;
		}
	}
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
	launch_scroll = 0;
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
	int len;

	len = XLookupString(e, buf, sizeof(buf) - 1, &ks, NULL);

	if(ks == XK_Escape){
		launcher_hide();
		return;
	}
	if(ks == XK_Return || ks == XK_KP_Enter){
		if(launch_sel >= 0 && launch_sel < launch_nfiltered){
			const char *cmd = launch_cmds[launch_filtered[launch_sel]];
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
	if(ks == XK_Left || ks == XK_Up){
		if(launch_sel > 0){
			launch_sel--;
			if(launch_sel < launch_scroll)
				launch_scroll = launch_sel;
			launcher_draw();
		}
		return;
	}
	if(ks == XK_Right || ks == XK_Down){
		if(launch_sel < launch_nfiltered - 1){
			launch_sel++;
			if(launch_sel >= launch_scroll + launch_nitems && launch_nitems > 0)
				launch_scroll = launch_sel - launch_nitems + 1;
			launcher_draw();
		}
		return;
	}
	if(ks == XK_Tab){
		if(e->state & ShiftMask){
			if(launch_sel > 0){
				launch_sel--;
				if(launch_sel < launch_scroll)
					launch_scroll = launch_sel;
			}
		} else {
			if(launch_sel < launch_nfiltered - 1){
				launch_sel++;
				if(launch_sel >= launch_scroll + launch_nitems && launch_nitems > 0)
					launch_scroll = launch_sel - launch_nitems + 1;
			}
		}
		launcher_draw();
		return;
	}

	if(len > 0 && len < (int)sizeof(launch_filter) - launch_filterlen - 1){
		if(buf[0] >= ' ' && buf[0] < 127){
			memcpy(launch_filter + launch_filterlen, buf, (size_t)len);
			launch_filterlen += len;
			launch_filter[launch_filterlen] = '\0';
			launcher_filter();
			launcher_draw();
		}
	}
}

static void
bar_drawtabs(int x, int tabarea, int rightw)
{
	Client *c;
	const char *name;
	char trunc[256];
	int i, j, tw, tabx, nlen, drawn, ty;

	bar_ntabs = 0;
	for(c = clients; c && bar_ntabs < MAXCLIENTS; c = c->next)
		if(c->virt == curdesk)
			bar_tabs[bar_ntabs++] = c;

	/* sort by window id for stable order */
	for(i = 0; i < bar_ntabs - 1; i++){
		int min = i;
		for(j = i + 1; j < bar_ntabs; j++)
			if(bar_tabs[j]->win < bar_tabs[min]->win)
				min = j;
		if(min != i){
			Client *tmp = bar_tabs[i];
			bar_tabs[i] = bar_tabs[min];
			bar_tabs[min] = tmp;
		}
	}

	if(bar_ntabs <= 0 || tabarea <= 0)
		return;

	ty = BAR_PAD + xftfont->ascent;
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

		int maxw = tw - 2 * BAR_BTN_PAD;
		if(maxw > 0 && xft_textwidth(name, nlen) > maxw){
			while(nlen > 0 && xft_textwidth(name, nlen) + xft_textwidth("..", 2) > maxw)
				nlen--;
			if(nlen > 0){
				memcpy(trunc, name, (size_t)nlen);
				trunc[nlen] = '.';
				trunc[nlen+1] = '.';
				trunc[nlen+2] = '\0';
				name = trunc;
				nlen += 2;
			}
		}

		bar_tab_x[drawn] = tabx;
		bar_tab_w[drawn] = tw;
		bar_tabs[drawn] = c;
		drawn++;

		if(c == current){
			XftDrawRect(bardraw, &bar_sel,
				tabx, 0, (unsigned int)tw, barh);
			XftDrawStringUtf8(bardraw, &bar_self, xftfont,
				tabx + BAR_BTN_PAD, ty, (const FcChar8 *)name, nlen);
		} else {
			XftDrawRect(bardraw, &bar_tab,
				tabx, 0, (unsigned int)tw, barh);
			XftDrawStringUtf8(bardraw, &bar_fg, xftfont,
				tabx + BAR_BTN_PAD, ty, (const FcChar8 *)name, nlen);
		}
		XSetForeground(dpy, bargc, col_bar_bd);
		XDrawRectangle(dpy, barpix, bargc, tabx, 0,
			(unsigned int)(tw - 1), barh - 1);
		tabx += tw + BAR_GAP;
	}
	bar_ntabs = drawn;
}

static void
bar_redraw(void)
{
	time_t now;
	struct tm *t;
	char tbuf[64], bbuf[32];
	int x, blen, tlen;
	int rightw, tabarea, statusw, i;

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

	/* Run button */
	bar_run_x = x;
	bar_run_w = xft_textwidth("Run", 3) + 2 * BAR_BTN_PAD;
	bar_drawbtn(x, bar_run_w, "Run", 3, 0, &bar_run);
	x += bar_run_w + BAR_GAP * 2;

	/* Right side: Desktops, Battery, Clock, Exit */
	bar_desk_w = xft_textwidth("0", 1) + 2 * BAR_BTN_PAD;
	bar_exit_w = xft_textwidth("Exit", 4) + 2 * BAR_BTN_PAD;
	statusw = xft_textwidth(bbuf, blen) + xft_textwidth(tbuf, tlen) + BAR_GAP * 2;
	rightw = (bar_desk_w + BAR_GAP) * NDESKS + statusw + bar_exit_w + BAR_PAD;
	tabarea = (int)barw - x - rightw;

	bar_drawtabs(x, tabarea, rightw);

	/* Desktop buttons */
	x = (int)barw - rightw + BAR_PAD;
	for(i = 0; i < NDESKS; i++){
		char dbuf[2];
		dbuf[0] = (char)('1' + i);
		dbuf[1] = '\0';
		bar_desk_x[i] = x;
		bar_drawbtn(x, bar_desk_w, dbuf, 1, i == curdesk, &bar_desk[i]);
		x += bar_desk_w + BAR_GAP;
	}

	/* Battery & Clock */
	bar_status_x = x;
	if(bbuf[0]){
		XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, BAR_PAD + xftfont->ascent,
			(const FcChar8 *)bbuf, blen);
		x += xft_textwidth(bbuf, blen);
	}
	XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, BAR_PAD + xftfont->ascent,
		(const FcChar8 *)tbuf, tlen);
	x += xft_textwidth(tbuf, tlen) + BAR_GAP * 2;

	/* Exit button */
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
	if(x <= CORNER){
		if(y <= BORDER) return BorderNNW;
		if(y >= fh - BORDER) return BorderSSW;
	}
	if(x >= fw - CORNER){
		if(y <= BORDER) return BorderNNE;
		if(y >= fh - BORDER) return BorderSSE;
	}
	if(y <= BORDER) return BorderN;
	if(y >= fh - BORDER) return BorderS;
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

	if(c->label){
		free(c->label);
		c->label = NULL;
	}
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
	int n, i;

	c->proto = 0;
	if(XGetWMProtocols(dpy, c->win, &protos, &n)){
		for(i = 0; i < n; i++){
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
	XEvent ev;

	memset(&ev, 0, sizeof ev);
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
	long data[2];

	data[0] = state;
	data[1] = (long)None;
	XChangeProperty(dpy, c->win, wm_state, wm_state, 32,
		PropModeReplace, (unsigned char *)data, 2);
}

static void
sendconfig(Client *c)
{
	XConfigureEvent ce;

	memset(&ce, 0, sizeof ce);
	ce.type = ConfigureNotify;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = (int)c->dx;
	ce.height = (int)c->dy;
	ce.border_width = 0;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
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
unfocus(Client *c)
{
	if(!c)
		return;
	setborder(c, 0);
	grabbuttons(c, 0);
}

static void
focus(Client *c)
{
	if(current && current != c)
		unfocus(current);
	if(!c)
		return;
	setborder(c, 1);
	grabbuttons(c, 1);
	XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
	if(c->proto & Ptakefocus)
		sendcmessage(c->win, wm_protocols, wm_take_focus);
	XSync(dpy, False);
	XClearArea(dpy, c->win, 0, 0, 0, 0, True);
	XRaiseWindow(dpy, c->frame);
	current = c;
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
		XMoveResizeWindow(dpy, c->win, 0, 0, c->dx, c->dy);
	} else {
		XMoveResizeWindow(dpy, c->frame,
			c->x - BORDER, c->y - BORDER,
			c->dx + 2 * BORDER, c->dy + 2 * BORDER);
		XMoveResizeWindow(dpy, c->win,
			BORDER, BORDER, c->dx, c->dy);
	}
}

static void focusnext(void);

static void
switch_to(int n)
{
	Client *c;

	if(n < 0 || n >= NDESKS || n == curdesk)
		return;
	deskfocus[curdesk] = current;
	curdesk = n;
	for(c = clients; c; c = c->next){
		if(c->virt != curdesk)
			XUnmapWindow(dpy, c->frame);
		else
			XMapWindow(dpy, c->frame);
	}
	current = deskfocus[curdesk];
	if(current)
		focus(current);
	else
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
	raisebar();
	bar_redraw();
}

static void
sendtodesktop(Client *c, int n)
{
	if(!c || n < 0 || n >= NDESKS || n == curdesk)
		return;
	c->virt = n;
	XUnmapWindow(dpy, c->frame);
	if(current == c){
		current = NULL;
		focusnext();
	}
	deskfocus[n] = c;
	switch_to(n);
}

static void
focusnext(void)
{
	Client *c;

	current = NULL;
	for(c = clients; c; c = c->next)
		if(c->virt == curdesk){
			focus(c);
			return;
		}
	XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
}

static Client *
manage(Window w)
{
	Client *c;
	XWindowAttributes wa;
	XSetWindowAttributes sa;
	XSizeHints hints;
	long supplied;

	if(!XGetWindowAttributes(dpy, w, &wa))
		return NULL;
	if(wa.override_redirect)
		return NULL;
	c = winclient(w);
	if(c)
		return c;

	c = calloc(1, sizeof *c);
	if(!c)
		return NULL;

	c->win = w;
	c->virt = curdesk;

	if(sweep_pending){
		c->x = sweep_x;
		c->y = sweep_y;
		c->dx = sweep_dx;
		c->dy = sweep_dy;
		sweep_pending = 0;
	} else {
		c->dx = (unsigned int)wa.width;
		c->dy = (unsigned int)wa.height;
		c->x = wa.x;
		c->y = wa.y;

		memset(&hints, 0, sizeof hints);
		if(XGetWMNormalHints(dpy, w, &hints, &supplied)
		&& (hints.flags & (USPosition | PPosition))
		&& c->x > 0 && c->y > 0){
			/* honour explicit position */
		} else {
			c->x = (int)(sw - c->dx) / 2;
			c->y = (int)barh + (int)(sh - barh - c->dy) / 2;
		}

		if(c->x + (int)c->dx + BORDER > (int)sw)
			c->x = (int)sw - (int)c->dx - BORDER;
		if(c->y + (int)c->dy + BORDER > (int)sh)
			c->y = (int)sh - (int)c->dy - BORDER;
		if(c->x < BORDER)
			c->x = BORDER;
		if(c->y < (int)barh + BORDER)
			c->y = (int)barh + BORDER;
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
	XMapWindow(dpy, w);
	XMapWindow(dpy, c->frame);
	XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
	XSetWindowBorderWidth(dpy, w, 0);

	c->next = clients;
	clients = c;
	setwmstate(c, NormalState);
	focus(c);
	sendconfig(c);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
		(int)c->dx / 2, (int)c->dy / 2);
	bar_redraw();
	return c;
}

static void
unmanage(Client *c)
{
	Client **pp;
	int i;

	for(pp = &clients; *pp; pp = &(*pp)->next)
		if(*pp == c){
			*pp = c->next;
			break;
		}
	for(i = 0; i < NDESKS; i++)
		if(deskfocus[i] == c)
			deskfocus[i] = NULL;
	if(current == c)
		focusnext();
	raisebar();
	bar_redraw();
	if(c->label)
		free(c->label);
	free(c);
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
	c->prev_tiled = TileNone;
	c->fullscreen = 0;
}

static void
tilegeom(int dir, int *nx, int *ny, unsigned int *ndx, unsigned int *ndy)
{
	*nx = BORDER;
	*ny = (int)barh + BORDER;
	*ndx = sw - 2 * BORDER;
	*ndy = sh - barh - 2 * BORDER;

	switch(dir){
	case TileN:
		*ndy = (sh - barh) / 2 - 2 * BORDER;
		break;
	case TileS:
		*ny = (int)barh + (int)(sh - barh) / 2 + BORDER;
		*ndy = (sh - barh) / 2 - 2 * BORDER;
		break;
	case TileW:
		*ndx = sw / 2 - 2 * BORDER;
		break;
	case TileE:
		*nx = (int)sw / 2 + BORDER;
		*ndx = sw / 2 - 2 * BORDER;
		break;
	case TileNW:
		*ndx = sw / 2 - 2 * BORDER;
		*ndy = (sh - barh) / 2 - 2 * BORDER;
		break;
	case TileNE:
		*nx = (int)sw / 2 + BORDER;
		*ndx = sw / 2 - 2 * BORDER;
		*ndy = (sh - barh) / 2 - 2 * BORDER;
		break;
	case TileSW:
		*ny = (int)barh + (int)(sh - barh) / 2 + BORDER;
		*ndx = sw / 2 - 2 * BORDER;
		*ndy = (sh - barh) / 2 - 2 * BORDER;
		break;
	case TileSE:
		*nx = (int)sw / 2 + BORDER;
		*ny = (int)barh + (int)(sh - barh) / 2 + BORDER;
		*ndx = sw / 2 - 2 * BORDER;
		*ndy = (sh - barh) / 2 - 2 * BORDER;
		break;
	case TileMax:
		break;
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
		if(c->prev_tiled){
			tilegeom(c->prev_tiled, &nx, &ny, &ndx, &ndy);
			c->x = nx;
			c->y = ny;
			c->dx = ndx;
			c->dy = ndy;
			c->tiled = c->prev_tiled;
		} else {
			c->x = c->ox;
			c->y = c->oy;
			c->dx = c->odx;
			c->dy = c->ody;
			c->tiled = TileNone;
		}
		c->prev_tiled = TileNone;
	} else {
		if(!c->tiled){
			c->ox = c->x;
			c->oy = c->y;
			c->odx = c->dx;
			c->ody = c->dy;
		}
		c->prev_tiled = c->tiled;
		tilegeom(dir, &nx, &ny, &ndx, &ndy);
		c->x = nx;
		c->y = ny;
		c->dx = ndx;
		c->dy = ndy;
		c->tiled = dir;
	}
	applylayout(c);
	sendconfig(c);
	raisebar();
}

static void
maximize(Client *c)
{
	tile(c, TileMax);
}

static void
fullscreen(Client *c)
{
	int nx, ny;
	unsigned int ndx, ndy;

	if(!c)
		return;
	if(c->fullscreen){
		c->fullscreen = 0;
		if(c->tiled){
			tilegeom(c->tiled, &nx, &ny, &ndx, &ndy);
			c->x = nx;
			c->y = ny;
			c->dx = ndx;
			c->dy = ndy;
		} else {
			c->x = c->ox;
			c->y = c->oy;
			c->dx = c->odx;
			c->dy = c->ody;
		}
		applylayout(c);
		sendconfig(c);
		raisebar();
	} else {
		if(!c->tiled){
			c->ox = c->x;
			c->oy = c->y;
			c->odx = c->dx;
			c->ody = c->dy;
		}
		c->fullscreen = 1;
		c->x = 0;
		c->y = 0;
		c->dx = sw;
		c->dy = sh;
		applylayout(c);
		sendconfig(c);
		XRaiseWindow(dpy, c->frame);
	}
}

static void
outline_show(int x, int y, unsigned int w, unsigned int h)
{
	XMoveResizeWindow(dpy, swN, x, y, MAX(w, 1), BORDER);
	XMoveResizeWindow(dpy, swS, x, y+(int)h-BORDER, MAX(w, 1), BORDER);
	XMoveResizeWindow(dpy, swW, x, y, BORDER, MAX(h, 1));
	XMoveResizeWindow(dpy, swE, x+(int)w-BORDER, y, BORDER, MAX(h, 1));
	XMapRaised(dpy, swN);
	XMapRaised(dpy, swS);
	XMapRaised(dpy, swW);
	XMapRaised(dpy, swE);
}

static void
outline_hide(void)
{
	XUnmapWindow(dpy, swN);
	XUnmapWindow(dpy, swS);
	XUnmapWindow(dpy, swW);
	XUnmapWindow(dpy, swE);
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
	int bx, by, drawn, done;
	unsigned int bdx, bdy;

	if(XGrabPointer(dpy, root, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_sweep,
		CurrentTime) != GrabSuccess)
		return 0;

	drawn = 0;
	bx = by = 0;
	bdx = bdy = 0;

	XSync(dpy, False);
	while(XCheckMaskEvent(dpy, ButtonReleaseMask|ButtonPressMask, &ev))
		;

	if(!have_origin){
		done = 0;
		while(!done){
			XMaskEvent(dpy, ButtonPressMask|ButtonReleaseMask
				|PointerMotionMask, &ev);
			if(ev.type == ButtonPress){
				sx = ev.xbutton.x_root;
				sy = ev.xbutton.y_root;
				done = 1;
			}
		}
	}

	done = 0;
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
			break;
		case ButtonPress:
			done = 1;
			break;
		}
	}

	if(drawn)
		outline_hide();
	XUngrabPointer(dpy, CurrentTime);

	if(bdx <= 2*BORDER + MINSIZE || bdy <= 2*BORDER + MINSIZE)
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
	sweep_x = bx + BORDER;
	sweep_y = by + BORDER;
	sweep_dx = bdx - 2 * BORDER;
	sweep_dy = bdy - 2 * BORDER;
	sweep_pending = 1;
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
	clearsnap(c);
	applylayout(c);
	sendconfig(c);
	raisebar();
}

static void
pullclient(Client *c, int bl, XButtonEvent *start)
{
	XEvent ev;
	int ox, oy, cx, cy;
	unsigned int odx, ody;
	int bx, by;
	unsigned int bdx, bdy;

	ox = c->x; oy = c->y;
	odx = c->dx; ody = c->dy;
	cx = start->x_root; cy = start->y_root;

	if(XGrabPointer(dpy, root, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root,
		c_border[bl], CurrentTime) != GrabSuccess)
		return;

	bx = ox - BORDER; by = oy - BORDER;
	bdx = odx + 2*BORDER; bdy = ody + 2*BORDER;
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
			default:
				break;
			}
			if(ndx < MINSIZE) ndx = MINSIZE;
			if(ndy < MINSIZE) ndy = MINSIZE;
			bx = nx - BORDER; by = ny - BORDER;
			bdx = (unsigned int)ndx + 2*BORDER;
			bdy = (unsigned int)ndy + 2*BORDER;
			outline_show(bx, by, bdx, bdy);
			XFlush(dpy);
		} else if(ev.type == ButtonPress){
			outline_hide();
			break;
		} else if(ev.type == ButtonRelease){
			outline_hide();
			c->x = bx + BORDER; c->y = by + BORDER;
			c->dx = bdx - 2*BORDER; c->dy = bdy - 2*BORDER;
			applylayout(c);
			sendconfig(c);
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
	int ox, oy, mx, my;
	int bx, by;
	unsigned int bdx, bdy;

	if(!c)
		return;
	ox = c->x; oy = c->y;
	mx = start->x_root; my = start->y_root;

	if(XGrabPointer(dpy, root, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_box,
		CurrentTime) != GrabSuccess)
		return;

	bdx = c->dx + 2*BORDER; bdy = c->dy + 2*BORDER;
	bx = ox - BORDER; by = oy - BORDER;
	outline_show(bx, by, bdx, bdy);

	for(;;){
		XMaskEvent(dpy, ButtonPressMask|ButtonReleaseMask
			|PointerMotionMask, &ev);
		if(ev.type == MotionNotify){
			clearsnap(c);
			bx = ox + (ev.xmotion.x_root - mx) - BORDER;
			by = oy + (ev.xmotion.y_root - my) - BORDER;
			outline_show(bx, by, bdx, bdy);
			XFlush(dpy);
		} else if(ev.type == ButtonRelease){
			outline_hide();
			c->x = bx + BORDER;
			c->y = by + BORDER;
			XMoveWindow(dpy, c->frame, bx, by);
			sendconfig(c);
			raisebar();
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
	int i;
	for(i = 0; i < bar_ntabs; i++)
		if(x >= bar_tab_x[i] && x < bar_tab_x[i] + bar_tab_w[i])
			return bar_tabs[i];
	return NULL;
}

static void
buttonpress(XButtonEvent *e)
{
	Client *c;
	int bl, i;
	unsigned int btn;

	btn = e->button;
	if(btn == Button1 && (e->state & ControlMask))
		btn = Button3;
	else if(btn == Button1 && (e->state & Mod1Mask))
		btn = Button2;

	/* launcher click handling */
	if(launch_visible){
		if(e->window == barwin){
			int idx = launcher_hittest(e->x);
			if(idx >= 0 && idx < launch_nfiltered){
				const char *cmd = launch_cmds[launch_filtered[idx]];
				launcher_hide();
				if(btn == Button3){
					int bx, by;
					unsigned int bdx, bdy;
					if(dosweep(0, 0, 0, &bx, &by, &bdx, &bdy))
						setsweep(bx, by, bdx, bdy);
				}
				spawn(cmd);
				return;
			}
			/* click on input field - ignore */
			if(e->x < 200 + BAR_GAP + BAR_GAP)
				return;
		}
		launcher_hide();
		return;
	}

	if(e->window == barwin){
		if(e->x >= bar_run_x && e->x < bar_run_x + bar_run_w){
			return;
		}
		for(i = 0; i < NDESKS; i++){
			if(e->x >= bar_desk_x[i] && e->x < bar_desk_x[i] + bar_desk_w){
				if(btn == Button1)
					switch_to(i);
				else if(btn == Button2){
					if(current && i != curdesk){
						Client *tosend = current;
						tosend->virt = i;
						XUnmapWindow(dpy, tosend->frame);
						deskfocus[i] = tosend;
						focusnext();
						bar_redraw();
					}
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
			case Button1:
				maximize(c);
				break;
			case Button2:
				closeclient(c);
				break;
			case Button3:
				reshapeclient(c);
				break;
			}
			return;
		}
		return;
	}

	if(e->window == root){
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
			/* double-click: tile to that side/corner */
			if(e->window == last_click_win
			&& e->time - last_click_time < DBLCLICK_MS){
				int px = e->x_root;
				int py = e->y_root;
				int old_fx = c->x - BORDER;
				int old_fy = c->y - BORDER;
				int dir;
				last_click_time = 0;
				last_click_win = None;
				if(bl == BorderNNW || bl == BorderWNW)
					dir = TileNW;
				else if(bl == BorderNNE || bl == BorderENE)
					dir = TileNE;
				else if(bl == BorderSSW || bl == BorderWSW)
					dir = TileSW;
				else if(bl == BorderSSE || bl == BorderESE)
					dir = TileSE;
				else if(bl == BorderN)
					dir = TileN;
				else if(bl == BorderS)
					dir = TileS;
				else if(bl == BorderW)
					dir = TileW;
				else
					dir = TileE;
				tile(c, dir);
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
	int bl;

	if(e->window == barwin){
		if(launch_visible)
			return;
		if(e->x >= bar_run_x && e->x < bar_run_x + bar_run_w){
			launcher_show();
		} else {
			c = bar_hittest(e->x);
			if(c && c != current)
				focus(c);
		}
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
	KeySym ks;

	if(launch_visible){
		launcher_key(e);
		return;
	}

	ks = XLookupKeysym(e, 0);
	if(ks == XK_F11)
		fullscreen(current);
}

static void
configreq(XConfigureRequestEvent *e)
{
	XWindowChanges wc;
	Client *c;

	c = winclient(e->window);
	if(c){
		if(c->fullscreen || c->tiled){
			sendconfig(c);
			return;
		}
		if(e->value_mask & CWX) c->x = e->x;
		if(e->value_mask & CWY) c->y = e->y;
		if(e->value_mask & CWWidth) c->dx = (unsigned int)e->width;
		if(e->value_mask & CWHeight) c->dy = (unsigned int)e->height;
		applylayout(c);
		sendconfig(c);
		return;
	}
	wc.x = e->x;
	wc.y = e->y;
	wc.width = e->width;
	wc.height = e->height;
	wc.border_width = e->border_width;
	wc.sibling = e->above;
	wc.stack_mode = e->detail;
	XConfigureWindow(dpy, e->window, (unsigned int)e->value_mask, &wc);
}

static void
unmapnotify(XUnmapEvent *e)
{
	Client *c;

	c = winclient(e->window);
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
	Client *c;

	c = winclient(e->window);
	if(!c)
		return;
	XDestroyWindow(dpy, c->frame);
	unmanage(c);
}

static void
propertynotify(XPropertyEvent *e)
{
	Client *c;

	c = winclient(e->window);
	if(!c)
		return;
	if(e->atom == XA_WM_NAME || e->atom == net_wm_name){
		getname(c);
		bar_redraw();
	}
}

static void
scan(void)
{
	unsigned int i, n;
	Window d1, d2, *wins;
	XWindowAttributes wa;

	wins = NULL;
	if(!XQueryTree(dpy, root, &d1, &d2, &wins, &n))
		return;
	for(i = 0; i < n; i++){
		if(!XGetWindowAttributes(dpy, wins[i], &wa))
			continue;
		if(wa.override_redirect || wa.map_state != IsViewable)
			continue;
		manage(wins[i]);
	}
	if(wins) XFree(wins);
}

static void
grabkeys(void)
{
	unsigned int mods[] = { 0, LockMask, Mod2Mask, LockMask|Mod2Mask };
	unsigned int i;
	KeyCode f11;

	f11 = XKeysymToKeycode(dpy, XK_F11);
	for(i = 0; i < LENGTH(mods); i++)
		XGrabKey(dpy, f11, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
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
	col_bar_bd = getcolor(COL_BAR_BD);

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
	net_wm_name   = XInternAtom(dpy, "_NET_WM_NAME", False);
	utf8_string   = XInternAtom(dpy, "UTF8_STRING", False);

	col_active   = getcolor(COL_ACTIVE);
	col_inactive = getcolor(COL_INACTIVE);
	col_red      = getcolor(COL_SWEEP_BD);

	xftfont = XftFontOpenName(dpy, screen, XFTFONT);
	if(!xftfont)
		errx(1, "XftFontOpenName failed for %s", XFTFONT);

	c_arrow = makecursor(&bigarrow);
	c_sweep = makecursor(&sweepdata);
	c_box   = makecursor(&boxdata);

	c_border[BorderUnknown] = None;
	c_border[BorderN]   = XCreateFontCursor(dpy, XC_top_side);
	c_border[BorderNNE] = XCreateFontCursor(dpy, XC_top_right_corner);
	c_border[BorderENE] = c_border[BorderNNE];
	c_border[BorderE]   = XCreateFontCursor(dpy, XC_right_side);
	c_border[BorderESE] = XCreateFontCursor(dpy, XC_bottom_right_corner);
	c_border[BorderSSE] = c_border[BorderESE];
	c_border[BorderS]   = XCreateFontCursor(dpy, XC_bottom_side);
	c_border[BorderSSW] = XCreateFontCursor(dpy, XC_bottom_left_corner);
	c_border[BorderWSW] = c_border[BorderSSW];
	c_border[BorderW]   = XCreateFontCursor(dpy, XC_left_side);
	c_border[BorderWNW] = XCreateFontCursor(dpy, XC_top_left_corner);
	c_border[BorderNNW] = c_border[BorderWNW];

	swN = make_outline_bar();
	swS = make_outline_bar();
	swE = make_outline_bar();
	swW = make_outline_bar();

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
	grabkeys();
}

static void
cleanup(void)
{
	Client *c, *next;

	for(c = clients; c; c = next){
		next = c->next;
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XReparentWindow(dpy, c->win, root, c->x, c->y);
		XRemoveFromSaveSet(dpy, c->win);
		XDestroyWindow(dpy, c->frame);
		if(c->label) free(c->label);
		free(c);
	}
	clients = NULL;
	current = NULL;

	closebattery();
	if(bardraw) XftDrawDestroy(bardraw);
	if(barpix) XFreePixmap(dpy, barpix);
	if(bargc) XFreeGC(dpy, bargc);
	XDestroyWindow(dpy, barwin);

	{
		int i;
		for(i = 0; i < launch_ncmds; i++)
			free(launch_cmds[i]);
	}

	XDestroyWindow(dpy, swN);
	XDestroyWindow(dpy, swS);
	XDestroyWindow(dpy, swE);
	XDestroyWindow(dpy, swW);

	{
		XftColor *cols[] = {
			&bar_fg, &bar_bg, &bar_sel, &bar_self,
			&bar_tab, &bar_run, &bar_exit,
			&bar_desk[0], &bar_desk[1], &bar_desk[2], &bar_desk[3]
		};
		size_t i;
		for(i = 0; i < LENGTH(cols); i++)
			XftColorFree(dpy, DefaultVisual(dpy, screen),
				DefaultColormap(dpy, screen), cols[i]);
	}
	if(xftfont) XftFontClose(dpy, xftfont);

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
			bar_deadline += BAR_REFRESH;
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
			case MotionNotify:
				motionnotify(&ev.xmotion); break;
			case Expose:
				if(ev.xexpose.count != 0)
					break;
				if(ev.xexpose.window == barwin)
					XCopyArea(dpy, barpix, barwin, bargc,
						0, 0, barw, barh, 0, 0);
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
	if(argc > 1){
		if(strcmp(argv[1], "-v") == 0){
			fprintf(stderr, "9x more scum than rio: " VERSION "\n");
			return 0;
		}
		fprintf(stderr, "usage: 9x [-v]\n");
		return 1;
	}
	dpy = XOpenDisplay(NULL);
	if(!dpy){
		fprintf(stderr, "9x: cannot open display\n");
		return 1;
	}
	setup();
	scan();
	run();
	cleanup();
	return 0;
}

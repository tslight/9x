/*
 * 9x more scummy and rat infested than rio. Just say nein.
 *
 * Copyright (c) 2026 Toby Slight.  MIT License.
 */

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <machine/apmvar.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#define COL_ACTIVE     0x55AAAA
#define COL_INACTIVE   0x9EEEEE
#define COL_MENU_BG    0xE9FFE9
#define COL_MENU_BG_S  0x448844
#define COL_MENU_FG    0x000000
#define COL_MENU_FG_S  0xE9FFE9
#define COL_MENU_BD    0x88CC88
#define COL_SWEEP_BD   0xDD0000
#define COL_ROOT_BG    0x777777
#define COL_BAR_BG     0x9EEEEE
#define COL_BAR_FG     0x000000

#define BORDER      4
#define CORNER      25
#define MOD         Mod1Mask
#define XFTFONT     "monospace:size=9"
#define BARFONT     "monospace:bold:size=9"
#define TERM        "xterm"
#define MAXCLIENTS  512
#define MENUH_PAD   4
#define LAUNCH_MAX  4096
#define NDESKS      10
#define BAR_PAD     2
#define BAR_REFRESH 60
#define TIMEFMT     "%H:%M %a %d/%m"

#define LENGTH(x)   (sizeof(x) / sizeof((x)[0]))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define U(x)        ((unsigned int)(x))

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

typedef struct Client Client;
struct Client {
	Window       win;
	Window       frame;
	int          x, y, dx, dy;
	int          ox, oy, odx, ody;
	int          maximized;
	int          fullscreen;
	int          border;
	int          proto;
	int          reparenting;
	int          virt;
	char        *label;
	Client      *next;
};

#define Pdelete    1
#define Ptakefocus 2

static Display      *dpy;
static int           screen;
static Window        root;
static int           sw, sh;
static XftFont      *xftfont;
static Cursor        c_arrow, c_sweep, c_box;
static Cursor        c_border[NBorder];
static Atom          wm_protocols, wm_delete, wm_take_focus, wm_state;
static Client       *clients;
static Client       *current;
static unsigned long col_active, col_inactive;
static unsigned long col_menu_bg, col_menu_bd, col_red;
static XftColor      xft_menu_fg, xft_menu_fgs;
static volatile sig_atomic_t running = 1;

static Window        swN, swS, swE, swW;

static Window        tab_overlay = None;
static XftDraw      *tab_xftdraw;
static int           tab_active;
static int           tab_sel;
static Client       *tab_cls[MAXCLIENTS];
static char         *tab_names[MAXCLIENTS];
static int           tab_n;

static char        **execs;
static size_t        nexecs;

static int           sweep_pending;
static int           sweep_x, sweep_y, sweep_dx, sweep_dy;

static int           curdesk;
static Client       *deskfocus[NDESKS];
static char         *desknames[NDESKS] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"
};

static Window        barwin;
static Pixmap        barpix;
static GC            bargc;
static XftFont      *barfont;
static XftDraw      *bardraw;
static XftColor      bar_fg, bar_bg;
static unsigned int  barw, barh;
static int           bar_batt = -1;
static int           bar_onac;
static time_t        bar_deadline;

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
	XColor bl, wh, dummy;

	XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
		"black", &bl, &dummy);
	XAllocNamedColor(dpy, DefaultColormap(dpy, screen),
		"white", &wh, &dummy);
	f = XCreatePixmapFromBitmapData(dpy, root, (char *)d->fore,
		d->width, d->width, 1, 0, 1);
	m = XCreatePixmapFromBitmapData(dpy, root, (char *)d->mask,
		d->width, d->width, 1, 0, 1);
	return XCreatePixmapCursor(dpy, f, m, &bl, &wh,
		d->hot[0], d->hot[1]);
}

static int
xft_textwidth(const char *s, size_t len)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(dpy, xftfont, (const FcChar8 *)s, (int)len, &ext);
	return ext.xOff;
}

static int
bar_textwidth(const char *s)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(dpy, barfont, (const FcChar8 *)s, (int)strlen(s), &ext);
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
	if(fork() == 0){
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
#else
static void initbattery(void) {}
static void readbattery(void) { bar_batt = -1; }
#endif

static void
bar_redraw(void)
{
	time_t now;
	struct tm *t;
	char tbuf[64], bbuf[32], dbuf[8];
	int x, y;
	unsigned int w, h;

	now = time(NULL);
	t = localtime(&now);
	strftime(tbuf, sizeof tbuf, TIMEFMT, t);
	snprintf(dbuf, sizeof dbuf, "[%d] ", curdesk + 1);

	if(bar_batt >= 0)
		snprintf(bbuf, sizeof bbuf, bar_onac ? "%d%% " : "!%d%% ", bar_batt);
	else
		bbuf[0] = '\0';

	w = U(bar_textwidth(dbuf) + bar_textwidth(bbuf) + bar_textwidth(tbuf)) + BAR_PAD * 2;
	h = U(barfont->ascent + barfont->descent) + BAR_PAD * 2;
	if(w != barw || h != barh){
		barw = w;
		barh = h;
		if(barpix)
			XFreePixmap(dpy, barpix);
		barpix = XCreatePixmap(dpy, barwin, barw, barh,
			U(DefaultDepth(dpy, screen)));
		XftDrawChange(bardraw, barpix);
		XMoveResizeWindow(dpy, barwin,
			sw - (int)barw, sh - (int)barh, barw, barh);
	}

	XSetForeground(dpy, bargc, bar_bg.pixel);
	XFillRectangle(dpy, barpix, bargc, 0, 0, barw, barh);

	x = BAR_PAD;
	y = BAR_PAD + barfont->ascent;

	XftDrawStringUtf8(bardraw, &bar_fg, barfont, x, y,
		(const FcChar8 *)dbuf, (int)strlen(dbuf));
	x += bar_textwidth(dbuf);

	if(bbuf[0]){
		XftDrawStringUtf8(bardraw, &bar_fg, barfont, x, y,
			(const FcChar8 *)bbuf, (int)strlen(bbuf));
		x += bar_textwidth(bbuf);
	}

	XftDrawStringUtf8(bardraw, &bar_fg, barfont, x, y,
		(const FcChar8 *)tbuf, (int)strlen(tbuf));

	XCopyArea(dpy, barpix, barwin, bargc, 0, 0, barw, barh, 0, 0);
}

static int
borderorient(Client *c, int x, int y)
{
	int fw, fh;

	fw = c->dx + 2 * BORDER;
	fh = c->dy + 2 * BORDER;

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
	if(c->label){
		XFree(c->label);
		c->label = NULL;
	}
	XFetchName(dpy, c->win, &c->label);
}

static void
getproto(Client *c)
{
	Atom *protos;
	int n, i;

	protos = NULL;
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

	memset(&ev, 0, sizeof(ev));
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

	memset(&ce, 0, sizeof(ce));
	ce.type = ConfigureNotify;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->dx;
	ce.height = c->dy;
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
	unsigned int mods[] = { 0, LockMask, Mod2Mask, LockMask|Mod2Mask };
	unsigned int i;

	XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
	if(!focused)
		XGrabButton(dpy, Button1, AnyModifier, c->win, False,
			ButtonPressMask, GrabModeSync, GrabModeSync,
			None, None);
	for(i = 0; i < LENGTH(mods); i++){
		XGrabButton(dpy, Button1, MOD|mods[i], c->win, False,
			ButtonPressMask, GrabModeAsync, GrabModeAsync,
			None, None);
		XGrabButton(dpy, Button3, MOD|mods[i], c->win, False,
			ButtonPressMask, GrabModeAsync, GrabModeAsync,
			None, None);
	}
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
	XRaiseWindow(dpy, c->frame);
	current = c;
	raisebar();
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
		XMoveResizeWindow(dpy, c->frame, 0, 0, U(sw), U(sh));
		XMoveResizeWindow(dpy, c->win, 0, 0, U(sw), U(sh));
	} else {
		XMoveResizeWindow(dpy, c->frame,
			c->x - c->border, c->y - c->border,
			U(c->dx + 2 * c->border), U(c->dy + 2 * c->border));
		XMoveResizeWindow(dpy, c->win,
			c->border, c->border, U(c->dx), U(c->dy));
	}
}

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
	if(winclient(w))
		return winclient(w);

	c = calloc(1, sizeof(Client));
	if(!c)
		return NULL;

	c->win = w;
	c->border = BORDER;
	c->virt = curdesk;

	if(sweep_pending){
		c->x = sweep_x;
		c->y = sweep_y;
		c->dx = sweep_dx;
		c->dy = sweep_dy;
		sweep_pending = 0;
	} else {
		c->dx = wa.width;
		c->dy = wa.height;
		c->x = wa.x;
		c->y = wa.y;

		memset(&hints, 0, sizeof(hints));
		if(XGetWMNormalHints(dpy, w, &hints, &supplied)
		&& (hints.flags & (USPosition | PPosition))
		&& c->x > 0 && c->y > 0){
			/* honour explicit position */
		} else {
			c->x = (sw - c->dx) / 2;
			c->y = (sh - c->dy) / 2;
		}

		if(c->x - BORDER < 0)
			c->x = BORDER;
		if(c->y - BORDER < 0)
			c->y = BORDER;
		if(c->x + c->dx + BORDER > sw)
			c->x = sw - c->dx - BORDER;
		if(c->y + c->dy + BORDER > sh)
			c->y = sh - c->dy - BORDER;
	}

	getname(c);
	getproto(c);

	sa.override_redirect = True;
	sa.background_pixel = col_inactive;
	sa.event_mask = SubstructureRedirectMask | SubstructureNotifyMask
		| ButtonPressMask | ButtonReleaseMask
		| PointerMotionMask | LeaveWindowMask;

	c->frame = XCreateWindow(dpy, root,
		c->x - c->border, c->y - c->border,
		U(c->dx + 2 * c->border), U(c->dy + 2 * c->border),
		0, CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWEventMask, &sa);

	c->reparenting = 1;
	XAddToSaveSet(dpy, w);
	XReparentWindow(dpy, w, c->frame, c->border, c->border);
	XResizeWindow(dpy, w, U(c->dx), U(c->dy));
	XMapWindow(dpy, w);
	XMapWindow(dpy, c->frame);
	XSelectInput(dpy, w, PropertyChangeMask | StructureNotifyMask);
	XSetWindowBorderWidth(dpy, w, 0);

	c->next = clients;
	clients = c;
	setwmstate(c, NormalState);
	focus(c);
	sendconfig(c);
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
	if(current == c){
		current = NULL;
		{
			Client *cc;
			for(cc = clients; cc; cc = cc->next)
				if(cc->virt == curdesk){
					focus(cc);
					break;
				}
		}
		if(!current)
			XSetInputFocus(dpy, root, RevertToPointerRoot,
				CurrentTime);
	}
	raisebar();
	if(c->label)
		XFree(c->label);
	free(c);
}

static void
deleteclient(Client *c)
{
	if(!c)
		return;
	if(c->proto & Pdelete)
		sendcmessage(c->win, wm_protocols, wm_delete);
	else
		XKillClient(dpy, c->win);
}

static void
togglemax(Client *c)
{
	if(!c)
		return;
	if(c->fullscreen)
		return;
	if(c->maximized){
		c->x = c->ox;
		c->y = c->oy;
		c->dx = c->odx;
		c->dy = c->ody;
		c->maximized = 0;
	} else {
		c->ox = c->x;
		c->oy = c->y;
		c->odx = c->dx;
		c->ody = c->dy;
		c->x = BORDER;
		c->y = BORDER;
		c->dx = sw - 2 * BORDER;
		c->dy = sh - 2 * BORDER;
		c->maximized = 1;
	}
	applylayout(c);
	sendconfig(c);
	raisebar();
}

static void
togglefullscreen(Client *c)
{
	if(!c)
		return;
	if(c->fullscreen){
		c->fullscreen = 0;
		if(c->maximized){
			c->x = BORDER;
			c->y = BORDER;
			c->dx = sw - 2 * BORDER;
			c->dy = sh - 2 * BORDER;
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
		if(!c->maximized){
			c->ox = c->x;
			c->oy = c->y;
			c->odx = c->dx;
			c->ody = c->dy;
		}
		c->fullscreen = 1;
		applylayout(c);
		sendconfig(c);
		XRaiseWindow(dpy, c->frame);
	}
}

static void
outline_show(int x, int y, int w, int h)
{
	XMoveResizeWindow(dpy, swN, x, y, U(MAX(w, 1)), BORDER);
	XMoveResizeWindow(dpy, swS, x, y + h - BORDER, U(MAX(w, 1)), BORDER);
	XMoveResizeWindow(dpy, swW, x, y, BORDER, U(MAX(h, 1)));
	XMoveResizeWindow(dpy, swE, x + w - BORDER, y, BORDER, U(MAX(h, 1)));
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
sweep(int *rx, int *ry, int *rdx, int *rdy)
{
	XEvent ev;
	int sx, sy, status;
	int bx, by, bdx, bdy, drawn, done;

	status = XGrabPointer(dpy, root, False,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_sweep, CurrentTime);
	if(status != GrabSuccess)
		return 0;

	drawn = 0;
	done = 0;
	bx = by = bdx = bdy = 0;
	sx = sy = 0;

	XSync(dpy, False);
	while(XCheckMaskEvent(dpy, ButtonReleaseMask | ButtonPressMask, &ev))
		;

	while(!done){
		XMaskEvent(dpy, ButtonPressMask | ButtonReleaseMask
			| PointerMotionMask, &ev);
		switch(ev.type){
		case ButtonPress:
			sx = ev.xbutton.x_root;
			sy = ev.xbutton.y_root;
			done = 1;
			break;
		case MotionNotify:
		case ButtonRelease:
			break;
		}
	}

	done = 0;
	while(!done){
		XMaskEvent(dpy, ButtonPressMask | ButtonReleaseMask
			| PointerMotionMask, &ev);
		switch(ev.type){
		case MotionNotify: {
			int x1 = MIN(sx, ev.xmotion.x_root);
			int y1 = MIN(sy, ev.xmotion.y_root);
			int x2 = MAX(sx, ev.xmotion.x_root);
			int y2 = MAX(sy, ev.xmotion.y_root);

			bx = x1; by = y1;
			bdx = x2 - x1; bdy = y2 - y1;
			if(bdx >= 2 * BORDER && bdy >= 2 * BORDER){
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
			if(drawn) outline_hide();
			XUngrabPointer(dpy, CurrentTime);
			return 0;
		}
	}

	if(drawn)
		outline_hide();
	XUngrabPointer(dpy, CurrentTime);

	if(bdx <= 2 * BORDER + 10 || bdy <= 2 * BORDER + 10)
		return 0;

	*rx = bx;
	*ry = by;
	*rdx = bdx;
	*rdy = bdy;
	return 1;
}

static void
reshapeclient(Client *c)
{
	int bx, by, bdx, bdy;

	if(!c)
		return;
	if(!sweep(&bx, &by, &bdx, &bdy))
		return;
	c->x = bx + BORDER;
	c->y = by + BORDER;
	c->dx = bdx - 2 * BORDER;
	c->dy = bdy - 2 * BORDER;
	c->maximized = 0;
	c->fullscreen = 0;
	XMoveResizeWindow(dpy, c->frame, bx, by, U(bdx), U(bdy));
	XMoveResizeWindow(dpy, c->win,
		c->border, c->border, U(c->dx), U(c->dy));
	sendconfig(c);
	raisebar();
}

static void
pullclient(Client *c, int bl, XButtonEvent *start)
{
	XEvent ev;
	int status;
	int ox, oy, odx, ody;
	int cx, cy;
	int bx, by, bdx, bdy;

	ox = c->x; oy = c->y;
	odx = c->dx; ody = c->dy;
	cx = start->x_root; cy = start->y_root;

	status = XGrabPointer(dpy, root, False,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root,
		c_border[bl], CurrentTime);
	if(status != GrabSuccess)
		return;

	bx = ox - BORDER; by = oy - BORDER;
	bdx = odx + 2 * BORDER; bdy = ody + 2 * BORDER;
	outline_show(bx, by, bdx, bdy);

	for(;;){
		XMaskEvent(dpy, ButtonPressMask | ButtonReleaseMask
			| PointerMotionMask, &ev);
		if(ev.type == MotionNotify){
			int ddx = ev.xmotion.x_root - cx;
			int ddy = ev.xmotion.y_root - cy;
			int nx = ox, ny = oy, ndx = odx, ndy = ody;

			switch(bl){
			case BorderN:
				ny = oy + ddy; ndy = ody - ddy; break;
			case BorderS:
				ndy = ody + ddy; break;
			case BorderE:
				ndx = odx + ddx; break;
			case BorderW:
				nx = ox + ddx; ndx = odx - ddx; break;
			case BorderNNW: case BorderWNW:
				nx = ox + ddx; ndx = odx - ddx;
				ny = oy + ddy; ndy = ody - ddy; break;
			case BorderNNE: case BorderENE:
				ndx = odx + ddx;
				ny = oy + ddy; ndy = ody - ddy; break;
			case BorderSSE: case BorderESE:
				ndx = odx + ddx;
				ndy = ody + ddy; break;
			case BorderSSW: case BorderWSW:
				nx = ox + ddx; ndx = odx - ddx;
				ndy = ody + ddy; break;
			default:
				break;
			}
			if(ndx < 20) ndx = 20;
			if(ndy < 20) ndy = 20;
			bx = nx - BORDER; by = ny - BORDER;
			bdx = ndx + 2 * BORDER; bdy = ndy + 2 * BORDER;
			outline_show(bx, by, bdx, bdy);
			XFlush(dpy);
		} else if(ev.type == ButtonPress){
			if(ev.xbutton.button == Button3){
				outline_hide();
				XUngrabPointer(dpy, CurrentTime);
				deleteclient(c);
				return;
			}
			if(ev.xbutton.button == Button2){
				outline_hide();
				XUngrabPointer(dpy, CurrentTime);
				reshapeclient(c);
				return;
			}
			outline_hide();
			break;
		} else if(ev.type == ButtonRelease){
			outline_hide();
			c->x = bx + BORDER; c->y = by + BORDER;
			c->dx = bdx - 2 * BORDER; c->dy = bdy - 2 * BORDER;
			c->maximized = 0;
			c->fullscreen = 0;
			XMoveResizeWindow(dpy, c->frame, bx, by, U(bdx), U(bdy));
			XMoveResizeWindow(dpy, c->win,
				c->border, c->border, U(c->dx), U(c->dy));
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
	int ox, oy, mx, my, status;
	int bx, by, bdx, bdy;

	if(!c)
		return;
	ox = c->x; oy = c->y;
	mx = start->x_root; my = start->y_root;

	status = XGrabPointer(dpy, root, False,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_box, CurrentTime);
	if(status != GrabSuccess)
		return;

	bdx = c->dx + 2 * BORDER; bdy = c->dy + 2 * BORDER;
	bx = ox - BORDER; by = oy - BORDER;
	outline_show(bx, by, bdx, bdy);

	for(;;){
		XMaskEvent(dpy, ButtonPressMask | ButtonReleaseMask
			| PointerMotionMask, &ev);
		if(ev.type == MotionNotify){
			bx = ox + (ev.xmotion.x_root - mx) - BORDER;
			by = oy + (ev.xmotion.y_root - my) - BORDER;
			outline_show(bx, by, bdx, bdy);
			XFlush(dpy);
		} else if(ev.type == ButtonRelease){
			outline_hide();
			c->x = bx + BORDER;
			c->y = by + BORDER;
			c->maximized = 0;
			c->fullscreen = 0;
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
sweepnew(XButtonEvent *start)
{
	XEvent ev;
	int sx, sy, done, status;
	int bx, by, bdx, bdy, drawn;

	status = XGrabPointer(dpy, root, False,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, root, c_sweep, CurrentTime);
	if(status != GrabSuccess)
		return;

	sx = start->x_root;
	sy = start->y_root;
	bx = by = bdx = bdy = drawn = done = 0;

	while(!done){
		XMaskEvent(dpy, ButtonPressMask | ButtonReleaseMask
			| PointerMotionMask, &ev);
		switch(ev.type){
		case MotionNotify: {
			int x1 = MIN(sx, ev.xmotion.x_root);
			int y1 = MIN(sy, ev.xmotion.y_root);
			int x2 = MAX(sx, ev.xmotion.x_root);
			int y2 = MAX(sy, ev.xmotion.y_root);

			bx = x1; by = y1;
			bdx = x2 - x1; bdy = y2 - y1;
			if(bdx >= 2 * BORDER && bdy >= 2 * BORDER){
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
			if(drawn) outline_hide();
			XUngrabPointer(dpy, CurrentTime);
			if(bdx > 2 * BORDER + 10 && bdy > 2 * BORDER + 10){
				sweep_x = bx + BORDER;
				sweep_y = by + BORDER;
				sweep_dx = bdx - 2 * BORDER;
				sweep_dy = bdy - 2 * BORDER;
				sweep_pending = 1;
				spawn(TERM);
			}
			return;
		case ButtonPress:
			done = 1;
			break;
		}
	}
	if(drawn) outline_hide();
	XUngrabPointer(dpy, CurrentTime);
}

static void
tab_draw(void)
{
	int itemh, wide, mw_w, mw_h, ox, oy, i;
	XftColor selbg;

	if(!xftfont || tab_n <= 0)
		return;

	itemh = xftfont->ascent + xftfont->descent + MENUH_PAD;
	wide = 0;
	for(i = 0; i < tab_n; i++){
		int tw = xft_textwidth(tab_names[i],
			strlen(tab_names[i])) + 8;
		if(tw > wide) wide = tw;
	}
	if(wide < 200) wide = 200;
	if(wide > sw - 40) wide = sw - 40;
	mw_w = wide;
	mw_h = tab_n * itemh + MENUH_PAD * 2;
	ox = (sw - mw_w) / 2;
	oy = (sh - mw_h) / 2;

	XMoveResizeWindow(dpy, tab_overlay, ox, oy, U(mw_w), U(mw_h));
	XMapRaised(dpy, tab_overlay);
	XClearWindow(dpy, tab_overlay);

	selbg = getxftcolor(COL_MENU_BG_S);
	for(i = 0; i < tab_n; i++){
		XftColor *fg;
		int ty = MENUH_PAD + i * itemh;

		if(i == tab_sel){
			XftDrawRect(tab_xftdraw, &selbg,
				0, ty, U(mw_w), U(itemh));
			fg = &xft_menu_fgs;
		} else {
			fg = &xft_menu_fg;
		}
		XftDrawStringUtf8(tab_xftdraw, fg, xftfont,
			4, ty + xftfont->ascent,
			(const FcChar8 *)tab_names[i],
			(int)strlen(tab_names[i]));
	}
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &selbg);
}

static void
tab_show(void)
{
	Client *c;

	tab_n = 0;
	for(c = clients; c && tab_n < MAXCLIENTS; c = c->next){
		if(c->virt != curdesk)
			continue;
		tab_cls[tab_n] = c;
		tab_names[tab_n] = c->label ? c->label : "(unnamed)";
		tab_n++;
	}
	if(tab_n == 0)
		return;
	tab_sel = (tab_n > 1) ? 1 : 0;
	tab_active = 1;
	XGrabKeyboard(dpy, root, False,
		GrabModeAsync, GrabModeAsync, CurrentTime);
	tab_draw();
}

static void
tab_hide(int apply)
{
	Client *c;

	tab_active = 0;
	XUnmapWindow(dpy, tab_overlay);
	XUngrabKeyboard(dpy, CurrentTime);
	if(apply && tab_n > 0 && tab_sel >= 0 && tab_sel < tab_n){
		c = tab_cls[tab_sel];
		promote(c);
		focus(c);
		XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
			c->dx / 2, c->dy / 2);
	}
}

static int
is_modifier_release(KeySym ks)
{
	return ks == XK_Alt_L || ks == XK_Alt_R
		|| ks == XK_Meta_L || ks == XK_Meta_R;
}

static void
menu_draw(Window mw, XftDraw *xd, XftColor *selbg,
	char **names, int n, int sel, int itemh, int mw_w)
{
	int i;

	XClearWindow(dpy, mw);
	for(i = 0; i < n; i++){
		XftColor *fg;
		int iy = MENUH_PAD + i * itemh;

		if(i == sel){
			XftDrawRect(xd, selbg, 0, iy, U(mw_w), U(itemh));
			fg = &xft_menu_fgs;
		} else {
			fg = &xft_menu_fg;
		}
		XftDrawStringUtf8(xd, fg, xftfont, 4,
			iy + xftfont->ascent,
			(const FcChar8 *)names[i],
			(int)strlen(names[i]));
	}
	XFlush(dpy);
}

static void winmenu(int, int);
static void deskmenu(int, int);

static void
winmenu(int mx, int my)
{
	Window mw;
	XSetWindowAttributes sa;
	XEvent ev;
	XftDraw *xd;
	XftColor selbg;
	Client *cls[MAXCLIENTS];
	char *names[MAXCLIENTS];
	int ncls, itemh, mw_w, mw_h, x, y, i;
	int sel, done, armed;

	if(!xftfont)
		return;
	ncls = 0;
	{
		Client *c;
		for(c = clients; c && ncls < MAXCLIENTS; c = c->next){
			if(c->virt != curdesk)
				continue;
			cls[ncls] = c;
			names[ncls] = c->label ? c->label : "(unnamed)";
			ncls++;
		}
	}
	if(ncls == 0)
		return;

	itemh = xftfont->ascent + xftfont->descent + MENUH_PAD;
	mw_w = 0;
	for(i = 0; i < ncls; i++){
		int tw = xft_textwidth(names[i], strlen(names[i])) + 8;
		if(tw > mw_w) mw_w = tw;
	}
	if(mw_w < 200) mw_w = 200;
	if(mw_w > sw - 40) mw_w = sw - 40;
	mw_h = ncls * itemh + MENUH_PAD * 2;

	x = mx; y = my;
	if(x + mw_w > sw) x = sw - mw_w;
	if(y + mw_h > sh) y = sh - mw_h;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask | ButtonPressMask
		| PointerMotionMask | ButtonReleaseMask;

	mw = XCreateWindow(dpy, root, x, y, U(mw_w), U(mw_h), 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel
		| CWEventMask, &sa);
	XMapRaised(dpy, mw);

	xd = XftDrawCreate(dpy, mw, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	XGrabPointer(dpy, mw, True,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	armed = 0;
	sel = 0;
	done = 0;
	selbg = getxftcolor(COL_MENU_BG_S);
	menu_draw(mw, xd, &selbg, names, ncls, sel, itemh, mw_w);

	while(!done){
		XNextEvent(dpy, &ev);
		switch(ev.type){
		case Expose:
			menu_draw(mw, xd, &selbg, names, ncls, sel, itemh, mw_w);
			break;
		case MotionNotify:
			if(armed){
				int ny = ev.xmotion.y - MENUH_PAD;
				int old = sel;
				if(ny >= 0 && ny < ncls * itemh)
					sel = ny / itemh;
				if(sel != old)
					menu_draw(mw, xd, &selbg, names, ncls,
						sel, itemh, mw_w);
			}
			break;
		case ButtonRelease:
			if(!armed){
				armed = 1;
				break;
			}
			if(sel >= 0 && sel < ncls){
				Client *c = cls[sel];
				promote(c);
				focus(c);
				XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
					c->dx / 2, c->dy / 2);
			}
			done = 1;
			break;
		case ButtonPress:
			if(ev.xbutton.window != mw)
				done = 1;
			break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &selbg);
	XftDrawDestroy(xd);
	XDestroyWindow(dpy, mw);
}

static void
deskmenu(int mx, int my)
{
	Window mw;
	XSetWindowAttributes sa;
	XEvent ev;
	XftDraw *xd;
	XftColor selbg;
	int itemh, mw_w, mw_h, x, y, i;
	int sel, done, armed;

	if(!xftfont)
		return;

	itemh = xftfont->ascent + xftfont->descent + MENUH_PAD;
	mw_w = 0;
	for(i = 0; i < NDESKS; i++){
		int tw = xft_textwidth(desknames[i], strlen(desknames[i])) + 8;
		if(tw > mw_w) mw_w = tw;
	}
	if(mw_w < 80) mw_w = 80;
	mw_h = NDESKS * itemh + MENUH_PAD * 2;

	x = mx; y = my;
	if(x + mw_w > sw) x = sw - mw_w;
	if(y + mw_h > sh) y = sh - mw_h;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask | ButtonPressMask
		| PointerMotionMask | ButtonReleaseMask;

	mw = XCreateWindow(dpy, root, x, y, U(mw_w), U(mw_h), 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel
		| CWEventMask, &sa);
	XMapRaised(dpy, mw);

	xd = XftDrawCreate(dpy, mw, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	XGrabPointer(dpy, mw, True,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	armed = 0;
	sel = curdesk;
	done = 0;
	selbg = getxftcolor(COL_MENU_BG_S);
	menu_draw(mw, xd, &selbg, desknames, NDESKS, sel, itemh, mw_w);

	while(!done){
		XNextEvent(dpy, &ev);
		switch(ev.type){
		case Expose:
			menu_draw(mw, xd, &selbg, desknames, NDESKS, sel, itemh, mw_w);
			break;
		case MotionNotify:
			if(armed){
				int ny = ev.xmotion.y - MENUH_PAD;
				int old = sel;
				if(ny >= 0 && ny < NDESKS * itemh)
					sel = ny / itemh;
				if(sel != old)
					menu_draw(mw, xd, &selbg, desknames, NDESKS,
						sel, itemh, mw_w);
			}
			break;
		case ButtonRelease:
			if(!armed){
				armed = 1;
				break;
			}
			if(sel >= 0 && sel < NDESKS)
				switch_to(sel);
			done = 1;
			break;
		case ButtonPress:
			if(ev.xbutton.window != mw)
				done = 1;
			break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &selbg);
	XftDrawDestroy(xd);
	XDestroyWindow(dpy, mw);
}

static int
execcmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static void
build_execs(void)
{
	char *path, *pathcpy, *dir;
	DIR *dp;
	struct dirent *de;
	struct stat sb;
	char full[1024];
	size_t cap;

	if(execs)
		return;
	cap = 1024;
	execs = malloc(cap * sizeof(char *));
	if(!execs)
		return;
	nexecs = 0;

	path = getenv("PATH");
	if(!path) path = "/usr/bin:/bin:/usr/local/bin";
	pathcpy = strdup(path);
	if(!pathcpy)
		return;

	for(dir = strtok(pathcpy, ":"); dir; dir = strtok(NULL, ":")){
		dp = opendir(dir);
		if(!dp) continue;
		while((de = readdir(dp)) != NULL){
			int dup;
			size_t j;

			if(de->d_name[0] == '.') continue;
			snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
			if(stat(full, &sb) < 0) continue;
			if(!S_ISREG(sb.st_mode)) continue;
			if(!(sb.st_mode & (S_IXUSR|S_IXGRP|S_IXOTH))) continue;
			dup = 0;
			for(j = 0; j < nexecs; j++)
				if(strcmp(execs[j], de->d_name) == 0){
					dup = 1;
					break;
				}
			if(dup) continue;
			if(nexecs >= cap){
				cap *= 2;
				execs = realloc(execs, cap * sizeof(char *));
				if(!execs){
					nexecs = 0;
					break;
				}
			}
			execs[nexecs++] = strdup(de->d_name);
		}
		closedir(dp);
	}
	free(pathcpy);
	qsort(execs, nexecs, sizeof(char *), execcmp);
}

static void
exec_draw(Window mw, XftDraw *xd, XftColor *selbg, char **filtered,
	int nfilt, int fsel, const char *input, int itemh, int mw_w)
{
	char prompt[LAUNCH_MAX + 4];
	int i;

	XClearWindow(dpy, mw);
	snprintf(prompt, sizeof(prompt), "$ %s_", input);
	XftDrawStringUtf8(xd, &xft_menu_fg, xftfont,
		4, MENUH_PAD + xftfont->ascent,
		(const FcChar8 *)prompt, (int)strlen(prompt));
	for(i = 0; i < nfilt; i++){
		XftColor *fg;
		int iy = MENUH_PAD + (i + 1) * itemh;

		if(i == fsel){
			XftDrawRect(xd, selbg, 0, iy, U(mw_w), U(itemh));
			fg = &xft_menu_fgs;
		} else {
			fg = &xft_menu_fg;
		}
		XftDrawStringUtf8(xd, fg, xftfont, 4,
			iy + xftfont->ascent,
			(const FcChar8 *)filtered[i],
			(int)strlen(filtered[i]));
	}
	XFlush(dpy);
}

static void
exec_filter(char **filtered, int *nfilt, int maxlines,
	const char *input, int len)
{
	size_t i;

	*nfilt = 0;
	for(i = 0; i < nexecs && *nfilt < maxlines; i++)
		if(len == 0 || strstr(execs[i], input))
			filtered[(*nfilt)++] = execs[i];
}

static void
launch_at(int mx, int my, int from_button)
{
	Window mw;
	XSetWindowAttributes sa;
	XEvent ev;
	XftDraw *xd;
	XftColor selbg;
	char input[LAUNCH_MAX];
	char **filtered;
	int len, done, armed;
	int mw_w, mw_h, itemh, nfilt, fsel;
	int x, y, maxlines, centered;
	size_t i;

	if(!xftfont)
		return;
	build_execs();
	filtered = malloc(nexecs * sizeof(char *));
	if(!filtered)
		return;

	itemh = xftfont->ascent + xftfont->descent + MENUH_PAD;
	input[0] = '\0';
	len = 0;
	fsel = 0;
	centered = !from_button;

	maxlines = (sh - MENUH_PAD * 2) / itemh - 1;
	if(maxlines < 1) maxlines = 1;

	exec_filter(filtered, &nfilt, maxlines, input, len);

	mw_w = 0;
	for(i = 0; i < nexecs; i++){
		int tw = xft_textwidth(execs[i], strlen(execs[i])) + 8;
		if(tw > mw_w) mw_w = tw;
	}
	mw_w += xft_textwidth("$ ", 2) + 8;
	if(mw_w < 200) mw_w = 200;
	if(mw_w > sw - 40) mw_w = sw - 40;

	mw_h = itemh * (1 + nfilt) + MENUH_PAD * 2;
	if(centered){
		x = (sw - mw_w) / 2;
		y = 0;
	} else {
		x = mx;
		y = my;
	}
	if(x + mw_w > sw) x = sw - mw_w;
	if(y + mw_h > sh) y = sh - mw_h;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask
		| PointerMotionMask | ButtonReleaseMask;

	mw = XCreateWindow(dpy, root, x, y, U(mw_w), U(mw_h), 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel
		| CWEventMask, &sa);
	XMapRaised(dpy, mw);

	xd = XftDrawCreate(dpy, mw, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	if(XGrabKeyboard(dpy, mw, True, GrabModeAsync, GrabModeAsync,
		CurrentTime) != GrabSuccess){
		XftDrawDestroy(xd);
		XDestroyWindow(dpy, mw);
		free(filtered);
		return;
	}
	XGrabPointer(dpy, mw, True,
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	armed = from_button ? 0 : 1;
	selbg = getxftcolor(COL_MENU_BG_S);
	exec_draw(mw, xd, &selbg, filtered, nfilt, fsel, input, itemh, mw_w);

	done = 0;
	while(!done){
		XNextEvent(dpy, &ev);
		switch(ev.type){
		case Expose:
			break;
		case MotionNotify:
			if(armed){
				int ny = ev.xmotion.y - MENUH_PAD - itemh;
				int old = fsel;
				if(ny >= 0 && ny < nfilt * itemh)
					fsel = ny / itemh;
				if(fsel != old)
					goto redraw;
			}
			break;
		case ButtonRelease:
			if(!armed){
				armed = 1;
				break;
			}
			if(nfilt > 0 && fsel >= 0 && fsel < nfilt)
				spawn(filtered[fsel]);
			done = 1;
			break;
		case ButtonPress:
			if(ev.xbutton.window != mw)
				done = 1;
			break;
		case KeyPress: {
			char buf[32];
			KeySym ks;
			int count;

			count = XLookupString(&ev.xkey, buf, sizeof(buf),
				&ks, NULL);
			if(ks == XK_Escape){
				done = 1;
			} else if(ks == XK_Return || ks == XK_KP_Enter){
				if(nfilt > 0 && fsel >= 0)
					spawn(filtered[fsel]);
				else if(len > 0)
					spawn(input);
				done = 1;
			} else if(ks == XK_Tab){
				if(nfilt > 0 && fsel >= 0 && fsel < nfilt){
					strncpy(input, filtered[fsel],
						LAUNCH_MAX - 1);
					input[LAUNCH_MAX - 1] = '\0';
					len = (int)strlen(input);
				}
				goto refilter;
			} else if(ks == XK_BackSpace){
				if(len > 0) input[--len] = '\0';
				fsel = 0;
				goto refilter;
			} else if(ks == XK_u && (ev.xkey.state & ControlMask)){
				len = 0; input[0] = '\0'; fsel = 0;
				goto refilter;
			} else if(ks == XK_n && (ev.xkey.state & ControlMask)){
				if(fsel < nfilt - 1) fsel++;
				goto redraw;
			} else if(ks == XK_p && (ev.xkey.state & ControlMask)){
				if(fsel > 0) fsel--;
				goto redraw;
			} else if(ks == XK_Down){
				if(fsel < nfilt - 1) fsel++;
				goto redraw;
			} else if(ks == XK_Up){
				if(fsel > 0) fsel--;
				goto redraw;
			} else if(count > 0 && buf[0] >= ' ' && buf[0] <= '~'){
				if(len < LAUNCH_MAX - 1){
					input[len++] = buf[0];
					input[len] = '\0';
				}
				fsel = 0;
				goto refilter;
			}
			break;

		refilter:
			exec_filter(filtered, &nfilt, maxlines, input, len);
			if(fsel >= nfilt) fsel = nfilt - 1;
			if(fsel < 0) fsel = 0;
			mw_h = itemh * (1 + nfilt) + MENUH_PAD * 2;
			if(centered){ x = (sw - mw_w) / 2; y = 0; }
			if(y + mw_h > sh) y = sh - mw_h;
			if(y < 0) y = 0;
			XMoveResizeWindow(dpy, mw, x, y, U(mw_w), U(mw_h));
			XftDrawChange(xd, mw);
			/* fallthrough */
		redraw:
			exec_draw(mw, xd, &selbg, filtered, nfilt, fsel,
				input, itemh, mw_w);
			break;
		}
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XftColorFree(dpy, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen), &selbg);
	XftDrawDestroy(xd);
	XDestroyWindow(dpy, mw);
	free(filtered);
}

static void
buttonpress(XButtonEvent *e)
{
	Client *c;
	int bl;

	if(e->window == root || e->window == barwin){
		switch(e->button){
		case Button1:
			winmenu(e->x_root, e->y_root);
			break;
		case Button2:
			deskmenu(e->x_root, e->y_root);
			break;
		case Button3:
			sweepnew(e);
			break;
		case Button4:
			if(curdesk > 0)
				switch_to(curdesk - 1);
			break;
		case Button5:
			if(curdesk < NDESKS - 1)
				switch_to(curdesk + 1);
			break;
		}
		return;
	}

	c = winclient(e->window);
	if(c){
		if(e->button == 1 && (e->state & MOD)){
			pullclient(c, BorderSSE, e);
			return;
		}
		if(e->button == 3 && (e->state & MOD)){
			moveclient(c, e);
			return;
		}
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
		if(e->button == 1)
			pullclient(c, bl, e);
		else if(e->button == 2)
			togglemax(c);
		else if(e->button == 3)
			moveclient(c, e);
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
	static const KeySym deskkeys[NDESKS] = {
		XK_1, XK_2, XK_3, XK_4, XK_5,
		XK_6, XK_7, XK_8, XK_9, XK_0
	};
	int i;

	ks = XLookupKeysym(e, 0);
	if(tab_active){
		XAllowEvents(dpy, AsyncKeyboard, e->time);
		if(ks == XK_Tab){
			if(e->state & ShiftMask)
				tab_sel = (tab_sel - 1 + tab_n) % tab_n;
			else
				tab_sel = (tab_sel + 1) % tab_n;
			tab_draw();
		} else if(ks == XK_Escape){
			tab_hide(0);
		}
		return;
	}

	for(i = 0; i < NDESKS; i++){
		if(ks == deskkeys[i]){
			switch_to(i);
			return;
		}
	}

	switch(ks){
	case XK_Tab:
		XAllowEvents(dpy, AsyncKeyboard, e->time);
		tab_show();
		break;
	case XK_q:
		deleteclient(current);
		break;
	case XK_m:
		togglemax(current);
		break;
	case XK_F11:
		togglefullscreen(current);
		break;
	case XK_space:
		launch_at(0, 0, 0);
		break;
	case XK_Return:
		spawn(TERM);
		break;
	case XK_Left:
		if(curdesk > 0)
			switch_to(curdesk - 1);
		break;
	case XK_Right:
		if(curdesk < NDESKS - 1)
			switch_to(curdesk + 1);
		break;
	}
}

static void
keyrelease(XKeyEvent *e)
{
	KeySym ks;

	if(!tab_active)
		return;
	ks = XLookupKeysym(e, 0);
	if(is_modifier_release(ks))
		tab_hide(1);
}

static void
configreq(XConfigureRequestEvent *e)
{
	XWindowChanges wc;
	Client *c;

	c = winclient(e->window);
	if(c){
		if(e->value_mask & CWX) c->x = e->x;
		if(e->value_mask & CWY) c->y = e->y;
		if(e->value_mask & CWWidth) c->dx = e->width;
		if(e->value_mask & CWHeight) c->dy = e->height;
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
	XConfigureWindow(dpy, e->window, U(e->value_mask), &wc);
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
	if(c && e->atom == XA_WM_NAME)
		getname(c);
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
	unsigned int i, j;
	KeyCode tab, q, m, space, ret, left, right, f11;
	KeyCode dk[NDESKS];
	static const KeySym deskkeys[NDESKS] = {
		XK_1, XK_2, XK_3, XK_4, XK_5,
		XK_6, XK_7, XK_8, XK_9, XK_0
	};

	tab   = XKeysymToKeycode(dpy, XK_Tab);
	q     = XKeysymToKeycode(dpy, XK_q);
	m     = XKeysymToKeycode(dpy, XK_m);
	space = XKeysymToKeycode(dpy, XK_space);
	ret   = XKeysymToKeycode(dpy, XK_Return);
	left  = XKeysymToKeycode(dpy, XK_Left);
	right = XKeysymToKeycode(dpy, XK_Right);
	f11   = XKeysymToKeycode(dpy, XK_F11);
	for(j = 0; j < NDESKS; j++)
		dk[j] = XKeysymToKeycode(dpy, deskkeys[j]);

	for(i = 0; i < LENGTH(mods); i++){
		XGrabKey(dpy, tab, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeSync);
		XGrabKey(dpy, tab, MOD|ShiftMask|mods[i], root,
			True, GrabModeAsync, GrabModeSync);
		XGrabKey(dpy, q, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, m, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, f11, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, space, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, ret, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, left, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, right, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		for(j = 0; j < NDESKS; j++)
			XGrabKey(dpy, dk[j], MOD|mods[i], root,
				True, GrabModeAsync, GrabModeAsync);
	}
}

static void
setup_bar(void)
{
	XSetWindowAttributes wa;
	struct tm fat;
	char tbuf[64], maxstr[128];

	barfont = XftFontOpenName(dpy, screen, BARFONT);
	if(!barfont)
		barfont = XftFontOpenName(dpy, screen, "monospace:bold:size=11");

	bar_fg = getxftcolor(COL_BAR_FG);
	bar_bg = getxftcolor(COL_BAR_BG);

	memset(&fat, 0, sizeof fat);
	fat.tm_mon = 8;
	fat.tm_mday = 28;
	fat.tm_hour = 20;
	fat.tm_year = 100;
	strftime(tbuf, sizeof tbuf, TIMEFMT, &fat);
	snprintf(maxstr, sizeof maxstr, "[10] !100%% %s", tbuf);
	barw = U(bar_textwidth(maxstr)) + BAR_PAD * 2;
	barh = U(barfont->ascent + barfont->descent) + BAR_PAD * 2;

	wa.override_redirect = True;
	wa.background_pixel = bar_bg.pixel;
	wa.event_mask = ExposureMask | ButtonPressMask;
	barwin = XCreateWindow(dpy, root,
		sw - (int)barw, sh - (int)barh, barw, barh, 0,
		DefaultDepth(dpy, screen),
		InputOutput, DefaultVisual(dpy, screen),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

	bargc = XCreateGC(dpy, barwin, 0, NULL);
	barpix = XCreatePixmap(dpy, barwin, barw, barh,
		U(DefaultDepth(dpy, screen)));
	bardraw = XftDrawCreate(dpy, barpix,
		DefaultVisual(dpy, screen), DefaultColormap(dpy, screen));

	XMapRaised(dpy, barwin);
	initbattery();
	readbattery();
	bar_redraw();
	bar_deadline = time(NULL) + BAR_REFRESH;
}

static void
setup(void)
{
	XSetWindowAttributes wa, sa;

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	XSetErrorHandler(handler);

	wm_protocols  = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wm_delete     = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wm_take_focus = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	wm_state      = XInternAtom(dpy, "WM_STATE", False);

	col_active   = getcolor(COL_ACTIVE);
	col_inactive = getcolor(COL_INACTIVE);
	col_menu_bg  = getcolor(COL_MENU_BG);
	col_menu_bd  = getcolor(COL_MENU_BD);
	col_red      = getcolor(COL_SWEEP_BD);

	xftfont = XftFontOpenName(dpy, screen, XFTFONT);
	if(!xftfont)
		xftfont = XftFontOpenName(dpy, screen, "monospace:size=11");

	xft_menu_fg  = getxftcolor(COL_MENU_FG);
	xft_menu_fgs = getxftcolor(COL_MENU_FG_S);

	c_arrow = makecursor(&bigarrow);
	c_sweep = makecursor(&sweepdata);
	c_box   = makecursor(&boxdata);

	c_border[BorderN]   = XCreateFontCursor(dpy, 138);
	c_border[BorderNNE] = XCreateFontCursor(dpy, 136);
	c_border[BorderENE] = c_border[BorderNNE];
	c_border[BorderE]   = XCreateFontCursor(dpy, 96);
	c_border[BorderESE] = XCreateFontCursor(dpy, 14);
	c_border[BorderSSE] = c_border[BorderESE];
	c_border[BorderS]   = XCreateFontCursor(dpy, 16);
	c_border[BorderSSW] = XCreateFontCursor(dpy, 12);
	c_border[BorderWSW] = c_border[BorderSSW];
	c_border[BorderW]   = XCreateFontCursor(dpy, 70);
	c_border[BorderWNW] = XCreateFontCursor(dpy, 134);
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

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask;
	tab_overlay = XCreateWindow(dpy, root, 0, 0, 1, 1, 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel
		| CWEventMask, &sa);
	tab_xftdraw = XftDrawCreate(dpy, tab_overlay,
		DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	tab_active = 0;

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
		XMapWindow(dpy, c->frame);
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XReparentWindow(dpy, c->win, root, c->x, c->y);
		XRemoveFromSaveSet(dpy, c->win);
		XDestroyWindow(dpy, c->frame);
		if(c->label) XFree(c->label);
		free(c);
	}
	clients = NULL;
	current = NULL;
	if(bardraw) XftDrawDestroy(bardraw);
	if(barfont) XftFontClose(dpy, barfont);
	if(tab_xftdraw) XftDrawDestroy(tab_xftdraw);
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
			bar_deadline = now + BAR_REFRESH;
		}

		while(XPending(dpy)){
			XNextEvent(dpy, &ev);
			switch(ev.type){
			case KeyPress:         keypress(&ev.xkey);                 break;
			case KeyRelease:       keyrelease(&ev.xkey);               break;
			case ButtonPress:      buttonpress(&ev.xbutton);           break;
			case MapRequest:       manage(ev.xmaprequest.window);      break;
			case ConfigureRequest: configreq(&ev.xconfigurerequest);   break;
			case UnmapNotify:      unmapnotify(&ev.xunmap);            break;
			case DestroyNotify:    destroynotify(&ev.xdestroywindow);  break;
			case PropertyNotify:   propertynotify(&ev.xproperty);      break;
			case MotionNotify:     motionnotify(&ev.xmotion);          break;
			case Expose:
				if(ev.xexpose.count != 0)
					break;
				if(tab_active && ev.xexpose.window == tab_overlay)
					tab_draw();
				else if(ev.xexpose.window == barwin)
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
		if(strcmp(argv[1], "-v") == 0
		|| strcmp(argv[1], "-version") == 0){
			fprintf(stderr, "9x-" VERSION "\n");
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
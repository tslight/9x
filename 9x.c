/*
 * 9x more scum & rats than rio. Just say NEIN.
 *
 * Copyright (c) 2026 Toby Slight <0xff.art>
 */

#include <err.h>
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
#define INPUTMAX    512
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

typedef struct Client Client;
struct Client {
	Window  win;
	Window  frame;
	int     x, y;
	unsigned int dx, dy;
	unsigned int odx, ody;
	int     ox, oy;
	int     maximized;
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
static Client       *clients;
static Client       *current;
static unsigned long col_active, col_inactive;
static unsigned long col_menu_bg, col_menu_bd, col_red;
static XftColor      xft_menu_fg, xft_menu_fgs, xft_menu_selbg;
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
static int           sweep_x, sweep_y;
static unsigned int  sweep_dx, sweep_dy;

static int           curdesk;
static Client       *deskfocus[NDESKS];

static Window        barwin;
static Pixmap        barpix;
static GC            bargc;
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

static void
bar_redraw(void)
{
	time_t now;
	struct tm *t;
	char tbuf[64], bbuf[32], dbuf[8];
	int x, y, dlen, blen, tlen;
	unsigned int w, h;

	now = time(NULL);
	t = localtime(&now);
	if(t)
		strftime(tbuf, sizeof tbuf, TIMEFMT, t);
	else
		strcpy(tbuf, "??:??");
	snprintf(dbuf, sizeof dbuf, "[%d] ", curdesk + 1);

	if(bar_batt >= 0)
		snprintf(bbuf, sizeof bbuf,
			bar_onac ? "%d%% " : "!%d%% ", bar_batt);
	else
		bbuf[0] = '\0';

	dlen = (int)strlen(dbuf);
	blen = (int)strlen(bbuf);
	tlen = (int)strlen(tbuf);

	w = (unsigned int)(xft_textwidth(dbuf, dlen)
		+ xft_textwidth(bbuf, blen) + xft_textwidth(tbuf, tlen))
		+ BAR_PAD * 2;
	h = (unsigned int)(xftfont->ascent + xftfont->descent) + BAR_PAD * 2;
	if(w != barw || h != barh){
		barw = w;
		barh = h;
		if(barpix)
			XFreePixmap(dpy, barpix);
		barpix = XCreatePixmap(dpy, barwin, barw, barh,
			(unsigned int)DefaultDepth(dpy, screen));
		XftDrawChange(bardraw, barpix);
		XMoveResizeWindow(dpy, barwin,
			(int)(sw - barw), (int)(sh - barh), barw, barh);
	}

	XSetForeground(dpy, bargc, bar_bg.pixel);
	XFillRectangle(dpy, barpix, bargc, 0, 0, barw, barh);

	x = BAR_PAD;
	y = BAR_PAD + xftfont->ascent;

	XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, y,
		(const FcChar8 *)dbuf, dlen);
	x += xft_textwidth(dbuf, dlen);

	if(bbuf[0]){
		XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, y,
			(const FcChar8 *)bbuf, blen);
		x += xft_textwidth(bbuf, blen);
	}

	XftDrawStringUtf8(bardraw, &bar_fg, xftfont, x, y,
		(const FcChar8 *)tbuf, tlen);

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

static void
sendtodesktop(Client *c, int n)
{
	if(!c || n < 0 || n >= NDESKS || n == curdesk)
		return;
	c->virt = n;
	XUnmapWindow(dpy, c->frame);
	if(deskfocus[curdesk] == c)
		deskfocus[curdesk] = NULL;
	deskfocus[n] = c;
	if(current == c)
		focusnext();
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
			c->y = (int)(sh - c->dy) / 2;
		}

		if(c->x + (int)c->dx + BORDER > (int)sw)
			c->x = (int)sw - (int)c->dx - BORDER;
		if(c->y + (int)c->dy + BORDER > (int)sh)
			c->y = (int)sh - (int)c->dy - BORDER;
		if(c->x < BORDER)
			c->x = BORDER;
		if(c->y < BORDER)
			c->y = BORDER;
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
	if(c->label)
		XFree(c->label);
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
maximize(Client *c)
{
	if(!c || c->fullscreen)
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
fullscreen(Client *c)
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
sweepspawn(const char *cmd)
{
	int bx, by;
	unsigned int bdx, bdy;

	if(!dosweep(0, 0, 0, &bx, &by, &bdx, &bdy))
		return;
	setsweep(bx, by, bdx, bdy);
	spawn(cmd);
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
	c->maximized = 0;
	c->fullscreen = 0;
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
			if(ev.xbutton.button == Button3){
				outline_hide();
				XUngrabPointer(dpy, CurrentTime);
				maximize(c);
				return;
			}
			outline_hide();
			break;
		} else if(ev.type == ButtonRelease){
			outline_hide();
			c->x = bx + BORDER; c->y = by + BORDER;
			c->dx = bdx - 2*BORDER; c->dy = bdy - 2*BORDER;
			c->maximized = 0;
			c->fullscreen = 0;
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
	int bx, by;
	unsigned int bdx, bdy;

	if(!dosweep(1, start->x_root, start->y_root,
		&bx, &by, &bdx, &bdy))
		return;
	setsweep(bx, by, bdx, bdy);
	spawn(TERM);
}

static void
freenames(char **names, int n)
{
	int i;

	for(i = 0; i < n; i++){
		free(names[i]);
		names[i] = NULL;
	}
}

static void
tab_draw(void)
{
	int itemh, wide, mw_w, mw_h, ox, oy, i;

	if(!xftfont || tab_n <= 0)
		return;

	itemh = xftfont->ascent + xftfont->descent;
	wide = 0;
	for(i = 0; i < tab_n; i++){
		int tw = xft_textwidth(tab_names[i],
			(int)strlen(tab_names[i])) + 8;
		if(tw > wide) wide = tw;
	}
	if(wide < 200) wide = 200;
	if(wide > (int)sw - 40) wide = (int)sw - 40;
	mw_w = wide;
	mw_h = tab_n * itemh;
	ox = ((int)sw - mw_w) / 2;
	oy = ((int)sh - mw_h) / 2;

	XMoveResizeWindow(dpy, tab_overlay, ox, oy,
		(unsigned int)mw_w, (unsigned int)mw_h);
	XMapRaised(dpy, tab_overlay);
	XClearWindow(dpy, tab_overlay);

	for(i = 0; i < tab_n; i++){
		XftColor *fg;
		int ty = i * itemh;

		if(i == tab_sel){
			XftDrawRect(tab_xftdraw, &xft_menu_selbg,
				0, ty, (unsigned int)mw_w,
				(unsigned int)itemh);
			fg = &xft_menu_fgs;
		} else {
			fg = &xft_menu_fg;
		}
		XftDrawStringUtf8(tab_xftdraw, fg, xftfont,
			4, ty + xftfont->ascent,
			(const FcChar8 *)tab_names[i],
			(int)strlen(tab_names[i]));
	}
}

static void
tab_show(void)
{
	Client *c;
	const char *name;

	freenames(tab_names, tab_n);
	tab_n = 0;
	for(c = clients; c && tab_n < MAXCLIENTS; c = c->next){
		if(c->virt != curdesk)
			continue;
		name = c->label ? c->label : "(unnamed)";
		tab_names[tab_n] = strdup(name);
		if(!tab_names[tab_n])
			continue;
		tab_cls[tab_n] = c;
		tab_n++;
	}
	if(tab_n == 0)
		return;
	tab_sel = (tab_n > 1) ? 1 : 0;
	if(XGrabKeyboard(dpy, root, False, GrabModeAsync,
		GrabModeAsync, CurrentTime) != GrabSuccess){
		freenames(tab_names, tab_n);
		tab_n = 0;
		return;
	}
	tab_active = 1;
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
			(int)c->dx / 2, (int)c->dy / 2);
	}
	freenames(tab_names, tab_n);
	tab_n = 0;
}

static void
menu_draw(Window mw, XftDraw *xd, char **names, int n,
	int sel, int itemh, int mw_w)
{
	int i;

	XClearWindow(dpy, mw);
	for(i = 0; i < n; i++){
		XftColor *fg;
		int iy = i * itemh;

		if(i == sel){
			XftDrawRect(xd, &xft_menu_selbg, 0, iy,
				(unsigned int)mw_w, (unsigned int)itemh);
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

static void unmapnotify(XUnmapEvent *);
static void destroynotify(XDestroyWindowEvent *);

static int
execcmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static void
free_execs(void)
{
	size_t i;

	for(i = 0; i < nexecs; i++)
		free(execs[i]);
	free(execs);
	execs = NULL;
	nexecs = 0;
}

static void
build_execs(void)
{
	char *path, *pathcpy, *dir, **tmp;
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
	if(!pathcpy){
		free(execs);
		execs = NULL;
		return;
	}

	for(dir = strtok(pathcpy, ":"); dir; dir = strtok(NULL, ":")){
		dp = opendir(dir);
		if(!dp) continue;
		while((de = readdir(dp)) != NULL){
			int dup;
			size_t j;

			if(de->d_name[0] == '.') continue;
			snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
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
				tmp = realloc(execs, cap * sizeof(char *));
				if(!tmp){
					closedir(dp);
					free(pathcpy);
					free_execs();
					return;
				}
				execs = tmp;
			}
			execs[nexecs++] = strdup(de->d_name);
		}
		closedir(dp);
	}
	free(pathcpy);
	qsort(execs, nexecs, sizeof(char *), execcmp);
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
exec_draw(Window mw, XftDraw *xd, char **filtered,
	int nfilt, int fsel, const char *input, int itemh, int mw_w)
{
	char prompt[INPUTMAX + 4];
	int i;

	XClearWindow(dpy, mw);
	snprintf(prompt, sizeof prompt, "%s_", input);
	XftDrawStringUtf8(xd, &xft_menu_fg, xftfont,
		4, xftfont->ascent,
		(const FcChar8 *)prompt, (int)strlen(prompt));
	for(i = 0; i < nfilt; i++){
		XftColor *fg;
		int iy = (i + 1) * itemh;

		if(i == fsel){
			XftDrawRect(xd, &xft_menu_selbg, 0, iy,
				(unsigned int)mw_w, (unsigned int)itemh);
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
launch(void)
{
	Window mw;
	XSetWindowAttributes sa;
	XEvent ev;
	XftDraw *xd;
	char input[INPUTMAX];
	char chosen[INPUTMAX];
	char **filtered;
	int len, done = 0, sweep = 0;
	int mw_w, mw_h, itemh, nfilt, fsel;
	int x, y, maxlines;
	size_t i;

	if(!xftfont)
		return;
	build_execs();
	filtered = malloc(nexecs * sizeof(char *));
	if(!filtered)
		return;

	itemh = xftfont->ascent + xftfont->descent;
	input[0] = '\0';
	chosen[0] = '\0';
	len = 0;
	fsel = 0;

	maxlines = (int)sh / itemh - 1;
	if(maxlines < 1) maxlines = 1;

	exec_filter(filtered, &nfilt, maxlines, input, len);

	mw_w = 0;
	for(i = 0; i < nexecs; i++){
		int tw = xft_textwidth(execs[i],
			(int)strlen(execs[i])) + 8;
		if(tw > mw_w) mw_w = tw;
	}
	if(mw_w < 200) mw_w = 200;
	if(mw_w > (int)sw - 40) mw_w = (int)sw - 40;

	/* launcher is centred horizontally, anchored to top */
	x = ((int)sw - mw_w) / 2;
	mw_h = itemh * (1 + nfilt);
	y = 0;

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	/* keyboard events via window mask; pointer events via grab */
	sa.event_mask = KeyPressMask;

	mw = XCreateWindow(dpy, root, x, y,
		(unsigned int)mw_w, (unsigned int)mw_h, 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask,
		&sa);
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
	XGrabPointer(dpy, mw, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

	/* warp pointer to first menu item, if available */
	if(nfilt > 0){
		XWarpPointer(dpy, None, mw, 0, 0, 0, 0, mw_w/2,
			itemh + itemh/2);
	}

	exec_draw(mw, xd, filtered, nfilt, fsel, input, itemh, mw_w);

	while(!done){
		XNextEvent(dpy, &ev);
		if(ev.type == KeyPress){
			char buf[32];
			KeySym ks;
			int count, refilter = 0;
			count = XLookupString(&ev.xkey, buf, sizeof buf,
				&ks, NULL);
			if(ks == XK_Escape){
				done = 1;
			} else if(ks == XK_Return || ks == XK_KP_Enter){
				if(nfilt > 0 && fsel >= 0){
					strncpy(chosen, filtered[fsel],
						INPUTMAX-1);
					chosen[INPUTMAX-1] = '\0';
				} else if(len > 0){
					strncpy(chosen, input, INPUTMAX-1);
					chosen[INPUTMAX-1] = '\0';
				}
				if(ev.xkey.state & Mod1Mask)
					sweep = 1;
				done = 1;
			} else if(ks == XK_Tab){
				if(nfilt > 0 && fsel >= 0 && fsel < nfilt){
					strncpy(input, filtered[fsel],
						INPUTMAX-1);
					input[INPUTMAX-1] = '\0';
					len = (int)strlen(input);
				}
				refilter = 1;
			} else if(ks == XK_BackSpace){
				if(len > 0) input[--len] = '\0';
				fsel = 0;
				refilter = 1;
			} else if(ks == XK_Down){
				if(fsel < nfilt - 1) fsel++;
				exec_draw(mw, xd, filtered, nfilt, fsel,
					input, itemh, mw_w);
			} else if(ks == XK_Up){
				if(fsel > 0) fsel--;
				exec_draw(mw, xd, filtered, nfilt, fsel,
					input, itemh, mw_w);
			} else if(count > 0 && buf[0] >= ' ' && buf[0] <= '~'){
				if(len < INPUTMAX - 1){
					input[len++] = buf[0];
					input[len] = '\0';
				}
				fsel = 0;
				refilter = 1;
			}
			if(refilter){
				exec_filter(filtered, &nfilt, maxlines,
					input, len);
				if(fsel >= nfilt) fsel = nfilt - 1;
				if(fsel < 0) fsel = 0;
				mw_h = itemh * (1 + nfilt);
				XMoveResizeWindow(dpy, mw, x, y,
					(unsigned int)mw_w,
					(unsigned int)mw_h);
				XftDrawChange(xd, mw);
				exec_draw(mw, xd, filtered, nfilt, fsel,
					input, itemh, mw_w);
			}
		} else if(ev.type == ButtonPress){
			if(ev.xbutton.x < 0 || ev.xbutton.x >= mw_w
			|| ev.xbutton.y < 0 || ev.xbutton.y >= mw_h){
				done = 1;
				break;
			}
		} else if(ev.type == ButtonRelease){
			if(ev.xbutton.x < 0 || ev.xbutton.x >= mw_w
			|| ev.xbutton.y < 0 || ev.xbutton.y >= mw_h){
				done = 1;
				break;
			}
			if(nfilt > 0){
				int idx = ev.xbutton.y / itemh - 1;
				if(idx >= 0 && idx < nfilt){
					fsel = idx;
					strncpy(chosen, filtered[fsel], INPUTMAX-1);
					chosen[INPUTMAX-1] = '\0';
					if(ev.xbutton.button == Button3)
						sweep = 1;
					done = 1;
				}
			}
		} else if(ev.type == MotionNotify){
			if(nfilt > 0){
				int idx = ev.xmotion.y / itemh - 1;
				if(idx != fsel && idx >= 0 && idx < nfilt){
					fsel = idx;
					exec_draw(mw, xd, filtered, nfilt, fsel,
						input, itemh, mw_w);
				}
			}
		}
	}

	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XftDrawDestroy(xd);
	XDestroyWindow(dpy, mw);
	XFlush(dpy);
	free(filtered);

	if(chosen[0]){
		if(sweep)
			sweepspawn(chosen);
		else
			spawn(chosen);
	}
}

static int
winmenu_rebuild(Client **cls, char **names, int *selp)
{
	Client *c;
	int ncls = 0;
	names[0] = strdup("Run");
	if(!names[0])
		return 0;
	cls[0] = NULL;
	ncls = 1;
	for(c = clients; c && ncls < MAXCLIENTS - 1; c = c->next){
		if(c->virt != curdesk)
			continue;
		names[ncls] = strdup(c->label ? c->label : "(unnamed)");
		if(!names[ncls])
			continue;
		cls[ncls] = c;
		ncls++;
	}
	names[ncls] = strdup("Exit");
	if(names[ncls]){
		cls[ncls] = NULL;
		ncls++;
	}
	if(*selp >= ncls)
		*selp = ncls - 1;
	if(*selp < 0)
		*selp = 0;
	return ncls;
}

static void
winmenu(int mx, int my)
{
	Window mw;
	XSetWindowAttributes sa;
	XEvent ev;
	XftDraw *xd;
	Client *cls[MAXCLIENTS];
	char *names[MAXCLIENTS];
	int ncls, itemh, mw_w, mw_h, x, y, i;
	int sel, done, armed;
	int dolaunch = 0;
	Client *reshapetarget = NULL;

	if(!xftfont)
		return;

	if(tab_active)
		tab_hide(0);

	sel = 0;
	ncls = winmenu_rebuild(cls, names, &sel);
	if(ncls == 0)
		return;

	itemh = xftfont->ascent + xftfont->descent;
	mw_w = 0;
	for(i = 0; i < ncls; i++){
		int tw = xft_textwidth(names[i],
			(int)strlen(names[i])) + 8;
		if(tw > mw_w) mw_w = tw;
	}
	if(mw_w < 200) mw_w = 200;
	if(mw_w > (int)sw - 40) mw_w = (int)sw - 40;
	mw_h = ncls * itemh;

	x = mx; y = my;
	if(x + mw_w > (int)sw) x = (int)sw - mw_w;
	if(y + mw_h > (int)sh) y = (int)sh - mw_h;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask
		| PointerMotionMask | ButtonReleaseMask;

	mw = XCreateWindow(dpy, root, x, y,
		(unsigned int)mw_w, (unsigned int)mw_h, 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask,
		&sa);
	XMapRaised(dpy, mw);

	xd = XftDrawCreate(dpy, mw, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	XGrabPointer(dpy, mw, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	/* grab keyboard to prevent alt-tab on open menu conflict */
	if(XGrabKeyboard(dpy, mw, True, GrabModeAsync, GrabModeAsync,
		CurrentTime) != GrabSuccess){
		XUngrabPointer(dpy, CurrentTime);
		XftDrawDestroy(xd);
		XDestroyWindow(dpy, mw);
		freenames(names, ncls);
		return;
	}

	armed = 0;
	done = 0;
	menu_draw(mw, xd, names, ncls, sel, itemh, mw_w);

	/* warp pointer to first menu item */
	XWarpPointer(dpy, None, mw, 0, 0, 0, 0, mw_w/2, itemh/2);

	while(!done){
		XNextEvent(dpy, &ev);
		switch(ev.type){
		case KeyPress:
			done = 1;
			break;
		case UnmapNotify:
			unmapnotify(&ev.xunmap);
			freenames(names, ncls);
			ncls = winmenu_rebuild(cls, names, &sel);
			if(ncls == 0){ done = 1; break; }
			mw_h = ncls * itemh;
			XResizeWindow(dpy, mw, (unsigned int)mw_w,
				(unsigned int)mw_h);
			XftDrawChange(xd, mw);
			menu_draw(mw, xd, names, ncls, sel, itemh, mw_w);
			break;
		case DestroyNotify:
			destroynotify(&ev.xdestroywindow);
			freenames(names, ncls);
			ncls = winmenu_rebuild(cls, names, &sel);
			if(ncls == 0){ done = 1; break; }
			mw_h = ncls * itemh;
			XResizeWindow(dpy, mw, (unsigned int)mw_w,
				(unsigned int)mw_h);
			XftDrawChange(xd, mw);
			menu_draw(mw, xd, names, ncls, sel, itemh, mw_w);
			break;
		case Expose:
			menu_draw(mw, xd, names, ncls, sel, itemh, mw_w);
			break;
		case MotionNotify:
			if(armed){
				int ny = ev.xmotion.y;
				int old = sel;
				if(ny >= 0 && ny < ncls * itemh)
					sel = ny / itemh;
				if(sel != old)
					menu_draw(mw, xd, names, ncls,
						sel, itemh, mw_w);
			}
			break;
		case ButtonRelease:
			if(ev.xbutton.x < 0 || ev.xbutton.x >= mw_w
			|| ev.xbutton.y < 0 || ev.xbutton.y >= mw_h){
				done = 1;
				break;
			}
			if(!armed){ armed = 1; break; }
			switch(ev.xbutton.button){
			case Button1:
				if(sel >= 0 && sel < ncls){
					if(cls[sel] == NULL){
						if(sel == 0)
							dolaunch = 1;
						else
							running = 0;
					} else {
						Client *c = cls[sel];
						promote(c);
						focus(c);
						XWarpPointer(dpy, None, c->win, 0, 0, 0, 0,
							(int)c->dx/2, (int)c->dy/2);
					}
				}
				done = 1;
				break;
			case Button2:
				if(sel >= 0 && sel < ncls && cls[sel])
					closeclient(cls[sel]);
				break;
			case Button3:
				if(sel >= 0 && sel < ncls && cls[sel])
					reshapetarget = cls[sel];
				done = 1;
				break;
			default:
				done = 1;
				break;
			}
			break;
		case ButtonPress:
			if(ev.xbutton.x < 0 || ev.xbutton.x >= mw_w
			|| ev.xbutton.y < 0 || ev.xbutton.y >= mw_h){
				done = 1;
			}
			break;
		}
	}
	freenames(names, ncls);
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XftDrawDestroy(xd);
	XDestroyWindow(dpy, mw);
	XFlush(dpy);

	if(dolaunch){
		launch();
		return;
	}

	if(reshapetarget){
		promote(reshapetarget);
		focus(reshapetarget);
		reshapeclient(reshapetarget);
	}
}

static void
deskmenu(int mx, int my)
{
	Window mw;
	XSetWindowAttributes sa;
	XEvent ev;
	XftDraw *xd;
	int itemh, mw_w, mw_h, x, y, i;
	int sel, done, armed;
	char dnames[NDESKS][4];
	char *dp[NDESKS];

	if(!xftfont)
		return;

	if(tab_active)
		tab_hide(0);

	for(i = 0; i < NDESKS; i++){
		snprintf(dnames[i], sizeof dnames[i], "%d", i + 1);
		dp[i] = dnames[i];
	}

	itemh = xftfont->ascent + xftfont->descent;
	mw_w = 0;
	for(i = 0; i < NDESKS; i++){
		int tw = xft_textwidth(dp[i], (int)strlen(dp[i])) + 4;
		if(tw > mw_w) mw_w = tw;
	}
	if(mw_w < 80) mw_w = 80;
	mw_h = NDESKS * itemh;

	x = mx; y = my;
	if(x + mw_w > (int)sw) x = (int)sw - mw_w;
	if(y + mw_h > (int)sh) y = (int)sh - mw_h;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask | ButtonPressMask | KeyPressMask
		| PointerMotionMask | ButtonReleaseMask;

	mw = XCreateWindow(dpy, root, x, y,
		(unsigned int)mw_w, (unsigned int)mw_h, 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask,
		&sa);
	XMapRaised(dpy, mw);

	xd = XftDrawCreate(dpy, mw, DefaultVisual(dpy, screen),
		DefaultColormap(dpy, screen));
	XGrabPointer(dpy, mw, False,
		ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
		GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	/* grab keyboard to prevent alt-tab on open menu conflict */
	if(XGrabKeyboard(dpy, mw, True, GrabModeAsync, GrabModeAsync,
		CurrentTime) != GrabSuccess){
		XUngrabPointer(dpy, CurrentTime);
		XftDrawDestroy(xd);
		XDestroyWindow(dpy, mw);
		return;
	}

	armed = 0;
	sel = curdesk;
	done = 0;
	menu_draw(mw, xd, dp, NDESKS, sel, itemh, mw_w);

	/* warp pointer to first menu item */
	XWarpPointer(dpy, None, mw, 0, 0, 0, 0, mw_w/2, itemh/2);

	while(!done){
		XNextEvent(dpy, &ev);
		switch(ev.type){
		case KeyPress:
			done = 1;
			break;
		case Expose:
			menu_draw(mw, xd, dp, NDESKS, sel, itemh, mw_w);
			break;
		case MotionNotify:
			if(armed){
				int ny = ev.xmotion.y;
				int old = sel;
				if(ny >= 0 && ny < NDESKS * itemh)
					sel = ny / itemh;
				if(sel != old)
					menu_draw(mw, xd, dp, NDESKS, sel, itemh, mw_w);
			}
			break;
		case ButtonRelease:
			if(ev.xbutton.x < 0 || ev.xbutton.x >= mw_w
			|| ev.xbutton.y < 0 || ev.xbutton.y >= mw_h){
				done = 1;
				break;
			}
			if(!armed){ armed = 1; break; }
			if(sel >= 0 && sel < NDESKS)
				switch_to(sel);
			done = 1;
			break;
		case ButtonPress:
			if(ev.xbutton.x < 0 || ev.xbutton.x >= mw_w
			|| ev.xbutton.y < 0 || ev.xbutton.y >= mw_h){
				done = 1;
			}
			break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	XftDrawDestroy(xd);
	XDestroyWindow(dpy, mw);
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
			closeclient(c);
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
		XK_6, XK_7, XK_8, XK_9
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
			if(e->state & ShiftMask)
				sendtodesktop(current, i);
			else
				switch_to(i);
			return;
		}
	}

	switch(ks){
	case XK_Tab:
		XAllowEvents(dpy, AsyncKeyboard, e->time);
		tab_show();
		break;
	case XK_F4:
		closeclient(current);
		break;
	case XK_F10:
		maximize(current);
		break;
	case XK_F11:
		fullscreen(current);
		break;
	case XK_space:
		launch();
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
	if(ks == XK_Alt_L || ks == XK_Alt_R
	|| ks == XK_Meta_L || ks == XK_Meta_R)
		tab_hide(1);
}

static void
configreq(XConfigureRequestEvent *e)
{
	XWindowChanges wc;
	Client *c;

	c = winclient(e->window);
	if(c){
		if(c->fullscreen || c->maximized){
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
	KeyCode tab, space, f4, f10, f11;
	KeyCode dk[NDESKS];
	static const KeySym deskkeys[NDESKS] = {
		XK_1, XK_2, XK_3, XK_4, XK_5,
		XK_6, XK_7, XK_8, XK_9
	};

	tab   = XKeysymToKeycode(dpy, XK_Tab);
	space = XKeysymToKeycode(dpy, XK_space);
	f4    = XKeysymToKeycode(dpy, XK_F4);
	f10   = XKeysymToKeycode(dpy, XK_F10);
	f11   = XKeysymToKeycode(dpy, XK_F11);
	for(j = 0; j < NDESKS; j++)
		dk[j] = XKeysymToKeycode(dpy, deskkeys[j]);

	for(i = 0; i < LENGTH(mods); i++){
		XGrabKey(dpy, tab, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeSync);
		XGrabKey(dpy, tab, MOD|ShiftMask|mods[i], root,
			True, GrabModeAsync, GrabModeSync);
		XGrabKey(dpy, f4, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, f10, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, f11, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		XGrabKey(dpy, space, MOD|mods[i], root,
			True, GrabModeAsync, GrabModeAsync);
		for(j = 0; j < NDESKS; j++){
			XGrabKey(dpy, dk[j], MOD|mods[i], root,
				True, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, dk[j], MOD|ShiftMask|mods[i], root,
				True, GrabModeAsync, GrabModeAsync);
		}
	}
}

static void
setup_bar(void)
{
	XSetWindowAttributes wa;
	struct tm fat;
	char tbuf[64], maxstr[128];

	bar_fg = getxftcolor(COL_BAR_FG);
	bar_bg = getxftcolor(COL_BAR_BG);

	memset(&fat, 0, sizeof fat);
	fat.tm_mon = 8;
	fat.tm_mday = 28;
	fat.tm_hour = 20;
	fat.tm_year = 100;
	strftime(tbuf, sizeof tbuf, TIMEFMT, &fat);
	snprintf(maxstr, sizeof maxstr, "[9] !100%% %s", tbuf);
	barw = (unsigned int)xft_textwidth(maxstr, (int)strlen(maxstr)) + BAR_PAD * 2;
	barh = (unsigned int)(xftfont->ascent + xftfont->descent) + BAR_PAD*2;

	wa.override_redirect = True;
	wa.background_pixel = bar_bg.pixel;
	wa.event_mask = ExposureMask | ButtonPressMask;
	barwin = XCreateWindow(dpy, root,
		(int)(sw - barw), (int)(sh - barh), barw, barh, 0,
		DefaultDepth(dpy, screen),
		InputOutput, DefaultVisual(dpy, screen),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

	bargc = XCreateGC(dpy, barwin, 0, NULL);
	barpix = XCreatePixmap(dpy, barwin, barw, barh,
		(unsigned int)DefaultDepth(dpy, screen));
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
	sw = (unsigned int)DisplayWidth(dpy, screen);
	sh = (unsigned int)DisplayHeight(dpy, screen);

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
		errx(1, "XftFontOpenName failed for %s", XFTFONT);

	xft_menu_fg    = getxftcolor(COL_MENU_FG);
	xft_menu_fgs   = getxftcolor(COL_MENU_FG_S);
	xft_menu_selbg = getxftcolor(COL_MENU_BG_S);

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

	sa.override_redirect = True;
	sa.background_pixel = col_menu_bg;
	sa.border_pixel = col_menu_bd;
	sa.event_mask = ExposureMask;
	tab_overlay = XCreateWindow(dpy, root, 0, 0, 1, 1, 2,
		CopyFromParent, InputOutput, CopyFromParent,
		CWOverrideRedirect|CWBackPixel|CWBorderPixel|CWEventMask,
		&sa);
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
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		XReparentWindow(dpy, c->win, root, c->x, c->y);
		XRemoveFromSaveSet(dpy, c->win);
		XDestroyWindow(dpy, c->frame);
		if(c->label) XFree(c->label);
		free(c);
	}
	clients = NULL;
	current = NULL;

	freenames(tab_names, tab_n);
	tab_n = 0;
	if(tab_xftdraw) XftDrawDestroy(tab_xftdraw);
	XDestroyWindow(dpy, tab_overlay);

	closebattery();
	if(bardraw) XftDrawDestroy(bardraw);
	if(barpix) XFreePixmap(dpy, barpix);
	if(bargc) XFreeGC(dpy, bargc);
	XDestroyWindow(dpy, barwin);

	XDestroyWindow(dpy, swN);
	XDestroyWindow(dpy, swS);
	XDestroyWindow(dpy, swE);
	XDestroyWindow(dpy, swW);

	if(xftfont) XftFontClose(dpy, xftfont);
	free_execs();

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
			case KeyRelease:
				keyrelease(&ev.xkey); break;
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
				if(tab_active
				&& ev.xexpose.window == tab_overlay)
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

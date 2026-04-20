/* Colors - Plan 9 palette */
#define COL_ACTIVE     0x55AAAA    /* active border - teal */
#define COL_INACTIVE   0x9EEEEE    /* inactive border - pale cyan */
#define COL_SWEEP_BD   0xAA0000    /* sweep outline - red */
#define COL_ROOT_BG    0x999999    /* root background - grey */

/* Bar colors */
#define COL_BAR_BG     0xEAFFFF    /* background - pale cyan */
#define COL_BAR_FG     0x000000    /* text - black */
#define COL_BAR_SEL    0x55AAAA    /* selection - teal */
#define COL_BAR_SELF   0xFFFFFF    /* selection text - white */
#define COL_BAR_TAB    0xEEFFEE    /* tab - pale green */
#define COL_BAR_RUN    0x99EE99    /* run button - green */
#define COL_BAR_EXIT   0xEE9999    /* exit button - red */
#define COL_BAR_DESK0  0xEAFFEA    /* desk empty */
#define COL_BAR_DESK1  0xFFEEFF    /* desk 1 window */
#define COL_BAR_DESK2  0xFFAAFF    /* desk 2 windows */
#define COL_BAR_DESK3  0xFF77FF    /* desk 3+ windows */

/* Geometry */
#define BORDER      4
#define CORNER      32

/* Bar */
#define BAR_PAD     2
#define BAR_BTN_PAD 2
#define BAR_GAP     2
#define BAR_REFRESH 60
#define LAUNCH_FILTER_W 200

/* Behavior */
#define NDESKS      9
#define DBLCLICK_MS 500
#define EDGE_SNAP   16
#define TIMEFMT     "%H:%M %a %d/%m"
#define TERM        "xterm"
#define XFTFONT     "sans:bold:size=8"

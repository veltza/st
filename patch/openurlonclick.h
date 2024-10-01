#include <spawn.h>

void clearurl(int clearhyperlinkhint);
void drawhyperlinkhint(void);
char *detecturl(int col, int row, int draw);
void openUrlOnClick(int col, int row, char* url_opener);

static inline void restoremousecursor(void)
{
	if (!(win.mode & MODE_MOUSE) && xw.pointerisvisible)
		XDefineCursor(xw.dpy, xw.win, xw.vpointer);
	clearurl(1);
}

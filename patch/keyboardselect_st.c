#include <wctype.h>
#include <string.h>
#include <unistd.h>
#include <pcre.h>
#include <wchar.h>
#include <string.h>

enum keyboardselect_mode {
	KBDS_MODE_MOVE    = 0,
	KBDS_MODE_SELECT  = 1<<1,
	KBDS_MODE_LSELECT = 1<<2,
	KBDS_MODE_FIND    = 1<<3,
	KBDS_MODE_SEARCH  = 1<<4,
	KBDS_MODE_FLASH   = 1<<5,
	KBDS_MODE_REGEX   = 1<<6,
	KBDS_MODE_URL   = 1<<7,
};

enum cursor_wrap {
	KBDS_WRAP_NONE    = 0,
	KBDS_WRAP_LINE    = 1<<0,
	KBDS_WRAP_EDGE    = 1<<1,
};

typedef struct {
	int x;
	int y;
	Line line;
	int len;
} KCursor;

typedef struct {
	unsigned int start;
	unsigned int len;
	unsigned int miss;
	char * pattern;
} RegexResult;

typedef struct {
	KCursor c;
	unsigned int len;
	char * pattern;
} RegexKCursor;

typedef struct {
    RegexKCursor *array;
    size_t used;
    size_t size;   
} RegexKCursorArray;

struct {
	int cx;
	int len;
	int maxlen;
	int dir;
	int wordonly;
	int directsearch;
	int ignorecase;
	Glyph *str;
} kbds_searchobj;

typedef struct {
	Rune *array;
	size_t used;
	size_t size;
} CharArray;

typedef struct {
	KCursor *array;
	size_t used;
	size_t size;
} KCursorArray;

static int kbds_in_use, kbds_quant;
static int kbds_seltype = SEL_REGULAR;
static int kbds_mode;
static int kbds_finddir, kbds_findtill;
static Rune kbds_findchar;
static KCursor kbds_c, kbds_oc;
static CharArray flash_next_char_record, flash_used_label;
static KCursorArray flash_kcursor_record;
static RegexKCursorArray regex_kcursor_record;

static const char *flash_key_label[52] = {
	"j", "f", "d", "k", "l", "h", "g", "a", "s", "o",
	"i", "e", "u", "n", "c", "m", "r", "p", "b", "t",
	"w", "v", "x", "y", "q", "z",
	"I", "J", "L", "H", "A", "B", "Y", "D", "E", "F",
	"G", "Q", "R", "T", "U", "V", "W", "X", "Z", "C",
	"K", "M", "N", "O", "P", "S"
};

static const char *valid_char[] = {
	"0","1","2","3","4","5","6","7","8","9",
    "j", "f", "d", "k", "l", "h", "g", "a", "s", "o", 
    "i", "e", "u", "n", "c", "m", "r", "p", "b", "t", 
    "w", "v", "x", "y", "q", "z",
    "I", "J", "L", "H", "A", "B", "Y", "D", "E", "F", 
    "G", "Q", "R", "T", "U", "V", "W", "X", "Z", "C",
    "K", "M", "N", "O", "P", "S",
    ".", "/", "#", 
    "-", "_", "=", "+", "(", ")", "@", "!", "$", "&", "*",
    "[", "]", "{", "}", "|", "\\", ":", ";", "\"", "'",
    "<", ">", ",", "?", "`", "~" 
};


int is_chinese_character(wchar_t ch) {
    // check if the character is a Chinese character
    if ((ch >= 0x4E00 && ch <= 0x9FFF) || 
        (ch >= 0x3400 && ch <= 0x4DBF) || 
        (ch >= 0x20000 && ch <= 0x2A6DF) ||
        (ch >= 0x2A700 && ch <= 0x2B73F) ||
        (ch >= 0x2B740 && ch <= 0x2B81F) ||
        (ch >= 0x2B820 && ch <= 0x2CEAF) ||
        (ch >= 0x2CEB0 && ch <= 0x2EBEF) ||
        (ch >= 0x30000 && ch <= 0x3134F)) {
        return 1; 
		}
    return 0;
}

int
is_valid_head_char(Rune u) {
	int i;
	for ( i = 0; i < LEN(valid_char); i++) {
		if (u == *valid_char[i]) {
			return 1;
		}
	}
	return 0;
}

void
init_regex_kcursor_array(RegexKCursorArray *a, size_t initialSize) {
    a->array = (RegexKCursor *)xmalloc(initialSize * sizeof(RegexKCursor));
    a->used = 0;
    a->size = initialSize;
}

void
insert_regex_kcursor_array(RegexKCursorArray *a, RegexKCursor element) {
    if (a->used == a->size) {
        size_t newSize = a->size == 0 ? 1 : a->size * 2;
        RegexKCursor *newArray = (RegexKCursor *)xrealloc(a->array, newSize * sizeof(RegexKCursor));
        a->array = newArray;
        a->size = newSize;
    }
    a->array[a->used++] = element;
}

void
reset_regex_kcursor_array(RegexKCursorArray *a) {
    free(a->array);
    a->array = NULL;
    a->used = 0;
    a->size = 0;
}

void
init_char_array(CharArray *a, size_t initialSize) {
	a->array = (Rune *)xmalloc(initialSize * sizeof(Rune));
	a->used = 0;
	a->size = initialSize;
}

void
insert_char_array(CharArray *a, Rune element) {
	if (a->used == a->size) {
		a->size *= 2;
		a->array = (Rune *)xrealloc(a->array, a->size * sizeof(Rune));
	}
	a->array[a->used++] = element;
}

void
reset_char_array(CharArray *a) {
	free(a->array);
	a->array = NULL;
	a->used = 0;
	a->size = 0;
}

void
init_kcursor_array(KCursorArray *a, size_t initialSize) {
	a->array = (KCursor *)xmalloc(initialSize * sizeof(KCursor));
	a->used = 0;
	a->size = initialSize;
}

void
insert_kcursor_array(KCursorArray *a, KCursor element) {
	if (a->used == a->size) {
		size_t newSize = a->size == 0 ? 1 : a->size * 2;
		KCursor *newArray = (KCursor *)xrealloc(a->array, newSize * sizeof(KCursor));
		a->array = newArray;
		a->size = newSize;
	}
	a->array[a->used++] = element;
}

void
reset_kcursor_array(KCursorArray *a) {
	free(a->array);
	a->array = NULL;
	a->used = 0;
	a->size = 0;
}

int
is_in_flash_used_label(Rune label) {
	int i;
	for ( i = 0; i < flash_used_label.used; i++) {
		if (label == flash_used_label.array[i]) {
			return 1;
		}
	}
	return 0;
}

int
is_in_flash_next_char_record(Rune label) {
	int i;
	for ( i = 0; i < flash_next_char_record.used; i++) {
		if (label == flash_next_char_record.array[i]) {
			return 1;
		}
	}
	return 0;
}

void
kbds_drawstatusbar(int y)
{
	static char *modes[] = { 
		" MOVE ", "", " SELECT ", " RSELECT ", " LSELECT ",
		" SEARCH FW ", " SEARCH BW ", " FIND FW ", " FIND BW ",
		" FLASH ", " REGEX ", "  URL "
	};
	static char quant[20] = { ' ' };
	static Glyph g;
	int i, n, m;
	int mlen, qlen;

	if (!kbds_in_use)
		return;

	g.mode = 0;
	g.fg = kbselectfg;
	g.bg = kbselectbg;

	/* draw the mode */
	if (y == 0) {
		if (kbds_isurlmode())
			m = 11;
		else if (kbds_isregexmode())
			m = 10;
		else if (kbds_isflashmode())
			m = 9;
		else if (kbds_issearchmode())
			m = 5 + (kbds_searchobj.dir < 0 ? 1 : 0);
		else if (kbds_mode & KBDS_MODE_FIND)
			m = 7 + (kbds_finddir < 0 ? 1 : 0);
		else if (kbds_mode & KBDS_MODE_SELECT)
			m = 2 + (kbds_seltype == SEL_RECTANGULAR ? 1 : 0);
		else
			m = kbds_mode;
		mlen = strlen(modes[m]);
		qlen = kbds_quant ? snprintf(quant+1, sizeof quant-1, "%i", kbds_quant) + 1 : 0;
		/* do not draw the mode if the cursor is behind it. */
		if (kbds_c.y != y || kbds_c.x < term.col - qlen - mlen) {
			for (n = mlen, i = term.col-1; i >= 0 && n > 0; i--) {
				g.u = modes[m][--n];
				xdrawglyph(g, i, y);
			}
			for (n = qlen; i >= 0 && n > 0; i--) {
				g.u = quant[--n];
				xdrawglyph(g, i, y);
			}
		}
	}

	/* draw the search bar */
	if (y == term.row-1 && (kbds_issearchmode() || kbds_isflashmode())) {
		/* search bar */
		for (g.u = ' ', i = 0; i < term.col; i++)
			xdrawglyph(g, i, y);
		/* search direction */
		g.u = (kbds_searchobj.dir > 0) ? '/' : '?';
		xdrawglyph(g, 0, y);
		/* search string and cursor */
		for (i = 0; i < kbds_searchobj.len; i++) {
			g.u = kbds_searchobj.str[i].u;
			g.mode = kbds_searchobj.str[i].mode;
			if (g.mode & ATTR_WDUMMY)
				continue;
			if (g.mode & ATTR_WIDE) {
				MODBIT(g.mode, i == kbds_searchobj.cx, ATTR_REVERSE);
				xdrawglyph(g, i + 1, y);
			} else if (i == kbds_searchobj.cx) {
				g.mode = ATTR_WIDE;
				xdrawglyph(g, i + 1, y);
				g.mode = ATTR_REVERSE;
				xdrawglyph(g, i + 1, y);
			} else if (g.u != ' ') {
				g.mode = ATTR_WIDE;
				xdrawglyph(g, i + 1, y);
			}
		}
		g.u = ' ';
		g.mode = (i == kbds_searchobj.cx) ? ATTR_REVERSE : 0;
		xdrawglyph(g, i + 1, y);
	}
}

void
kbds_deletechar(void)
{
	int w, size;
	int cx = kbds_searchobj.cx;

	if (cx >= kbds_searchobj.len)
		return;

	w = (cx < kbds_searchobj.len-1 && kbds_searchobj.str[cx].mode & ATTR_WIDE) ? 2 : 1;
	size = kbds_searchobj.maxlen - cx - w;

	if (size > 0)
		memmove(&kbds_searchobj.str[cx], &kbds_searchobj.str[cx+w], size * sizeof(Glyph));

	kbds_searchobj.len -= w;
}

int
kbds_insertchar(Rune u)
{
	int w = (wcwidth(u) > 1) ? 2 : 1;
	int cx = kbds_searchobj.cx;
	int size = kbds_searchobj.maxlen - cx - w;

	if (u < 0x20 || cx + w > kbds_searchobj.maxlen)
		return 0;

	if (size > 0)
		memmove(&kbds_searchobj.str[cx+w], &kbds_searchobj.str[cx], size * sizeof(Glyph));

	kbds_searchobj.str[cx].u = u;
	kbds_searchobj.str[cx].mode = (w > 1) ? ATTR_WIDE : ATTR_NULL;
	if (w > 1) {
		kbds_searchobj.str[cx+1].u = 0;
		kbds_searchobj.str[cx+1].mode = ATTR_WDUMMY;
	}

	kbds_searchobj.len = MIN(kbds_searchobj.len + w, kbds_searchobj.maxlen);
	if (kbds_searchobj.str[kbds_searchobj.len-1].mode & ATTR_WIDE)
		kbds_searchobj.len--;

	kbds_searchobj.cx = MIN(kbds_searchobj.cx + w, kbds_searchobj.len);
	return 1;
}

void
kbds_pasteintosearch(const char *data, int len, int append)
{
	static char buf[BUFSIZ];
	static int buflen;
	Rune u;
	int l, n, charsize;

	if (!append)
		buflen = 0;

	for (; len > 0; len -= l, data += l) {
		l = MIN(sizeof(buf) - buflen, len);
		memmove(buf + buflen, data, l);
		buflen += l;
		for (n = 0; n < buflen; n += charsize) {
			if (IS_SET(MODE_UTF8)) {
				/* process a complete utf8 char */
				charsize = utf8decode(buf + n, &u, buflen - n);
				if (charsize == 0)
					break;
			} else {
				u = buf[n] & 0xFF;
				charsize = 1;
			}
			kbds_insertchar(u);
		}
		buflen -= n;
		/* keep any incomplete UTF-8 byte sequence for the next call */
		if (buflen > 0)
			memmove(buf, buf + n, buflen);
	}
	term.dirty[term.row-1] = 1;
}

int
kbds_top(void)
{
	return IS_SET(MODE_ALTSCREEN) ? 0 : -term.histf + term.scr;
}

int
kbds_bot(void)
{
	return IS_SET(MODE_ALTSCREEN) ? term.row-1 : term.row-1 + term.scr;
}

int
kbds_iswrapped(KCursor *c)
{
	return c->len > 0 && (c->line[c->len-1].mode & ATTR_WRAP);
}

int
kbds_isselectmode(void)
{
	return kbds_in_use && (kbds_mode & (KBDS_MODE_SELECT | KBDS_MODE_LSELECT));
}

int
kbds_issearchmode(void)
{
	return kbds_in_use && (kbds_mode & KBDS_MODE_SEARCH);
}

int
kbds_isflashmode(void)
{
	return kbds_in_use && (kbds_mode & KBDS_MODE_FLASH);
}

int
kbds_isregexmode(void)
{
	return kbds_in_use && (kbds_mode & KBDS_MODE_REGEX);
}

int
kbds_isurlmode(void)
{
	return kbds_in_use && (kbds_mode & KBDS_MODE_URL);
}

void
kbds_setmode(int mode)
{
	kbds_mode = mode;
	term.dirty[0] = 1;
}

void
kbds_selecttext(void)
{
	if (kbds_isselectmode()) {
		if (kbds_mode & KBDS_MODE_LSELECT)
			selextend(term.col-1, kbds_c.y, SEL_RECTANGULAR, 0);
		else
			selextend(kbds_c.x, kbds_c.y, kbds_seltype, 0);
		if (sel.mode == SEL_IDLE)
			kbds_setmode(kbds_mode & ~(KBDS_MODE_SELECT | KBDS_MODE_LSELECT));
	}
}

void
kbds_copytoclipboard(void)
{
	if (kbds_mode & KBDS_MODE_LSELECT) {
		selextend(term.col-1, kbds_c.y, SEL_RECTANGULAR, 1);
		sel.type = SEL_REGULAR;
	} else {
		selextend(kbds_c.x, kbds_c.y, kbds_seltype, 1);
	}
	xsetsel(getsel());
}

void
kbds_clearhighlights(void)
{
	int x, y;
	Line line;

	for (y = (IS_SET(MODE_ALTSCREEN) ? 0 : -term.histf); y < term.row; y++) {
		line = TLINEABS(y);
		for (x = 0; x < term.col; x++) {
			line[x].mode &= ~ATTR_HIGHLIGHT;
			if (line[x].mode & ATTR_FLASH_LABEL) {
				line[x].mode &= ~ATTR_FLASH_LABEL;
				line[x].u = line[x].ubk;
			}
		}
	}
	tfulldirt();
}

void
kbds_moveto(int x, int y)
{
	if (y < 0)
		kscrollup(&((Arg){ .i = -y }));
	else if (y >= term.row)
		kscrolldown(&((Arg){ .i = y - term.row + 1 }));
	kbds_c.x = (x < 0) ? 0 : (x > term.col-1) ? term.col-1 : x;
	kbds_c.y = (y < 0) ? 0 : (y > term.row-1) ? term.row-1 : y;
	kbds_c.line = TLINE(kbds_c.y);
	kbds_c.len = tlinelen(kbds_c.line);
	if (kbds_c.x > 0 && (kbds_c.line[kbds_c.x].mode & ATTR_WDUMMY))
		kbds_c.x--;
	detecturl(kbds_c.x, kbds_c.y, 1);
}

int
kbds_moveforward(KCursor *c, int dx, int wrap)
{
	KCursor n = *c;

	n.x += dx;
	if (n.x >= 0 && n.x < term.col && (n.line[n.x].mode & ATTR_WDUMMY))
		n.x += dx;

	if (n.x < 0) {
		if (!wrap || --n.y < kbds_top())
			return 0;
		n.line = TLINE(n.y);
		n.len = tlinelen(n.line);
		if ((wrap & KBDS_WRAP_LINE) && kbds_iswrapped(&n))
			n.x = n.len-1;
		else if (wrap & KBDS_WRAP_EDGE)
			n.x = term.col-1;
		else
			return 0;
		n.x -= (n.x > 0 && (n.line[n.x].mode & ATTR_WDUMMY)) ? 1 : 0;
	} else if (n.x >= term.col) {
		if (((wrap & KBDS_WRAP_EDGE) ||
		    ((wrap & KBDS_WRAP_LINE) && kbds_iswrapped(&n))) && ++n.y <= kbds_bot()) {
			n.line = TLINE(n.y);
			n.len = tlinelen(n.line);
			n.x = 0;
		} else {
			return 0;
		}
	} else if (n.x >= n.len && dx > 0 && (wrap & KBDS_WRAP_LINE)) {
		if (n.x == n.len && kbds_iswrapped(&n) && n.y < kbds_bot()) {
			++n.y;
			n.line = TLINE(n.y);
			n.len = tlinelen(n.line);
			n.x = 0;
		} else if (!(wrap & KBDS_WRAP_EDGE)) {
			return 0;
		}
	}
	*c = n;
	return 1;
}

int
kbds_isdelim(KCursor c, int xoff, wchar_t *delims)
{
	if (xoff && !kbds_moveforward(&c, xoff, KBDS_WRAP_LINE))
		return 1;
	return wcschr(delims, c.line[c.x].u) != NULL;
}

void
kbds_jumptoprompt(int dy)
{
	int x = 0, y = kbds_c.y + dy, bot, prevscr;
	Line line;

	for (bot = kbds_bot(); bot > kbds_top(); bot--) {
		if (tlinelen(TLINE(bot)) > 0)
			break;
	}

	if ((dy > 0 && y > bot) || IS_SET(MODE_ALTSCREEN))
		return;

	LIMIT(y, kbds_top(), bot);

	for (; y >= kbds_top() && y <= bot; y += dy) {
		for (line = TLINE(y), x = 0; x < term.col; x++) {
			if (line[x].extra & EXT_FTCS_PROMPT_PS1)
				goto found;
		}
		x = 0;
	}

found:
	LIMIT(y, kbds_top(), bot);
	kbds_moveto(x, y);

	/* align the prompt to the top unless select mode is on */
	if (!kbds_isselectmode()) {
		prevscr = term.scr;
		kscrolldown(&((Arg){ .i = kbds_c.y }));
		kbds_moveto(kbds_c.x, kbds_c.y + term.scr - prevscr);
	}
}

int
kbds_ismatch(KCursor c)
{
	KCursor m = c;
	int i, next;
	Rune u;

	if (c.x + kbds_searchobj.len > c.len && (!kbds_iswrapped(&c) || c.y >= kbds_bot()))
		return 0;

	if (kbds_searchobj.wordonly && !kbds_isdelim(c, -1, kbds_sdelim))
		return 0;

	for (next = 0, i = 0; i < kbds_searchobj.len; i++) {
		if (kbds_searchobj.str[i].mode & ATTR_WDUMMY)
			continue;
		if ((next++ && !kbds_moveforward(&c, 1, KBDS_WRAP_LINE)) ||
		    (!kbds_searchobj.ignorecase && kbds_searchobj.str[i].u != c.line[c.x].u) ||
		    (kbds_searchobj.ignorecase && kbds_searchobj.str[i].u != towlower(c.line[c.x].u)))
			return 0;
	}

	if (kbds_searchobj.wordonly && !kbds_isdelim(c, 1, kbds_sdelim))
		return 0;

	for (i = 0; i < kbds_searchobj.len; i++) {
		if (!(kbds_searchobj.str[i].mode & ATTR_WDUMMY)) {
			m.line[m.x].mode |= ATTR_HIGHLIGHT;
			kbds_moveforward(&m, 1, KBDS_WRAP_LINE);
		}
	}

	if (kbds_isflashmode()) {
		m.line[m.x].ubk = m.line[m.x].u;
		u = kbds_searchobj.ignorecase ? towlower(m.line[m.x].u) : m.line[m.x].u;
		insert_char_array(&flash_next_char_record, u);
		insert_kcursor_array(&flash_kcursor_record, m);
	}

	return 1;
}

int
kbds_searchall(void)
{
	KCursor c;
	int count = 0;
	int i, j, is_invalid_label;
	CharArray valid_label;

	init_char_array(&flash_next_char_record, 1);
	init_char_array(&valid_label, 1);
	init_char_array(&flash_used_label, 1);
	init_kcursor_array(&flash_kcursor_record, 1);

	if (!kbds_searchobj.len)
		return 0;

	int begin = kbds_isflashmode() ? 0 : kbds_top();
	int end = kbds_isflashmode() ? term.row - 1 : kbds_bot();

	for (c.y = begin; c.y <= end; c.y++) {
		c.line = TLINE(c.y);
		c.len = tlinelen(c.line);
		for (c.x = 0; c.x < c.len; c.x++)
			count += kbds_ismatch(c);
	}

	for (i = 0; i < LEN(flash_key_label); i++) {
		is_invalid_label = 0;
		for ( j = 0; j < flash_next_char_record.used; j++) {
			if (flash_next_char_record.array[j] == *flash_key_label[i]) {
				is_invalid_label = 1;
				break;
			}
		}
		if (is_invalid_label == 0) {
			insert_char_array(&valid_label, *flash_key_label[i]);
		}
	}

	for ( i = 0; i < flash_kcursor_record.used; i++) {
		if (i < valid_label.used) {
			flash_kcursor_record.array[i].line[flash_kcursor_record.array[i].x].mode |= ATTR_FLASH_LABEL;
			insert_char_array(&flash_used_label, valid_label.array[i]);
			flash_kcursor_record.array[i].line[flash_kcursor_record.array[i].x].u = valid_label.array[i];
		}
	}

	reset_char_array(&valid_label);

	tfulldirt();

	return count;
}

RegexResult get_position_from_regex(char *pattern, unsigned int *wstr) {
	RegexResult result;

	unsigned int match_start = 0, match_length = 0;

    // check if the pattern contains any subpatterns
    int num_subpatterns = 0;
    for (int i = 0; pattern[i] != '\0'; ++i) {
        if (pattern[i] == '(') {
            num_subpatterns++;
        }
    }

    // if there are no subpatterns, exit with an error
    if (num_subpatterns == 0) {
		result.miss = 1;
		printf("No subpatterns found in pattern: %s\n", pattern);
		return result;
    }

    size_t len = wcstombs(NULL, wstr, 0);
    char *str = malloc(len + 1);
    if (!str) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    wcstombs(str, wstr, len + 1);

    const char *error;
    int erroffset;
    pcre *re = pcre_compile(pattern, PCRE_UTF8, &error, &erroffset, NULL);
    if (!re) {
        fprintf(stderr, "PCRE compilation failed at offset %d: %s\n", erroffset, error);
        free(str);
        exit(EXIT_FAILURE);
    }

    int ovector[30];
    int ret = pcre_exec(re, NULL, str, len, 0, 0, ovector, 30);
    if (ret >= 0) {
        // 	match success, extract the match string's index and length
        result.start = ovector[2];
        result.len = ovector[3] - ovector[2];
		result.miss = 0;
		result.pattern = pattern;
    } else if (ret == PCRE_ERROR_NOMATCH) {
		result.miss = 1;
    } else {
		result.miss = 1;
    }

    pcre_free(re);
    free(str);

	return result;
}

int
kbds_ismatch_regex(KCursor c,unsigned int head)
{
	KCursor m = c;
	Rune *target_str;
	RegexResult result;
	RegexKCursor regex_kcursor;
	unsigned int is_exists = 0;
	unsigned int h,i,j,k;
	unsigned int target_len =0;
	char *pattern;

	for (h=0; pattern_list[h] != NULL; h++) {
		pattern = pattern_list[h];
		target_len = m.len - head;
		target_str = xmalloc((target_len + 1) * sizeof(Rune));
		target_str[target_len] = L'\0';

		for (size_t j = 0; j < target_len; j++) {
    		target_str[j] = c.line[head + j].u;
    	}

		result = get_position_from_regex(pattern, target_str);
		if(result.miss == 0) {
			m.x = head + result.start;
			regex_kcursor.c = m;
			regex_kcursor.len = result.len;
			regex_kcursor.pattern = result.pattern;
			is_exists = 0;
			for ( k = 0; k < regex_kcursor_record.used; k++) {
				if (regex_kcursor.c.x == regex_kcursor_record.array[k].c.x && regex_kcursor.c.y == regex_kcursor_record.array[k].c.y) {
					is_exists = 1;
					if (regex_kcursor.len > regex_kcursor_record.array[k].len) {
						regex_kcursor_record.array[k].len = regex_kcursor.len;
					}
					break;
				}
				if (regex_kcursor.c.y == regex_kcursor_record.array[k].c.y && regex_kcursor.pattern == regex_kcursor_record.array[k].pattern && regex_kcursor.c.x < regex_kcursor_record.array[k].c.x && regex_kcursor.c.x + regex_kcursor.len -1 > regex_kcursor_record.array[k].c.x) {
					regex_kcursor.c.line[regex_kcursor.c.x].ubk = regex_kcursor.c.line[regex_kcursor.c.x].u;
					regex_kcursor_record.array[k].c = regex_kcursor.c;
					regex_kcursor_record.array[k].len = regex_kcursor.len;
					is_exists = 1;
					break;
				}
			}
			
			if (is_exists == 0) {
				regex_kcursor.c.line[regex_kcursor.c.x].ubk = regex_kcursor.c.line[regex_kcursor.c.x].u;
				insert_regex_kcursor_array(&regex_kcursor_record, regex_kcursor);
			}
		}
		XFree(target_str);
	}
}

int
kbds_search_regex(void)
{
	KCursor c;
	unsigned int head,count,bottom,target_len,i,j;
	Rune *target_str;
	int head_hit = 0;
	int bottom_hit = 0;

	init_char_array(&flash_used_label, 1);
	init_regex_kcursor_array(&regex_kcursor_record, 1);

	for (c.y = 0; c.y <= term.row - 1; c.y++) {
		c.line = TLINE(c.y);
		c.len = tlinelen(c.line);
		head_hit = 0;
		bottom_hit = 0;
		head = 0;
		bottom = 0;
		for (c.x = 0; c.x < c.len; c.x++) {
			if(head_hit == 0 && bottom_hit == 0 && c.line[c.x].u != L' ' && (is_valid_head_char(c.line[c.x].u) || is_chinese_character(c.line[c.x].u))) {
				head = c.x;
				head_hit = 1;
			}

			if(head_hit !=0 && c.line[c.x].u == L' ') {
				bottom = c.x - 1;
				bottom_hit = 1;
			}

			if(head_hit !=0 && c.x == c.len - 1) {
				bottom = c.x;
				bottom_hit = 1;
			}

			if (head_hit != 0 && bottom_hit != 0 && head != bottom) {
				count += kbds_ismatch_regex(c,head);

				head = 0;
				bottom = 0;
				head_hit = 0;
				bottom_hit = 0;
			}
		}
			
	}

	for ( i = 0; i < LEN(flash_key_label) && i < regex_kcursor_record.used; i++) {
		regex_kcursor_record.array[i].c.line[regex_kcursor_record.array[i].c.x].mode |= ATTR_FLASH_LABEL;
		insert_char_array(&flash_used_label, *flash_key_label[i]);
		regex_kcursor_record.array[i].c.line[regex_kcursor_record.array[i].c.x].u = *flash_key_label[i];
	}

	for ( i = 0; i < regex_kcursor_record.used;i++) {
		for ( j = 1; j < regex_kcursor_record.array[i].len; j++) {
			regex_kcursor_record.array[i].c.line[regex_kcursor_record.array[i].c.x + j].mode |= ATTR_HIGHLIGHT;
		}
	}

	tfulldirt();

	return count;
}

void copy_regex_result(KCursor m, unsigned int len) {
	char *dest = xmalloc(len+1);
	char *dup;
	int i;
	for (i = 0; i < len; i++) {
		if (i == 0)
			dest[i] = m.line[m.x].ubk;
		else
			dest[i] = m.line[m.x+i].u;
	}
	dest[len] = '\0';
	dup = strdup(dest);
	XFree(dest);

	xsetsel(dup);
}

int
kbds_search_url(void)
{
	KCursor c,m;
	unsigned int head,bottom,target_len,i,j;
	unsigned int count = 0;
	char *url;
	int head_hit = 0;
	int bottom_hit = 0;

	init_char_array(&flash_used_label, 1);
	init_kcursor_array(&flash_kcursor_record, 1);

	for (c.y = 0; c.y <= term.row - 1; c.y++) {
		c.line = TLINE(c.y);
		c.len = tlinelen(c.line);
		head_hit = 0;
		bottom_hit = 0;
		head = 0;
		bottom = 0;
		for (c.x = 0; c.x < c.len; c.x++) {
			if(head_hit == 0 && bottom_hit == 0 && c.line[c.x].u != L' ' && (is_valid_head_char(c.line[c.x].u) || is_chinese_character(c.line[c.x].u))) {
				head = c.x;
				head_hit = 1;
			}

			if(head_hit !=0 && c.line[c.x].u == L' ') {
				bottom = c.x - 1;
				bottom_hit = 1;
			}

			if(head_hit !=0 && c.x == c.len - 1) {
				bottom = c.x;
				bottom_hit = 1;
			}

			if (head_hit != 0 && bottom_hit != 0 && head != bottom) {
				url = detecturl(head,c.y,1);
				if (url != NULL) {
					m.x = head;
					m.y = c.y;
					m.line = TLINE(c.y);
					m.len = tlinelen(m.line);
					m.line[head].ubk |= m.line[head].u;
					m.line[head].mode |= ATTR_FLASH_LABEL;
					m.line[head].u = *flash_key_label[count];
					insert_char_array(&flash_used_label, *flash_key_label[count]);
					insert_kcursor_array(&flash_kcursor_record, m);
					count++;
				}
				head = 0;
				bottom = 0;
				head_hit = 0;
				bottom_hit = 0;
			}
		}
			
	}

	tfulldirt();

	return count;
}

void
jump_to_label(Rune label, int len) {
	int i;
	if (kbds_isurlmode()) {
		for ( i = 0; i < flash_kcursor_record.used; i++) {
			if (label == flash_kcursor_record.array[i].line[flash_kcursor_record.array[i].x].u) {
				flash_kcursor_record.array[i].line[flash_kcursor_record.array[i].x].u = flash_kcursor_record.array[i].line[flash_kcursor_record.array[i].x].ubk;
				openUrlOnClick(flash_kcursor_record.array[i].x, flash_kcursor_record.array[i].y, url_opener);
				return;
			}
		}		
	}

	if (kbds_isregexmode()) {
		for ( i = 0; i < regex_kcursor_record.used; i++) {
			if (label == regex_kcursor_record.array[i].c.line[regex_kcursor_record.array[i].c.x].u) {
				copy_regex_result(regex_kcursor_record.array[i].c, regex_kcursor_record.array[i].len);
				return;
			}
		}		
	}

	for ( i = 0; i < flash_kcursor_record.used; i++) {
		if (label == flash_kcursor_record.array[i].line[flash_kcursor_record.array[i].x].u) {
			kbds_moveto(flash_kcursor_record.array[i].x-len, flash_kcursor_record.array[i].y);
		}
	}
}

void
clear_flash_cache() {
	reset_char_array(&flash_next_char_record);
	reset_char_array(&flash_used_label);
	reset_kcursor_array(&flash_kcursor_record);
}

void
clear_regex_cache() {
	reset_regex_kcursor_array(&regex_kcursor_record);
	reset_char_array(&flash_used_label);
}

void
clear_url_cache() {
	reset_kcursor_array(&flash_kcursor_record);
	reset_char_array(&flash_used_label);
}

void
kbds_searchnext(int dir)
{
	KCursor c = kbds_c, n = kbds_c;
	int wrapped = 0;

	if (!kbds_searchobj.len) {
		kbds_quant = 0;
		return;
	}

	if (dir < 0 && c.x > c.len)
		c.x = c.len;

	for (kbds_quant = MAX(kbds_quant, 1); kbds_quant > 0;) {
		if (!kbds_moveforward(&c, dir, KBDS_WRAP_LINE)) {
			c.y += dir;
			if (c.y < kbds_top())
				c.y = kbds_bot(), wrapped++;
			else if (c.y > kbds_bot())
				c.y = kbds_top(), wrapped++;
			if (wrapped > 1)
				break;;
			c.line = TLINE(c.y);
			c.len = tlinelen(c.line);
			c.x = (dir < 0 && c.len > 0) ? c.len-1 : 0;
			c.x -= (c.x > 0 && (c.line[c.x].mode & ATTR_WDUMMY)) ? 1 : 0;
		}
		if (kbds_ismatch(c)) {
			n = c;
			kbds_quant--;
		}
	}

	kbds_moveto(n.x, n.y);
	kbds_quant = 0;
}

void
kbds_searchwordorselection(int dir)
{
	int ney;
	KCursor c = kbds_c;

	kbds_searchobj.cx = kbds_searchobj.len = 0;
	kbds_clearhighlights();

	if (kbds_isselectmode()) {
		c.x = sel.nb.x;
		c.y = sel.nb.y;
		c.line = TLINE(c.y);
		c.len = tlinelen(c.line);
		ney = (kbds_seltype == SEL_RECTANGULAR) ? sel.nb.y : sel.ne.y;
	} else {
		while (kbds_isdelim(c, 0, kbds_sdelim)) {
			if (!kbds_moveforward(&c, 1, KBDS_WRAP_LINE))
				return;
		}
		while (!kbds_isdelim(c, -1, kbds_sdelim))
			kbds_moveforward(&c, -1, KBDS_WRAP_LINE);
	}

	kbds_searchobj.maxlen = term.col;
	for (kbds_c = c; kbds_searchobj.len < kbds_searchobj.maxlen;) {
		if (!kbds_insertchar(towlower(c.line[c.x].u)) ||
		    !kbds_moveforward(&c, 1, KBDS_WRAP_LINE) ||
		    (kbds_isselectmode() && ((c.x > sel.ne.x && c.y == ney) || c.y > ney)) ||
		    (!kbds_isselectmode() && kbds_isdelim(c, 0, kbds_sdelim)))
			break;
	}

	kbds_searchobj.dir = dir;
	kbds_searchobj.ignorecase = 1;
	kbds_searchobj.wordonly = !kbds_isselectmode();
	selclear();
	kbds_setmode(KBDS_MODE_MOVE);
	kbds_moveto(kbds_c.x, kbds_c.y);
	kbds_searchall();
	kbds_searchnext(kbds_searchobj.dir);
}

void
kbds_findnext(int dir, int repeat)
{
	KCursor prev, c = kbds_c, n = kbds_c;
	int skipfirst, yoff = 0;

	if (c.len <= 0 || kbds_findchar == 0) {
		kbds_quant = 0;
		return;
	}

	if (dir < 0 && c.x > c.len)
		c.x = c.len;

	kbds_quant = MAX(kbds_quant, 1);
	skipfirst = (kbds_quant == 1 && repeat && kbds_findtill);

	while (kbds_quant > 0) {
		prev = c;
		if (!kbds_moveforward(&c, dir, KBDS_WRAP_LINE))
			break;
		if (c.line[c.x].u == kbds_findchar) {
			if (skipfirst && prev.x == kbds_c.x && prev.y == kbds_c.y) {
				skipfirst = 0;
				continue;
			}
			n.x = kbds_findtill ? prev.x : c.x;
			n.y = c.y;
			yoff = kbds_findtill ? prev.y - c.y : 0;
			kbds_quant--;
		}
	}

	kbds_moveto(n.x, n.y);
	kbds_moveto(kbds_c.x, kbds_c.y + yoff);
	kbds_quant = 0;
}

void
kbds_nextword(int start, int dir, wchar_t *delims)
{
	KCursor c = kbds_c, n = kbds_c;
	int xoff = start ? -1 : 1;

	if (dir < 0 && c.x > c.len)
		c.x = c.len;
	else if (dir > 0 && c.x >= c.len && c.len > 0)
		c.x = c.len-1;

	for (kbds_quant = MAX(kbds_quant, 1); kbds_quant > 0;) {
		if (!kbds_moveforward(&c, dir, KBDS_WRAP_LINE)) {
			c.y += dir;
			if (c.y < kbds_top() || c.y > kbds_bot())
				break;
			c.line = TLINE(c.y);
			c.len = tlinelen(c.line);
			c.x = (dir < 0 && c.len > 0) ? c.len-1 : 0;
			c.x -= (c.x > 0 && (c.line[c.x].mode & ATTR_WDUMMY)) ? 1 : 0;
		}
		if (c.len > 0 &&
		    !kbds_isdelim(c, 0, delims) && kbds_isdelim(c, xoff, delims)) {
			n = c;
			kbds_quant--;
		}
	}

	kbds_moveto(n.x, n.y);
	kbds_quant = 0;
}

int
kbds_drawcursor(void)
{
	if (kbds_in_use && (!kbds_issearchmode() || kbds_c.y != term.row-1)) {
		xdrawcursor(kbds_c.x, kbds_c.y, TLINE(kbds_c.y)[kbds_c.x],
		            kbds_oc.x, kbds_oc.y, TLINE(kbds_oc.y));
		kbds_oc = kbds_c;
	}
	return term.scr != 0 || kbds_in_use;
}

int
kbds_getcursor(int *cx, int *cy)
{
	if (kbds_in_use) {
		*cx = kbds_c.x;
		*cy = kbds_c.y;
		return 1;
	}
	return 0;
}

int
kbds_keyboardhandler(KeySym ksym, char *buf, int len, int forcequit)
{
	int i, q, dy, ox, oy, eol, islast, prevscr, count, wrap;
	int alt = IS_SET(MODE_ALTSCREEN);
	char *url;
	Line line;
	Rune u;

	if (kbds_isurlmode() && !forcequit) {
		switch (ksym) {
		case XK_Escape:
			kbds_searchobj.len = 0;
			kbds_setmode(kbds_mode & ~KBDS_MODE_URL);
			clear_url_cache();
			kbds_clearhighlights();
			/* If the direct search is aborted, we just go to the next switch
			 * statement and exit the keyboard selection mode immediately */
			if (kbds_searchobj.directsearch)
				break;
			return 0;
		default:
			if (len > 0) {
				utf8decode(buf, &u, len);
				if (is_in_flash_used_label(u) == 1) {
					jump_to_label(u, kbds_searchobj.len);
					kbds_searchobj.len = 0;
					kbds_setmode(kbds_mode & ~KBDS_MODE_URL);
					clear_url_cache();
					kbds_clearhighlights();
					kbds_selecttext();
					kbds_in_use = kbds_quant = 0;
					free(kbds_searchobj.str);
					return MODE_KBDSELECT;
				} else {
					return 0;
				}
			}
			break;
		}
	}

	if (kbds_isregexmode() && !forcequit) {
		switch (ksym) {
		case XK_Escape:
			kbds_searchobj.len = 0;
			kbds_setmode(kbds_mode & ~KBDS_MODE_REGEX);
			clear_regex_cache();
			kbds_clearhighlights();
			/* If the direct search is aborted, we just go to the next switch
			 * statement and exit the keyboard selection mode immediately */
			if (kbds_searchobj.directsearch)
				break;
			return 0;
		default:
			if (len > 0) {
				utf8decode(buf, &u, len);
				if (is_in_flash_used_label(u) == 1) {
					jump_to_label(u, kbds_searchobj.len);
					kbds_searchobj.len = 0;
					kbds_setmode(kbds_mode & ~KBDS_MODE_REGEX);
					clear_regex_cache();
					kbds_clearhighlights();
					kbds_selecttext();
					kbds_in_use = kbds_quant = 0;
					free(kbds_searchobj.str);
					return MODE_KBDSELECT;
				} else {
					return 0;
				}
			}
			break;
		}
	}

	if (kbds_isflashmode() && !forcequit) {
		switch (ksym) {
		case XK_Escape:
			kbds_searchobj.len = 0;
			kbds_setmode(kbds_mode & ~KBDS_MODE_FLASH);
			clear_flash_cache();
			kbds_clearhighlights();
			/* If the direct search is aborted, we just go to the next switch
			 * statement and exit the keyboard selection mode immediately */
			if (kbds_searchobj.directsearch)
				break;
			return 0;
		case XK_BackSpace:
			if (kbds_searchobj.cx == 0)
				break;
			kbds_clearhighlights();
			kbds_searchobj.cx = MAX(kbds_searchobj.cx-1, 0);
			if (kbds_searchobj.str[kbds_searchobj.cx].mode & ATTR_WDUMMY)
				kbds_searchobj.cx = MAX(kbds_searchobj.cx-1, 0);
			if (ksym == XK_BackSpace)
				kbds_deletechar();
			for (kbds_searchobj.ignorecase = 1, i = 0; i < kbds_searchobj.len; i++) {
				if (kbds_searchobj.str[i].u != towlower(kbds_searchobj.str[i].u)) {
					kbds_searchobj.ignorecase = 0;
					break;
				}
			}
			kbds_searchobj.wordonly = 0;
			count = kbds_searchall();
			return 0;
		default:
			if (len > 0) {
				utf8decode(buf, &u, len);
				if (is_in_flash_used_label(u) == 1) {
					jump_to_label(u, kbds_searchobj.len);
					kbds_searchobj.len = 0;
					kbds_setmode(kbds_mode & ~KBDS_MODE_FLASH);
					clear_flash_cache();
					kbds_clearhighlights();
					kbds_selecttext();
					return 0;
				} else if (kbds_searchobj.len > 0 && is_in_flash_next_char_record(u) == 0) {
					return 0;
				} else {
					clear_flash_cache();
				}
				kbds_clearhighlights();
				kbds_insertchar(u);
				for (kbds_searchobj.ignorecase = 1, i = 0; i < kbds_searchobj.len; i++) {
					if (kbds_searchobj.str[i].u != towlower(kbds_searchobj.str[i].u)) {
						kbds_searchobj.ignorecase = 0;
						break;
					}
				}
				kbds_searchobj.wordonly = 0;
				count = kbds_searchall();
				return 0;
			}
			break;
		}
	}

	if (kbds_issearchmode() && !forcequit) {
		switch (ksym) {
		case XK_Escape:
			kbds_searchobj.len = 0;
			/* FALLTHROUGH */
		case XK_Return:
			/* smart case */
			for (kbds_searchobj.ignorecase = 1, i = 0; i < kbds_searchobj.len; i++) {
				if (kbds_searchobj.str[i].u != towlower(kbds_searchobj.str[i].u)) {
					kbds_searchobj.ignorecase = 0;
					break;
				}
			}
			kbds_searchobj.wordonly = 0;
			count = kbds_searchall();
			kbds_searchnext(kbds_searchobj.dir);
			kbds_selecttext();
			kbds_setmode(kbds_mode & ~KBDS_MODE_SEARCH);
			if (count == 0 && kbds_searchobj.directsearch)
				ksym = XK_Escape;
			break;
		case XK_Delete:
		case XK_KP_Delete:
			kbds_deletechar();
			break;
		case XK_BackSpace:
			if (kbds_searchobj.cx == 0)
				break;
			/* FALLTHROUGH */
		case XK_Left:
		case XK_KP_Left:
			kbds_searchobj.cx = MAX(kbds_searchobj.cx-1, 0);
			if (kbds_searchobj.str[kbds_searchobj.cx].mode & ATTR_WDUMMY)
				kbds_searchobj.cx = MAX(kbds_searchobj.cx-1, 0);
			if (ksym == XK_BackSpace)
				kbds_deletechar();
			break;
		case XK_Right:
		case XK_KP_Right:
			kbds_searchobj.cx = MIN(kbds_searchobj.cx+1, kbds_searchobj.len);
			if (kbds_searchobj.cx < kbds_searchobj.len &&
			    kbds_searchobj.str[kbds_searchobj.cx].mode & ATTR_WDUMMY)
				kbds_searchobj.cx++;
			break;
		case XK_Home:
		case XK_KP_Home:
			kbds_searchobj.cx = 0;
			break;
		case XK_End:
		case XK_KP_End:
			kbds_searchobj.cx = kbds_searchobj.len;
			break;
		default:
			if (len > 0) {
				utf8decode(buf, &u, len);
				kbds_insertchar(u);
			}
			break;
		}
		/* If the direct search is aborted, we just go to the next switch
		 * statement and exit the keyboard selection mode immediately */
		if (!(ksym == XK_Escape && kbds_searchobj.directsearch)) {
			term.dirty[term.row-1] = 1;
			return 0;
		}
	} else if ((kbds_mode & KBDS_MODE_FIND) && !forcequit) {
		kbds_findchar = 0;
		switch (ksym) {
		case XK_Escape:
		case XK_Return:
			kbds_quant = 0;
			break;
		default:
			if (len < 1)
				return 0;
			utf8decode(buf, &kbds_findchar, len);
			kbds_findnext(kbds_finddir, 0);
			kbds_selecttext();
			break;
		}
		kbds_setmode(kbds_mode & ~KBDS_MODE_FIND);
		return 0;
	}

	switch (ksym) {
	case -1:
		kbds_searchobj.str = xmalloc(term.col * sizeof(Glyph));
		kbds_searchobj.cx = kbds_searchobj.len = 0;
		kbds_in_use = 1;
		kbds_moveto(term.c.x, term.c.y);
		kbds_oc = kbds_c;
		kbds_setmode(KBDS_MODE_MOVE);
		return MODE_KBDSELECT;
	case XK_V:
		if (kbds_mode & KBDS_MODE_LSELECT) {
			selclear();
			kbds_setmode(kbds_mode & ~(KBDS_MODE_SELECT | KBDS_MODE_LSELECT));
		} else if (kbds_mode & KBDS_MODE_SELECT) {
			selextend(term.col-1, kbds_c.y, SEL_RECTANGULAR, 0);
			sel.ob.x = 0;
			tfulldirt();
			kbds_setmode((kbds_mode ^ KBDS_MODE_SELECT) | KBDS_MODE_LSELECT);
		} else {
			selstart(0, kbds_c.y, 0);
			selextend(term.col-1, kbds_c.y, SEL_RECTANGULAR, 0);
			kbds_setmode(kbds_mode | KBDS_MODE_LSELECT);
		}
		break;
	case XK_v:
		if (kbds_mode & KBDS_MODE_SELECT) {
			selclear();
			kbds_setmode(kbds_mode & ~(KBDS_MODE_SELECT | KBDS_MODE_LSELECT));
		} else if (kbds_mode & KBDS_MODE_LSELECT) {
			selextend(kbds_c.x, kbds_c.y, kbds_seltype, 0);
			kbds_setmode((kbds_mode ^ KBDS_MODE_LSELECT) | KBDS_MODE_SELECT);
		} else {
			selstart(kbds_c.x, kbds_c.y, 0);
			kbds_setmode(kbds_mode | KBDS_MODE_SELECT);
		}
		break;
	case XK_S:
		if (!(kbds_mode & KBDS_MODE_LSELECT)) {
			kbds_seltype ^= (SEL_REGULAR | SEL_RECTANGULAR);
			selextend(kbds_c.x, kbds_c.y, kbds_seltype, 0);
		}
		break;
	case XK_o:
	case XK_O:
		ox = sel.ob.x; oy = sel.ob.y;
		if (kbds_mode & KBDS_MODE_SELECT) {
			if (kbds_seltype == SEL_RECTANGULAR && ksym == XK_O) {
				selstart(kbds_c.x, oy, 0);
				kbds_moveto(ox, kbds_c.y);
			} else {
				selstart(kbds_c.x, kbds_c.y, 0);
				kbds_moveto(ox, oy);
			}
		} else if (kbds_mode & KBDS_MODE_LSELECT) {
			selstart(0, kbds_c.y, 0);
			selextend(term.col-1, kbds_c.y, SEL_RECTANGULAR, 0);
			kbds_moveto(kbds_c.x, oy);
		}
		break;
	case XK_y:
	case XK_Y:
		if (kbds_isselectmode()) {
			kbds_copytoclipboard();
			selclear();
			kbds_setmode(kbds_mode & ~(KBDS_MODE_SELECT | KBDS_MODE_LSELECT));
		}
		break;
	case -2:
	case -3:
	case XK_slash:
	case XK_KP_Divide:
	case XK_question:
		kbds_searchobj.directsearch = (ksym == -2 || ksym == -3);
		kbds_searchobj.dir = (ksym == XK_question || ksym == -3) ? -1 : 1;
		kbds_searchobj.cx = kbds_searchobj.len = 0;
		kbds_searchobj.maxlen = term.col - 2;
		kbds_setmode(kbds_mode | KBDS_MODE_SEARCH);
		kbds_clearhighlights();
		return 0;
	case -4:
	case XK_s:
		kbds_searchobj.directsearch = (ksym == -4);
		kbds_searchobj.dir = 1;
		kbds_searchobj.cx = kbds_searchobj.len = 0;
		kbds_searchobj.maxlen = term.col - 2;
		kbds_setmode(kbds_mode | KBDS_MODE_FLASH);
		kbds_clearhighlights();
		return 0;
	case XK_p:
	case -5:
		kbds_searchobj.directsearch = (ksym == -5);
		kbds_searchobj.dir = 1;
		kbds_setmode(kbds_mode | KBDS_MODE_REGEX);
		kbds_clearhighlights();
		kbds_search_regex();
		return 0;
	case -6:
		kbds_searchobj.directsearch = (ksym == -6);
		kbds_searchobj.dir = 1;
		kbds_setmode(kbds_mode | KBDS_MODE_URL);
		kbds_clearhighlights();
		kbds_search_url();
		return 0;
	case XK_q:
	case XK_Escape:
		if (!kbds_in_use)
			return 0;
		if (kbds_quant && !forcequit) {
			kbds_quant = 0;
			break;
		}
		selclear();
		if (kbds_isselectmode() && !forcequit) {
			kbds_setmode(KBDS_MODE_MOVE);
			break;
		}
		kbds_setmode(KBDS_MODE_MOVE);
		/* FALLTHROUGH */
	case XK_Return:
		if (kbds_isselectmode())
			kbds_copytoclipboard();
		kbds_in_use = kbds_quant = 0;
		free(kbds_searchobj.str);
		kscrolldown(&((Arg){ .i = term.histf }));
		kbds_clearhighlights();
		return MODE_KBDSELECT;
	case XK_n:
	case XK_N:
		kbds_searchnext(ksym == XK_n ? kbds_searchobj.dir : -kbds_searchobj.dir);
		break;
	case XK_asterisk:
	case XK_KP_Multiply:
	case XK_numbersign:
		kbds_searchwordorselection(ksym == XK_numbersign ? -1 : 1);
		break;
	case XK_BackSpace:
		kbds_moveto(0, kbds_c.y);
		break;
	case XK_exclam:
		kbds_moveto(term.col/2, kbds_c.y);
		break;
	case XK_underscore:
		kbds_moveto(term.col-1, kbds_c.y);
		break;
	case XK_dollar:
	case XK_A:
		eol = kbds_c.len-1;
		line = kbds_c.line;
		islast = (kbds_c.x == eol || (kbds_c.x == eol-1 && (line[eol-1].mode & ATTR_WIDE)));
		if (islast && kbds_iswrapped(&kbds_c) && kbds_c.y < kbds_bot())
			kbds_moveto(tlinelen(TLINE(kbds_c.y+1))-1, kbds_c.y+1);
		else
			kbds_moveto(islast ? term.col-1 : eol, kbds_c.y);
		break;
	case XK_asciicircum:
	case XK_I:
		for (i = 0; i < kbds_c.len && kbds_c.line[i].u == ' '; i++)
			;
		kbds_moveto((i < kbds_c.len) ? i : 0, kbds_c.y);
		break;
	case XK_End:
	case XK_KP_End:
		kbds_moveto(kbds_c.x, term.row-1);
		break;
	case XK_Home:
	case XK_KP_Home:
	case XK_H:
		kbds_moveto(kbds_c.x, 0);
		break;
	case XK_M:
		kbds_moveto(kbds_c.x, alt ? (term.row-1) / 2
		                          : MIN(term.c.y + term.scr, term.row-1) / 2);
		break;
	case XK_L:
		kbds_moveto(kbds_c.x, alt ? term.row-1
		                          : MIN(term.c.y + term.scr, term.row-1));
		break;
	case XK_Page_Up:
	case XK_KP_Page_Up:
	case XK_K:
		prevscr = term.scr;
		kscrollup(&((Arg){ .i = term.row }));
		kbds_moveto(kbds_c.x, alt ? 0
		                          : MAX(0, kbds_c.y - term.row + term.scr - prevscr));
		break;
	case XK_Page_Down:
	case XK_KP_Page_Down:
	case XK_J:
		prevscr = term.scr;
		kscrolldown(&((Arg){ .i = term.row }));
		kbds_moveto(kbds_c.x, alt ? term.row-1
		                          : MIN(MIN(term.c.y + term.scr, term.row-1),
		                                    kbds_c.y + term.row + term.scr - prevscr));
		break;
	case XK_g:
		kscrollup(&((Arg){ .i = term.histf }));
		kbds_moveto(kbds_c.x, 0);
		break;
	case XK_G:
		kscrolldown(&((Arg){ .i = term.histf }));
		kbds_moveto(kbds_c.x, alt ? term.row-1 : term.c.y);
		break;
	case XK_b:
	case XK_B:
		kbds_nextword(1, -1, (ksym == XK_b) ? kbds_sdelim : kbds_ldelim);
		break;
	case XK_w:
	case XK_W:
		kbds_nextword(1, +1, (ksym == XK_w) ? kbds_sdelim : kbds_ldelim);
		break;
	case XK_e:
	case XK_E:
		kbds_nextword(0, +1, (ksym == XK_e) ? kbds_sdelim : kbds_ldelim);
		break;
	case XK_z:
		prevscr = term.scr;
		dy = kbds_c.y - (term.row-1) / 2;
		if (dy <= 0)
			kscrollup(&((Arg){ .i = -dy }));
		else
			kscrolldown(&((Arg){ .i = dy }));
		kbds_moveto(kbds_c.x, kbds_c.y + term.scr - prevscr);
		break;
	case XK_f:
	case XK_F:
	case XK_t:
	case XK_T:
		kbds_finddir = (ksym == XK_f || ksym == XK_t) ? 1 : -1;
		kbds_findtill = (ksym == XK_t || ksym == XK_T) ? 1 : 0;
		kbds_setmode(kbds_mode | KBDS_MODE_FIND);
		return 0;
	case XK_semicolon:
	case XK_r:
		kbds_findnext(kbds_finddir, 1);
		break;
	case XK_comma:
	case XK_R:
		kbds_findnext(-kbds_finddir, 1);
		break;
	case XK_Z:
		kbds_jumptoprompt(-1);
		break;
	case XK_X:
		kbds_jumptoprompt(1);
		break;
	case XK_u:
		openUrlOnClick(kbds_c.x, kbds_c.y, url_opener);
		break;
	case XK_U:
		copyUrlOnClick(kbds_c.x, kbds_c.y);
		break;
	case XK_0:
	case XK_KP_0:
		if (!kbds_quant) {
			kbds_moveto(0, kbds_c.y);
			break;
		}
		/* FALLTHROUGH */
	default:
		if (ksym >= XK_0 && ksym <= XK_9) {                 /* 0-9 keyboard */
			q = (kbds_quant * 10) + (ksym ^ XK_0);
			kbds_quant = q <= 99999999 ? q : kbds_quant;
			term.dirty[0] = 1;
			return 0;
		} else if (ksym >= XK_KP_0 && ksym <= XK_KP_9) {    /* 0-9 numpad */
			q = (kbds_quant * 10) + (ksym ^ XK_KP_0);
			kbds_quant = q <= 99999999 ? q : kbds_quant;
			term.dirty[0] = 1;
			return 0;
		} else if (ksym == XK_k || ksym == XK_h)
			i = ksym & 1;
		else if (ksym == XK_l || ksym == XK_j)
			i = ((ksym & 6) | 4) >> 1;
		else if (ksym >= XK_KP_Left && ksym <= XK_KP_Down)
			i = ksym - XK_KP_Left;
		else if ((XK_Home & ksym) != XK_Home || (i = (ksym ^ XK_Home) - 1) > 3)
			return 0;

		kbds_quant = (kbds_quant ? kbds_quant : 1);

		if (i & 1) {
			kbds_c.y += kbds_quant * (i & 2 ? 1 : -1);
		} else {
			for (;kbds_quant > 0; kbds_quant--) {
				if (!kbds_moveforward(&kbds_c, (i & 2) ? 1 : -1,
					    KBDS_WRAP_LINE | KBDS_WRAP_EDGE))
					break;
			}
		}
		kbds_moveto(kbds_c.x, kbds_c.y);
	}
	kbds_selecttext();
	kbds_quant = 0;
	term.dirty[0] = 1;
	return 0;
}

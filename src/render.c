#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <devices/hpet.h>

#include <tvi.h>

#define BLINK_VISIBLE_US 600000
#define BLINK_INVISIBLE_US 300000

static int cursor_visible;
static uint64_t last_blink_us;
static int hpet_fd = -2;
static uint64_t last_fs;
static uint64_t fallback_us;
static int saved_cursor_valid;
static int saved_cursor_x;
static int saved_cursor_y;
static char saved_cursor_under;

static void cursor_clear_overlay(void) {
	if (!cursor_visible || !saved_cursor_valid) return;
	term_goto(saved_cursor_x, saved_cursor_y);
	putchar(saved_cursor_under);
}

static void cursor_invalidate_overlay(void) {
	cursor_clear_overlay();
	saved_cursor_valid = 0;
	cursor_visible = 0;
}

static uint64_t read_femtoseconds(void) {
	uint64_t now = 0;
	if (hpet_fd == -2) hpet_fd = open("/dev/hpet", O_RDONLY);
	if (hpet_fd < 0) return 0;
	if (ioctl(hpet_fd, HPET_IOCTL_GET_FEMTOSECONDS, &now) == 0) return now;
	if (read(hpet_fd, &now, sizeof(now)) == (ssize_t)sizeof(now)) return now;
	return 0;
}

static uint64_t get_now_us(void) {
	uint64_t fs = read_femtoseconds();
	if (!fs) {
		fallback_us += 20000;
		return fallback_us;
	}
	if (!last_fs || fs < last_fs) {
		last_fs = fs;
		return fallback_us;
	}
	fallback_us += (fs - last_fs) / femtosecondsPerMicrosecond;
	last_fs = fs;
	return fallback_us;
}

void render_text(tvi_t *tvi, win_t *win) {
	(void)tvi;
	cursor_invalidate_overlay();
	for (int i=0; i<win->height-1; i++) {
		if (render_line(tvi, win, win->scroll + i) < 0) break;
	}
}

static size_t render_len(const char *str) {
	size_t len = 0;
	while (*str) {
		if (*str == '\t') {
			len += 8;
		} else {
			len++;
		}
		str++;
	}
	return len;
}

static size_t get_line_height(win_t *win, const char *line) {
	size_t line_len = render_len(line);
	size_t lines_count = (line_len + win->width - 1) / win->width;
	if (lines_count == 0) lines_count = 1;
	return lines_count;
}

static int get_line_y(win_t *win, int index) {
	if (index <= win->scroll) return 0;
	int y = 0;
	for (int i=win->scroll; i<index ;i++) {
		if (i >= win->lines_count) {
			y++;
			continue;
		}
		char *line = win->text[i];
		y += get_line_height(win, line);
	}
	return y;
}

int render_line(tvi_t *tvi, win_t *win, size_t index) {
	(void)tvi;
	cursor_invalidate_overlay();
	if (index < (size_t)win->scroll) return -1;
	int y = get_line_y(win, index);
	if (y >= win->height - 1) {
		return -1;
	}
	term_goto(win->x, win->y + y);
	term_reset_color();
	term_clear_line();
	if (index >= (size_t)win->lines_count) {
		term_non_text_color();
		printf("~");
		term_reset_color();
		return 0;
	}
	char *line = win->text[index];
	size_t line_height = get_line_height(win, line);
	if (y + line_height > (size_t)win->height - 1) {
		term_wrap_mark_color();
		printf("@@@");
	} else {
		if (line_height > 1) {
			for (size_t i=1; i<line_height; i++) {
				putchar('\n');
				term_reset_color();
				term_clear_line();
			}
			term_goto(win->x, win->y + y);
		}
		printf("%s", line);
	}
	term_reset_color();
	return 0;
}

void render_status(tvi_t *tvi, win_t *win) {
	(void)tvi;
	cursor_invalidate_overlay();
	term_goto(win->x, win->y + win->height - 1);
	term_inverse_color();
	term_clear_line();
	int y = win->cursor_y;
	if (y < 0) {
		y = 0;
	} else if (y >= win->lines_count) {
		y = win->lines_count - 1;
	}
	int x = win->cursor_x;
	const char *line = (win->lines_count > 0 && win->text) ? win->text[y] : "";
	size_t line_len = strlen(line);
	if ((size_t)x > line_len) x = line_len;

	const char *file = win->files[win->file_index];
	if (!file) file = "[NO NAME]";

	size_t max_len = win->width > 12 ? (size_t)(win->width - 12) : 0;
	if (strlen(file) > max_len) {
		file += strlen(file) - max_len;
		printf("<%s %d,%d", file, y+1, x+1);
	} else {
		printf("%s %d,%d", file, y+1, x+1);
	}
	term_goto(win->x + win->width - 3, win->y + win->height - 1);
	term_status_accent_color();
	printf("tvi");
	term_reset_color();
}

void render_window(tvi_t *tvi, win_t *win) {
	if (tvi->mode != MODE_VISUAL) return;
	render_text(tvi, win);
	render_status(tvi, win);
}

void render_all_windows(tvi_t *tvi) {
	if (tvi->mode != MODE_VISUAL) return;
	for (win_t *win=tvi->first_window; win; win=win->next) {
		render_window(tvi, win);
	}
}

static void cursor_get_screen(tvi_t *tvi, int *screen_x, int *screen_y, char *under) {
	*under = ' ';
	if (tvi->flags & FLAG_PROMPT) {
		*screen_x = (int)tvi->prompt_cursor;
		if (term_width > 0 && *screen_x >= term_width) *screen_x = term_width - 1;
		if (*screen_x < 0) *screen_x = 0;
		*screen_y = term_height-1;
		if (tvi->prompt_cursor < tvi->prompt_len) {
			char c = tvi->prompt[tvi->prompt_cursor];
			*under = (c >= 32 && c <= 126) ? c : ' ';
		}
		return;
	}

	win_t *win = tvi->focus_window;
	int x = win->cursor_x;
	int y = win->cursor_y;
	if (win->lines_count <= 0 || !win->text) {
		*screen_x = win->x;
		*screen_y = win->y;
		return;
	}
	if (y < 0) y = 0;
	if (y >= win->lines_count) y = win->lines_count - 1;
	const char *line = win->text[y];
	size_t line_len = strlen(line);
	if ((size_t)x > line_len) x = line_len;
	*screen_x = 0;
	for (int i=0; i<x; i++) {
		if (line[i] == '\t') {
			*screen_x += 8;
		} else {
			*screen_x += 1;
		}
	}
	int yoff = get_line_y(win, y);
	*screen_x += win->x;
	*screen_y = win->y + yoff;
	if ((size_t)x < line_len) {
		char c = line[x];
		*under = (c == '\t' || c < 32 || c > 126) ? ' ' : c;
	}
}

static void cursor_draw(tvi_t *tvi, int visible) {
	int x, y;
	char under;
	cursor_get_screen(tvi, &x, &y, &under);

	if (tvi->flags & FLAG_PROMPT) {
		if (cursor_visible && saved_cursor_valid) {
			term_goto(saved_cursor_x, saved_cursor_y);
			putchar(saved_cursor_under);
		}
		saved_cursor_valid = 0;
		term_goto(x, y);
		cursor_visible = visible;
		return;
	}

	if (cursor_visible && saved_cursor_valid) {
		term_goto(saved_cursor_x, saved_cursor_y);
		putchar(saved_cursor_under);
	}

	if (visible) {
		term_goto(x, y);
		putchar('_');
		saved_cursor_x = x;
		saved_cursor_y = y;
		saved_cursor_under = under;
		saved_cursor_valid = 1;
	}

	term_goto(x, y);
	cursor_visible = visible;
}

void render_cursor(tvi_t *tvi) {
	int x, y;
	char under;
	cursor_get_screen(tvi, &x, &y, &under);
	term_goto(x, y);
}

void render_prompt(tvi_t *tvi) {
	cursor_invalidate_overlay();
	term_goto(0, term_height-1);
	term_reset_color();
	term_clear_line();
	term_prompt_color();
	size_t max_len = tvi->prompt_len;
	if (term_width > 1 && max_len > (size_t)(term_width - 1)) max_len = (size_t)(term_width - 1);
	if (term_width <= 1) max_len = 0;
	printf("%.*s", (int)max_len, tvi->prompt);
	term_reset_color();
}

void render_flush(tvi_t *tvi) {
	if (tvi->mode != MODE_VISUAL) return;
	cursor_draw(tvi, 1);
	last_blink_us = get_now_us();
	fflush(stdout);
}

void render_blink_tick(tvi_t *tvi) {
	if (tvi->mode != MODE_VISUAL) return;
	uint64_t now = get_now_us();
	uint64_t limit = cursor_visible ? BLINK_VISIBLE_US : BLINK_INVISIBLE_US;
	if (now - last_blink_us < limit) return;
	last_blink_us = now;
	cursor_draw(tvi, !cursor_visible);
	fflush(stdout);
}

void error(tvi_t *tvi, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	cursor_invalidate_overlay();
	if (tvi->mode == MODE_VISUAL) term_goto(0, term_height-1);
	term_reset_color();
	term_clear_line();
	term_error_color();
	vprintf(fmt, args);
	va_end(args);
	term_reset_color();
	if (tvi->mode == MODE_EX) putchar('\n');
}

void print(tvi_t *tvi, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	cursor_invalidate_overlay();
	if (tvi->mode == MODE_VISUAL) term_goto(0, term_height-1);
	term_reset_color();
	term_clear_line();
	vprintf(fmt, args);
	va_end(args);
	term_reset_color();
	if (tvi->mode == MODE_EX && fmt[strlen(fmt)-1] != '\n') putchar('\n');
}

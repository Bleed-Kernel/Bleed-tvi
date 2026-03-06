#include <stdlib.h>
#include <string.h>
#include <tvi.h>

static int clamp_addr(int addr, int min, int max) {
	if (addr < min) return min;
	if (addr > max) return max;
	return addr;
}

// tools to manipulate text
void text_mark_dirty(win_t *win) {
	win->flags |= FLAG_DIRTY;
}

void text_insert_lines(win_t *win, int addr, char *const*lines, size_t lines_count) {
	if (!lines_count) return;
	text_mark_dirty(win);
	addr = clamp_addr(addr, 0, win->lines_count);
	win->text = realloc(win->text, sizeof(char*) * (win->lines_count + lines_count));
	memmove(&win->text[addr+lines_count], &win->text[addr], (win->lines_count-addr)*sizeof(char*));
	win->lines_count += lines_count;
	for (size_t i=0; i<lines_count; i++) {
		win->text[addr+i] = strdup(lines[i]);
	}
}

void text_insert_newline(win_t *win, int x, int y) {
	text_mark_dirty(win);
	char *line = win->text[y];
	size_t line_len = strlen(line);
	x = clamp_addr(x, 0, (int)line_len);
	char *new_line = &line[x];
	text_insert_lines(win, y+1, &new_line, 1);
	line[x] = '\0';
}

void text_insert_buf(win_t *win, int x, int y, const char *buf, size_t count) {
	if (!count) return;
	text_mark_dirty(win);
	char *line = win->text[y];
	size_t line_len = strlen(line);
	x = clamp_addr(x, 0, (int)line_len);
	line = realloc(line, line_len + 1 + count);
	memmove(&line[x+count], &line[x], line_len - x + 1);
	memcpy(&line[x], buf, count);
	win->text[y] = line;
}

void text_delete_reg(tvi_t *tvi, win_t *win, int x, int y, size_t count, int reg) {
	if (!count) return;
	text_mark_dirty(win);
	char *line = win->text[y];
	size_t line_len = strlen(line);
	x = clamp_addr(x, 0, (int)line_len);
	if ((size_t)x >= line_len) return;
	if (count > line_len - (size_t)x) count = line_len - (size_t)x;
	if (reg) {
		char *buf = &line[x];
		reg_write(tvi, reg, &buf, count, REG_CHAR);
	}
	memmove(&line[x], &line[x+count], line_len - x - count + 1);
}

void text_delete(win_t *win, int x, int y, size_t count) {
	text_delete_reg(NULL, win, x, y, count, 0);
}

void text_delete_lines_reg(tvi_t *tvi, win_t *win, int addr, size_t count, int reg) {
	if (!count || !win->lines_count) return;
	text_mark_dirty(win);
	addr = clamp_addr(addr, 0, win->lines_count - 1);
	if (count > (size_t)(win->lines_count - addr)) count = (size_t)(win->lines_count - addr);
	if (!count) return;
	if (reg) {
		reg_write(tvi, reg, &win->text[addr], count, REG_LINE);
	}

	// Keep one empty line to avoid an invalid empty buffer state.
	if (count == (size_t)win->lines_count) {
		for (int i = 0; i < win->lines_count; i++) {
			free(win->text[i]);
		}
		win->text = realloc(win->text, sizeof(char*));
		win->text[0] = strdup("");
		win->lines_count = 1;
		return;
	}

	memmove(&win->text[addr], &win->text[addr+count], (win->lines_count - addr - count) * sizeof(char*));
	win->lines_count -= count;
}

void text_delete_lines(win_t *win, int addr, size_t count) {
	text_delete_lines_reg(NULL, win, addr, count, 0);
}

void text_join(win_t *win, int first, int last, char sep) {
	size_t new_size = 1;
	if (sep) {
		new_size += (last - first);
	}
	for (int current=first; current<=last; current++) {
		new_size += strlen(win->text[current]);
	}
	char *line = win->text[first];
	line = realloc(line, new_size);
	for (int current=first+1; current<=last; current++) {
		if (sep) {
			char buf[2] = {sep, 0};
			strcat(line, buf);
		}
		strcat(line, win->text[current]);
	}
	win->text[first] = line;
	text_delete_lines(win, first+1, last-first);
}

void text_yank_lines(tvi_t *tvi, win_t *win, int addr, size_t count, int reg) {
	reg_write(tvi, reg, &win->text[addr], count, REG_LINE);
}

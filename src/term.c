#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <devices/keyboard.h>
#include <devices/console.h>
#include <libc/epoll.h>
#include <ansii.h>
#include <tvi.h>

enum {
	RAW_MODE_NONE = 0,
	RAW_MODE_TERMIOS,
	RAW_MODE_TTY_FLAGS,
};

static struct termios old;
int term_width;
int term_height;
static int push_char = -1;
static int raw_mode = RAW_MODE_NONE;
static int cursor_x;
static int cursor_y;

static uint32_t old_tty_flags;
static uint32_t old_kbd_flags;
static int have_old_kbd_flags;

static int term_read_byte(void) {
	if (push_char >= 0) {
		int c = push_char;
		push_char = -1;
		return c;
	}
	unsigned char c;
	if (read(STDIN_FILENO, &c, 1) != 1) return EOF;
	return c;
}

void term_fetch_size(void) {
	tty_winsize_t ws = {0};
	if (ioctl(STDOUT_FILENO, TTY_IOCTL_GET_WINSIZE, &ws) == 0 && ws.cols > 0 && ws.rows > 0) {
		term_width = (int)ws.cols;
		term_height = (int)ws.rows;
		return;
	}
	term_width = 80;
	term_height = 25;
}

static int term_translate_event(const keyboard_event_t *ev, int raw_char) {
	switch (ev->keycode) {
	case ArrowUp:
		return KEY_UP;
	case ArrowDown:
		return KEY_DOWN;
	case ArrowLeft:
		return KEY_LEFT;
	case ArrowRight:
		return KEY_RIGHT;
	case Home:
		return KEY_START;
	case End:
		return KEY_END;
	case Backspace:
	case Delete:
		return '\b';
	default:
		break;
	}

	char c = tty_key_to_ascii(ev);
	if (!c) return EOF;
	if (!raw_char && (ev->keymod & KEYMOD_CTRL)) {
		if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
		if (c >= 'A' && c <= 'Z') return CRTL(c);
	}
	return (unsigned char)c;
}

static int term_read_event_key(int raw_char, int block) {
	keyboard_event_t ev;
	for (;;) {
		ssize_t n = read(STDIN_FILENO, &ev, sizeof(ev));
		if (n != (ssize_t)sizeof(ev)) {
			if (!block) return EOF;
			for (volatile int i=0; i<5000; i++);
			continue;
		}
		if (ev.action == KEY_RELEASE) continue;
		int c = term_translate_event(&ev, raw_char);
		if (c != EOF) return c;
	}
}

int term_get_raw_char(void) {
	if (raw_mode == RAW_MODE_TTY_FLAGS) {
		int c = term_read_event_key(1, 1);
		if (c != EOF) return c;
	}
	return term_read_byte();
}

int term_enable_raw_mode(void) {
	uint32_t flags;
	if (ioctl(STDOUT_FILENO, TTY_IOCTL_GET_FLAGS, &flags) == 0) {
		old_tty_flags = flags;
		flags &= ~(TTY_CANNONICAL | TTY_ECHO);
		if (ioctl(STDOUT_FILENO, TTY_IOCTL_SET_FLAGS, &flags) == 0) {
			uint32_t kbd_flags;
			if (ioctl(STDIN_FILENO, TTY_IOCTL_GET_FLAGS, &kbd_flags) == 0) {
				old_kbd_flags = kbd_flags;
				have_old_kbd_flags = 1;
				kbd_flags |= TTY_NONBLOCK;
				(void)ioctl(STDIN_FILENO, TTY_IOCTL_SET_FLAGS, &kbd_flags);
			}
			raw_mode = RAW_MODE_TTY_FLAGS;
			return 0;
		}
	}
	if (tcgetattr(STDIN_FILENO, &old) < 0) {
		perror("tcgetattr");
		return -1;
	}
	struct termios new = old;
	new.c_lflag &= ~(ICANON | ECHO);
	if (tcsetattr(STDIN_FILENO, TCSANOW, &new) < 0) {
		perror("tcsetattr");
		return -1;
	}
	raw_mode = RAW_MODE_TERMIOS;
	return 0;
}

void term_quit_raw_mode(void) {
	if (raw_mode == RAW_MODE_TTY_FLAGS) {
		if (have_old_kbd_flags) {
			(void)ioctl(STDIN_FILENO, TTY_IOCTL_SET_FLAGS, &old_kbd_flags);
			have_old_kbd_flags = 0;
		}
		(void)ioctl(STDOUT_FILENO, TTY_IOCTL_SET_FLAGS, &old_tty_flags);
		raw_mode = RAW_MODE_NONE;
		return;
	}

	if (raw_mode != RAW_MODE_TERMIOS) return;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &old) < 0) {
		perror("tcsetattr");
	}
	raw_mode = RAW_MODE_NONE;
}

int term_have_input(void) {
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) return 0;

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) < 0) {
        close(epfd);
        return 0;
    }

    struct epoll_event rev;
    int nfds = epoll_wait(epfd, &rev, 1, 0);
    close(epfd);
    
    if (nfds > 0 && (rev.events & EPOLLIN)) {
        return 1;
    }

    return 0;
}

int term_get_key(void) {
	if (raw_mode == RAW_MODE_TTY_FLAGS) {
		return term_read_event_key(0, 0);
	}
	int c = term_read_byte();
	if (c != '\033') return c;
	if (!term_have_input()) return c;
	int c2 = term_read_byte();
	if (c2 == 'O') {
		int c3 = term_read_byte();
		switch (c3) {
		case 'H':
			return KEY_START;
		case 'F':
			return KEY_END;
		default:
			return '\033';
		}
	}
	if (c2 != '[') {
		push_char = c2;
		return c;
	}
	int c3 = term_read_byte();
	if (c3 >= '0' && c3 <= '9') {
		int n = c3 - '0';
		for (;;) {
			if (!term_have_input()) return '\033';
			int cx = term_read_byte();
			if (cx >= '0' && cx <= '9') {
				n = n * 10 + (cx - '0');
				continue;
			}
			if (cx == '~') {
				switch (n) {
				case 1:
				case 7:
					return KEY_START;
				case 4:
				case 8:
					return KEY_END;
				default:
					return '\033';
				}
			}
			if (cx == 'H') return KEY_START;
			if (cx == 'F') return KEY_END;
			return '\033';
		}
	}
	switch (c3) {
	case 'A':
		return KEY_UP;
	case 'B':
		return KEY_DOWN;
	case 'C':
		return KEY_RIGHT;
	case 'D':
		return KEY_LEFT;
	case 'H':
		return KEY_START;
	case 'F':
		return KEY_END;
	default:
		return '\033';
	}
}

void term_idle(void) {
	for (volatile int i=0; i<5000; i++);
}

int term_enter_fullscreen(void) {
	if (!isatty(STDOUT_FILENO)) return -1;

	printf(ESC"[J");
	term_goto(0, 0);
	fflush(stdout);
	return 0;

	printf(ESC"[?1049h");
	printf(ESC"[2J");
	printf(ESC"[H");
	fflush(stdout);
	return 0;
}

void term_exit_fullscreen(void) {
	printf(ANSI_RESET RESET_FG);
	if (term_height > 0) {
		term_goto(0, term_height-1);
	}
	printf(ESC"[K");
	putchar('\n');
	fflush(stdout);
}

void term_clear_line(void) {
	int stop = term_width;
	if (stop > 0) stop--;
	for (int i=cursor_x; i<stop; i++) putchar(' ');
	term_goto(cursor_x, cursor_y);
}

void term_goto(int x, int y) {
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (term_width > 0 && x >= term_width) x = term_width - 1;
	if (term_height > 0 && y >= term_height) y = term_height - 1;
	cursor_x = x;
	cursor_y = y;

	tty_cursor_t cursor = {
		.x = (uint32_t)x,
		.y = (uint32_t)y,
	};
	if (ioctl(STDOUT_FILENO, TTY_IOCTL_SET_CURSOR, &cursor) == 0) return;
	printf(ESC"[%d;%df", y+1, x+1);
}

void term_bell(void) {
	return;
}

int term_is_delete(int c) {
	if (raw_mode == RAW_MODE_TTY_FLAGS) return c == '\b' || c == 127;
	return c == old.c_cc[VERASE];
}

void term_reset_color(void) {
	printf(RESET);
}

void term_inverse_color(void) {
	printf(TVI_FG_STATUS ANSI_BOLD ANSI_UNDERLINE);
}

void term_error_color(void) {
	printf(TVI_FG_ERROR ANSI_BOLD);
}

void term_non_text_color(void) {
	printf(TVI_FG_NON_TEXT ANSI_BOLD);
}

void term_wrap_mark_color(void) {
	printf(TVI_FG_WRAP_MARK ANSI_BOLD);
}

void term_prompt_color(void) {
	printf(TVI_FG_PROMPT ANSI_BOLD);
}

void term_status_accent_color(void) {
	printf(TVI_FG_STATUS_ACCENT ANSI_BOLD);
}

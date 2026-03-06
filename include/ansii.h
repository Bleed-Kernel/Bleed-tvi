#ifndef ANSI_H
#define ANSI_H

#define RGB_FG(r,g,b) "\x1b[38;2;" #r ";" #g ";" #b "m"

#define ANSI_RESET      "\x1b[0m"
#define ANSI_BOLD       "\x1b[1m"
#define ANSI_DIM        "\x1b[2m"
#define ANSI_UNDERLINE  "\x1b[4m"
#define ANSI_REVERSE    "\x1b[7m"

#define TVI_FG_NORMAL        RGB_FG(224, 224, 224)
#define TVI_FG_NON_TEXT      RGB_FG(114, 159, 207)
#define TVI_FG_STATUS        RGB_FG(133, 153, 0)
#define TVI_FG_STATUS_ACCENT RGB_FG(181, 137, 0)
#define TVI_FG_PROMPT        RGB_FG(255, 175, 0)
#define TVI_FG_WRAP_MARK     RGB_FG(220, 50, 47)
#define TVI_FG_ERROR         RGB_FG(255, 95, 95)

#define RESET_FG    TVI_FG_NORMAL
#define RESET       ANSI_RESET RESET_FG

#define LOG_INFO    "\x1b[38;2;0;200;255m[INFO]" RESET "  "
#define LOG_OK      "\x1b[38;2;0;255;0m[OK]" RESET "    "
#define LOG_WARN    "\x1b[38;2;255;180;0m[WARN]" RESET "  "
#define LOG_ERROR   "\x1b[38;2;255;50;50m[ERROR]" RESET " "

#define RED_FG          "\x1b[38;2;255;0;0m"
#define GREEN_FG        "\x1b[38;2;0;255;0m"
#define BLUE_FG         "\x1b[38;2;0;0;255m"
#define YELLOW_FG       "\x1b[38;2;255;255;0m"
#define MAGENTA_FG      "\x1b[38;2;255;0;255m"
#define CYAN_FG         "\x1b[38;2;0;255;255m"
#define WHITE_FG        "\x1b[38;2;255;255;255m"
#define BLACK_FG        "\x1b[38;2;0;0;0m"
#define ORANGE_FG       "\x1b[38;2;255;165;0m"
#define PURPLE_FG       "\x1b[38;2;128;0;128m"
#define PINK_FG         "\x1b[38;2;255;192;203m"
#define GRAY_FG         "\x1b[38;2;128;128;128m"
#define LIGHT_GRAY_FG   "\x1b[38;2;192;192;192m"

#endif

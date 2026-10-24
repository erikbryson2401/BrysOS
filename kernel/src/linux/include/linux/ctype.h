/* BrysOS: clasificación de caracteres ASCII */
#ifndef _BRYS_LINUX_CTYPE_H
#define _BRYS_LINUX_CTYPE_H

static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int isalpha(int c) { return islower(c) || isupper(c); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int isprint(int c) { return c >= 32 && c < 127; }
static inline int ispunct(int c) { return isprint(c) && !isalnum(c) && c != ' '; }
static inline int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }
static inline int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
static inline char _tolower(char c) { return (char)(c | 0x20); }
static inline int hex_to_bin(unsigned char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = (unsigned char)tolower(ch);
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

#endif

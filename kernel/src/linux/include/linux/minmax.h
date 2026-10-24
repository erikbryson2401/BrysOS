/* BrysOS: min/max de Linux (evalúan cada argumento una sola vez) */
#ifndef _BRYS_LINUX_MINMAX_H
#define _BRYS_LINUX_MINMAX_H

#define min(x, y) ({ __typeof__(x) _a = (x); __typeof__(y) _b = (y); _a < _b ? _a : _b; })
#define max(x, y) ({ __typeof__(x) _a = (x); __typeof__(y) _b = (y); _a > _b ? _a : _b; })
#define min3(x, y, z) min((__typeof__(x))min(x, y), z)
#define max3(x, y, z) max((__typeof__(x))max(x, y), z)
#define min_t(type, x, y) ({ type _a = (x); type _b = (y); _a < _b ? _a : _b; })
#define max_t(type, x, y) ({ type _a = (x); type _b = (y); _a > _b ? _a : _b; })
#define clamp(val, lo, hi) min((__typeof__(val))max(val, lo), hi)
#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)
#define clamp_val(val, lo, hi) clamp_t(__typeof__(val), val, lo, hi)
#define min_not_zero(x, y) ({ __typeof__(x) __x = (x); __typeof__(y) __y = (y); \
    __x == 0 ? __y : ((__y == 0) ? __x : min(__x, __y)); })
#define umin(x, y) min_t(unsigned long long, x, y)
#define umax(x, y) max_t(unsigned long long, x, y)
#define in_range(val, start, len) ((val) >= (start) && (val) < (start) + (len))

#endif

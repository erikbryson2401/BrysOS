/* BrysOS: printk y sus variantes; todo va al registro de BrysOS */
#ifndef _BRYS_LINUX_PRINTK_H
#define _BRYS_LINUX_PRINTK_H

#include <linux/types.h>
#include <linux/compiler.h>

#define KERN_SOH "\001"
#define KERN_EMERG KERN_SOH "0"
#define KERN_ALERT KERN_SOH "1"
#define KERN_CRIT KERN_SOH "2"
#define KERN_ERR KERN_SOH "3"
#define KERN_WARNING KERN_SOH "4"
#define KERN_NOTICE KERN_SOH "5"
#define KERN_INFO KERN_SOH "6"
#define KERN_DEBUG KERN_SOH "7"
#define KERN_DEFAULT ""
#define KERN_CONT KERN_SOH "c"

int printk(const char* fmt, ...) __printf(1, 2);
int vprintk(const char* fmt, va_list args);

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_emerg(fmt, ...) printk(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert(fmt, ...) printk(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit(fmt, ...) printk(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err(fmt, ...) printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn(fmt, ...) printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning pr_warn
#define pr_notice(fmt, ...) printk(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) printk(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_cont(fmt, ...) printk(KERN_CONT fmt, ##__VA_ARGS__)
/* Los mensajes de depuración se compilan (comprueban el formato) pero no se escriben */
#define no_printk(fmt, ...) ({ if (0) printk(fmt, ##__VA_ARGS__); 0; })
#define pr_debug(fmt, ...) no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_devel(fmt, ...) no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err_once pr_err
#define pr_warn_once pr_warn
#define pr_info_once pr_info
#define pr_err_ratelimited pr_err
#define pr_warn_ratelimited pr_warn
#define pr_info_ratelimited pr_info
#define pr_debug_ratelimited pr_debug
#define printk_once printk
#define printk_ratelimited printk
#define net_ratelimit() 1
#define printk_ratelimit() 1

void dump_stack(void);
void print_hex_dump_bytes(const char* prefix, int type, const void* buf, size_t len);
void print_hex_dump(const char* level, const char* prefix, int type, int rowsize, int groupsize, const void* buf,
                    size_t len, bool ascii);
#define DUMP_PREFIX_NONE 0
#define DUMP_PREFIX_OFFSET 2

#endif

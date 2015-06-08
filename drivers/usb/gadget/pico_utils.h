/* pico_utils.h */
#ifndef _PICO_UTILS_H
#define _PICO_UTILS_H

#include <linux/printk.h>
#include <linux/stringify.h>

/* __func__ is a string, not a macro, coz the preprocessor does not know the name of the current function.
 * 
 * ‘##’ token paste operator has a special meaning when placed between a comma and a variable argument. 
 * if the variable argument is left out when the macro is used, then the comma before the ‘##’ will be deleted. */
#define PICOWRN(format, ...) printk(KERN_WARNING "[pico][WRN][" __FILE__ ":" __stringify(__LINE__) "]%s(): " format, __func__, ##__VA_ARGS__);
#define PICOERR(format, ...) printk(KERN_ERR "[pico][ERR][" __FILE__ ":" __stringify(__LINE__) "]%s(): " format, __func__, ##__VA_ARGS__);
#ifdef ENABLE_PICO_DBG
#define PICODBG(format, ...) printk(KERN_DEBUG "[pico][DBG][" __FILE__ ":" __stringify(__LINE__) "]%s(): " format, __func__, ##__VA_ARGS__);
#define PICOINFO(format, ...) printk(KERN_INFO "[pico][" __FILE__ ":" __stringify(__LINE__) "]%s(): " format, __func__, ##__VA_ARGS__);
#else
#define PICODBG(...)
#define PICOINFO(...)
#endif /* ENABLE_PICO_DBG */
#ifdef ENABLE_PICO_VDBG
#define PICOVDBG(format, ...) printk(KERN_DEBUG "[pico][VDBG][" __FILE__ ":" __stringify(__LINE__) "]%s(): " format, __func__, ##__VA_ARGS__);
#else
#define PICOVDBG(...)
#endif /* ENABLE_PICO_VDBG */

#define arraysize(x) (sizeof(x) / sizeof((x)[0]))

#endif /* _PICO_UTILS_H */

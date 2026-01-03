/* Public domain.  */
#ifndef _STDBOOL_H
#define _STDBOOL_H

/* C++ compatibility */
#ifdef __cplusplus
extern "C" {
#endif

/* Define bool type and values */
#define bool _Bool
#define true 1
#define false 0

/* Define __bool_true_false_are_defined to indicate this header is included */
#define __bool_true_false_are_defined 1

#ifdef __cplusplus
}
#endif

#endif /* _STDBOOL_H */
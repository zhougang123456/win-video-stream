#include "windows.h"

#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#if defined(__GNUC__)  && __GNUC__ >= 4
#  define SPICE_OFFSETOF(struct_type, member) \
    ((long) offsetof (struct_type, member))
#else
#  define SPICE_OFFSETOF(struct_type, member)	\
    ((long) ((BYTE*) &((struct_type*) 0)->member))
#endif

/* The SPICE_USE_SAFER_CONTAINEROF macro is used to avoid
 * compilation breakage with older spice-server releases which
 * triggered some errors without an additional patch.
 */
#if defined(__GNUC__) && defined(SPICE_USE_SAFER_CONTAINEROF) && \
    (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4))
#define SPICE_CONTAINEROF(ptr, struct_type, member) ({ \
    const typeof( ((struct_type *)0)->member ) *__mptr = (ptr);    \
    ((struct_type *)(void *)((BYTE *)(__mptr) - SPICE_OFFSETOF(struct_type, member))); })
#else
#define SPICE_CONTAINEROF(ptr, struct_type, member) \
    ((struct_type *)(void *)((BYTE *)(ptr) - SPICE_OFFSETOF(struct_type, member)))
#endif

#define SPICE_MEMBER_P(struct_p, struct_offset)   \
    ((void*) ((BYTE*) (struct_p) + (glong) (struct_offset)))
#define SPICE_MEMBER(member_type, struct_p, struct_offset)   \
    (*(member_type*) SPICE_STRUCT_MEMBER_P ((struct_p), (struct_offset)))

#define SPICE_N_ELEMENTS(arr) (sizeof (arr) / sizeof ((arr)[0]))

 /* a generic safe for loop macro  */
#define SAFE_FOREACH(link, next, cond, ring, data, get_data)            \
    for ((((link) = ((cond) ? ring_get_head(ring) : NULL)),             \
          ((next) = ((link) ? ring_next((ring), (link)) : NULL)),       \
          ((data) = ((link)? (get_data) : NULL)));                      \
         (link);                                                        \
         (((link) = (next)),                                            \
          ((next) = ((link) ? ring_next((ring), (link)) : NULL)),       \
          ((data) = ((link)? (get_data) : NULL))))
/* A union shaped like glibc's pthread_mutex_t: an anonymous union behind two typedefs */
typedef union {
    struct { int count; int owner; } data;
    char bytes[16];
    long align;
} lock_t;
typedef lock_t guard_t;

int lock_take(guard_t *lock, int mode);

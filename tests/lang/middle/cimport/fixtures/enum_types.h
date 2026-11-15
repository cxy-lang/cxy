/* Unnamed enums, the way glibc declares _SC_*: the constants are `int` in C */
enum {
    MODE_SHARED = 1,
    MODE_EXCLUSIVE = 2,
};

enum {
    BIG_FLAG = 0x80000000u,
};

int mode_value(int mode);
unsigned flag_value(unsigned flag);

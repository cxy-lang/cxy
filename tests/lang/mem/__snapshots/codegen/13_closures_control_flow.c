typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct __closure_env3 __closure_env3;
typedef struct __closure_env5 __closure_env5;
typedef struct Closure_130 Closure_130;
typedef struct __closure_env7 __closure_env7;
typedef struct Closure_151 Closure_151;
typedef struct __closure_env9 __closure_env9;
typedef struct __closure_env11 __closure_env11;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct __closure_env3 {
  int32_t a;
};

struct __closure_env5 {
  int32_t b;
};

struct Closure_130 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env7 {
  int8_t i;
};

struct Closure_151 {
  void * env;
  int8_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env9 {
  int32_t x;
};

struct __closure_env11 {
  int32_t y;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static int32_t __closure_body4(__closure_env3 * this);
static int32_t __closure_body6(__closure_env5 * this);
static int32_t testIfElse(bool cond);
static int8_t __closure_body8(__closure_env7 * this);
static void testLoopBreak();
static int32_t __closure_body10(__closure_env9 * this);
static int32_t __closure_body12(__closure_env11 * this);
static int32_t testSwitch(int32_t mode);
static void _main();



static void builtins_panicOnUnhandledException(builtins_Exception * ex) {
  char * __t2;
_L_entry:;
  if (ex != nullptr) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __t2 = ex->vtable->what(ex);
  __cxy_dump_unhandled_exception(ex->name, __t2, ex->stackTrace);
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body4(__closure_env3 * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->a;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t __closure_body6(__closure_env5 * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->b;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testIfElse(bool cond) {
  int32_t __ret;
  Closure_130 fn;
  int32_t a;
  __closure_env3 __t0;
  __closure_env3 __env1;
  int32_t b;
  __closure_env5 __t2;
  __closure_env5 __env3;
_L_entry:;
  if (cond) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  a = 10;
  __t0 = (__closure_env3){.a = a};
  __env1 = __t0;
  fn = (Closure_130){ .env = &__env1, .fn = __closure_body4, .dtor = nullptr };
  goto _L_IF0_end;
_L_IF0_else:;
  b = 20;
  __t2 = (__closure_env5){.b = b};
  __env3 = __t2;
  fn = (Closure_130){ .env = &__env3, .fn = __closure_body6, .dtor = nullptr };
  goto _L_IF0_end;
_L_IF0_end:;
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body8(__closure_env7 * this) {
  int8_t __ret;
_L_entry:;
  __ret = this->i;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testLoopBreak() {
  int8_t i;
  Closure_151 c;
  __closure_env7 __t4;
  __closure_env7 __env5;
_L_entry:;
  i = 0;
  goto _L_FOR0_cond;
_L_FOR0_cond:;
  if (i < 5) goto _L_FOR0_body; else goto _L_exit;
_L_FOR0_body:;
  __t4 = (__closure_env7){.i = i};
  __env5 = __t4;
  c = (Closure_151){ .env = &__env5, .fn = __closure_body8, .dtor = nullptr };
  if (i == 3) goto _L_exit; else goto _L_FOR0_update;
_L_FOR0_update:;
  i = (i + 1);
  goto _L_FOR0_cond;
_L_exit:;
  return;
}

static int32_t __closure_body10(__closure_env9 * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->x;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t __closure_body12(__closure_env11 * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->y;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testSwitch(int32_t mode) {
  int32_t __ret;
  Closure_130 fn;
  int32_t x;
  __closure_env9 __t6;
  __closure_env9 __env7;
  int32_t y;
  __closure_env11 __t8;
  __closure_env11 __env9;
_L_entry:;
  switch (mode) {
    case 0: goto _L_SWITCH0_case_0;
    default: goto _L_SWITCH0_case_1;
  }
_L_SWITCH0_exit:;
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_SWITCH0_case_0:;
  x = 100;
  __t6 = (__closure_env9){.x = x};
  __env7 = __t6;
  fn = (Closure_130){ .env = &__env7, .fn = __closure_body10, .dtor = nullptr };
  goto _L_SWITCH0_exit;
_L_SWITCH0_case_1:;
  y = 200;
  __t8 = (__closure_env11){.y = y};
  __env9 = __t8;
  fn = (Closure_130){ .env = &__env9, .fn = __closure_body12, .dtor = nullptr };
  goto _L_SWITCH0_exit;
_L_exit:;
  return __ret;
}

static void _main() {
_L_entry:;
  testIfElse(true);
  testIfElse(false);
  testLoopBreak();
  testSwitch(((int32_t)(0)));
  testSwitch(((int32_t)(1)));
  goto _L_exit;
_L_exit:;
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

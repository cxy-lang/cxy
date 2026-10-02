typedef struct __closure_env0 __closure_env0;
typedef struct __closure_env2 __closure_env2;
typedef struct Closure_24 Closure_24;
typedef struct __closure_env4 __closure_env4;
typedef struct Closure_45 Closure_45;
typedef struct __closure_env6 __closure_env6;
typedef struct __closure_env8 __closure_env8;

struct __closure_env0 {
  int32_t a;
};

struct __closure_env2 {
  int32_t b;
};

struct Closure_24 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env4 {
  int8_t i;
};

struct Closure_45 {
  void * env;
  int8_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env6 {
  int32_t x;
};

struct __closure_env8 {
  int32_t y;
};

static int32_t __closure_body1(__closure_env0 * this);
static int32_t __closure_body3(__closure_env2 * this);
static int32_t testIfElse(bool cond);
static int8_t __closure_body5(__closure_env4 * this);
static void testLoopBreak();
static int32_t __closure_body7(__closure_env6 * this);
static int32_t __closure_body9(__closure_env8 * this);
static int32_t testSwitch(int32_t mode);
static void _main();

static int32_t __closure_body1(__closure_env0 * this) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t __closure_body3(__closure_env2 * this) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testIfElse(bool cond) {
  int32_t __ret;
  Closure_24 fn;
  int32_t a;
  __closure_env0 __t0;
  __closure_env0 __env1;
  int32_t b;
  __closure_env2 __t2;
  __closure_env2 __env3;
_L_entry:;
  if (cond) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  a = 10;
  __t0 = (__closure_env0){a};
  __env1 = __t0;
  fn = (Closure_24){ .env = &__env1, .fn = __closure_body1, .dtor = nullptr };
  goto _L_IF0_end;
_L_IF0_else:;
  b = 20;
  __t2 = (__closure_env2){b};
  __env3 = __t2;
  fn = (Closure_24){ .env = &__env3, .fn = __closure_body3, .dtor = nullptr };
  goto _L_IF0_end;
_L_IF0_end:;
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body5(__closure_env4 * this) {
  int8_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testLoopBreak() {
  int8_t i;
  Closure_45 c;
  __closure_env4 __t4;
  __closure_env4 __env5;
_L_entry:;
  i = 0;
  goto _L_FOR0_cond;
_L_FOR0_cond:;
  if (i < 5) goto _L_FOR0_body; else goto _L_exit;
_L_FOR0_body:;
  __t4 = (__closure_env4){i};
  __env5 = __t4;
  c = (Closure_45){ .env = &__env5, .fn = __closure_body5, .dtor = nullptr };
  if (i == 3) goto _L_exit; else goto _L_FOR0_update;
_L_FOR0_update:;
  i = (i + 1);
  goto _L_FOR0_cond;
_L_exit:;
  return;
}

static int32_t __closure_body7(__closure_env6 * this) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t __closure_body9(__closure_env8 * this) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testSwitch(int32_t mode) {
  int32_t __ret;
  Closure_24 fn;
  int32_t x;
  __closure_env6 __t6;
  __closure_env6 __env7;
  int32_t y;
  __closure_env8 __t8;
  __closure_env8 __env9;
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
  __t6 = (__closure_env6){x};
  __env7 = __t6;
  fn = (Closure_24){ .env = &__env7, .fn = __closure_body7, .dtor = nullptr };
  goto _L_SWITCH0_exit;
_L_SWITCH0_case_1:;
  y = 200;
  __t8 = (__closure_env8){y};
  __env9 = __t8;
  fn = (Closure_24){ .env = &__env9, .fn = __closure_body9, .dtor = nullptr };
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

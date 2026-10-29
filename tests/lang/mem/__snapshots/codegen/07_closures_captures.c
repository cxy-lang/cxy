typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct __closure_env3 __closure_env3;
typedef struct Closure_136 Closure_136;
typedef struct __closure_env5 __closure_env5;
typedef struct __closure_env7 __closure_env7;
typedef struct __closure_env9 __closure_env9;
typedef struct __closure_env11 __closure_env11;
typedef struct __closure_env13 __closure_env13;
typedef struct __closure_env15 __closure_env15;
typedef struct __closure_env17 __closure_env17;

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
  int8_t a;
};

struct Closure_136 {
  void * env;
  int8_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env5 {
  int8_t * a;
};

struct __closure_env7 {
  int8_t a;
};

struct __closure_env9 {
  int8_t * a;
  int8_t * b;
};

struct __closure_env11 {
  int8_t a;
  int8_t b;
};

struct __closure_env13 {
  int8_t a;
  int8_t * b;
  int8_t c;
};

struct __closure_env15 {
  int8_t a;
  int8_t * b;
  int8_t * c;
};

struct __closure_env17 {
  int8_t * a;
  int8_t b;
  int8_t c;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static int8_t __closure_body4(__closure_env3 * this);
static int32_t testValueCapture();
static int8_t __closure_body6(__closure_env5 * this);
static int32_t testRefCapture();
static int8_t __closure_body8(__closure_env7 * this);
static int32_t testMoveCapture();
static int8_t __closure_body10(__closure_env9 * this);
static int32_t testDefaultRefCapture();
static int8_t __closure_body12(__closure_env11 * this);
static int32_t testDefaultMoveCapture();
static int8_t __closure_body14(__closure_env13 * this);
static int32_t testMixedCapture();
static int8_t __closure_body16(__closure_env15 * this);
static int32_t testTrailingDefaultRefCapture();
static int8_t __closure_body18(__closure_env17 * this);
static int32_t testTrailingDefaultMoveCapture();
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

static int8_t __closure_body4(__closure_env3 * this) {
  int8_t __ret;
_L_entry:;
  __ret = (this->a + 5);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testValueCapture() {
  int32_t __ret;
  int8_t a;
  Closure_136 c;
  __closure_env3 __t0;
  __closure_env3 __env1;
_L_entry:;
  a = 10;
  __t0 = (__closure_env3){.a = a};
  __env1 = __t0;
  c = (Closure_136){ .env = &__env1, .fn = __closure_body4, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body6(__closure_env5 * this) {
  int8_t __ret;
_L_entry:;
  __ret = ((*(this->a)) + 20);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testRefCapture() {
  int32_t __ret;
  int8_t a;
  Closure_136 c;
  __closure_env5 __t2;
  __closure_env5 __env3;
_L_entry:;
  a = 10;
  __t2 = (__closure_env5){.a = &a};
  __env3 = __t2;
  c = (Closure_136){ .env = &__env3, .fn = __closure_body6, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body8(__closure_env7 * this) {
  int8_t __ret;
_L_entry:;
  __ret = (this->a + 30);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testMoveCapture() {
  int32_t __ret;
  int8_t a;
  Closure_136 c;
  __closure_env7 __t4;
  __closure_env7 __env5;
_L_entry:;
  a = 10;
  __t4 = (__closure_env7){.a = a};
  __env5 = __t4;
  c = (Closure_136){ .env = &__env5, .fn = __closure_body8, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body10(__closure_env9 * this) {
  int8_t __ret;
_L_entry:;
  __ret = ((*(this->a)) + (*(this->b)));
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testDefaultRefCapture() {
  int32_t __ret;
  int8_t a;
  int8_t b;
  Closure_136 c;
  __closure_env9 __t6;
  __closure_env9 __env7;
_L_entry:;
  a = 10;
  b = 20;
  __t6 = (__closure_env9){.a = &a, .b = &b};
  __env7 = __t6;
  c = (Closure_136){ .env = &__env7, .fn = __closure_body10, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body12(__closure_env11 * this) {
  int8_t __ret;
_L_entry:;
  __ret = (this->a + this->b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testDefaultMoveCapture() {
  int32_t __ret;
  int8_t a;
  int8_t b;
  Closure_136 c;
  __closure_env11 __t8;
  __closure_env11 __env9;
_L_entry:;
  a = 10;
  b = 20;
  __t8 = (__closure_env11){.a = a, .b = b};
  __env9 = __t8;
  c = (Closure_136){ .env = &__env9, .fn = __closure_body12, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body14(__closure_env13 * this) {
  int8_t __ret;
_L_entry:;
  __ret = ((this->a + (*(this->b))) + this->c);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testMixedCapture() {
  int32_t __ret;
  int8_t a;
  int8_t b;
  int8_t c;
  Closure_136 fn;
  __closure_env13 __t10;
  __closure_env13 __env11;
_L_entry:;
  a = 10;
  b = 20;
  c = 30;
  __t10 = (__closure_env13){.a = a, .b = &b, .c = c};
  __env11 = __t10;
  fn = (Closure_136){ .env = &__env11, .fn = __closure_body14, .dtor = nullptr };
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body16(__closure_env15 * this) {
  int8_t __ret;
_L_entry:;
  __ret = ((this->a + (*(this->b))) + (*(this->c)));
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testTrailingDefaultRefCapture() {
  int32_t __ret;
  int8_t a;
  int8_t b;
  int8_t c;
  Closure_136 fn;
  __closure_env15 __t12;
  __closure_env15 __env13;
_L_entry:;
  a = 10;
  b = 20;
  c = 30;
  __t12 = (__closure_env15){.a = a, .b = &b, .c = &c};
  __env13 = __t12;
  fn = (Closure_136){ .env = &__env13, .fn = __closure_body16, .dtor = nullptr };
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body18(__closure_env17 * this) {
  int8_t __ret;
_L_entry:;
  __ret = (((*(this->a)) + this->b) + this->c);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testTrailingDefaultMoveCapture() {
  int32_t __ret;
  int8_t a;
  int8_t b;
  int8_t c;
  Closure_136 fn;
  __closure_env17 __t14;
  __closure_env17 __env15;
_L_entry:;
  a = 10;
  b = 20;
  c = 30;
  __t14 = (__closure_env17){.a = &a, .b = b, .c = c};
  __env15 = __t14;
  fn = (Closure_136){ .env = &__env15, .fn = __closure_body18, .dtor = nullptr };
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
_L_entry:;
  testValueCapture();
  testRefCapture();
  testMoveCapture();
  testDefaultRefCapture();
  testDefaultMoveCapture();
  testMixedCapture();
  testTrailingDefaultRefCapture();
  testTrailingDefaultMoveCapture();
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

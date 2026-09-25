typedef struct __closure_env0 __closure_env0;
typedef struct Closure_30 Closure_30;
typedef struct __closure_env2 __closure_env2;
typedef struct __closure_env4 __closure_env4;
typedef struct __closure_env6 __closure_env6;
typedef struct __closure_env8 __closure_env8;
typedef struct __closure_env10 __closure_env10;
typedef struct __closure_env12 __closure_env12;
typedef struct __closure_env14 __closure_env14;

struct __closure_env0 {
  int8_t a;
};

struct Closure_30 {
  void * env;
  int8_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env2 {
  int8_t * a;
};

struct __closure_env4 {
  int8_t a;
};

struct __closure_env6 {
  int8_t * a;
  int8_t * b;
};

struct __closure_env8 {
  int8_t a;
  int8_t b;
};

struct __closure_env10 {
  int8_t a;
  int8_t * b;
  int8_t c;
};

struct __closure_env12 {
  int8_t a;
  int8_t * b;
  int8_t * c;
};

struct __closure_env14 {
  int8_t * a;
  int8_t b;
  int8_t c;
};

static int8_t __closure_body1(__closure_env0 * this);
static int32_t testValueCapture();
static int8_t __closure_body3(__closure_env2 * this);
static int32_t testRefCapture();
static int8_t __closure_body5(__closure_env4 * this);
static int32_t testMoveCapture();
static int8_t __closure_body7(__closure_env6 * this);
static int32_t testDefaultRefCapture();
static int8_t __closure_body9(__closure_env8 * this);
static int32_t testDefaultMoveCapture();
static int8_t __closure_body11(__closure_env10 * this);
static int32_t testMixedCapture();
static int8_t __closure_body13(__closure_env12 * this);
static int32_t testTrailingDefaultRefCapture();
static int8_t __closure_body15(__closure_env14 * this);
static int32_t testTrailingDefaultMoveCapture();
static void _main();

static int8_t __closure_body1(__closure_env0 * this) {
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
  Closure_30 c;
  __closure_env0 __t0;
  __closure_env0 __env1;
_L_entry:;
  a = 10;
  __t0 = (__closure_env0){a};
  __env1 = __t0;
  c = (Closure_30){ .env = &__env1, .fn = __closure_body1, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body3(__closure_env2 * this) {
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
  Closure_30 c;
  __closure_env2 __t2;
  __closure_env2 __env3;
_L_entry:;
  a = 10;
  __t2 = (__closure_env2){&a};
  __env3 = __t2;
  c = (Closure_30){ .env = &__env3, .fn = __closure_body3, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body5(__closure_env4 * this) {
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
  Closure_30 c;
  __closure_env4 __t4;
  __closure_env4 __env5;
_L_entry:;
  a = 10;
  __t4 = (__closure_env4){a};
  __env5 = __t4;
  c = (Closure_30){ .env = &__env5, .fn = __closure_body5, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body7(__closure_env6 * this) {
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
  Closure_30 c;
  __closure_env6 __t6;
  __closure_env6 __env7;
_L_entry:;
  a = 10;
  b = 20;
  __t6 = (__closure_env6){&a, &b};
  __env7 = __t6;
  c = (Closure_30){ .env = &__env7, .fn = __closure_body7, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body9(__closure_env8 * this) {
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
  Closure_30 c;
  __closure_env8 __t8;
  __closure_env8 __env9;
_L_entry:;
  a = 10;
  b = 20;
  __t8 = (__closure_env8){a, b};
  __env9 = __t8;
  c = (Closure_30){ .env = &__env9, .fn = __closure_body9, .dtor = nullptr };
  __ret = c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body11(__closure_env10 * this) {
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
  Closure_30 fn;
  __closure_env10 __t10;
  __closure_env10 __env11;
_L_entry:;
  a = 10;
  b = 20;
  c = 30;
  __t10 = (__closure_env10){a, &b, c};
  __env11 = __t10;
  fn = (Closure_30){ .env = &__env11, .fn = __closure_body11, .dtor = nullptr };
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body13(__closure_env12 * this) {
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
  Closure_30 fn;
  __closure_env12 __t12;
  __closure_env12 __env13;
_L_entry:;
  a = 10;
  b = 20;
  c = 30;
  __t12 = (__closure_env12){a, &b, &c};
  __env13 = __t12;
  fn = (Closure_30){ .env = &__env13, .fn = __closure_body13, .dtor = nullptr };
  __ret = fn.fn(fn.env);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body15(__closure_env14 * this) {
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
  Closure_30 fn;
  __closure_env14 __t14;
  __closure_env14 __env15;
_L_entry:;
  a = 10;
  b = 20;
  c = 30;
  __t14 = (__closure_env14){&a, b, c};
  __env15 = __t14;
  fn = (Closure_30){ .env = &__env15, .fn = __closure_body15, .dtor = nullptr };
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
  _main();
  return 0;
}

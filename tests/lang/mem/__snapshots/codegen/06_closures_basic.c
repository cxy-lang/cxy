typedef struct Closure_26 Closure_26;
typedef struct Closure_30 Closure_30;

struct Closure_26 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct Closure_30 {
  void * env;
  int32_t (*fn)(void *, int32_t, int32_t);
  void (*dtor)(void *);
};

static void __closure_body0(void * this);
static void testSimple();
static int32_t __closure_body1(void * this, int32_t a, int32_t b);
static int32_t testParamsAndRet();
static void _main();

static void __closure_body0(void * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void testSimple() {
  Closure_26 c;
_L_entry:;
  c = (Closure_26){ .env = nullptr, .fn = __closure_body0, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body1(void * this, int32_t a, int32_t b) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testParamsAndRet() {
  int32_t __ret;
  Closure_30 add;
_L_entry:;
  add = (Closure_30){ .env = nullptr, .fn = __closure_body1, .dtor = nullptr };
  __ret = add.fn(add.env, 10, 20);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
_L_entry:;
  testSimple();
  testParamsAndRet();
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

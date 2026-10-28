typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Closure_127 Closure_127;
typedef struct Closure_131 Closure_131;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Closure_127 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct Closure_131 {
  void * env;
  int32_t (*fn)(void *, int32_t, int32_t);
  void (*dtor)(void *);
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void __closure_body3(void * this);
static void testSimple();
static int32_t __closure_body4(void * this, int32_t a, int32_t b);
static int32_t testParamsAndRet();
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

static void __closure_body3(void * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void testSimple() {
  Closure_127 c;
_L_entry:;
  c = (Closure_127){ .env = nullptr, .fn = __closure_body3, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body4(void * this, int32_t a, int32_t b) {
  int32_t __ret;
_L_entry:;
  __ret = (a + b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testParamsAndRet() {
  int32_t __ret;
  Closure_131 add;
_L_entry:;
  add = (Closure_131){ .env = nullptr, .fn = __closure_body4, .dtor = nullptr };
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

typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct __closure_env4 __closure_env4;
typedef struct Closure_129 Closure_129;
typedef struct __closure_env6 __closure_env6;
typedef struct Closure_140 Closure_140;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct __closure_env4 {
  int32_t factor;
};

struct Closure_129 {
  void * env;
  int32_t (*fn)(void *, int32_t);
  void (*dtor)(void *);
};

struct __closure_env6 {
  int8_t count;
};

struct Closure_140 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static int32_t __closure_body5(__closure_env4 * this, int32_t x);
static Closure_129 makeMultiplier(int32_t factor);
static int8_t __closure_body7(__closure_env6 * this);
static Closure_140 makeCounter();
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

static int32_t __closure_body5(__closure_env4 * this, int32_t x) {
  int32_t __ret;
_L_entry:;
  __ret = (x * this->factor);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Closure_129 makeMultiplier(int32_t factor) {
  Closure_129 __ret;
  __closure_env4 __t0;
  __closure_env4 * __env1;
_L_entry:;
  __t0 = (__closure_env4){.factor = factor};
  __env1 = __smart_ptr_alloc(sizeof(__closure_env4), nullptr);
  (*(__env1)) = __t0;
  __ret = (Closure_129){ .env = __env1, .fn = __closure_body5, .dtor = __smart_ptr_drop };
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body7(__closure_env6 * this) {
  int8_t __ret;
_L_entry:;
  __ret = (this->count + 1);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Closure_140 makeCounter() {
  Closure_140 __ret;
  int8_t count;
  __closure_env6 __t2;
  __closure_env6 * __env3;
_L_entry:;
  count = 0;
  __t2 = (__closure_env6){.count = count};
  __env3 = __smart_ptr_alloc(sizeof(__closure_env6), nullptr);
  (*(__env3)) = __t2;
  __ret = (Closure_140){ .env = __env3, .fn = __closure_body7, .dtor = __smart_ptr_drop };
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  Closure_129 mul;
  int32_t r1;
  Closure_140 cnt;
  int32_t r2;
_L_entry:;
  mul = makeMultiplier(((int32_t)(5)));
  r1 = mul.fn(mul.env, 10);
  cnt = makeCounter();
  r2 = cnt.fn(cnt.env);
  goto _L_exit;
_L_exit:;
  if (cnt.dtor) cnt.dtor(cnt.env);
  if (mul.dtor) mul.dtor(mul.env);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

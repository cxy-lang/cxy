typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Closure_129 Closure_129;
typedef struct Closure_132 Closure_132;
typedef struct __closure_env4 __closure_env4;
typedef struct __closure_env6 __closure_env6;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Closure_129 {
  void * env;
  int32_t (*fn)(void *, int32_t);
  void (*dtor)(void *);
};

struct Closure_132 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env4 {
  int8_t factor;
};

struct __closure_env6 {
  int8_t * count;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static int32_t apply(int32_t x, Closure_129 op);
static void invokeTwice(Closure_132 cb);
static int32_t __closure_body5(__closure_env4 * this, int32_t x);
static void __closure_body7(__closure_env6 * this);
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

static int32_t apply(int32_t x, Closure_129 op) {
  int32_t __ret;
_L_entry:;
  __ret = op.fn(op.env, x);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void invokeTwice(Closure_132 cb) {
_L_entry:;
  cb.fn(cb.env);
  cb.fn(cb.env);
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

static void __closure_body7(__closure_env6 * this) {
_L_entry:;
  (*(this->count)) = ((*(this->count)) + 1);
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
  int8_t factor;
  int32_t res;
  __closure_env4 __t0;
  __closure_env4 __env1;
  Closure_129 __t2;
  int8_t count;
  __closure_env6 __t3;
  __closure_env6 __env4;
  Closure_132 __t5;
_L_entry:;
  factor = 3;
  __t0 = (__closure_env4){.factor = factor};
  __env1 = __t0;
  __t2 = (Closure_129){ .env = &__env1, .fn = __closure_body5, .dtor = nullptr };
  res = apply(((int32_t)(10)), __t2);
  count = 0;
  __t3 = (__closure_env6){.count = &count};
  __env4 = __t3;
  __t5 = (Closure_132){ .env = &__env4, .fn = __closure_body7, .dtor = nullptr };
  invokeTwice(__t5);
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

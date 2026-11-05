typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Closure_139 Closure_139;
typedef struct Closure_142 Closure_142;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Closure_139 {
  void * env;
  int32_t (*fn)(void *, int32_t, int32_t);
  void (*dtor)(void *);
};

struct Closure_142 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static int32_t rawAdd(int32_t a, int32_t b);
static void rawSay();
static int32_t executeOp(int32_t a, int32_t b, Closure_139 op);
static void executeVoid(Closure_142 cb);
static int32_t __thunk5(void * this, int32_t _a0, int32_t _a1);
static void __thunk6(void * this);
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

static int32_t rawAdd(int32_t a, int32_t b) {
  int32_t __ret;
_L_entry:;
  __ret = (a + b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void rawSay() {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t executeOp(int32_t a, int32_t b, Closure_139 op) {
  int32_t __ret;
_L_entry:;
  __ret = op.fn(op.env, a, b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void executeVoid(Closure_142 cb) {
_L_entry:;
  cb.fn(cb.env);
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __thunk5(void * this, int32_t _a0, int32_t _a1) {
  int32_t __ret;
_L_entry:;
  __ret = rawAdd(_a0, _a1);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __thunk6(void * this) {
_L_entry:;
  rawSay();
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
  int32_t res;
  Closure_139 __t0;
  Closure_142 __t1;
_L_entry:;
  __t0 = (Closure_139){ .env = nullptr, .fn = __thunk5, .dtor = nullptr };
  res = executeOp(((int32_t)(10)), ((int32_t)(20)), __t0);
  __t1 = (Closure_142){ .env = nullptr, .fn = __thunk6, .dtor = nullptr };
  executeVoid(__t1);
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

typedef struct Closure_25 Closure_25;
typedef struct Closure_28 Closure_28;

struct Closure_25 {
  void * env;
  int32_t (*fn)(void *, int32_t, int32_t);
  void (*dtor)(void *);
};

struct Closure_28 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

static int32_t rawAdd(int32_t a, int32_t b);
static void rawSay();
static int32_t executeOp(int32_t a, int32_t b, Closure_25 op);
static void executeVoid(Closure_28 cb);
static int32_t __thunk2(void * this, int32_t _a0, int32_t _a1);
static void __thunk3(void * this);
static void _main();

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

static int32_t executeOp(int32_t a, int32_t b, Closure_25 op) {
  int32_t __ret;
_L_entry:;
  __ret = op.fn(op.env, a, b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void executeVoid(Closure_28 cb) {
_L_entry:;
  cb.fn(cb.env);
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __thunk2(void * this, int32_t _a0, int32_t _a1) {
  int32_t __ret;
_L_entry:;
  __ret = rawAdd(_a0, _a1);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __thunk3(void * this) {
_L_entry:;
  rawSay();
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
  int32_t res;
  Closure_25 __t0;
  Closure_28 __t1;
_L_entry:;
  __t0 = (Closure_25){ .env = nullptr, .fn = __thunk2, .dtor = nullptr };
  res = executeOp(((int32_t)(10)), ((int32_t)(20)), __t0);
  __t1 = (Closure_28){ .env = nullptr, .fn = __thunk3, .dtor = nullptr };
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

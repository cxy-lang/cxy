typedef struct Closure_23 Closure_23;
typedef struct Closure_26 Closure_26;
typedef struct __closure_env1 __closure_env1;
typedef struct __closure_env3 __closure_env3;

struct Closure_23 {
  void * env;
  int32_t (*fn)(void *, int32_t);
  void (*dtor)(void *);
};

struct Closure_26 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env1 {
  int8_t factor;
};

struct __closure_env3 {
  int8_t * count;
};

static int32_t apply(int32_t x, Closure_23 op);
static void invokeTwice(Closure_26 cb);
static int32_t __closure_body2(__closure_env1 * this, int32_t x);
static void __closure_body4(__closure_env3 * this);
static void _main();

static int32_t apply(int32_t x, Closure_23 op) {
  int32_t __ret;
_L_entry:;
  __ret = op.fn(op.env, x);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void invokeTwice(Closure_26 cb) {
_L_entry:;
  cb.fn(cb.env);
  cb.fn(cb.env);
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body2(__closure_env1 * this, int32_t x) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_body4(__closure_env3 * this) {
_L_entry:;
  (*(this->count)) = ((*(this->count)) + 1);
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
  int8_t factor;
  int32_t res;
  __closure_env1 __t0;
  __closure_env1 __env1;
  Closure_23 __t2;
  int8_t count;
  __closure_env3 __t3;
  __closure_env3 __env4;
  Closure_26 __t5;
_L_entry:;
  factor = 3;
  __t0 = (__closure_env1){factor};
  __env1 = __t0;
  __t2 = (Closure_23){ .env = &__env1, .fn = __closure_body2, .dtor = nullptr };
  res = apply(((int32_t)(10)), __t2);
  count = 0;
  __t3 = (__closure_env3){&count};
  __env4 = __t3;
  __t5 = (Closure_26){ .env = &__env4, .fn = __closure_body4, .dtor = nullptr };
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

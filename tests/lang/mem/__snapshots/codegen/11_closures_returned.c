typedef struct __closure_env1 __closure_env1;
typedef struct Closure_23 Closure_23;
typedef struct __closure_env3 __closure_env3;
typedef struct Closure_34 Closure_34;

struct __closure_env1 {
  int32_t factor;
};

struct Closure_23 {
  void * env;
  int32_t (*fn)(void *, int32_t);
  void (*dtor)(void *);
};

struct __closure_env3 {
  int8_t count;
};

struct Closure_34 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

static int32_t __closure_body2(__closure_env1 * this, int32_t x);
static Closure_23 makeMultiplier(int32_t factor);
static int8_t __closure_body4(__closure_env3 * this);
static Closure_34 makeCounter();
static void _main();

static int32_t __closure_body2(__closure_env1 * this, int32_t x) {
  int32_t __ret;
_L_entry:;
  __ret = (x * this->factor);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Closure_23 makeMultiplier(int32_t factor) {
  Closure_23 __ret;
  __closure_env1 __t0;
  __closure_env1 * __env1;
_L_entry:;
  __t0 = (__closure_env1){factor};
  __env1 = __smart_ptr_alloc(sizeof(__closure_env1), nullptr);
  (*(__env1)) = __t0;
  __ret = (Closure_23){ .env = __env1, .fn = __closure_body2, .dtor = __smart_ptr_drop };
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body4(__closure_env3 * this) {
  int8_t __ret;
_L_entry:;
  __ret = (this->count + 1);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Closure_34 makeCounter() {
  Closure_34 __ret;
  int8_t count;
  __closure_env3 __t2;
  __closure_env3 * __env3;
_L_entry:;
  count = 0;
  __t2 = (__closure_env3){count};
  __env3 = __smart_ptr_alloc(sizeof(__closure_env3), nullptr);
  (*(__env3)) = __t2;
  __ret = (Closure_34){ .env = __env3, .fn = __closure_body4, .dtor = __smart_ptr_drop };
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  Closure_23 mul;
  int32_t r1;
  Closure_34 cnt;
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
  _main();
  return 0;
}

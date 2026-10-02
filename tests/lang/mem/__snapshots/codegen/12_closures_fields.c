typedef struct Closure_24 Closure_24;
typedef struct Handler Handler;
typedef struct Closure_33 Closure_33;
typedef struct Dispatcher Dispatcher;
typedef struct __closure_env2 __closure_env2;

struct Closure_24 {
  void * env;
  int32_t (*fn)(void *, int32_t);
  void (*dtor)(void *);
};

struct Handler {
  Closure_24 fn;
};

struct Closure_33 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct Dispatcher {
  Closure_33 fn;
};

struct __closure_env2 {
  int8_t offset;
};

static void Handler_op__init(Handler * this, Closure_24 fn);
static int32_t Handler_call(Handler * this, int32_t x);
static void Handler_op__destructor(Handler * this);
static void Dispatcher_op__init(Dispatcher * this, Closure_33 fn);
static void Dispatcher_dispatch(Dispatcher * this);
static void Dispatcher_op__destructor(Dispatcher * this);
static void Dispatcher_op__destructor_fwd(void * ptr);
static int32_t __closure_body3(__closure_env2 * this, int32_t x);
static Handler Handler_op__initCover(Closure_24 fn);
static void __closure_body5(void * this);
static Dispatcher * Dispatcher_op__initCover(Closure_33 fn);
static void _main();

static void Handler_op__init(Handler * this, Closure_24 fn) {
_L_entry:;
  this->fn = fn;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t Handler_call(Handler * this, int32_t x) {
  int32_t __ret;
_L_entry:;
  __ret = this->fn.fn(this->fn.env, x);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void Handler_op__destructor(Handler * this) {
_L_entry:;
  if (this->fn.dtor) this->fn.dtor(this->fn.env);
  memset(&this->fn, 0, sizeof(Closure_24));
  goto _L_exit;
_L_exit:;
  return;
}

static void Dispatcher_op__init(Dispatcher * this, Closure_33 fn) {
_L_entry:;
  this->fn = fn;
  goto _L_exit;
_L_exit:;
  return;
}

static void Dispatcher_dispatch(Dispatcher * this) {
_L_entry:;
  this->fn.fn(this->fn.env);
  goto _L_exit;
_L_exit:;
  return;
}

static void Dispatcher_op__destructor(Dispatcher * this) {
_L_entry:;
  if (this->fn.dtor) this->fn.dtor(this->fn.env);
  memset(&this->fn, 0, sizeof(Closure_33));
  goto _L_exit;
_L_exit:;
  return;
}

static void Dispatcher_op__destructor_fwd(void * ptr) {
_L_entry:;
  Dispatcher_op__destructor(((Dispatcher *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body3(__closure_env2 * this, int32_t x) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Handler Handler_op__initCover(Closure_24 fn) {
  Handler __ret;
  Handler __obj4;
_L_entry:;
  __obj4 = (Handler){};
  Handler_op__init(&__obj4, fn);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_body5(void * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static Dispatcher * Dispatcher_op__initCover(Closure_33 fn) {
  Dispatcher * __ret;
  Dispatcher * __obj6;
  Dispatcher * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Dispatcher), Dispatcher_op__destructor_fwd);
  __obj6 = ((Dispatcher *)(__t0));
  memset(__obj6, 0, sizeof(Dispatcher));
  Dispatcher_op__init(__obj6, fn);
  __ret = __obj6;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  int8_t offset;
  Handler h;
  __closure_env2 __t1;
  __closure_env2 * __env2;
  Closure_24 __t3;
  int32_t v;
  Dispatcher * d;
  Closure_33 __t4;
_L_entry:;
  offset = 1;
  __t1 = (__closure_env2){offset};
  __env2 = __smart_ptr_alloc(sizeof(__closure_env2), nullptr);
  (*(__env2)) = __t1;
  __t3 = (Closure_24){ .env = __env2, .fn = __closure_body3, .dtor = __smart_ptr_drop };
  h = Handler_op__initCover(__t3);
  v = Handler_call(&h, ((int32_t)(41)));
  __t4 = (Closure_33){ .env = nullptr, .fn = __closure_body5, .dtor = nullptr };
  d = Dispatcher_op__initCover(__t4);
  Dispatcher_dispatch(d);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(d);
  Handler_op__destructor(&(h));
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Closure_126 Closure_126;
typedef struct Handler Handler;
typedef struct Closure_134 Closure_134;
typedef struct Dispatcher Dispatcher;
typedef struct __closure_env5 __closure_env5;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Closure_126 {
  void * env;
  int32_t (*fn)(void *, int32_t);
  void (*dtor)(void *);
};

struct Handler {
  Closure_126 fn;
};

struct Closure_134 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct Dispatcher {
  Closure_134 fn;
};

struct __closure_env5 {
  int8_t offset;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Handler_op__init(Handler * this, Closure_126 fn);
static int32_t Handler_call(Handler * this, int32_t x);
static void Handler_op__destructor(Handler * this);
static void Dispatcher_op__init(Dispatcher * this, Closure_134 fn);
static void Dispatcher_dispatch(Dispatcher * this);
static void Dispatcher_op__destructor(Dispatcher * this);
static void Dispatcher_op__destructor_fwd(void * ptr);
static int32_t __closure_body6(__closure_env5 * this, int32_t x);
static Handler Handler_op__initCover(Closure_126 fn);
static void __closure_body8(void * this);
static Dispatcher * Dispatcher_op__initCover(Closure_134 fn);
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

static void Handler_op__init(Handler * this, Closure_126 fn) {
_L_entry:;
  if (this->fn.dtor) this->fn.dtor(this->fn.env);
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
  memset(&(this->fn), 0, sizeof(Closure_126));
  goto _L_exit;
_L_exit:;
  return;
}

static void Dispatcher_op__init(Dispatcher * this, Closure_134 fn) {
_L_entry:;
  if (this->fn.dtor) this->fn.dtor(this->fn.env);
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
  memset(&(this->fn), 0, sizeof(Closure_134));
  goto _L_exit;
_L_exit:;
  return;
}

static void Dispatcher_op__destructor_fwd(void * ptr) {
_L_entry:;
  Dispatcher_op__destructor(((Dispatcher *)(((Dispatcher *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body6(__closure_env5 * this, int32_t x) {
  int32_t __ret;
_L_entry:;
  __ret = (x + this->offset);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Handler Handler_op__initCover(Closure_126 fn) {
  Handler __ret;
  Handler __obj7;
_L_entry:;
  __obj7 = (Handler){};
  Handler_op__init(&__obj7, fn);
  __ret = __obj7;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_body8(void * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static Dispatcher * Dispatcher_op__initCover(Closure_134 fn) {
  Dispatcher * __ret;
  Dispatcher * __obj9;
  Dispatcher * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Dispatcher), Dispatcher_op__destructor_fwd);
  __obj9 = ((Dispatcher *)(__t0));
  memset(__obj9, 0, sizeof(Dispatcher));
  Dispatcher_op__init(((Dispatcher *)(__obj9)), fn);
  __ret = __obj9;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  int8_t offset;
  Handler h;
  __closure_env5 __t1;
  __closure_env5 * __env2;
  Closure_126 __t3;
  int32_t v;
  Dispatcher * d;
  Closure_134 __t4;
_L_entry:;
  offset = 1;
  __t1 = (__closure_env5){.offset = offset};
  __env2 = __smart_ptr_alloc(sizeof(__closure_env5), nullptr);
  (*(__env2)) = __t1;
  __t3 = (Closure_126){ .env = __env2, .fn = __closure_body6, .dtor = __smart_ptr_drop };
  h = Handler_op__initCover(__t3);
  v = Handler_call(&h, ((int32_t)(41)));
  __t4 = (Closure_134){ .env = nullptr, .fn = __closure_body8, .dtor = nullptr };
  d = Dispatcher_op__initCover(__t4);
  Dispatcher_dispatch(((Dispatcher *)(d)));
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

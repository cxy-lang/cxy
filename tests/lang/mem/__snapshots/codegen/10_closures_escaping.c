typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Closure_126 Closure_126;
typedef struct Storage Storage;

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
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct Storage {
  Closure_126 cb;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Storage_op__init(Storage * this, Closure_126 cb);
static void Storage_op__destructor(Storage * this);
static void Storage_op__destructor_fwd(void * ptr);
static Storage * Storage_op__initCover(Closure_126 cb);
static Storage * storeCallback(Closure_126 cb);
static void __closure_body4(void * this);
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

static void Storage_op__init(Storage * this, Closure_126 cb) {
_L_entry:;
  if (this->cb.dtor) this->cb.dtor(this->cb.env);
  this->cb = cb;
  goto _L_exit;
_L_exit:;
  return;
}

static void Storage_op__destructor(Storage * this) {
_L_entry:;
  if (this->cb.dtor) this->cb.dtor(this->cb.env);
  memset(&(this->cb), 0, sizeof(Closure_126));
  goto _L_exit;
_L_exit:;
  return;
}

static void Storage_op__destructor_fwd(void * ptr) {
_L_entry:;
  Storage_op__destructor(((Storage *)(((Storage *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static Storage * Storage_op__initCover(Closure_126 cb) {
  Storage * __ret;
  Storage * __obj3;
  Storage * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Storage), Storage_op__destructor_fwd);
  __obj3 = ((Storage *)(__t0));
  memset(__obj3, 0, sizeof(Storage));
  Storage_op__init(((Storage *)(__obj3)), cb);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Storage * storeCallback(Closure_126 cb) {
  Storage * __ret;
_L_entry:;
  __ret = Storage_op__initCover(cb);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_body4(void * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
  int8_t x;
  Storage * s;
  Closure_126 __t1;
_L_entry:;
  x = 100;
  __t1 = (Closure_126){ .env = nullptr, .fn = __closure_body4, .dtor = nullptr };
  s = storeCallback(__t1);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(s);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

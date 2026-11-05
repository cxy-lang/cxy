typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Handle Handle;
typedef struct Pair Pair;
typedef struct Outer Outer;
typedef struct Tuple_Handle_i8 Tuple_Handle_i8;
typedef struct Optional_Handle Optional_Handle;
typedef struct Closure_160 Closure_160;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Handle {
  int32_t fd;
};

struct Pair {
  Handle * first;
  int32_t count;
};

struct Outer {
  Pair pair;
  int32_t tag;
};

struct Tuple_Handle_i8 {
  Handle * _0;
  int8_t _1;
};

struct Optional_Handle {
  bool ok;
  Handle * val;
};

struct Closure_160 {
  void * env;
  int8_t (*fn)(void *);
  void (*dtor)(void *);
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Handle_op__init(Handle * this, int32_t fd);
static void Handle_op__deinit(Handle * this);
static void Handle_op__destructor(Handle * this);
static void Handle_op__destructor_fwd(void * ptr);
static void Pair_op__destructor(Pair * this);
static void Outer_op__destructor(Outer * this);
static Handle * Handle_op__initCover(int32_t fd);
static Handle * make();
static int8_t __closure_body4(void * this);
static void _main();
static void __tuple_dctor_153(Tuple_Handle_i8 * tup);

static const __TypeInfo __ti_Handle = { .name = "Handle", .size = sizeof(Handle), .align = _Alignof(Handle), .index = 0, .dropFields = Handle_op__destructor_fwd };


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

static void Handle_op__init(Handle * this, int32_t fd) {
_L_entry:;
  this->fd = fd;
  goto _L_exit;
_L_exit:;
  return;
}

static void Handle_op__deinit(Handle * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Handle_op__destructor(Handle * this) {
_L_entry:;
  Handle_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void Handle_op__destructor_fwd(void * ptr) {
_L_entry:;
  Handle_op__destructor(((Handle *)(((Handle *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void Pair_op__destructor(Pair * this) {
_L_entry:;
  __smart_ptr_drop(this->first);
  this->first = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void Outer_op__destructor(Outer * this) {
_L_entry:;
  Pair_op__destructor(&(this->pair));
  memset(&(this->pair), 0, sizeof(Pair));
  goto _L_exit;
_L_exit:;
  return;
}

static Handle * Handle_op__initCover(int32_t fd) {
  Handle * __ret;
  Handle * __obj3;
  Handle * __t2;
_L_entry:;
  __t2 = __mm_alloc(&__ti_Handle, nullptr);
  __obj3 = ((Handle *)(__t2));
  memset(__obj3, 0, sizeof(Handle));
  Handle_op__init(((Handle *)(__obj3)), fd);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Handle * make() {
  Handle * __ret;
_L_entry:;
  __ret = Handle_op__initCover(((int32_t)(7)));
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int8_t __closure_body4(void * this) {
  int8_t __ret;
_L_entry:;
  __ret = 1;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  Tuple_Handle_i8 t;
  Handle * __t3;
  Optional_Handle o;
  Handle * __t4;
  Outer o2;
  Handle * __t5;
  Pair __t6;
  Closure_160 f;
_L_entry:;
  __t3 = make();
  t = (Tuple_Handle_i8){__t3, 1};
  __t4 = make();
  o.ok = true;
  o.val = __t4;
  __t5 = make();
  __t6 = (Pair){.first = __t5, .count = 1};
  o2 = (Outer){.pair = __t6, .tag = 2};
  f = (Closure_160){ .env = nullptr, .fn = __closure_body4, .dtor = nullptr };
  goto _L_exit;
_L_exit:;
  Outer_op__destructor(&(o2));
    if (o.ok) __smart_ptr_drop(o.val);
  __tuple_dctor_153(&(t));
  return;
}

static void __tuple_dctor_153(Tuple_Handle_i8 * tup) {
_L_entry:;
  __smart_ptr_drop(tup->_0);
  tup->_0 = nullptr;
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

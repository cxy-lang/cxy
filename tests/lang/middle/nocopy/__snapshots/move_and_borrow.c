typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Handle Handle;
typedef struct Pair Pair;

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

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Handle_op__init(Handle * this, int32_t fd);
static void Handle_op__deinit(Handle * this);
static void Handle_op__destructor(Handle * this);
static void Handle_op__destructor_fwd(void * ptr);
static void Pair_op__destructor(Pair * this);
static int32_t use(Handle * h);
static int32_t peek(Handle * h);
static Handle * Handle_op__initCover(int32_t fd);
static Handle * make();
static void _main();

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

static int32_t use(Handle * h) {
  int32_t __ret;
_L_entry:;
  __ret = h->fd;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(h);
  return __ret;
}

static int32_t peek(Handle * h) {
  int32_t __ret;
_L_entry:;
  __ret = h->fd;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Handle * Handle_op__initCover(int32_t fd) {
  Handle * __ret;
  Handle * __obj3;
  Handle * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_Handle, nullptr);
  __obj3 = ((Handle *)(__t1));
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

static void _main() {
  Handle * a;
  int32_t n;
  Handle * b;
  int32_t c;
  Handle * __t2;
  Pair p;
  Pair q;
_L_entry:;
  a = Handle_op__initCover(((int32_t)(1)));
  n = peek(((Handle *)(((Handle *)(a)))));
  b = a;
  __t2 = make();
  c = use(__t2);
  p = (Pair){.first = b, .count = 2};
  q = p;
  goto _L_exit;
_L_exit:;
  Pair_op__destructor(&(q));
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

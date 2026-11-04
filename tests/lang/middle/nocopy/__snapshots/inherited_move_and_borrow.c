typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Base Base;
typedef struct Child Child;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Base {
  int32_t fd;
};

struct Child {
  int32_t fd;
  int32_t extra;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Base_op__init(Base * this, int32_t fd);
static void Base_op__deinit(Base * this);
static void Base_op__destructor(Base * this);
static void Child_op__init(Child * this, int32_t fd);
static void Child_op__destructor(Child * this);
static void Child_op__destructor_fwd(void * ptr);
static int32_t peekBase(Base * b);
static int32_t takeChild(Child * c);
static Child * Child_op__initCover(int32_t fd);
static void _main();

static const __TypeInfo __ti_Child = { .name = "Child", .size = sizeof(Child), .align = _Alignof(Child), .index = 0, .dropFields = Child_op__destructor_fwd };


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

static void Base_op__init(Base * this, int32_t fd) {
_L_entry:;
  this->fd = fd;
  goto _L_exit;
_L_exit:;
  return;
}

static void Base_op__deinit(Base * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Base_op__destructor(Base * this) {
_L_entry:;
  Base_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void Child_op__init(Child * this, int32_t fd) {
_L_entry:;
  Base_op__init(this, fd);
  this->extra = 0;
  goto _L_exit;
_L_exit:;
  return;
}

static void Child_op__destructor(Child * this) {
_L_entry:;
  Base_op__destructor(((Base *)(this)));
  goto _L_exit;
_L_exit:;
  return;
}

static void Child_op__destructor_fwd(void * ptr) {
_L_entry:;
  Child_op__destructor(((Child *)(((Child *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t peekBase(Base * b) {
  int32_t __ret;
_L_entry:;
  __ret = b->fd;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t takeChild(Child * c) {
  int32_t __ret;
_L_entry:;
  __ret = c->fd;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(c);
  return __ret;
}

static Child * Child_op__initCover(int32_t fd) {
  Child * __ret;
  Child * __obj3;
  Child * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_Child, nullptr);
  __obj3 = ((Child *)(__t0));
  memset(__obj3, 0, sizeof(Child));
  Child_op__init(((Child *)(__obj3)), fd);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  Child * c;
  int32_t n;
  Child * d;
  int32_t m;
_L_entry:;
  c = Child_op__initCover(((int32_t)(1)));
  n = peekBase(((Base *)(((Child *)(c)))));
  d = c;
  m = takeChild(d);
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

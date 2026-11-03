typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Base Base;
typedef struct Derived Derived;

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
  int32_t id;
};

struct Derived {
  int32_t id;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Base_op__init(Base * this, int32_t id);
static void Base_op__deinit(Base * this);
static void Base_op__destructor(Base * this);
static void Base_op__destructor_fwd(void * ptr);
static void Derived_op__init(Derived * this, int32_t id);
static void Derived_op__destructor(Derived * this);
static void Derived_op__destructor_fwd(void * ptr);
static Derived * Derived_op__initCover(int32_t id);
static Base * upcast();
static Base * Base_op__initCover(int32_t id);
static Base * plain();
static void _main();

static const __TypeInfo __ti_Derived = { .name = "Derived", .size = sizeof(Derived), .align = _Alignof(Derived), .dropFields = Derived_op__destructor_fwd };
static const __TypeInfo __ti_Base = { .name = "Base", .size = sizeof(Base), .align = _Alignof(Base), .dropFields = Base_op__destructor_fwd };


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

static void Base_op__init(Base * this, int32_t id) {
_L_entry:;
  this->id = id;
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

static void Base_op__destructor_fwd(void * ptr) {
_L_entry:;
  Base_op__destructor(((Base *)(((Base *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void Derived_op__init(Derived * this, int32_t id) {
_L_entry:;
  Base_op__init(this, id);
  goto _L_exit;
_L_exit:;
  return;
}

static void Derived_op__destructor(Derived * this) {
_L_entry:;
  Base_op__destructor(((Base *)(this)));
  goto _L_exit;
_L_exit:;
  return;
}

static void Derived_op__destructor_fwd(void * ptr) {
_L_entry:;
  Derived_op__destructor(((Derived *)(((Derived *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static Derived * Derived_op__initCover(int32_t id) {
  Derived * __ret;
  Derived * __obj3;
  Derived * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_Derived, nullptr);
  __obj3 = ((Derived *)(__t0));
  memset(__obj3, 0, sizeof(Derived));
  Derived_op__init(((Derived *)(__obj3)), id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Base * upcast() {
  Base * __ret;
  Derived * d;
_L_entry:;
  d = Derived_op__initCover(((int32_t)(1)));
  __ret = __smart_ptr_get(((Base *)(d)));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(d);
  return __ret;
}

static Base * Base_op__initCover(int32_t id) {
  Base * __ret;
  Base * __obj4;
  Base * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_Base, nullptr);
  __obj4 = ((Base *)(__t1));
  memset(__obj4, 0, sizeof(Base));
  Base_op__init(((Base *)(__obj4)), id);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Base * plain() {
  Base * __ret;
  Base * b;
_L_entry:;
  b = Base_op__initCover(((int32_t)(2)));
  __ret = __smart_ptr_get(b);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(b);
  return __ret;
}

static void _main() {
  Base * a;
  Base * b;
_L_entry:;
  a = upcast();
  b = plain();
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(b);
  __smart_ptr_drop(a);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

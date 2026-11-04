typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct BaseResource_VTable BaseResource_VTable;
typedef struct BaseResource BaseResource;
typedef struct DerivedResource DerivedResource;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct BaseResource_VTable {
  uint64_t __tid;
  void (*op__deinit)(BaseResource *);
  int32_t (*getId)(BaseResource *);
};

struct BaseResource {
  BaseResource_VTable * vtable;
  int32_t id;
};

struct DerivedResource {
  BaseResource_VTable * vtable;
  int32_t id;
  int32_t extra;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void BaseResource_op__init(BaseResource * this, int32_t id);
static void BaseResource_op__deinit(BaseResource * this);
static int32_t BaseResource_getId(BaseResource * this);
static void BaseResource_op__destructor(BaseResource * this);
static void BaseResource_op__destructor_fwd(void * ptr);
static void BaseResource___defaults_init(BaseResource * this);
static void DerivedResource_op__init(DerivedResource * this, int32_t id, int32_t extra);
static void DerivedResource_op__deinit(DerivedResource * this);
static int32_t DerivedResource_getId(DerivedResource * this);
static void DerivedResource_op__destructor(DerivedResource * this);
static void DerivedResource_op__destructor_fwd(void * ptr);
static void DerivedResource___defaults_init(DerivedResource * this);
static int32_t takeBaseByValue(BaseResource * b);
static int32_t takeBaseByRef(BaseResource * b);
static DerivedResource * DerivedResource_op__initCover(int32_t id, int32_t extra);
static void testClassUpcast();
static void testClassMoveUpcast();
static void _main();

static const __TypeInfo __ti_DerivedResource = { .name = "DerivedResource", .size = sizeof(DerivedResource), .align = _Alignof(DerivedResource), .index = 0, .dropFields = DerivedResource_op__destructor_fwd };


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

BaseResource_VTable BaseResource_vTable = (BaseResource_VTable){.__tid = 132ULL, .op__deinit = ((void (*)(BaseResource *))(BaseResource_op__deinit)), .getId = ((int32_t (*)(BaseResource *))(BaseResource_getId))};
BaseResource_VTable DerivedResource_vTable = (BaseResource_VTable){.__tid = ((uint64_t)(144)), .op__deinit = ((void (*)(BaseResource *))(DerivedResource_op__deinit)), .getId = ((int32_t (*)(BaseResource *))(DerivedResource_getId))};

static void BaseResource_op__init(BaseResource * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void BaseResource_op__deinit(BaseResource * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t BaseResource_getId(BaseResource * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->id;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void BaseResource_op__destructor(BaseResource * this) {
_L_entry:;
  this->vtable->op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void BaseResource_op__destructor_fwd(void * ptr) {
_L_entry:;
  BaseResource_op__destructor(((BaseResource *)(((BaseResource *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void BaseResource___defaults_init(BaseResource * this) {
_L_entry:;
  this->vtable = &BaseResource_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static void DerivedResource_op__init(DerivedResource * this, int32_t id, int32_t extra) {
_L_entry:;
  BaseResource_op__init(this, id);
  this->extra = extra;
  goto _L_exit;
_L_exit:;
  return;
}

static void DerivedResource_op__deinit(DerivedResource * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t DerivedResource_getId(DerivedResource * this) {
  int32_t __ret;
_L_entry:;
  __ret = (this->id + this->extra);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void DerivedResource_op__destructor(DerivedResource * this) {
_L_entry:;
  BaseResource_op__destructor(((BaseResource *)(this)));
  DerivedResource_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void DerivedResource_op__destructor_fwd(void * ptr) {
_L_entry:;
  DerivedResource_op__destructor(((DerivedResource *)(((DerivedResource *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void DerivedResource___defaults_init(DerivedResource * this) {
_L_entry:;
  BaseResource___defaults_init(this);
  this->vtable = &DerivedResource_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t takeBaseByValue(BaseResource * b) {
  int32_t __ret;
_L_entry:;
  __ret = b->vtable->getId(((BaseResource *)(b)));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(b);
  return __ret;
}

static int32_t takeBaseByRef(BaseResource * b) {
  int32_t __ret;
_L_entry:;
  __ret = b->vtable->getId(b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static DerivedResource * DerivedResource_op__initCover(int32_t id, int32_t extra) {
  DerivedResource * __ret;
  DerivedResource * __obj3;
  DerivedResource * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_DerivedResource, nullptr);
  __obj3 = ((DerivedResource *)(__t0));
  memset(__obj3, 0, sizeof(DerivedResource));
  DerivedResource___defaults_init(((DerivedResource *)(__obj3)));
  DerivedResource_op__init(((DerivedResource *)(__obj3)), id, extra);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testClassUpcast() {
  DerivedResource * d;
  BaseResource * b;
  int32_t val1;
  DerivedResource * __copy0;
  int32_t val2;
_L_entry:;
  d = DerivedResource_op__initCover(((int32_t)(10)), ((int32_t)(5)));
  b = __smart_ptr_get(d);
  __copy0 = __smart_ptr_get(d);
  val1 = takeBaseByValue(((BaseResource *)(__copy0)));
  val2 = takeBaseByRef(((BaseResource *)(((DerivedResource *)(d)))));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(b);
  __smart_ptr_drop(d);
  return;
}

static void testClassMoveUpcast() {
  DerivedResource * d;
  BaseResource * b;
_L_entry:;
  d = DerivedResource_op__initCover(((int32_t)(100)), ((int32_t)(20)));
  b = d;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(b);
  return;
}

static void _main() {
_L_entry:;
  testClassUpcast();
  testClassMoveUpcast();
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

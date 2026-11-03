typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct Optional_Resource Optional_Resource;
typedef struct Holder Holder;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Resource {
  int32_t id;
};

struct Optional_Resource {
  bool ok;
  Resource * val;
};

struct Holder {
  Optional_Resource res;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static void Holder_op__destructor(Holder * this);
static int32_t isSetConst(Optional_Resource * it);
static Resource * Resource_op__initCover(int32_t id);
static int32_t isSetI_OZResource_E(Optional_Resource * it);
static void _main();

static const __TypeInfo __ti_Resource = { .name = "Resource", .size = sizeof(Resource), .align = _Alignof(Resource), .dropFields = Resource_op__destructor_fwd };


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

static void Resource_op__init(Resource * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void Resource_op__deinit(Resource * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Resource_op__destructor(Resource * this) {
_L_entry:;
  Resource_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void Resource_op__destructor_fwd(void * ptr) {
_L_entry:;
  Resource_op__destructor(((Resource *)(((Resource *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void Holder_op__destructor(Holder * this) {
_L_entry:;
    if (this->res.ok) __smart_ptr_drop(this->res.val);
  memset(&(this->res), 0, sizeof(Optional_Resource));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t isSetConst(Optional_Resource * it) {
  int32_t __ret;
_L_entry:;
  if (it->ok) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __ret = 1;
  goto _L_exit;
_L_IF0_end:;
  __ret = 0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj3;
  Resource * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_Resource, nullptr);
  __obj3 = ((Resource *)(__t0));
  memset(__obj3, 0, sizeof(Resource));
  Resource_op__init(((Resource *)(__obj3)), id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t isSetI_OZResource_E(Optional_Resource * it) {
  int32_t __ret;
_L_entry:;
  if (it->ok) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __ret = 1;
  goto _L_exit;
_L_IF0_end:;
  __ret = 0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  Holder full;
  Resource * __t1;
  Optional_Resource __t2;
  Holder empty;
_L_entry:;
  __t1 = Resource_op__initCover(((int32_t)(1)));
  __t2.ok = true;
  __t2.val = __t1;
  full = (Holder){.res = __t2};
  empty = (Holder){.res = (Optional_Resource){ .ok = false }};
  isSetI_OZResource_E(((Optional_Resource *)(&full.res)));
  isSetI_OZResource_E(((Optional_Resource *)(&empty.res)));
  isSetConst(&full.res);
  isSetConst(&empty.res);
  goto _L_exit;
_L_exit:;
  Holder_op__destructor(&(empty));
  Holder_op__destructor(&(full));
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

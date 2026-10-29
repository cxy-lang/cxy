typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct Slice_Resource Slice_Resource;
typedef struct __drop_flags___array_dctor_137_t __drop_flags___array_dctor_137_t;

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

struct Slice_Resource {
  Resource * * ptr;
  uint64_t len;
};

struct __drop_flags___array_dctor_137_t {
  uint64_t arr: 1;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static Resource * Resource_op__initCover(int32_t id);
static void testArrayRAII();
static void testArrayIndexOverwrite();
static void testArrayMoveElement();
static void testArraySliceBorrow();
static void _main();
static void __array_dctor_137(Slice_Resource arr);


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

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj3;
  Resource * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj3 = ((Resource *)(__t0));
  memset(__obj3, 0, sizeof(Resource));
  Resource_op__init(((Resource *)(__obj3)), id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testArrayRAII() {
  Resource * arr[3];
_L_entry:;
  arr[0] = Resource_op__initCover(((int32_t)(1)));
  arr[1] = Resource_op__initCover(((int32_t)(2)));
  arr[2] = Resource_op__initCover(((int32_t)(3)));
  goto _L_exit;
_L_exit:;
  __array_dctor_137((Slice_Resource){ .ptr = arr, .len = 3 });
  return;
}

static void testArrayIndexOverwrite() {
  Resource * arr[2];
_L_entry:;
  arr[0] = Resource_op__initCover(((int32_t)(10)));
  arr[1] = Resource_op__initCover(((int32_t)(20)));
  __smart_ptr_drop(arr[0]);
  arr[0] = Resource_op__initCover(((int32_t)(30)));
  goto _L_exit;
_L_exit:;
  __array_dctor_137((Slice_Resource){ .ptr = arr, .len = 2 });
  return;
}

static void testArrayMoveElement() {
  Resource * arr[2];
  Resource * r;
_L_entry:;
  arr[0] = Resource_op__initCover(((int32_t)(100)));
  arr[1] = Resource_op__initCover(((int32_t)(200)));
  r = arr[1];
  arr[1] = nullptr;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(r);
  __array_dctor_137((Slice_Resource){ .ptr = arr, .len = 2 });
  return;
}

static void testArraySliceBorrow() {
  Resource * a[2];
  Slice_Resource b;
  Slice_Resource c;
_L_entry:;
  a[0] = Resource_op__initCover(((int32_t)(1)));
  a[1] = Resource_op__initCover(((int32_t)(2)));
  b.ptr = a;
  b.len = 2ULL;
  c.ptr = a;
  c.len = 2ULL;
  goto _L_exit;
_L_exit:;
      __array_dctor_137((Slice_Resource){ .ptr = a, .len = 2 });
  return;
}

static void _main() {
_L_entry:;
  testArrayRAII();
  testArrayIndexOverwrite();
  testArrayMoveElement();
  testArraySliceBorrow();
  goto _L_exit;
_L_exit:;
  return;
}

static void __array_dctor_137(Slice_Resource arr) {
  __drop_flags___array_dctor_137_t __drop_flags;
  uint64_t __i;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags___array_dctor_137_t));
  __drop_flags.arr = 1ULL;
  __i = 0ULL;
  goto _L_FOR0_cond;
_L_FOR0_cond:;
  if (__i < arr.len) goto _L_FOR0_body; else goto _L_exit;
_L_FOR0_body:;
  __smart_ptr_drop(arr.ptr[__i]);
  goto _L_FOR0_update;
_L_FOR0_update:;
  __i = (__i + 1ULL);
  goto _L_FOR0_cond;
_L_exit:;
  if (__drop_flags.arr)   __drop_flags.arr = 0ULL;
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

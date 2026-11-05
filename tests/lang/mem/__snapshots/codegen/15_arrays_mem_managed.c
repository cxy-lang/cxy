typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct Slice_Resource Slice_Resource;
typedef struct __drop_flags___array_dctor_141_t __drop_flags___array_dctor_141_t;

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

struct __drop_flags___array_dctor_141_t {
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
static void __array_dctor_141(Slice_Resource arr);

static const __TypeInfo __ti_Resource = { .name = "Resource", .size = sizeof(Resource), .align = _Alignof(Resource), .index = 0, .dropFields = Resource_op__destructor_fwd };


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
  __t0 = __mm_alloc(&__ti_Resource, nullptr);
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
  Resource * __t1;
  Resource * __t2;
  Resource * __t3;
_L_entry:;
  __t1 = Resource_op__initCover(((int32_t)(1)));
  __t2 = Resource_op__initCover(((int32_t)(2)));
  __t3 = Resource_op__initCover(((int32_t)(3)));
  arr[0] = __t1;
  arr[1] = __t2;
  arr[2] = __t3;
  goto _L_exit;
_L_exit:;
  __array_dctor_141((Slice_Resource){ .ptr = arr, .len = 3 });
  return;
}

static void testArrayIndexOverwrite() {
  Resource * arr[2];
  Resource * __t4;
  Resource * __t5;
_L_entry:;
  __t4 = Resource_op__initCover(((int32_t)(10)));
  __t5 = Resource_op__initCover(((int32_t)(20)));
  arr[0] = __t4;
  arr[1] = __t5;
  __smart_ptr_drop(arr[0]);
  arr[0] = Resource_op__initCover(((int32_t)(30)));
  goto _L_exit;
_L_exit:;
  __array_dctor_141((Slice_Resource){ .ptr = arr, .len = 2 });
  return;
}

static void testArrayMoveElement() {
  Resource * arr[2];
  Resource * __t6;
  Resource * __t7;
  Resource * r;
  Resource * __mv0;
_L_entry:;
  __t6 = Resource_op__initCover(((int32_t)(100)));
  __t7 = Resource_op__initCover(((int32_t)(200)));
  arr[0] = __t6;
  arr[1] = __t7;
  __mv0 = arr[1];
  arr[1] = nullptr;
  r = __mv0;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(r);
  __array_dctor_141((Slice_Resource){ .ptr = arr, .len = 2 });
  return;
}

static void testArraySliceBorrow() {
  Resource * a[2];
  Resource * __t8;
  Resource * __t9;
  Slice_Resource b;
  Slice_Resource c;
_L_entry:;
  __t8 = Resource_op__initCover(((int32_t)(1)));
  __t9 = Resource_op__initCover(((int32_t)(2)));
  a[0] = __t8;
  a[1] = __t9;
  b.ptr = a;
  b.len = 2ULL;
  c.ptr = a;
  c.len = 2ULL;
  goto _L_exit;
_L_exit:;
      __array_dctor_141((Slice_Resource){ .ptr = a, .len = 2 });
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

static void __array_dctor_141(Slice_Resource arr) {
  __drop_flags___array_dctor_141_t __drop_flags;
  uint64_t __i;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags___array_dctor_141_t));
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

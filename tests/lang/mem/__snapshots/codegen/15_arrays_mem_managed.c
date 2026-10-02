typedef struct Resource Resource;
typedef struct Slice_ptr Slice_ptr;

struct Resource {
  int32_t id;
};

struct Slice_ptr {
  Resource * * ptr;
  uint64_t len;
};

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
static void __array_dctor_32(Slice_ptr arr);

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
  Resource_op__destructor(((Resource *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj0;
  Resource * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj0 = ((Resource *)(__t0));
  memset(__obj0, 0, sizeof(Resource));
  Resource_op__init(__obj0, id);
  __ret = __obj0;
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
  __array_dctor_32((Slice_ptr){ .ptr = arr, .len = 3 });
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
  __array_dctor_32((Slice_ptr){ .ptr = arr, .len = 2 });
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
  __array_dctor_32((Slice_ptr){ .ptr = arr, .len = 2 });
  return;
}

static void testArraySliceBorrow() {
  Resource * a[2];
  Slice_ptr b;
  Slice_ptr c;
_L_entry:;
  a[0] = Resource_op__initCover(((int32_t)(1)));
  a[1] = Resource_op__initCover(((int32_t)(2)));
  b.ptr = a;
  b.len = 2;
  c.ptr = a;
  c.len = 2;
  goto _L_exit;
_L_exit:;
    __array_dctor_32((Slice_ptr){ .ptr = a, .len = 2 });
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

static void __array_dctor_32(Slice_ptr arr) {
  uint64_t __i;
_L_entry:;
  __i = 0;
  goto _L_FOR0_cond;
_L_FOR0_cond:;
  if (__i < arr.len) goto _L_FOR0_body; else goto _L_exit;
_L_FOR0_body:;
  __smart_ptr_drop(arr.ptr[__i]);
  goto _L_FOR0_update;
_L_FOR0_update:;
  __i = (__i + 1);
  goto _L_FOR0_cond;
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

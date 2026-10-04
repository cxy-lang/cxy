typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct Slice_i32 Slice_i32;
typedef struct Buffer Buffer;
typedef struct Optional_i32 Optional_i32;
typedef struct Slice_Resource Slice_Resource;

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

struct Slice_i32 {
  int32_t * ptr;
  uint64_t len;
};

struct Buffer {
  int32_t * _data;
  uint64_t _len;
};

struct Optional_i32 {
  bool ok;
  int32_t val;
};

struct Slice_Resource {
  Resource * * ptr;
  uint64_t len;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static void testArraySlice();
static void testReslice();
static void Buffer_op__init(Buffer * this, int32_t * data, uint64_t len);
static Slice_i32 Buffer_op__slice(Buffer * this, int32_t st, Optional_i32 e);
static Buffer Buffer_op__initCover(int32_t * data, uint64_t len);
static void testCustomSliceOverload();
static Resource * Resource_op__initCover(int32_t id);
static void testManagedArraySlice();
static void _main();
static void __array_dctor_148(Slice_Resource arr);


static void builtins_panicOnUnhandledException(builtins_Exception * ex) {
  char * __t1;
_L_entry:;
  if (ex != nullptr) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __t1 = ex->vtable->what(ex);
  __cxy_dump_unhandled_exception(ex->name, __t1, ex->stackTrace);
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
  Resource_op__destructor(((Resource *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static void testArraySlice() {
  int32_t arr[5];
  Slice_i32 s1;
  Slice_i32 s2;
  Slice_i32 s3;
  Slice_i32 s4;
_L_entry:;
  arr[0] = 10;
  arr[1] = 20;
  arr[2] = 30;
  arr[3] = 40;
  arr[4] = 50;
  s1 = (Slice_i32){&arr[1], 3};
  s2 = (Slice_i32){&arr[2], 3};
  s3 = (Slice_i32){&arr[0], 3};
  s4 = (Slice_i32){&arr[0], 5};
  goto _L_exit;
_L_exit:;
  return;
}

static void testReslice() {
  int32_t arr[6];
  Slice_i32 s;
  Slice_i32 sub;
_L_entry:;
  arr[0] = 1;
  arr[1] = 2;
  arr[2] = 3;
  arr[3] = 4;
  arr[4] = 5;
  arr[5] = 6;
  s = (Slice_i32){&arr[1], 4};
  sub = (Slice_i32){(s.ptr + 1), 2};
  goto _L_exit;
_L_exit:;
  return;
}

static void Buffer_op__init(Buffer * this, int32_t * data, uint64_t len) {
_L_entry:;
  this->_data = data;
  this->_len = len;
  goto _L_exit;
_L_exit:;
  return;
}

static Slice_i32 Buffer_op__slice(Buffer * this, int32_t st, Optional_i32 e) {
  Slice_i32 __ret;
  int32_t end;
  int32_t __t0;
_L_entry:;
  if (e.ok) goto _L_COALESCE0_then; else goto _L_COALESCE0_else;
_L_COALESCE0_then:;
  __t0 = e.val;
  goto _L_COALESCE0_merge;
_L_COALESCE0_else:;
  __t0 = ((int32_t)(this->_len));
  goto _L_COALESCE0_merge;
_L_COALESCE0_merge:;
  end = __t0;
  __ret = (Slice_i32){(this->_data + st), ((uint64_t)((end - st)))};
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Buffer Buffer_op__initCover(int32_t * data, uint64_t len) {
  Buffer __ret;
  Buffer __obj3;
_L_entry:;
  __obj3 = (Buffer){};
  Buffer_op__init(&__obj3, data, len);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCustomSliceOverload() {
  int32_t raw[5];
  Buffer buf;
  Slice_i32 s1;
  Optional_i32 __t1;
  Slice_i32 s2;
  Optional_i32 __t2;
_L_entry:;
  raw[0] = 100;
  raw[1] = 200;
  raw[2] = 300;
  raw[3] = 400;
  raw[4] = 500;
  buf = Buffer_op__initCover(&raw[0], ((uint64_t)(5)));
  __t1.ok = true;
  __t1.val = 4;
  s1 = Buffer_op__slice(&buf, ((int32_t)(1)), __t1);
  __t2.ok = false;
  s2 = Buffer_op__slice(&buf, ((int32_t)(2)), __t2);
  goto _L_exit;
_L_exit:;
  return;
}

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj4;
  Resource * __t3;
_L_entry:;
  __t3 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj4 = ((Resource *)(__t3));
  memset(__obj4, 0, sizeof(Resource));
  Resource_op__init(__obj4, id);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testManagedArraySlice() {
  Resource * arr[4];
  Slice_Resource s;
_L_entry:;
  arr[0] = Resource_op__initCover(((int32_t)(1)));
  arr[1] = Resource_op__initCover(((int32_t)(2)));
  arr[2] = Resource_op__initCover(((int32_t)(3)));
  arr[3] = Resource_op__initCover(((int32_t)(4)));
  s = (Slice_Resource){&arr[1], 2};
  goto _L_exit;
_L_exit:;
    __array_dctor_148((Slice_Resource){ .ptr = arr, .len = 4 });
  return;
}

static void _main() {
_L_entry:;
  testArraySlice();
  testReslice();
  testCustomSliceOverload();
  testManagedArraySlice();
  goto _L_exit;
_L_exit:;
  return;
}

static void __array_dctor_148(Slice_Resource arr) {
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

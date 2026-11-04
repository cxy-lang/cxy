typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct Optional_i64 Optional_i64;
typedef struct Tuple_ptr_char_Optional_i64 Tuple_ptr_char_Optional_i64;
typedef struct Tuple_Optional_i64_ptr_char Tuple_Optional_i64_ptr_char;
typedef struct Tuple_ptr_char_Tuple_Optional_i64_ptr_char Tuple_ptr_char_Tuple_Optional_i64_ptr_char;
typedef struct Optional_Resource Optional_Resource;
typedef struct Tuple_Optional_Resource_i32 Tuple_Optional_Resource_i32;

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

struct Optional_i64 {
  bool ok;
  int64_t val;
};

struct Tuple_ptr_char_Optional_i64 {
  char * _0;
  Optional_i64 _1;
};

struct Tuple_Optional_i64_ptr_char {
  Optional_i64 _0;
  char * _1;
};

struct Tuple_ptr_char_Tuple_Optional_i64_ptr_char {
  char * _0;
  Tuple_Optional_i64_ptr_char _1;
};

struct Optional_Resource {
  bool ok;
  Resource * val;
};

struct Tuple_Optional_Resource_i32 {
  Optional_Resource _0;
  int32_t _1;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static Resource * Resource_op__initCover(int32_t id);
static void _main();
static void __tuple_dctor_148(Tuple_Optional_Resource_i32 * tup);

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

static void _main() {
  Tuple_ptr_char_Optional_i64 a;
  Tuple_ptr_char_Optional_i64 b;
  Tuple_ptr_char_Tuple_Optional_i64_ptr_char c;
  Tuple_Optional_Resource_i32 d;
  Tuple_Optional_Resource_i32 e;
  Resource * __t1;
  Optional_Resource __t2;
_L_entry:;
  a = (Tuple_ptr_char_Optional_i64){"", (Optional_i64){ .ok = false }};
  b = (Tuple_ptr_char_Optional_i64){"", (Optional_i64){ .ok = true, .val = 5LL }};
  c = (Tuple_ptr_char_Tuple_Optional_i64_ptr_char){"", (Tuple_Optional_i64_ptr_char){(Optional_i64){ .ok = false }, "x"}};
  d = (Tuple_Optional_Resource_i32){(Optional_Resource){ .ok = false }, 1};
  __t1 = Resource_op__initCover(((int32_t)(1)));
  __t2.ok = true;
  __t2.val = __t1;
  e = (Tuple_Optional_Resource_i32){__t2, 2};
  goto _L_exit;
_L_exit:;
  __tuple_dctor_148(&(e));
  __tuple_dctor_148(&(d));
  return;
}

static void __tuple_dctor_148(Tuple_Optional_Resource_i32 * tup) {
_L_entry:;
    if (tup->_0.ok) __smart_ptr_drop(tup->_0.val);
  memset(&(tup->_0), 0, sizeof(Optional_Resource));
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

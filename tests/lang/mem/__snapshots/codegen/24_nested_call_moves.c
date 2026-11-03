typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Keys Keys;
typedef struct Page Page;
typedef struct Props Props;
typedef struct Outer Outer;
typedef struct Optional_Keys Optional_Keys;
typedef struct Inner Inner;
typedef struct Wrapper Wrapper;
typedef struct Tuple_Page_i8 Tuple_Page_i8;
typedef struct __drop_flags_testConditional_t __drop_flags_testConditional_t;
typedef struct Slice_Page Slice_Page;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Keys {
  int32_t id;
};

struct Page {
  Keys * keys;
};

struct Props {
  Page * page;
  int32_t tag;
};

struct Outer {
  Props props;
};

struct Optional_Keys {
  bool ok;
  Keys * val;
};

struct Inner {
  Optional_Keys keys;
};

struct Wrapper {
  Inner inner;
};

struct Tuple_Page_i8 {
  Page * _0;
  int8_t _1;
};

struct __drop_flags_testConditional_t {
  uint64_t keys: 1;
  uint64_t props: 1;
};

struct Slice_Page {
  Page * * ptr;
  uint64_t len;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Keys_op__init(Keys * this, int32_t id);
static void Keys_op__deinit(Keys * this);
static void Keys_op__destructor(Keys * this);
static void Keys_op__destructor_fwd(void * ptr);
static void Page_op__init(Page * this, Keys * k);
static void Page_op__deinit(Page * this);
static void Page_op__destructor(Page * this);
static void Page_op__destructor_fwd(void * ptr);
static void Props_op__destructor(Props * this);
static Keys * Keys_op__initCover(int32_t id);
static Page * Page_op__initCover(Keys * k);
static void testStructField();
static void Outer_op__destructor(Outer * this);
static void testNestedLiteral();
static void Inner_op__destructor(Inner * this);
static void Wrapper_op__destructor(Wrapper * this);
static void testOptionalField();
static void testTupleElement();
static void testArrayElement();
static void testConditional(bool cond);
static void _main();
static void __tuple_dctor_163(Tuple_Page_i8 * tup);
static void __array_dctor_169(Slice_Page arr);


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

static void Keys_op__init(Keys * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void Keys_op__deinit(Keys * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Keys_op__destructor(Keys * this) {
_L_entry:;
  Keys_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void Keys_op__destructor_fwd(void * ptr) {
_L_entry:;
  Keys_op__destructor(((Keys *)(((Keys *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void Page_op__init(Page * this, Keys * k) {
_L_entry:;
  __smart_ptr_drop(this->keys);
  this->keys = k;
  goto _L_exit;
_L_exit:;
  return;
}

static void Page_op__deinit(Page * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Page_op__destructor(Page * this) {
_L_entry:;
  Page_op__deinit(this);
  __smart_ptr_drop(this->keys);
  this->keys = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void Page_op__destructor_fwd(void * ptr) {
_L_entry:;
  Page_op__destructor(((Page *)(((Page *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void Props_op__destructor(Props * this) {
_L_entry:;
  __smart_ptr_drop(this->page);
  this->page = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static Keys * Keys_op__initCover(int32_t id) {
  Keys * __ret;
  Keys * __obj3;
  Keys * __t1;
_L_entry:;
  __t1 = __smart_ptr_alloc(sizeof(Keys), Keys_op__destructor_fwd);
  __obj3 = ((Keys *)(__t1));
  memset(__obj3, 0, sizeof(Keys));
  Keys_op__init(((Keys *)(__obj3)), id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Page * Page_op__initCover(Keys * k) {
  Page * __ret;
  Page * __obj4;
  Page * __t2;
_L_entry:;
  __t2 = __smart_ptr_alloc(sizeof(Page), Page_op__destructor_fwd);
  __obj4 = ((Page *)(__t2));
  memset(__obj4, 0, sizeof(Page));
  Page_op__init(((Page *)(__obj4)), k);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testStructField() {
  Keys * keys;
  Props props;
  Page * __t3;
_L_entry:;
  keys = Keys_op__initCover(((int32_t)(1)));
  __t3 = Page_op__initCover(keys);
  props = (Props){.page = __t3, .tag = 1};
  goto _L_exit;
_L_exit:;
  Props_op__destructor(&(props));
  return;
}

static void Outer_op__destructor(Outer * this) {
_L_entry:;
  Props_op__destructor(&(this->props));
  memset(&(this->props), 0, sizeof(Props));
  goto _L_exit;
_L_exit:;
  return;
}

static void testNestedLiteral() {
  Keys * keys;
  Outer outer;
  Page * __t5;
  Props __t6;
_L_entry:;
  keys = Keys_op__initCover(((int32_t)(5)));
  __t5 = Page_op__initCover(keys);
  __t6 = (Props){.page = __t5, .tag = 5};
  outer = (Outer){.props = __t6};
  goto _L_exit;
_L_exit:;
  Outer_op__destructor(&(outer));
  return;
}

static void Inner_op__destructor(Inner * this) {
_L_entry:;
    if (this->keys.ok) __smart_ptr_drop(this->keys.val);
  memset(&(this->keys), 0, sizeof(Optional_Keys));
  goto _L_exit;
_L_exit:;
  return;
}

static void Wrapper_op__destructor(Wrapper * this) {
_L_entry:;
  Inner_op__destructor(&(this->inner));
  memset(&(this->inner), 0, sizeof(Inner));
  goto _L_exit;
_L_exit:;
  return;
}

static void testOptionalField() {
  Keys * keys;
  Wrapper w;
  Optional_Keys __t8;
  Inner __t9;
_L_entry:;
  keys = Keys_op__initCover(((int32_t)(6)));
  __t8.ok = true;
  __t8.val = keys;
  __t9 = (Inner){.keys = __t8};
  w = (Wrapper){.inner = __t9};
  goto _L_exit;
_L_exit:;
  Wrapper_op__destructor(&(w));
  return;
}

static void testTupleElement() {
  Keys * keys;
  Tuple_Page_i8 tup;
  Page * __t10;
_L_entry:;
  keys = Keys_op__initCover(((int32_t)(2)));
  __t10 = Page_op__initCover(keys);
  tup = (Tuple_Page_i8){__t10, 2};
  goto _L_exit;
_L_exit:;
  __tuple_dctor_163(&(tup));
  return;
}

static void testArrayElement() {
  Keys * keys;
  Page * arr[1];
  Page * __t11;
_L_entry:;
  keys = Keys_op__initCover(((int32_t)(3)));
  __t11 = Page_op__initCover(keys);
  arr[0] = __t11;
  goto _L_exit;
_L_exit:;
  __array_dctor_169((Slice_Page){ .ptr = arr, .len = 1 });
  return;
}

static void testConditional(bool cond) {
  __drop_flags_testConditional_t __drop_flags;
  Keys * keys;
  Props props;
  Page * __t12;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testConditional_t));
  keys = Keys_op__initCover(((int32_t)(4)));
  __drop_flags.keys = 1ULL;
  if (cond) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __drop_flags.keys = 0ULL;
  __t12 = Page_op__initCover(keys);
  props = (Props){.page = __t12, .tag = 4};
  __drop_flags.props = 1ULL;
  Props_op__destructor(&(props));
  __drop_flags.props = 0ULL;
  goto _L_exit;
_L_exit:;
  if (__drop_flags.keys) __smart_ptr_drop(keys);
  __drop_flags.keys = 0ULL;
  return;
}

static void _main() {
_L_entry:;
  testStructField();
  testNestedLiteral();
  testOptionalField();
  testTupleElement();
  testArrayElement();
  testConditional(true);
  testConditional(false);
  goto _L_exit;
_L_exit:;
  return;
}

static void __tuple_dctor_163(Tuple_Page_i8 * tup) {
_L_entry:;
  __smart_ptr_drop(tup->_0);
  tup->_0 = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void __array_dctor_169(Slice_Page arr) {
  uint64_t __i;
_L_entry:;
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
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

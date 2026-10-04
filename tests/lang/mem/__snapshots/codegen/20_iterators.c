typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Optional_i32 Optional_i32;
typedef struct RangeIterator RangeIterator;
typedef struct CustomRange CustomRange;
typedef struct Tuple_char_i32 Tuple_char_i32;
typedef struct Optional_Tuple_char_i32 Optional_Tuple_char_i32;
typedef struct __Iterator3 __Iterator3;
typedef struct InlineCollection InlineCollection;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Optional_i32 {
  bool ok;
  int32_t val;
};

struct RangeIterator {
  int32_t limit;
  int32_t cur;
};

struct CustomRange {
  int32_t limit;
};

struct Tuple_char_i32 {
  char _0;
  int32_t _1;
};

struct Optional_Tuple_char_i32 {
  bool ok;
  Tuple_char_i32 val;
};

struct __Iterator3 {
  char * s;
  int32_t i;
};

struct InlineCollection {
  char * s;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static Optional_i32 RangeIterator_op__call(RangeIterator * this);
static bool RangeIterator_op__truthy(RangeIterator * this);
static RangeIterator CustomRange_op__range(CustomRange * this);
static Optional_Tuple_char_i32 __Iterator3_op__call(__Iterator3 * this);
static bool __Iterator3_op__truthy(__Iterator3 * this);
static __Iterator3 InlineCollection_op__range(InlineCollection * this);
static void testCustomNamedIterator();
static void testInlineIteratorWithDestructuring();
static void _main();


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

static Optional_i32 RangeIterator_op__call(RangeIterator * this) {
  Optional_i32 __ret;
_L_entry:;
  if (this->cur < this->limit) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __ret.ok = true;
  __ret.val = (++(this->cur));
  goto _L_exit;
_L_IF0_end:;
  __ret.ok = false;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static bool RangeIterator_op__truthy(RangeIterator * this) {
  bool __ret;
_L_entry:;
  __ret = (this->cur < this->limit);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static RangeIterator CustomRange_op__range(CustomRange * this) {
  RangeIterator __ret;
_L_entry:;
  __ret = (RangeIterator){this->limit, ((int32_t)(0))};
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Optional_Tuple_char_i32 __Iterator3_op__call(__Iterator3 * this) {
  Optional_Tuple_char_i32 __ret;
  uint64_t __t0;
  Tuple_char_i32 __t1;
_L_entry:;
  __t0 = strlen(this->s);
  if (this->i < __t0) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t1 = (Tuple_char_i32){this->s[this->i], (++(this->i))};
  __ret.ok = true;
  __ret.val = __t1;
  goto _L_exit;
_L_IF0_end:;
  __ret.ok = false;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static bool __Iterator3_op__truthy(__Iterator3 * this) {
  bool __ret;
  uint64_t __t2;
_L_entry:;
  __t2 = strlen(this->s);
  __ret = (this->i < __t2);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static __Iterator3 InlineCollection_op__range(InlineCollection * this) {
  __Iterator3 __ret;
_L_entry:;
  __ret = (__Iterator3){this->s, 0};
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCustomNamedIterator() {
  CustomRange r;
  RangeIterator __it4;
  bool __t3;
  Optional_i32 __item5;
  int32_t val;
_L_entry:;
  r = (CustomRange){5};
  __it4 = CustomRange_op__range(&r);
  goto _L_WHILE0_cond;
_L_WHILE0_cond:;
  __t3 = RangeIterator_op__truthy(&__it4);
  if (__t3) goto _L_WHILE0_body; else goto _L_exit;
_L_WHILE0_body:;
  __item5 = RangeIterator_op__call(&__it4);
  val = __item5.val;
  goto _L_WHILE0_cond;
_L_exit:;
  return;
}

static void testInlineIteratorWithDestructuring() {
  InlineCollection c;
  __Iterator3 __it6;
  bool __t4;
  Optional_Tuple_char_i32 __item7;
_L_entry:;
  c = (InlineCollection){"hello"};
  __it6 = InlineCollection_op__range(&c);
  goto _L_WHILE0_cond;
_L_WHILE0_cond:;
  __t4 = __Iterator3_op__truthy(&__it6);
  if (__t4) goto _L_WHILE0_body; else goto _L_exit;
_L_WHILE0_body:;
  __item7 = __Iterator3_op__call(&__it6);
  goto _L_WHILE0_cond;
_L_exit:;
  return;
}

static void _main() {
_L_entry:;
  testCustomNamedIterator();
  testInlineIteratorWithDestructuring();
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

typedef struct Optional_i32 Optional_i32;
typedef struct RangeIterator RangeIterator;
typedef struct CustomRange CustomRange;
typedef struct Tuple_char_i32 Tuple_char_i32;
typedef struct Optional_Tuple_char_i32 Optional_Tuple_char_i32;
typedef struct __Iterator0 __Iterator0;
typedef struct InlineCollection InlineCollection;

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

struct __Iterator0 {
  char * s;
  int32_t i;
};

struct InlineCollection {
  char * s;
};

static Optional_i32 RangeIterator_op__call(RangeIterator * this);
static bool RangeIterator_op__truthy(RangeIterator * this);
static RangeIterator CustomRange_op__range(CustomRange * this);
static Optional_Tuple_char_i32 __Iterator0_op__call(__Iterator0 * this);
static bool __Iterator0_op__truthy(__Iterator0 * this);
static __Iterator0 InlineCollection_op__range(InlineCollection * this);
static void testCustomNamedIterator();
static void testInlineIteratorWithDestructuring();
static void _main();

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

static Optional_Tuple_char_i32 __Iterator0_op__call(__Iterator0 * this) {
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

static bool __Iterator0_op__truthy(__Iterator0 * this) {
  bool __ret;
  uint64_t __t2;
_L_entry:;
  __t2 = strlen(this->s);
  __ret = (this->i < __t2);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static __Iterator0 InlineCollection_op__range(InlineCollection * this) {
  __Iterator0 __ret;
_L_entry:;
  __ret = (__Iterator0){this->s, 0};
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCustomNamedIterator() {
  CustomRange r;
  RangeIterator __it1;
  bool __t3;
  Optional_i32 __item2;
  int32_t val;
_L_entry:;
  r = (CustomRange){5};
  __it1 = CustomRange_op__range(&r);
  goto _L_WHILE0_cond;
_L_WHILE0_cond:;
  __t3 = RangeIterator_op__truthy(&__it1);
  if (__t3) goto _L_WHILE0_body; else goto _L_exit;
_L_WHILE0_body:;
  __item2 = RangeIterator_op__call(&__it1);
  val = __item2.val;
  goto _L_WHILE0_cond;
_L_exit:;
  return;
}

static void testInlineIteratorWithDestructuring() {
  InlineCollection c;
  __Iterator0 __it3;
  bool __t4;
  Optional_Tuple_char_i32 __item4;
_L_entry:;
  c = (InlineCollection){"hello"};
  __it3 = InlineCollection_op__range(&c);
  goto _L_WHILE0_cond;
_L_WHILE0_cond:;
  __t4 = __Iterator0_op__truthy(&__it3);
  if (__t4) goto _L_WHILE0_body; else goto _L_exit;
_L_WHILE0_body:;
  __item4 = __Iterator0_op__call(&__it3);
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

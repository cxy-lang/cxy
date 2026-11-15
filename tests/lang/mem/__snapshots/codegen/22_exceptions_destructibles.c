typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_Exception builtins_Exception;
typedef struct String String;
typedef struct Container Container;
typedef struct CustomError CustomError;
typedef union Result_String_val Result_String_val;
typedef struct Result_String Result_String;
typedef struct __drop_flags_testBubbleUpDestructible_t __drop_flags_testBubbleUpDestructible_t;
typedef struct __drop_flags_testReraiseDestructible_t __drop_flags_testReraiseDestructible_t;
typedef struct __drop_flags_testRaiseCaught_t __drop_flags_testRaiseCaught_t;
typedef union Result_i32_val Result_i32_val;
typedef struct Result_i32 Result_i32;

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct String {
  char * s;
};

struct Container {
  String * name;
  int32_t tag;
};

struct CustomError {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
  String * reason;
};

union Result_String_val {
  String * _0;
  builtins_Exception * _1;
};

struct Result_String {
  uint32_t tag;
  Result_String_val val;
};

struct __drop_flags_testBubbleUpDestructible_t {
  uint64_t s: 1;
};

struct __drop_flags_testReraiseDestructible_t {
  uint64_t res: 1;
};

struct __drop_flags_testRaiseCaught_t {
  uint64_t res: 1;
};

union Result_i32_val {
  int32_t _0;
  builtins_Exception * _1;
};

struct Result_i32 {
  uint32_t tag;
  Result_i32_val val;
};

static void builtins_Exception_op__init(builtins_Exception * this, char * name);
static char * builtins_Exception_what(builtins_Exception * this);
static void builtins_Exception_push(builtins_Exception * this, char * mod, char * cls, char * fun, char * path, uint64_t line);
static void builtins_Exception_op__deinit(builtins_Exception * this);
static void builtins_Exception_op__destructor(builtins_Exception * this);
static void builtins_Exception_op__destructor_fwd(void * ptr);
static void builtins_Exception___defaults_init(builtins_Exception * this);
static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void String_op__init(String * this, char * s);
static char * String_str(String * this);
static void Container_op__init(Container * this, int32_t tag, String * name);
static void Container_op__deinit(Container * this);
static void Container_op__destructor(Container * this);
static void Container_op__destructor_fwd(void * ptr);
static void CustomError_op__init(CustomError * this, String * reason);
static char * CustomError_what(CustomError * this);
static void CustomError_op__destructor(CustomError * this);
static void CustomError_op__destructor_fwd(void * ptr);
static void CustomError___defaults_init(CustomError * this);
static String * String_op__initCover(char * s);
static Container * Container_op__initCover(int32_t tag, String * name);
static CustomError * CustomError_op__initCover(String * reason);
static Result_String produceString(int32_t x);
static void testDirectSuccess();
static void testDirectFailure();
static void testYieldDestructible();
static Result_String testBubbleUpDestructible(int32_t x);
static void testHandleBubbleUp();
static Result_String testReraiseDestructible(int32_t x);
static Result_String testRaiseCaught(int32_t x);
static int32_t peekString(String * s);
static Result_i32 testBorrowedPayload(int32_t x);
static void _main();

static const __TypeInfo __ti_String = { .name = "String", .size = sizeof(String), .align = _Alignof(String), .index = 0, .dropFields = nullptr };
static const __TypeInfo __ti_Container = { .name = "Container", .size = sizeof(Container), .align = _Alignof(Container), .index = 1, .dropFields = Container_op__destructor_fwd };
static const __TypeInfo __ti_CustomError = { .name = "CustomError", .size = sizeof(CustomError), .align = _Alignof(CustomError), .index = 2, .dropFields = CustomError_op__destructor_fwd };

builtins_Exception_VTable builtins_Exception_vTable = (builtins_Exception_VTable){.__tid = 95ULL, .what = ((char * (*)(builtins_Exception *))(builtins_Exception_what))};

static void builtins_Exception_op__init(builtins_Exception * this, char * name) {
_L_entry:;
  this->name = name;
  this->stackTrace = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static char * builtins_Exception_what(builtins_Exception * this) {
  char * __ret;
_L_entry:;
  __ret = this->name;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void builtins_Exception_push(builtins_Exception * this, char * mod, char * cls, char * fun, char * path, uint64_t line) {
_L_entry:;
  if (this->stackTrace == nullptr) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  this->stackTrace = __cxy_stack_trace_alloc();
  goto _L_IF0_end;
_L_IF0_end:;
  __cxy_stack_trace_push(this->stackTrace, mod, cls, fun, path, line);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_Exception_op__deinit(builtins_Exception * this) {
_L_entry:;
  if (this->stackTrace != nullptr) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __cxy_stack_trace_destroy(this->stackTrace);
  this->stackTrace = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_Exception_op__destructor(builtins_Exception * this) {
_L_entry:;
  builtins_Exception_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_Exception_op__destructor_fwd(void * ptr) {
_L_entry:;
  builtins_Exception_op__destructor(((builtins_Exception *)(((builtins_Exception *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_Exception___defaults_init(builtins_Exception * this) {
_L_entry:;
  this->stackTrace = nullptr;
  this->vtable = &builtins_Exception_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

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

builtins_Exception_VTable CustomError_vTable = (builtins_Exception_VTable){.__tid = ((uint64_t)(148)), .what = ((char * (*)(builtins_Exception *))(CustomError_what))};

static void String_op__init(String * this, char * s) {
_L_entry:;
  this->s = s;
  goto _L_exit;
_L_exit:;
  return;
}

static char * String_str(String * this) {
  char * __ret;
_L_entry:;
  __ret = this->s;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void Container_op__init(Container * this, int32_t tag, String * name) {
_L_entry:;
  this->tag = tag;
  __smart_ptr_drop(this->name);
  this->name = __smart_ptr_get(name);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(name);
  return;
}

static void Container_op__deinit(Container * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Container_op__destructor(Container * this) {
_L_entry:;
  Container_op__deinit(this);
  __smart_ptr_drop(this->name);
  this->name = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void Container_op__destructor_fwd(void * ptr) {
_L_entry:;
  Container_op__destructor(((Container *)(((Container *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void CustomError_op__init(CustomError * this, String * reason) {
_L_entry:;
  builtins_Exception_op__init(this, "CustomError");
  __smart_ptr_drop(this->reason);
  this->reason = __smart_ptr_get(reason);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(reason);
  return;
}

static char * CustomError_what(CustomError * this) {
  char * __ret;
_L_entry:;
  __ret = String_str(((String *)(this->reason)));
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void CustomError_op__destructor(CustomError * this) {
_L_entry:;
  builtins_Exception_op__destructor(((builtins_Exception *)(this)));
  __smart_ptr_drop(this->reason);
  this->reason = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void CustomError_op__destructor_fwd(void * ptr) {
_L_entry:;
  CustomError_op__destructor(((CustomError *)(((CustomError *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void CustomError___defaults_init(CustomError * this) {
_L_entry:;
  builtins_Exception___defaults_init(this);
  this->vtable = &CustomError_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static String * String_op__initCover(char * s) {
  String * __ret;
  String * __obj3;
  String * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_String, nullptr);
  __obj3 = ((String *)(__t0));
  memset(__obj3, 0, sizeof(String));
  String_op__init(((String *)(__obj3)), s);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Container * Container_op__initCover(int32_t tag, String * name) {
  Container * __ret;
  Container * __obj4;
  Container * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_Container, nullptr);
  __obj4 = ((Container *)(__t1));
  memset(__obj4, 0, sizeof(Container));
  Container_op__init(((Container *)(__obj4)), tag, name);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static CustomError * CustomError_op__initCover(String * reason) {
  CustomError * __ret;
  CustomError * __obj5;
  CustomError * __t2;
_L_entry:;
  __t2 = __mm_alloc(&__ti_CustomError, nullptr);
  __obj5 = ((CustomError *)(__t2));
  memset(__obj5, 0, sizeof(CustomError));
  CustomError___defaults_init(((CustomError *)(__obj5)));
  CustomError_op__init(((CustomError *)(__obj5)), reason);
  __ret = __obj5;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_String produceString(int32_t x) {
  Result_String __ret;
  Container * c;
  String * __t3;
  String * __t4;
  CustomError * __t5;
  String * __t6;
_L_entry:;
  __t3 = String_op__initCover("local_temp");
  c = Container_op__initCover(x, __t3);
  if (x < 0) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t4 = String_op__initCover("negative input error");
  __t5 = CustomError_op__initCover(__t4);
  builtins_Exception_push(__t5, "main", "", "produceString", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 24ULL);
  __ret.tag = 1U;
  __ret.val._1 = __t5;
  goto _L_exit;
_L_IF0_end:;
  __ret.tag = 0U;
  __t6 = String_op__initCover("success_string");
  __ret.val._0 = __t6;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(c);
  return __ret;
}

static void testDirectSuccess() {
  String * s;
  Result_String __t7;
  String * __catch_res8;
  String * __t9;
  String * __mv0;
_L_entry:;
  __t7 = produceString(((int32_t)(10)));
  __catch_res8 = nullptr;
  if (__t7.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t9 = String_op__initCover("fallback");
  __catch_res8 = __t9;
  __smart_ptr_drop(__t7.val._1);
  __t7.val._1 = nullptr;
  goto _L_merge_1;
_L_cont_1:;
  __mv0 = __t7.val._0;
  __t7.val._0 = nullptr;
  __catch_res8 = __mv0;
  goto _L_merge_1;
_L_merge_1:;
  s = __catch_res8;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(s);
  return;
}

static void testDirectFailure() {
  String * s;
  Result_String __t10;
  String * __catch_res11;
  String * __t12;
  String * __mv1;
_L_entry:;
  __t10 = produceString(((int32_t)((-1))));
  __catch_res11 = nullptr;
  if (__t10.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t12 = String_op__initCover("fallback");
  __catch_res11 = __t12;
  __smart_ptr_drop(__t10.val._1);
  __t10.val._1 = nullptr;
  goto _L_merge_1;
_L_cont_1:;
  __mv1 = __t10.val._0;
  __t10.val._0 = nullptr;
  __catch_res11 = __mv1;
  goto _L_merge_1;
_L_merge_1:;
  s = __catch_res11;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(s);
  return;
}

static void testYieldDestructible() {
  String * s;
  Result_String __t13;
  String * __catch_res14;
  String * __t15;
  String * __mv2;
_L_entry:;
  __t13 = produceString(((int32_t)((-5))));
  __catch_res14 = nullptr;
  if (__t13.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t15 = String_op__initCover("yielded_fallback");
  __catch_res14 = __t15;
  __smart_ptr_drop(__t13.val._1);
  __t13.val._1 = nullptr;
  goto _L_merge_1;
_L_cont_1:;
  __mv2 = __t13.val._0;
  __t13.val._0 = nullptr;
  __catch_res14 = __mv2;
  goto _L_merge_1;
_L_merge_1:;
  s = __catch_res14;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(s);
  return;
}

static Result_String testBubbleUpDestructible(int32_t x) {
  __drop_flags_testBubbleUpDestructible_t __drop_flags;
  Result_String __ret;
  Container * c;
  String * __t16;
  String * s;
  Result_String __call_res_17;
  builtins_Exception * __mv3;
  String * __mv4;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testBubbleUpDestructible_t));
  __t16 = String_op__initCover("enclosing_container");
  c = Container_op__initCover(x, __t16);
  __call_res_17 = produceString(x);
  if (__call_res_17.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_17.val._1, "main", "", "testBubbleUpDestructible", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 45ULL);
  __mv3 = __call_res_17.val._1;
  __call_res_17.val._1 = nullptr;
  __ret.val._1 = __mv3;
  goto _L_exit;
_L_call_cont_1:;
  __mv4 = __call_res_17.val._0;
  __call_res_17.val._0 = nullptr;
  s = __mv4;
  __drop_flags.s = 1ULL;
  __ret.tag = 0U;
  __ret.val._0 = __smart_ptr_get(s);
  goto _L_exit;
_L_exit:;
  if (__drop_flags.s) __smart_ptr_drop(s);
  __drop_flags.s = 0ULL;
  __smart_ptr_drop(c);
  return __ret;
}

static void testHandleBubbleUp() {
  String * res1;
  Result_String __t18;
  String * __catch_res19;
  String * __t20;
  String * __mv5;
  String * res2;
  Result_String __t21;
  String * __catch_res22;
  String * __t23;
  String * __mv6;
_L_entry:;
  __t18 = testBubbleUpDestructible(((int32_t)(20)));
  __catch_res19 = nullptr;
  if (__t18.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t20 = String_op__initCover("error_caught");
  __catch_res19 = __t20;
  __smart_ptr_drop(__t18.val._1);
  __t18.val._1 = nullptr;
  goto _L_merge_1;
_L_cont_1:;
  __mv5 = __t18.val._0;
  __t18.val._0 = nullptr;
  __catch_res19 = __mv5;
  goto _L_merge_1;
_L_merge_1:;
  res1 = __catch_res19;
  __t21 = testBubbleUpDestructible(((int32_t)((-20))));
  __catch_res22 = nullptr;
  if (__t21.tag == 1U) goto _L_catch_1; else goto _L_cont_2;
_L_catch_1:;
  __t23 = String_op__initCover("error_caught");
  __catch_res22 = __t23;
  __smart_ptr_drop(__t21.val._1);
  __t21.val._1 = nullptr;
  goto _L_merge_2;
_L_cont_2:;
  __mv6 = __t21.val._0;
  __t21.val._0 = nullptr;
  __catch_res22 = __mv6;
  goto _L_merge_2;
_L_merge_2:;
  res2 = __catch_res22;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(res2);
  __smart_ptr_drop(res1);
  return;
}

static Result_String testReraiseDestructible(int32_t x) {
  __drop_flags_testReraiseDestructible_t __drop_flags;
  Result_String __ret;
  String * local;
  String * res;
  Result_String __t24;
  String * __catch_res25;
  builtins_Exception * __mv7;
  String * __mv8;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testReraiseDestructible_t));
  local = String_op__initCover("before_catch");
  __t24 = produceString(x);
  __catch_res25 = nullptr;
  if (__t24.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__t24.val._1, "main", "", "testReraiseDestructible", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 57ULL);
  __mv7 = __t24.val._1;
  __t24.val._1 = nullptr;
  __ret.val._1 = __mv7;
  goto _L_exit;
_L_cont_1:;
  __mv8 = __t24.val._0;
  __t24.val._0 = nullptr;
  __catch_res25 = __mv8;
  goto _L_merge_1;
_L_merge_1:;
  res = __catch_res25;
  __drop_flags.res = 1ULL;
  __ret.tag = 0U;
  __ret.val._0 = __smart_ptr_get(res);
  goto _L_exit;
_L_exit:;
  if (__drop_flags.res) __smart_ptr_drop(res);
  __drop_flags.res = 0ULL;
  __smart_ptr_drop(local);
  return __ret;
}

static Result_String testRaiseCaught(int32_t x) {
  __drop_flags_testRaiseCaught_t __drop_flags;
  Result_String __ret;
  String * res;
  Result_String __t26;
  String * __catch_res27;
  builtins_Exception * __mv9;
  String * __mv10;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testRaiseCaught_t));
  __t26 = produceString(x);
  __catch_res27 = nullptr;
  if (__t26.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  builtins_Exception_push(__t26.val._1, "main", "", "testRaiseCaught", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 64ULL);
  __ret.tag = 1U;
  __mv9 = __t26.val._1;
  __t26.val._1 = nullptr;
  __ret.val._1 = __mv9;
  goto _L_exit;
_L_cont_1:;
  __mv10 = __t26.val._0;
  __t26.val._0 = nullptr;
  __catch_res27 = __mv10;
  goto _L_merge_1;
_L_merge_1:;
  res = __catch_res27;
  __drop_flags.res = 1ULL;
  __ret.tag = 0U;
  __ret.val._0 = __smart_ptr_get(res);
  goto _L_exit;
_L_exit:;
  if (__drop_flags.res) __smart_ptr_drop(res);
  __drop_flags.res = 0ULL;
  return __ret;
}

static int32_t peekString(String * s) {
  int32_t __ret;
_L_entry:;
  __ret = 1;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_i32 testBorrowedPayload(int32_t x) {
  Result_i32 __ret;
  Result_String __call_res_28;
  builtins_Exception * __mv11;
  int32_t __t29;
  String * __p12;
_L_entry:;
  __ret.tag = 0U;
  __call_res_28 = produceString(x);
  if (__call_res_28.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_28.val._1, "main", "", "testBorrowedPayload", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 73ULL);
  __mv11 = __call_res_28.val._1;
  __call_res_28.val._1 = nullptr;
  __ret.val._1 = __mv11;
  goto _L_exit;
_L_call_cont_1:;
  __p12 = __call_res_28.val._0;
  __call_res_28.val._0 = nullptr;
  __t29 = peekString(((String *)(__p12)));
  __smart_ptr_drop(__p12);
  __ret.val._0 = __t29;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  String * r1;
  Result_String __t30;
  String * __catch_res31;
  String * __t32;
  String * __mv13;
  String * r2;
  Result_String __t33;
  String * __catch_res34;
  String * __t35;
  String * __mv14;
  String * r3;
  Result_String __t36;
  String * __catch_res37;
  String * __t38;
  String * __mv15;
  String * r4;
  Result_String __t39;
  String * __catch_res40;
  String * __t41;
  String * __mv16;
  int32_t r5;
  Result_i32 __t42;
  int32_t __catch_res43;
  int32_t r6;
  Result_i32 __t44;
  int32_t __catch_res45;
_L_entry:;
  testDirectSuccess();
  testDirectFailure();
  testYieldDestructible();
  testHandleBubbleUp();
  __t30 = testReraiseDestructible(((int32_t)(10)));
  __catch_res31 = nullptr;
  if (__t30.tag == 1U) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t32 = String_op__initCover("reraised_caught");
  __catch_res31 = __t32;
  __smart_ptr_drop(__t30.val._1);
  __t30.val._1 = nullptr;
  goto _L_merge_1;
_L_cont_1:;
  __mv13 = __t30.val._0;
  __t30.val._0 = nullptr;
  __catch_res31 = __mv13;
  goto _L_merge_1;
_L_merge_1:;
  r1 = __catch_res31;
  __t33 = testReraiseDestructible(((int32_t)((-10))));
  __catch_res34 = nullptr;
  if (__t33.tag == 1U) goto _L_catch_1; else goto _L_cont_2;
_L_catch_1:;
  __t35 = String_op__initCover("reraised_caught");
  __catch_res34 = __t35;
  __smart_ptr_drop(__t33.val._1);
  __t33.val._1 = nullptr;
  goto _L_merge_2;
_L_cont_2:;
  __mv14 = __t33.val._0;
  __t33.val._0 = nullptr;
  __catch_res34 = __mv14;
  goto _L_merge_2;
_L_merge_2:;
  r2 = __catch_res34;
  __t36 = testRaiseCaught(((int32_t)(10)));
  __catch_res37 = nullptr;
  if (__t36.tag == 1U) goto _L_catch_2; else goto _L_cont_3;
_L_catch_2:;
  __t38 = String_op__initCover("raised_caught");
  __catch_res37 = __t38;
  __smart_ptr_drop(__t36.val._1);
  __t36.val._1 = nullptr;
  goto _L_merge_3;
_L_cont_3:;
  __mv15 = __t36.val._0;
  __t36.val._0 = nullptr;
  __catch_res37 = __mv15;
  goto _L_merge_3;
_L_merge_3:;
  r3 = __catch_res37;
  __t39 = testRaiseCaught(((int32_t)((-10))));
  __catch_res40 = nullptr;
  if (__t39.tag == 1U) goto _L_catch_3; else goto _L_cont_4;
_L_catch_3:;
  __t41 = String_op__initCover("raised_caught");
  __catch_res40 = __t41;
  __smart_ptr_drop(__t39.val._1);
  __t39.val._1 = nullptr;
  goto _L_merge_4;
_L_cont_4:;
  __mv16 = __t39.val._0;
  __t39.val._0 = nullptr;
  __catch_res40 = __mv16;
  goto _L_merge_4;
_L_merge_4:;
  r4 = __catch_res40;
  __t42 = testBorrowedPayload(((int32_t)(10)));
  if (__t42.tag == 1U) goto _L_catch_4; else goto _L_cont_5;
_L_catch_4:;
  __catch_res43 = 0;
  __smart_ptr_drop(__t42.val._1);
  __t42.val._1 = nullptr;
  goto _L_merge_5;
_L_cont_5:;
  __catch_res43 = __t42.val._0;
  goto _L_merge_5;
_L_merge_5:;
  r5 = __catch_res43;
  __t44 = testBorrowedPayload(((int32_t)((-10))));
  if (__t44.tag == 1U) goto _L_catch_5; else goto _L_cont_6;
_L_catch_5:;
  __catch_res45 = 0;
  __smart_ptr_drop(__t44.val._1);
  __t44.val._1 = nullptr;
  goto _L_merge_6;
_L_cont_6:;
  __catch_res45 = __t44.val._0;
  goto _L_merge_6;
_L_merge_6:;
  r6 = __catch_res45;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(r4);
  __smart_ptr_drop(r3);
  __smart_ptr_drop(r2);
  __smart_ptr_drop(r1);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

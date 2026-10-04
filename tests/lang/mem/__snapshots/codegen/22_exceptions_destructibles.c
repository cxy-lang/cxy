typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_Exception builtins_Exception;
typedef struct String String;
typedef struct Container Container;
typedef struct CustomError CustomError;
typedef union Result_String_val Result_String_val;
typedef struct Result_String Result_String;
typedef struct __drop_flags_testBubbleUpDestructible_t __drop_flags_testBubbleUpDestructible_t;
typedef struct __drop_flags_testReraiseDestructible_t __drop_flags_testReraiseDestructible_t;

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
  uint64_t s;
};

struct __drop_flags_testReraiseDestructible_t {
  uint64_t res;
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
static void _main();

builtins_Exception_VTable Exception_vTable = (builtins_Exception_VTable){83, ((char * (*)(builtins_Exception *))(builtins_Exception_what))};

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
  builtins_Exception_op__destructor(((builtins_Exception *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_Exception___defaults_init(builtins_Exception * this) {
_L_entry:;
  this->stackTrace = nullptr;
  this->vtable = &Exception_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

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

builtins_Exception_VTable CustomError_vTable = (builtins_Exception_VTable){((uint64_t)(137)), ((char * (*)(builtins_Exception *))(CustomError_what))};

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
  memset(this->name, 0, sizeof(String));
  goto _L_exit;
_L_exit:;
  return;
}

static void Container_op__destructor_fwd(void * ptr) {
_L_entry:;
  Container_op__destructor(((Container *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static void CustomError_op__init(CustomError * this, String * reason) {
_L_entry:;
  builtins_Exception_op__init(this, "CustomError");
  this->reason = __smart_ptr_get(reason);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(reason);
  return;
}

static char * CustomError_what(CustomError * this) {
  char * __ret;
_L_entry:;
  __ret = String_str(this->reason);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void CustomError_op__destructor(CustomError * this) {
_L_entry:;
  builtins_Exception_op__destructor(((builtins_Exception *)(this)));
  __smart_ptr_drop(this->reason);
  memset(this->reason, 0, sizeof(String));
  goto _L_exit;
_L_exit:;
  return;
}

static void CustomError_op__destructor_fwd(void * ptr) {
_L_entry:;
  CustomError_op__destructor(((CustomError *)(ptr)));
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
  __t0 = __smart_ptr_alloc(sizeof(String), nullptr);
  __obj3 = ((String *)(__t0));
  memset(__obj3, 0, sizeof(String));
  String_op__init(__obj3, s);
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
  __t1 = __smart_ptr_alloc(sizeof(Container), Container_op__destructor_fwd);
  __obj4 = ((Container *)(__t1));
  memset(__obj4, 0, sizeof(Container));
  Container_op__init(__obj4, tag, name);
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
  __t2 = __smart_ptr_alloc(sizeof(CustomError), CustomError_op__destructor_fwd);
  __obj5 = ((CustomError *)(__t2));
  memset(__obj5, 0, sizeof(CustomError));
  CustomError___defaults_init(__obj5);
  CustomError_op__init(__obj5, reason);
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
  builtins_Exception_push(__t5, "main", "", "produceString", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 24);
  __ret.tag = 1;
  __ret.val._1 = __t5;
  goto _L_exit;
_L_IF0_end:;
  __ret.tag = 0;
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
_L_entry:;
  __t7 = produceString(((int32_t)(10)));
  if (__t7.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t9 = String_op__initCover("fallback");
  __catch_res8 = __t9;
  __smart_ptr_drop(__t7.val._1);
  __t7.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res8 = __t7.val._0;
  __t7.tag = 0;
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
_L_entry:;
  __t10 = produceString(((int32_t)((-1))));
  if (__t10.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t12 = String_op__initCover("fallback");
  __catch_res11 = __t12;
  __smart_ptr_drop(__t10.val._1);
  __t10.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res11 = __t10.val._0;
  __t10.tag = 0;
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
_L_entry:;
  __t13 = produceString(((int32_t)((-5))));
  if (__t13.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t15 = String_op__initCover("yielded_fallback");
  __catch_res14 = __t15;
  __smart_ptr_drop(__t13.val._1);
  __t13.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res14 = __t13.val._0;
  __t13.tag = 0;
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
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testBubbleUpDestructible_t));
  __t16 = String_op__initCover("enclosing_container");
  c = Container_op__initCover(x, __t16);
  __call_res_17 = produceString(x);
  if (__call_res_17.tag == 1) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1;
  builtins_Exception_push(__call_res_17.val._1, "main", "", "testBubbleUpDestructible", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 45);
  __ret.val._1 = __call_res_17.val._1;
  __call_res_17.tag = 0;
  goto _L_exit;
_L_call_cont_1:;
  s = __call_res_17.val._0;
  __drop_flags.s = 1;
  __ret.tag = 0;
  __ret.val._0 = __smart_ptr_get(s);
  goto _L_exit;
_L_exit:;
  if (__drop_flags.s) __smart_ptr_drop(s);
  __drop_flags.s = 0;
  __smart_ptr_drop(c);
  return __ret;
}

static void testHandleBubbleUp() {
  String * res1;
  Result_String __t18;
  String * __catch_res19;
  String * __t20;
  String * res2;
  Result_String __t21;
  String * __catch_res22;
  String * __t23;
_L_entry:;
  __t18 = testBubbleUpDestructible(((int32_t)(20)));
  if (__t18.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t20 = String_op__initCover("error_caught");
  __catch_res19 = __t20;
  __smart_ptr_drop(__t18.val._1);
  __t18.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res19 = __t18.val._0;
  __t18.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  res1 = __catch_res19;
  __t21 = testBubbleUpDestructible(((int32_t)((-20))));
  if (__t21.tag == 1) goto _L_catch_1; else goto _L_cont_2;
_L_catch_1:;
  __t23 = String_op__initCover("error_caught");
  __catch_res22 = __t23;
  __smart_ptr_drop(__t21.val._1);
  __t21.tag = 0;
  goto _L_merge_2;
_L_cont_2:;
  __catch_res22 = __t21.val._0;
  __t21.tag = 0;
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
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testReraiseDestructible_t));
  local = String_op__initCover("before_catch");
  __t24 = produceString(x);
  if (__t24.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __ret.tag = 1;
  builtins_Exception_push(__t24.val._1, "main", "", "testReraiseDestructible", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/22_exceptions_destructibles.cxy", 57);
  __ret.val._1 = __t24.val._1;
  __t24.tag = 0;
  goto _L_exit;
_L_cont_1:;
  __catch_res25 = __t24.val._0;
  __t24.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  res = __catch_res25;
  __drop_flags.res = 1;
  __ret.tag = 0;
  __ret.val._0 = __smart_ptr_get(res);
  goto _L_exit;
_L_exit:;
  if (__drop_flags.res) __smart_ptr_drop(res);
  __drop_flags.res = 0;
  __smart_ptr_drop(local);
  return __ret;
}

static void _main() {
  String * r1;
  Result_String __t26;
  String * __catch_res27;
  String * __t28;
  String * r2;
  Result_String __t29;
  String * __catch_res30;
  String * __t31;
_L_entry:;
  testDirectSuccess();
  testDirectFailure();
  testYieldDestructible();
  testHandleBubbleUp();
  __t26 = testReraiseDestructible(((int32_t)(10)));
  if (__t26.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __t28 = String_op__initCover("reraised_caught");
  __catch_res27 = __t28;
  __smart_ptr_drop(__t26.val._1);
  __t26.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res27 = __t26.val._0;
  __t26.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  r1 = __catch_res27;
  __t29 = testReraiseDestructible(((int32_t)((-10))));
  if (__t29.tag == 1) goto _L_catch_1; else goto _L_cont_2;
_L_catch_1:;
  __t31 = String_op__initCover("reraised_caught");
  __catch_res30 = __t31;
  __smart_ptr_drop(__t29.val._1);
  __t29.tag = 0;
  goto _L_merge_2;
_L_cont_2:;
  __catch_res30 = __t29.val._0;
  __t29.tag = 0;
  goto _L_merge_2;
_L_merge_2:;
  r2 = __catch_res30;
  goto _L_exit;
_L_exit:;
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

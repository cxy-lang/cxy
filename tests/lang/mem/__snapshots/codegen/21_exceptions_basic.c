typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_Exception builtins_Exception;
typedef struct Resource Resource;
typedef struct TestError TestError;
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

struct Resource {
  int32_t id;
};

struct TestError {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
  char * msg;
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

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static void TestError_op__init(TestError * this, char * msg);
static char * TestError_what(TestError * this);
static void TestError_op__destructor(TestError * this);
static void TestError_op__destructor_fwd(void * ptr);
static void TestError___defaults_init(TestError * this);
static TestError * TestError_op__initCover(char * msg);
static Result_i32 mayFail(int32_t x);
static int32_t testCatchDefault();
static void testCatchDiscard();
static int32_t testCatchAnonymous();
static int32_t testCatchNamed();
static int32_t testCatchYield();
static Resource * Resource_op__initCover(int32_t id);
static Result_i32 level3(int32_t x);
static Result_i32 level2(int32_t x);
static Result_i32 level1(int32_t x);
static int32_t testBubbleUpUnwind();
static Result_i32 testReraise(int32_t x);
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

builtins_Exception_VTable TestError_vTable = (builtins_Exception_VTable){((uint64_t)(130)), ((char * (*)(builtins_Exception *))(TestError_what))};

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

static void TestError_op__init(TestError * this, char * msg) {
_L_entry:;
  builtins_Exception_op__init(this, "TestError");
  this->msg = msg;
  goto _L_exit;
_L_exit:;
  return;
}

static char * TestError_what(TestError * this) {
  char * __ret;
_L_entry:;
  __ret = this->msg;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void TestError_op__destructor(TestError * this) {
_L_entry:;
  builtins_Exception_op__destructor(((builtins_Exception *)(this)));
  goto _L_exit;
_L_exit:;
  return;
}

static void TestError_op__destructor_fwd(void * ptr) {
_L_entry:;
  TestError_op__destructor(((TestError *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static void TestError___defaults_init(TestError * this) {
_L_entry:;
  builtins_Exception___defaults_init(this);
  this->vtable = &TestError_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static TestError * TestError_op__initCover(char * msg) {
  TestError * __ret;
  TestError * __obj3;
  TestError * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(TestError), TestError_op__destructor_fwd);
  __obj3 = ((TestError *)(__t0));
  memset(__obj3, 0, sizeof(TestError));
  TestError___defaults_init(__obj3);
  TestError_op__init(__obj3, msg);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_i32 mayFail(int32_t x) {
  Result_i32 __ret;
  TestError * __t1;
_L_entry:;
  if (x < 0) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t1 = TestError_op__initCover("negative value");
  builtins_Exception_push(__t1, "main", "", "mayFail", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/21_exceptions_basic.cxy", 11);
  __ret.tag = 1;
  __ret.val._1 = __t1;
  goto _L_exit;
_L_IF0_end:;
  __ret.tag = 0;
  __ret.val._0 = (x * 2);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testCatchDefault() {
  int32_t __ret;
  int32_t a;
  Result_i32 __t2;
  int32_t __catch_res3;
  int32_t b;
  Result_i32 __t4;
  int32_t __catch_res5;
_L_entry:;
  __t2 = mayFail(((int32_t)(10)));
  if (__t2.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __catch_res3 = 0;
  __smart_ptr_drop(__t2.val._1);
  __t2.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res3 = __t2.val._0;
  __t2.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  a = __catch_res3;
  __t4 = mayFail(((int32_t)((-5))));
  if (__t4.tag == 1) goto _L_catch_1; else goto _L_cont_2;
_L_catch_1:;
  __catch_res5 = (-1);
  __smart_ptr_drop(__t4.val._1);
  __t4.tag = 0;
  goto _L_merge_2;
_L_cont_2:;
  __catch_res5 = __t4.val._0;
  __t4.tag = 0;
  goto _L_merge_2;
_L_merge_2:;
  b = __catch_res5;
  __ret = (a + b);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCatchDiscard() {
  Result_i32 __t6;
  Result_i32 __t7;
_L_entry:;
  __t6 = mayFail(((int32_t)(10)));
  if (__t6.tag == 1) goto _L_catch_0; else goto _L_merge_1;
_L_catch_0:;
  __smart_ptr_drop(__t6.val._1);
  __t6.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  __t7 = mayFail(((int32_t)((-1))));
  if (__t7.tag == 1) goto _L_catch_1; else goto _L_exit;
_L_catch_1:;
  __smart_ptr_drop(__t7.val._1);
  __t7.tag = 0;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t testCatchAnonymous() {
  int32_t __ret;
  int8_t res;
  Result_i32 __t8;
  int32_t __catch_res9;
_L_entry:;
  res = 0;
  __t8 = mayFail(((int32_t)((-10))));
  if (__t8.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  res = 42;
  __smart_ptr_drop(__t8.val._1);
  __t8.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res9 = __t8.val._0;
  __t8.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  __ret = res;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testCatchNamed() {
  int32_t __ret;
  int8_t res;
  Result_i32 __t10;
  int32_t __catch_res11;
_L_entry:;
  res = 0;
  __t10 = mayFail(((int32_t)((-20))));
  if (__t10.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  res = 100;
  __smart_ptr_drop(__t10.val._1);
  __t10.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res11 = __t10.val._0;
  __t10.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  __ret = res;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t testCatchYield() {
  int32_t __ret;
  int32_t val;
  Result_i32 __t12;
  int32_t __catch_res13;
_L_entry:;
  __t12 = mayFail(((int32_t)((-30))));
  if (__t12.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __catch_res13 = 99;
  __smart_ptr_drop(__t12.val._1);
  __t12.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res13 = __t12.val._0;
  __t12.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  val = __catch_res13;
  __ret = val;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj4;
  Resource * __t14;
_L_entry:;
  __t14 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj4 = ((Resource *)(__t14));
  memset(__obj4, 0, sizeof(Resource));
  Resource_op__init(__obj4, id);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_i32 level3(int32_t x) {
  Result_i32 __ret;
  Resource * res;
  TestError * __t15;
_L_entry:;
  res = Resource_op__initCover(x);
  if (x < 0) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t15 = TestError_op__initCover("level3 failed");
  builtins_Exception_push(__t15, "main", "", "level3", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/21_exceptions_basic.cxy", 53);
  __ret.tag = 1;
  __ret.val._1 = __t15;
  goto _L_exit;
_L_IF0_end:;
  __ret.tag = 0;
  __ret.val._0 = (x + 10);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(res);
  return __ret;
}

static Result_i32 level2(int32_t x) {
  Result_i32 __ret;
  Resource * res;
  int32_t v;
  Result_i32 __call_res_16;
_L_entry:;
  res = Resource_op__initCover((x + 1));
  __call_res_16 = level3(x);
  if (__call_res_16.tag == 1) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1;
  builtins_Exception_push(__call_res_16.val._1, "main", "", "level2", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/21_exceptions_basic.cxy", 60);
  __ret.val._1 = __call_res_16.val._1;
  __call_res_16.tag = 0;
  goto _L_exit;
_L_call_cont_1:;
  v = __call_res_16.val._0;
  __ret.tag = 0;
  __ret.val._0 = (v + 20);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(res);
  return __ret;
}

static Result_i32 level1(int32_t x) {
  Result_i32 __ret;
  Resource * res;
  int32_t v;
  Result_i32 __call_res_17;
_L_entry:;
  res = Resource_op__initCover((x + 2));
  __call_res_17 = level2(x);
  if (__call_res_17.tag == 1) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1;
  builtins_Exception_push(__call_res_17.val._1, "main", "", "level1", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/21_exceptions_basic.cxy", 66);
  __ret.val._1 = __call_res_17.val._1;
  __call_res_17.tag = 0;
  goto _L_exit;
_L_call_cont_1:;
  v = __call_res_17.val._0;
  __ret.tag = 0;
  __ret.val._0 = (v + 30);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(res);
  return __ret;
}

static int32_t testBubbleUpUnwind() {
  int32_t __ret;
  int32_t result;
  Result_i32 __t18;
  int32_t __catch_res19;
_L_entry:;
  __t18 = level1(((int32_t)((-1))));
  if (__t18.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __catch_res19 = (-999);
  __smart_ptr_drop(__t18.val._1);
  __t18.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res19 = __t18.val._0;
  __t18.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  result = __catch_res19;
  __ret = result;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_i32 testReraise(int32_t x) {
  Result_i32 __ret;
  int32_t v;
  Result_i32 __t20;
  int32_t __catch_res21;
_L_entry:;
  __t20 = mayFail(x);
  if (__t20.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __ret.tag = 1;
  builtins_Exception_push(__t20.val._1, "main", "", "testReraise", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/21_exceptions_basic.cxy", 79);
  __ret.val._1 = __t20.val._1;
  __t20.tag = 0;
  goto _L_exit;
_L_cont_1:;
  __catch_res21 = __t20.val._0;
  __t20.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  v = __catch_res21;
  __ret.tag = 0;
  __ret.val._0 = v;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  int32_t r1;
  int32_t r2;
  int32_t r3;
  int32_t r4;
  int32_t r5;
  int32_t r6;
  Result_i32 __t22;
  int32_t __catch_res23;
  int32_t r7;
  Result_i32 __t24;
  int32_t __catch_res25;
_L_entry:;
  r1 = testCatchDefault();
  testCatchDiscard();
  r2 = testCatchAnonymous();
  r3 = testCatchNamed();
  r4 = testCatchYield();
  r5 = testBubbleUpUnwind();
  __t22 = testReraise(((int32_t)(5)));
  if (__t22.tag == 1) goto _L_catch_0; else goto _L_cont_1;
_L_catch_0:;
  __catch_res23 = (-1);
  __smart_ptr_drop(__t22.val._1);
  __t22.tag = 0;
  goto _L_merge_1;
_L_cont_1:;
  __catch_res23 = __t22.val._0;
  __t22.tag = 0;
  goto _L_merge_1;
_L_merge_1:;
  r6 = __catch_res23;
  __t24 = testReraise(((int32_t)((-5))));
  if (__t24.tag == 1) goto _L_catch_1; else goto _L_cont_2;
_L_catch_1:;
  __catch_res25 = (-2);
  __smart_ptr_drop(__t24.val._1);
  __t24.tag = 0;
  goto _L_merge_2;
_L_cont_2:;
  __catch_res25 = __t24.val._0;
  __t24.tag = 0;
  goto _L_merge_2;
_L_merge_2:;
  r7 = __catch_res25;
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

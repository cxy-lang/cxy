typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_Exception builtins_Exception;
typedef struct String String;
typedef struct HeavyResource HeavyResource;
typedef struct VoidError VoidError;
typedef struct Result_void Result_void;
typedef struct __drop_flags_testVoidCatch_t __drop_flags_testVoidCatch_t;

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

struct HeavyResource {
  String * data;
};

struct VoidError {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
  int32_t code;
};

struct Result_void {
  uint32_t tag;
  builtins_Exception * val;
};

struct __drop_flags_testVoidCatch_t {
  uint64_t dummy: 1;
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
static void HeavyResource_op__init(HeavyResource * this, String * data);
static void HeavyResource_op__deinit(HeavyResource * this);
static void HeavyResource_op__destructor(HeavyResource * this);
static void HeavyResource_op__destructor_fwd(void * ptr);
static void VoidError_op__init(VoidError * this, int32_t code);
static char * VoidError_what(VoidError * this);
static void VoidError_op__destructor(VoidError * this);
static void VoidError_op__destructor_fwd(void * ptr);
static void VoidError___defaults_init(VoidError * this);
static String * String_op__initCover(char * s);
static HeavyResource * HeavyResource_op__initCover(String * data);
static VoidError * VoidError_op__initCover(int32_t code);
static Result_void performVoidWork(bool fail);
static void testVoidCatch();
static Result_void level2Void(bool fail);
static void testVoidBubbleUp();
static void _main();

static const __TypeInfo __ti_String = { .name = "String", .size = sizeof(String), .align = _Alignof(String), .dropFields = nullptr };
static const __TypeInfo __ti_HeavyResource = { .name = "HeavyResource", .size = sizeof(HeavyResource), .align = _Alignof(HeavyResource), .dropFields = HeavyResource_op__destructor_fwd };
static const __TypeInfo __ti_VoidError = { .name = "VoidError", .size = sizeof(VoidError), .align = _Alignof(VoidError), .dropFields = VoidError_op__destructor_fwd };

builtins_Exception_VTable builtins_Exception_vTable = (builtins_Exception_VTable){.__tid = 87ULL, .what = ((char * (*)(builtins_Exception *))(builtins_Exception_what))};

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

builtins_Exception_VTable VoidError_vTable = (builtins_Exception_VTable){.__tid = ((uint64_t)(140)), .what = ((char * (*)(builtins_Exception *))(VoidError_what))};

static void String_op__init(String * this, char * s) {
_L_entry:;
  this->s = s;
  goto _L_exit;
_L_exit:;
  return;
}

static void HeavyResource_op__init(HeavyResource * this, String * data) {
_L_entry:;
  __smart_ptr_drop(this->data);
  this->data = __smart_ptr_get(data);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(data);
  return;
}

static void HeavyResource_op__deinit(HeavyResource * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void HeavyResource_op__destructor(HeavyResource * this) {
_L_entry:;
  HeavyResource_op__deinit(this);
  __smart_ptr_drop(this->data);
  this->data = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void HeavyResource_op__destructor_fwd(void * ptr) {
_L_entry:;
  HeavyResource_op__destructor(((HeavyResource *)(((HeavyResource *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void VoidError_op__init(VoidError * this, int32_t code) {
_L_entry:;
  builtins_Exception_op__init(this, "VoidError");
  this->code = code;
  goto _L_exit;
_L_exit:;
  return;
}

static char * VoidError_what(VoidError * this) {
  char * __ret;
_L_entry:;
  __ret = "some error";
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void VoidError_op__destructor(VoidError * this) {
_L_entry:;
  builtins_Exception_op__destructor(((builtins_Exception *)(this)));
  goto _L_exit;
_L_exit:;
  return;
}

static void VoidError_op__destructor_fwd(void * ptr) {
_L_entry:;
  VoidError_op__destructor(((VoidError *)(((VoidError *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void VoidError___defaults_init(VoidError * this) {
_L_entry:;
  builtins_Exception___defaults_init(this);
  this->vtable = &VoidError_vTable;
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

static HeavyResource * HeavyResource_op__initCover(String * data) {
  HeavyResource * __ret;
  HeavyResource * __obj4;
  HeavyResource * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_HeavyResource, nullptr);
  __obj4 = ((HeavyResource *)(__t1));
  memset(__obj4, 0, sizeof(HeavyResource));
  HeavyResource_op__init(((HeavyResource *)(__obj4)), data);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static VoidError * VoidError_op__initCover(int32_t code) {
  VoidError * __ret;
  VoidError * __obj5;
  VoidError * __t2;
_L_entry:;
  __t2 = __mm_alloc(&__ti_VoidError, nullptr);
  __obj5 = ((VoidError *)(__t2));
  memset(__obj5, 0, sizeof(VoidError));
  VoidError___defaults_init(((VoidError *)(__obj5)));
  VoidError_op__init(((VoidError *)(__obj5)), code);
  __ret = __obj5;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_void performVoidWork(bool fail) {
  Result_void __ret;
  HeavyResource * r1;
  String * __t3;
  HeavyResource * r2;
  String * __t4;
  VoidError * __t5;
_L_entry:;
  __t3 = String_op__initCover("res_1");
  r1 = HeavyResource_op__initCover(__t3);
  __t4 = String_op__initCover("res_2");
  r2 = HeavyResource_op__initCover(__t4);
  if (fail) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t5 = VoidError_op__initCover(((int32_t)(404)));
  builtins_Exception_push(__t5, "main", "", "performVoidWork", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/23_exceptions_void.cxy", 23ULL);
  __ret.tag = 1U;
  __ret.val = __t5;
  goto _L_exit;
_L_IF0_end:;
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(r2);
  __smart_ptr_drop(r1);
  return __ret;
}

static void testVoidCatch() {
  __drop_flags_testVoidCatch_t __drop_flags;
  Result_void __t6;
  Result_void __t7;
  Result_void __t8;
  Result_void __t9;
  HeavyResource * dummy;
  String * __t10;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testVoidCatch_t));
  __t6 = performVoidWork(false);
  if (__t6.tag == 1U) goto _L_catch_0; else goto _L_merge_1;
_L_catch_0:;
  __smart_ptr_drop(__t6.val);
  __t6.val = nullptr;
  __t6.tag = 0U;
  goto _L_merge_1;
_L_merge_1:;
  __t7 = performVoidWork(true);
  if (__t7.tag == 1U) goto _L_catch_1; else goto _L_merge_2;
_L_catch_1:;
  __smart_ptr_drop(__t7.val);
  __t7.val = nullptr;
  __t7.tag = 0U;
  goto _L_merge_2;
_L_merge_2:;
  __t8 = performVoidWork(true);
  if (__t8.tag == 1U) goto _L_catch_2; else goto _L_merge_3;
_L_catch_2:;
  __smart_ptr_drop(__t8.val);
  __t8.val = nullptr;
  __t8.tag = 0U;
  goto _L_merge_3;
_L_merge_3:;
  __t9 = performVoidWork(true);
  if (__t9.tag == 1U) goto _L_catch_3; else goto _L_exit;
_L_catch_3:;
  __t10 = String_op__initCover("handled_in_catch");
  dummy = HeavyResource_op__initCover(__t10);
  __drop_flags.dummy = 1ULL;
  __smart_ptr_drop(dummy);
  __drop_flags.dummy = 0ULL;
  __smart_ptr_drop(__t9.val);
  __t9.val = nullptr;
  __t9.tag = 0U;
  goto _L_exit;
_L_exit:;
  return;
}

static Result_void level2Void(bool fail) {
  Result_void __ret;
  HeavyResource * local;
  String * __t11;
  Result_void __call_res_12;
_L_entry:;
  __t11 = String_op__initCover("level2_res");
  local = HeavyResource_op__initCover(__t11);
  __call_res_12 = performVoidWork(fail);
  if (__call_res_12.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_12.val, "main", "", "level2Void", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/23_exceptions_void.cxy", 42ULL);
  __ret.val = __call_res_12.val;
  __call_res_12.tag = 0U;
  goto _L_exit;
_L_call_cont_1:;
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(local);
  return __ret;
}

static void testVoidBubbleUp() {
  Result_void __t13;
  Result_void __t14;
_L_entry:;
  __t13 = level2Void(false);
  if (__t13.tag == 1U) goto _L_catch_0; else goto _L_merge_1;
_L_catch_0:;
  __smart_ptr_drop(__t13.val);
  __t13.val = nullptr;
  __t13.tag = 0U;
  goto _L_merge_1;
_L_merge_1:;
  __t14 = level2Void(true);
  if (__t14.tag == 1U) goto _L_catch_1; else goto _L_exit;
_L_catch_1:;
  __smart_ptr_drop(__t14.val);
  __t14.val = nullptr;
  __t14.tag = 0U;
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
_L_entry:;
  testVoidCatch();
  testVoidBubbleUp();
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

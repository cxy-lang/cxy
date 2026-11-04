typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_Exception builtins_Exception;
typedef struct Resource Resource;
typedef struct Hello Hello;
typedef struct Result_void Result_void;
typedef struct __drop_flags_Hello_sayerI_ZResource_RZResource_E_t __drop_flags_Hello_sayerI_ZResource_RZResource_E_t;
typedef struct __drop_flags_Hello_sayerI_ZResource_ZResource_E_t __drop_flags_Hello_sayerI_ZResource_ZResource_E_t;

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

struct Hello {
  bool _active;
};

struct Result_void {
  uint32_t tag;
  builtins_Exception * val;
};

struct __drop_flags_Hello_sayerI_ZResource_RZResource_E_t {
  uint64_t args: 1;
};

struct __drop_flags_Hello_sayerI_ZResource_ZResource_E_t {
  uint64_t args: 1;
  uint64_t args1: 1;
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
static Resource * Resource_op__initCover(int32_t id);
static builtins_Exception * builtins_Exception_op__initCover(char * name);
static void Hello_sayI_ZResource_ZResource_RZResource_E(Hello * this, Resource * args, Resource * args1, Resource * args2);
static Result_void Hello_sayerI_ZResource_RZResource_E(Hello * this, Resource * name, Resource * args, Resource * args1);
static void Hello_sayI_ZResource_ZResource_ZResource_E(Hello * this, Resource * args, Resource * args1, Resource * args2);
static Result_void Hello_sayerI_ZResource_ZResource_E(Hello * this, Resource * name, Resource * args, Resource * args1);
static Result_void _main();

static const __TypeInfo __ti_Resource = { .name = "Resource", .size = sizeof(Resource), .align = _Alignof(Resource), .index = 0, .dropFields = Resource_op__destructor_fwd };
static const __TypeInfo __ti_builtins_Exception = { .name = "Exception", .size = sizeof(builtins_Exception), .align = _Alignof(builtins_Exception), .index = 1, .dropFields = builtins_Exception_op__destructor_fwd };

builtins_Exception_VTable builtins_Exception_vTable = (builtins_Exception_VTable){.__tid = 91ULL, .what = ((char * (*)(builtins_Exception *))(builtins_Exception_what))};

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

static builtins_Exception * builtins_Exception_op__initCover(char * name) {
  builtins_Exception * __ret;
  builtins_Exception * __obj4;
  builtins_Exception * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_builtins_Exception, nullptr);
  __obj4 = ((builtins_Exception *)(__t1));
  memset(__obj4, 0, sizeof(builtins_Exception));
  builtins_Exception___defaults_init(((builtins_Exception *)(__obj4)));
  builtins_Exception_op__init(((builtins_Exception *)(__obj4)), name);
  __ret = __obj4;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void Hello_sayI_ZResource_ZResource_RZResource_E(Hello * this, Resource * args, Resource * args1, Resource * args2) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(args1);
  __smart_ptr_drop(args);
  return;
}

static Result_void Hello_sayerI_ZResource_RZResource_E(Hello * this, Resource * name, Resource * args, Resource * args1) {
  __drop_flags_Hello_sayerI_ZResource_RZResource_E_t __drop_flags;
  Result_void __ret;
  builtins_Exception * __t2;
  Resource * __copy0;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_Hello_sayerI_ZResource_RZResource_E_t));
  __drop_flags.args = 1ULL;
  if ((!(this->_active))) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t2 = builtins_Exception_op__initCover("not active");
  builtins_Exception_push(__t2, "main", "Hello", "main.Hello.sayerI_ZResource_RZResource_E", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/31_variadic_move_args.cxy", 17ULL);
  __ret.tag = 1U;
  __ret.val = __t2;
  goto _L_exit;
_L_IF0_end:;
  __copy0 = __smart_ptr_get(name);
  __drop_flags.args = 0ULL;
  Hello_sayI_ZResource_ZResource_RZResource_E(this, __copy0, args, args1);
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  if (__drop_flags.args) __smart_ptr_drop(args);
  __drop_flags.args = 0ULL;
  __smart_ptr_drop(name);
  return __ret;
}

static void Hello_sayI_ZResource_ZResource_ZResource_E(Hello * this, Resource * args, Resource * args1, Resource * args2) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(args2);
  __smart_ptr_drop(args1);
  __smart_ptr_drop(args);
  return;
}

static Result_void Hello_sayerI_ZResource_ZResource_E(Hello * this, Resource * name, Resource * args, Resource * args1) {
  __drop_flags_Hello_sayerI_ZResource_ZResource_E_t __drop_flags;
  Result_void __ret;
  builtins_Exception * __t3;
  Resource * __copy1;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_Hello_sayerI_ZResource_ZResource_E_t));
  __drop_flags.args = 1ULL;
  __drop_flags.args1 = 1ULL;
  if ((!(this->_active))) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t3 = builtins_Exception_op__initCover("not active");
  builtins_Exception_push(__t3, "main", "Hello", "main.Hello.sayerI_ZResource_ZResource_E", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/31_variadic_move_args.cxy", 17ULL);
  __ret.tag = 1U;
  __ret.val = __t3;
  goto _L_exit;
_L_IF0_end:;
  __copy1 = __smart_ptr_get(name);
  __drop_flags.args = 0ULL;
  __drop_flags.args1 = 0ULL;
  Hello_sayI_ZResource_ZResource_ZResource_E(this, __copy1, args, args1);
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  if (__drop_flags.args1) __smart_ptr_drop(args1);
  __drop_flags.args1 = 0ULL;
  if (__drop_flags.args) __smart_ptr_drop(args);
  __drop_flags.args = 0ULL;
  __smart_ptr_drop(name);
  return __ret;
}

static Result_void _main() {
  Result_void __ret;
  Hello h;
  Resource * r;
  Resource * __t4;
  Resource * __t5;
  Result_void __call_res_6;
  Resource * __t7;
  Resource * __t8;
  Resource * __t9;
  Result_void __call_res_10;
_L_entry:;
  h = (Hello){._active = true};
  r = Resource_op__initCover(((int32_t)(3)));
  __t4 = Resource_op__initCover(((int32_t)(1)));
  __t5 = Resource_op__initCover(((int32_t)(2)));
  __call_res_6 = Hello_sayerI_ZResource_RZResource_E(&h, __t4, __t5, ((Resource *)(r)));
  if (__call_res_6.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_6.val, "main", "", "main", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/31_variadic_move_args.cxy", 26ULL);
  __ret.val = __call_res_6.val;
  __call_res_6.tag = 0U;
  goto _L_exit;
_L_call_cont_1:;
  __t7 = Resource_op__initCover(((int32_t)(4)));
  __t8 = Resource_op__initCover(((int32_t)(5)));
  __t9 = Resource_op__initCover(((int32_t)(6)));
  __call_res_10 = Hello_sayerI_ZResource_ZResource_E(&h, __t7, __t8, __t9);
  if (__call_res_10.tag == 1U) goto _L_call_err_1; else goto _L_call_cont_2;
_L_call_err_1:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_10.val, "main", "", "main", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/31_variadic_move_args.cxy", 27ULL);
  __ret.val = __call_res_10.val;
  __call_res_10.tag = 0U;
  goto _L_exit;
_L_call_cont_2:;
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(r);
  return __ret;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  Result_void res = _main();
  if (res.tag == 1) {
    builtins_Exception *ex = (builtins_Exception *)res.val;
    if (ex) {
      builtins_panicOnUnhandledException(ex);
      __smart_ptr_drop(ex);
    }
    return 1;
  }
  return 0;
}

typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_OutputStream_VTable builtins_OutputStream_VTable;
typedef struct builtins_OutputStream builtins_OutputStream;
typedef struct Optional_u64 Optional_u64;
typedef struct builtins_String builtins_String;
typedef struct builtins_StdOutputStream builtins_StdOutputStream;
typedef struct _opaque_pthread_t _opaque_pthread_t;
typedef struct __darwin_pthread_handler_rec __darwin_pthread_handler_rec;
typedef struct thread_ThreadError thread_ThreadError;
typedef struct thread_Handle thread_Handle;
typedef struct thread_Thread thread_Thread;
typedef struct Closure_888 Closure_888;
typedef union Result_Thread_val Result_Thread_val;
typedef struct Result_Thread Result_Thread;
typedef struct atomic_AtomicI_i32E atomic_AtomicI_i32E;
typedef int64_t atomic_MemoryOrder;
#define atomic_MemoryOrder_Relaxed ((atomic_MemoryOrder)0)
#define atomic_MemoryOrder_Acquire ((atomic_MemoryOrder)2)
#define atomic_MemoryOrder_Release ((atomic_MemoryOrder)3)
#define atomic_MemoryOrder_AcqRel ((atomic_MemoryOrder)4)
#define atomic_MemoryOrder_SeqCst ((atomic_MemoryOrder)5)
typedef struct User User;
typedef struct __closure_env21 __closure_env21;
typedef struct Result_void Result_void;
typedef struct __closure_env23 __closure_env23;

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_OutputStream_VTable {
  uint64_t __tid;
  Optional_u64 (*append)(builtins_OutputStream *, void *, uint64_t);
};

struct builtins_OutputStream {
  builtins_OutputStream_VTable * vtable;
};

struct Optional_u64 {
  bool ok;
  uint64_t val;
};

struct builtins_String {
  builtins_OutputStream_VTable * vtable;
  uint64_t _capacity;
  uint64_t _size;
  char * _data;
};

struct builtins_StdOutputStream {
  builtins_OutputStream_VTable * vtable;
  int32_t fd;
};

struct _opaque_pthread_t {
  int64_t __sig;
  __darwin_pthread_handler_rec * __cleanup_stack;
  char __opaque[8176];
};

struct __darwin_pthread_handler_rec {
  void (*__routine)(void *);
  void * __arg;
  __darwin_pthread_handler_rec * __next;
};

struct thread_ThreadError {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
  builtins_String * msg;
};

struct thread_Handle {
  _opaque_pthread_t * hdl;
  uint64_t _id;
};

struct thread_Thread {
  thread_Handle _handle;
  bool _joinable;
};

struct Closure_888 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

union Result_Thread_val {
  thread_Thread _0;
  builtins_Exception * _1;
};

struct Result_Thread {
  uint32_t tag;
  Result_Thread_val val;
};

struct atomic_AtomicI_i32E {
  int32_t _value;
};

struct User {
  builtins_String * name;
  int32_t age;
};

struct __closure_env21 {
  User user;
};

struct Result_void {
  uint32_t tag;
  builtins_Exception * val;
};

struct __closure_env23 {
  User user;
};

static void builtins_Exception_op__init(builtins_Exception * this, char * name);
static char * builtins_Exception_what(builtins_Exception * this);
static void builtins_Exception_push(builtins_Exception * this, char * mod, char * cls, char * fun, char * path, uint64_t line);
static void builtins_Exception_op__deinit(builtins_Exception * this);
static void builtins_Exception_op__destructor(builtins_Exception * this);
static void builtins_Exception___defaults_init(builtins_Exception * this);
static void builtins_panicOnUnhandledException(builtins_Exception * ex);
static void builtins_OutputStream_appendStringI_sE(builtins_OutputStream * this, char * s);
static void builtins_OutputStream_appendSignedInt(builtins_OutputStream * this, int128_t num);
static void builtins_OutputStream_appendChar(builtins_OutputStream * this, int32_t ch);
static void builtins_OutputStream___defaults_init(builtins_OutputStream * this);
static uint64_t builtins_maxI_u64E(uint64_t a, uint64_t b);
static void builtins_String_grow(builtins_String * this, uint64_t growSize);
static void builtins_String_op__initI_sE(builtins_String * this, char * str);
static void builtins_String_op__deinit(builtins_String * this);
static void builtins_String_clear(builtins_String * this);
static Optional_u64 builtins_String_append(builtins_String * this, char * str, uint64_t size);
static char * builtins_String_str(builtins_String * this);
static void builtins_String_op__destructor(builtins_String * this);
static void builtins_String_op__destructor_fwd(void * ptr);
static void builtins_String___defaults_init(builtins_String * this);
static void builtins_StdOutputStream_op__init(builtins_StdOutputStream * this, int32_t fd);
static Optional_u64 builtins_StdOutputStream_append(builtins_StdOutputStream * this, char * str, uint64_t size);
static void builtins_StdOutputStream_op__destructor(builtins_StdOutputStream * this);
static void builtins_StdOutputStream_op__destructor_fwd(void * ptr);
static void builtins_StdOutputStream___defaults_init(builtins_StdOutputStream * this);
static builtins_StdOutputStream * builtins_StdOutputStream_op__initCover(int32_t fd);
static builtins_OutputStream * builtins_OutputStream_op__lshiftI_wE(builtins_OutputStream * this, int32_t val);
__attribute__((constructor)) static void builtins___init();
static void builtins___fini();

extern int32_t thrd_join(_opaque_pthread_t * thr, int32_t * res);
extern int32_t thrd_id(_opaque_pthread_t * thr, uint64_t * id);
static void thread_ThreadError_op__init(thread_ThreadError * this, builtins_String * msg);
static char * thread_ThreadError_what(thread_ThreadError * this);
static void thread_ThreadError_op__destructor(thread_ThreadError * this);
static void thread_ThreadError_op__destructor_fwd(void * ptr);
static void thread_ThreadError___defaults_init(thread_ThreadError * this);
static void thread_Handle_op__init(thread_Handle * this, _opaque_pthread_t * hdl);
static thread_Handle thread_Handle_op__initCover(_opaque_pthread_t * hdl);
static void thread_Thread_op__init(thread_Thread * this, _opaque_pthread_t * hdl, bool joinable);
static void thread_Thread_join(thread_Thread * this);
extern bool __cxy_thread_launch(_opaque_pthread_t * * hdl, void * cl, uint64_t size);
static builtins_String * thread_String_op__initI_sECoverI_sE(char * str);
static thread_ThreadError * thread_ThreadError_op__initCover(builtins_String * msg);
static thread_Thread thread_Thread_op__initCover(_opaque_pthread_t * hdl, bool joinable);
static Result_Thread thread_launch(Closure_888 fn);


static void atomic_AtomicI_i32E_op__initI_E(atomic_AtomicI_i32E * this);
static int32_t atomic_AtomicI_i32E_load(atomic_AtomicI_i32E * this, atomic_MemoryOrder order);
static void atomic_AtomicI_i32E_store(atomic_AtomicI_i32E * this, int32_t value, atomic_MemoryOrder order);
static atomic_AtomicI_i32E atomic_AtomicI_i32E_op__initCoverI_E();
static User User_op__copy(User * this);
static void User_op__destructor(User * this);
static void __closure_env21_op__destructor(__closure_env21 * this);
static builtins_OutputStream * builtins_OutputStream_op__lshiftI_i32E(builtins_OutputStream * this, int32_t val);
static void printlnI_i32E(int32_t args);
static int32_t __closure_body22(__closure_env21 * this);
static Result_void detached(User user);
static void __closure_env23_op__destructor(__closure_env23 * this);
static int32_t __closure_body24(__closure_env23 * this);
static Result_void joined(User user);
static Result_void _main();
__attribute__((constructor)) static void __init();

static const __TypeInfo __ti_builtins_StdOutputStream = { .name = "builtins.StdOutputStream", .size = sizeof(builtins_StdOutputStream), .align = _Alignof(builtins_StdOutputStream), .index = 0, .dropFields = builtins_StdOutputStream_op__destructor_fwd };
static const __TypeInfo __ti_builtins_String = { .name = "builtins.String", .size = sizeof(builtins_String), .align = _Alignof(builtins_String), .index = 1, .dropFields = builtins_String_op__destructor_fwd };
static const __TypeInfo __ti_thread_ThreadError = { .name = "thread.ThreadError", .size = sizeof(thread_ThreadError), .align = _Alignof(thread_ThreadError), .index = 2, .dropFields = thread_ThreadError_op__destructor_fwd };
static const __TypeInfo __ti___closure_env21 = { .name = "__closure_env21", .size = sizeof(__closure_env21), .align = _Alignof(__closure_env21), .index = 3, .dropFields = __closure_env21_op__destructor };
static const __TypeInfo __ti___closure_env23 = { .name = "__closure_env23", .size = sizeof(__closure_env23), .align = _Alignof(__closure_env23), .index = 4, .dropFields = __closure_env23_op__destructor };

builtins_Exception_VTable builtins_Exception_vTable = (builtins_Exception_VTable){.__tid = 95ULL, .what = ((char * (*)(builtins_Exception *))(builtins_Exception_what))};
builtins_OutputStream_VTable builtins_OutputStream_vTable = (builtins_OutputStream_VTable){.__tid = 193ULL, .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(nullptr))};
builtins_OutputStream_VTable builtins_String_vTable = (builtins_OutputStream_VTable){.__tid = ((uint64_t)(215)), .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(builtins_String_append))};
builtins_OutputStream_VTable builtins_StdOutputStream_vTable = (builtins_OutputStream_VTable){.__tid = ((uint64_t)(312)), .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(builtins_StdOutputStream_append))};
builtins_OutputStream * builtins_stdout;
builtins_OutputStream * builtins_stderr;
int64_t builtins_SysConfPageSize;
int64_t builtins_SysConfNumProcs;

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

static void builtins_OutputStream_appendStringI_sE(builtins_OutputStream * this, char * s) {
  uint64_t __t44;
_L_entry:;
  if (s != nullptr) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __t44 = strlen(s);
  this->vtable->append(this, ((void *)(s)), __t44);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_OutputStream_appendSignedInt(builtins_OutputStream * this, int128_t num) {
  char buf[64];
  uint64_t len;
_L_entry:;
  len = sputi128(num, buf, ((uint64_t)(64)));
  this->vtable->append(this, ((void *)(buf)), len);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_OutputStream_appendChar(builtins_OutputStream * this, int32_t ch) {
  char data[5];
  uint64_t size;
_L_entry:;
  size = 0;
  if (ch < 128) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  data[0] = ((char)(ch));
  size = 1;
  goto _L_IF0_end;
_L_IF0_else:;
  if (ch < 2048) goto _L_IF1_then; else goto _L_IF1_else;
_L_IF0_end:;
  this->vtable->append(this, ((void *)(data)), size);
  goto _L_exit;
_L_IF1_then:;
  data[0] = ((char)((192 | (ch >> 6))));
  data[1] = ((char)((128 | (ch & 63))));
  size = 2;
  goto _L_IF0_end;
_L_IF1_else:;
  if (ch < 65536) goto _L_IF2_then; else goto _L_IF2_else;
_L_IF2_then:;
  data[0] = ((char)((224 | (ch >> 12))));
  data[1] = ((char)((128 | ((ch >> 6) & 63))));
  data[2] = ((char)((128 | (ch & 63))));
  size = 3;
  goto _L_IF0_end;
_L_IF2_else:;
  if (ch < 2097152) goto _L_IF3_then; else goto _L_IF0_end;
_L_IF3_then:;
  data[0] = ((char)((240 | (ch >> 16))));
  data[1] = ((char)((128 | ((ch >> 12) & 63))));
  data[2] = ((char)((128 | ((ch >> 6) & 63))));
  data[3] = ((char)((128 | (ch & 63))));
  size = 4;
  goto _L_IF0_end;
_L_exit:;
  return;
}

static void builtins_OutputStream___defaults_init(builtins_OutputStream * this) {
_L_entry:;
  this->vtable = &builtins_OutputStream_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static uint64_t builtins_maxI_u64E(uint64_t a, uint64_t b) {
  uint64_t __ret;
  uint64_t _if8;
_L_entry:;
  if (a > b) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  _if8 = a;
  goto _L_IF0_end;
_L_IF0_else:;
  _if8 = b;
  goto _L_IF0_end;
_L_IF0_end:;
  __ret = _if8;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void builtins_String_grow(builtins_String * this, uint64_t growSize) {
  uint64_t newSize;
  void * __t47;
  void * tmp;
_L_entry:;
  newSize = builtins_maxI_u64E((this->_size + growSize), 16ULL);
  if (this->_data == nullptr) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  __t47 = __alloc_buffer((newSize + 1));
  this->_data = ((char *)(__t47));
  this->_capacity = newSize;
  goto _L_exit;
_L_IF0_else:;
  if (this->_capacity < newSize) goto _L_WHILE0_cond; else goto _L_exit;
_L_WHILE0_cond:;
  if (this->_capacity < newSize) goto _L_WHILE0_body; else goto _L_WHILE0_exit;
_L_WHILE0_body:;
  this->_capacity = (this->_capacity << 1);
  goto _L_WHILE0_cond;
_L_WHILE0_exit:;
  tmp = __realloc_buffer(((void *)(this->_data)), (this->_capacity + 1));
  if (tmp == nullptr) goto _L_IF2_then; else goto _L_IF2_end;
_L_IF2_then:;
  __cxy_panic("__builtins.cxy", 769ULL, 17ULL, "failed to grow string buffer");
  goto _L_IF2_end;
_L_IF2_end:;
  this->_data = ((char *)(tmp));
  this->_data[this->_size] = '\0';
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_String_op__initI_sE(builtins_String * this, char * str) {
_L_entry:;
  builtins_OutputStream_appendStringI_sE(this, str);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_String_op__deinit(builtins_String * this) {
_L_entry:;
  builtins_String_clear(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_String_clear(builtins_String * this) {
_L_entry:;
  if (this->_data != nullptr) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __free_buffer(((void *)(this->_data)));
  this->_data = nullptr;
  this->_capacity = 0;
  this->_size = 0;
  goto _L_exit;
_L_exit:;
  return;
}

static Optional_u64 builtins_String_append(builtins_String * this, char * str, uint64_t size) {
  Optional_u64 __ret;
  char * p;
_L_entry:;
  if (size != 0ULL) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  builtins_String_grow(this, size);
  p = (this->_data + this->_size);
  memmove(((void *)(p)), ((void *)(str)), size);
  this->_size = (this->_size + size);
  this->_data[this->_size] = '\0';
  goto _L_IF0_end;
_L_IF0_end:;
  __ret.ok = true;
  __ret.val = size;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static char * builtins_String_str(builtins_String * this) {
  char * __ret;
_L_entry:;
  __ret = ((char *)(this->_data));
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void builtins_String_op__destructor(builtins_String * this) {
_L_entry:;
  builtins_String_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_String_op__destructor_fwd(void * ptr) {
_L_entry:;
  builtins_String_op__destructor(((builtins_String *)(((builtins_String *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_String___defaults_init(builtins_String * this) {
_L_entry:;
  builtins_OutputStream___defaults_init(this);
  this->_capacity = 0;
  this->_size = 0;
  this->_data = nullptr;
  this->vtable = &builtins_String_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_StdOutputStream_op__init(builtins_StdOutputStream * this, int32_t fd) {
_L_entry:;
  this->fd = fd;
  goto _L_exit;
_L_exit:;
  return;
}

static Optional_u64 builtins_StdOutputStream_append(builtins_StdOutputStream * this, char * str, uint64_t size) {
  Optional_u64 __ret;
_L_entry:;
  if (size != 0ULL) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  write(this->fd, ((void *)(str)), size);
  goto _L_IF0_end;
_L_IF0_end:;
  __ret.ok = true;
  __ret.val = size;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void builtins_StdOutputStream_op__destructor(builtins_StdOutputStream * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_StdOutputStream_op__destructor_fwd(void * ptr) {
_L_entry:;
  builtins_StdOutputStream_op__destructor(((builtins_StdOutputStream *)(((builtins_StdOutputStream *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins_StdOutputStream___defaults_init(builtins_StdOutputStream * this) {
_L_entry:;
  builtins_OutputStream___defaults_init(this);
  this->vtable = &builtins_StdOutputStream_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static builtins_StdOutputStream * builtins_StdOutputStream_op__initCover(int32_t fd) {
  builtins_StdOutputStream * __ret;
  builtins_StdOutputStream * __obj7;
  builtins_StdOutputStream * __t98;
_L_entry:;
  __t98 = __mm_alloc(&__ti_builtins_StdOutputStream, nullptr);
  __obj7 = ((builtins_StdOutputStream *)(__t98));
  memset(__obj7, 0, sizeof(builtins_StdOutputStream));
  builtins_StdOutputStream___defaults_init(((builtins_StdOutputStream *)(__obj7)));
  builtins_StdOutputStream_op__init(((builtins_StdOutputStream *)(__obj7)), fd);
  __ret = __obj7;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static builtins_OutputStream * builtins_OutputStream_op__lshiftI_wE(builtins_OutputStream * this, int32_t val) {
  builtins_OutputStream * __ret;
_L_entry:;
  builtins_OutputStream_appendChar(this, val);
  __ret = this;
  goto _L_exit;
_L_exit:;
  return __ret;
}

__attribute__((constructor)) static void builtins___init() {
_L_entry:;
  builtins_stdout = builtins_StdOutputStream_op__initCover(((int32_t)(1)));
  builtins_stderr = builtins_StdOutputStream_op__initCover(((int32_t)(2)));
  builtins_SysConfPageSize = sysconf(((int32_t)(29)));
  builtins_SysConfNumProcs = sysconf(((int32_t)(58)));
  goto _L_exit;
_L_exit:;
  return;
}

static void builtins___fini() {
_L_entry:;
  __smart_ptr_drop(builtins_stderr);
  builtins_stderr = nullptr;
  __smart_ptr_drop(builtins_stdout);
  builtins_stdout = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

__attribute__((constructor)) static void builtins___fini_reg(void) { __cxy_register_process_fini(builtins___fini); }

builtins_Exception_VTable thread_ThreadError_vTable = (builtins_Exception_VTable){.__tid = ((uint64_t)(856)), .what = ((char * (*)(builtins_Exception *))(thread_ThreadError_what))};

static void thread_ThreadError_op__init(thread_ThreadError * this, builtins_String * msg) {
_L_entry:;
  builtins_Exception_op__init(this, "ThreadError");
  __smart_ptr_drop(this->msg);
  this->msg = __smart_ptr_get(msg);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(msg);
  return;
}

static char * thread_ThreadError_what(thread_ThreadError * this) {
  char * __ret;
  char * _if18;
_L_entry:;
  if (this->msg == nullptr) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  _if18 = "";
  goto _L_IF0_end;
_L_IF0_else:;
  _if18 = builtins_String_str(((builtins_String *)(this->msg)));
  goto _L_IF0_end;
_L_IF0_end:;
  __ret = _if18;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void thread_ThreadError_op__destructor(thread_ThreadError * this) {
_L_entry:;
  builtins_Exception_op__destructor(((builtins_Exception *)(this)));
  __smart_ptr_drop(this->msg);
  this->msg = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void thread_ThreadError_op__destructor_fwd(void * ptr) {
_L_entry:;
  thread_ThreadError_op__destructor(((thread_ThreadError *)(((thread_ThreadError *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void thread_ThreadError___defaults_init(thread_ThreadError * this) {
_L_entry:;
  builtins_Exception___defaults_init(this);
  this->vtable = &thread_ThreadError_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static void thread_Handle_op__init(thread_Handle * this, _opaque_pthread_t * hdl) {
_L_entry:;
  this->hdl = hdl;
  thrd_id(hdl, &this->_id);
  goto _L_exit;
_L_exit:;
  return;
}

static thread_Handle thread_Handle_op__initCover(_opaque_pthread_t * hdl) {
  thread_Handle __ret;
  thread_Handle __obj14;
_L_entry:;
  __obj14 = (thread_Handle){.hdl = ((_opaque_pthread_t *)(nullptr)), ._id = 0ULL};
  thread_Handle_op__init(&__obj14, hdl);
  __ret = __obj14;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void thread_Thread_op__init(thread_Thread * this, _opaque_pthread_t * hdl, bool joinable) {
_L_entry:;
  this->_handle = thread_Handle_op__initCover(hdl);
  this->_joinable = joinable;
  goto _L_exit;
_L_exit:;
  return;
}

static void thread_Thread_join(thread_Thread * this) {
_L_entry:;
  if (this->_joinable) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  thrd_join(this->_handle.hdl, ((int32_t *)(nullptr)));
  this->_joinable = false;
  goto _L_exit;
_L_exit:;
  return;
}

static builtins_String * thread_String_op__initI_sECoverI_sE(char * str) {
  builtins_String * __ret;
  builtins_String * __obj15;
  builtins_String * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_builtins_String, nullptr);
  __obj15 = ((builtins_String *)(__t1));
  memset(__obj15, 0, sizeof(builtins_String));
  builtins_String___defaults_init(((builtins_String *)(__obj15)));
  builtins_String_op__initI_sE(((builtins_String *)(__obj15)), str);
  __ret = __obj15;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static thread_ThreadError * thread_ThreadError_op__initCover(builtins_String * msg) {
  thread_ThreadError * __ret;
  thread_ThreadError * __obj16;
  thread_ThreadError * __t2;
_L_entry:;
  __t2 = __mm_alloc(&__ti_thread_ThreadError, nullptr);
  __obj16 = ((thread_ThreadError *)(__t2));
  memset(__obj16, 0, sizeof(thread_ThreadError));
  thread_ThreadError___defaults_init(((thread_ThreadError *)(__obj16)));
  thread_ThreadError_op__init(((thread_ThreadError *)(__obj16)), msg);
  __ret = __obj16;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static thread_Thread thread_Thread_op__initCover(_opaque_pthread_t * hdl, bool joinable) {
  thread_Thread __ret;
  thread_Thread __obj17;
_L_entry:;
  __obj17 = (thread_Thread){._joinable = false};
  thread_Thread_op__init(&__obj17, hdl, joinable);
  __ret = __obj17;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_Thread thread_launch(Closure_888 fn) {
  Result_Thread __ret;
  _opaque_pthread_t * hdl;
  bool status;
  uint64_t __t3;
  builtins_String * __t4;
  thread_ThreadError * __t5;
  thread_Thread __t6;
_L_entry:;
  __t3 = sizeof(Closure_888);
  status = __cxy_thread_launch(&hdl, ((void *)(&fn)), __t3);
  if ((!(status))) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __t4 = thread_String_op__initI_sECoverI_sE("Failed to launch thread");
  __t5 = thread_ThreadError_op__initCover(__t4);
  builtins_Exception_push(__t5, "thread", "", "thread.thread_launch", "/Users/dc/.cxy/lib/cxy/std/stdlib/thread.cxy", 69ULL);
  __ret.tag = 1U;
  __ret.val._1 = __t5;
  goto _L_exit;
_L_IF0_end:;
  memset(&fn, 0, sizeof(Closure_888));
  __ret.tag = 0U;
  __t6 = thread_Thread_op__initCover(hdl, true);
  __ret.val._0 = __t6;
  goto _L_exit;
_L_exit:;
  if (fn.dtor) fn.dtor(fn.env);
  return __ret;
}

atomic_AtomicI_i32E detachedDone;

static void atomic_AtomicI_i32E_op__initI_E(atomic_AtomicI_i32E * this) {
_L_entry:;
  this->_value = 0;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t atomic_AtomicI_i32E_load(atomic_AtomicI_i32E * this, atomic_MemoryOrder order) {
  int32_t __ret;
_L_entry:;
  __ret = __atomic_load_n(&this->_value, ((int32_t)(order)));
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void atomic_AtomicI_i32E_store(atomic_AtomicI_i32E * this, int32_t value, atomic_MemoryOrder order) {
_L_entry:;
  __atomic_store_n(&this->_value, value, ((int32_t)(order)));
  goto _L_exit;
_L_exit:;
  return;
}

static atomic_AtomicI_i32E atomic_AtomicI_i32E_op__initCoverI_E() {
  atomic_AtomicI_i32E __ret;
  atomic_AtomicI_i32E __obj20;
_L_entry:;
  __obj20 = (atomic_AtomicI_i32E){};
  atomic_AtomicI_i32E_op__initI_E(&__obj20);
  __ret = __obj20;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static User User_op__copy(User * this) {
  User __ret;
  builtins_String * __t0;
_L_entry:;
  __t0 = __smart_ptr_get(this->name);
  __ret = (User){.name = __t0, .age = this->age};
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void User_op__destructor(User * this) {
_L_entry:;
  __smart_ptr_drop(this->name);
  this->name = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void __closure_env21_op__destructor(__closure_env21 * this) {
_L_entry:;
  User_op__destructor(&(this->user));
  memset(&(this->user), 0, sizeof(User));
  goto _L_exit;
_L_exit:;
  return;
}

static builtins_OutputStream * builtins_OutputStream_op__lshiftI_i32E(builtins_OutputStream * this, int32_t val) {
  builtins_OutputStream * __ret;
_L_entry:;
  builtins_OutputStream_appendSignedInt(this, ((int128_t)(val)));
  __ret = this;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void printlnI_i32E(int32_t args) {
_L_entry:;
  builtins_OutputStream_op__lshiftI_i32E(((builtins_OutputStream *)(builtins_stdout)), args);
  builtins_OutputStream_op__lshiftI_wE(((builtins_OutputStream *)(builtins_stdout)), '\n');
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body22(__closure_env21 * this) {
  int32_t __ret;
_L_entry:;
  printlnI_i32E(this->user.age);
  atomic_AtomicI_i32E_store(&detachedDone, ((int32_t)(1)), atomic_MemoryOrder_SeqCst);
  __ret = 0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_void detached(User user) {
  Result_void __ret;
  __closure_env21 __t2;
  User __copy0;
  __closure_env21 * __env3;
  Closure_888 __t4;
  Result_Thread __call_res_5;
  builtins_Exception * __mv1;
_L_entry:;
  __copy0 = User_op__copy(&(user));
  __t2 = (__closure_env21){.user = __copy0};
  __env3 = __mm_alloc(&__ti___closure_env21, nullptr);
  (*(__env3)) = __t2;
  __t4 = (Closure_888){ .env = __env3, .fn = __closure_body22, .dtor = __smart_ptr_drop };
  __call_res_5 = thread_launch(__t4);
  if (__call_res_5.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_5.val._1, "main", "", "detached", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/32_launch_env_ownership.cxy", 16ULL);
  __mv1 = __call_res_5.val._1;
  __call_res_5.val._1 = nullptr;
  __ret.val = __mv1;
  goto _L_exit;
_L_call_cont_1:;
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  User_op__destructor(&(user));
  return __ret;
}

static void __closure_env23_op__destructor(__closure_env23 * this) {
_L_entry:;
  User_op__destructor(&(this->user));
  memset(&(this->user), 0, sizeof(User));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body24(__closure_env23 * this) {
  int32_t __ret;
_L_entry:;
  printlnI_i32E(this->user.age);
  __ret = 0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Result_void joined(User user) {
  Result_void __ret;
  thread_Thread t;
  __closure_env23 __t7;
  User __copy2;
  __closure_env23 * __env8;
  Closure_888 __t9;
  Result_Thread __call_res_10;
  builtins_Exception * __mv3;
_L_entry:;
  __copy2 = User_op__copy(&(user));
  __t7 = (__closure_env23){.user = __copy2};
  __env8 = __mm_alloc(&__ti___closure_env23, nullptr);
  (*(__env8)) = __t7;
  __t9 = (Closure_888){ .env = __env8, .fn = __closure_body24, .dtor = __smart_ptr_drop };
  __call_res_10 = thread_launch(__t9);
  if (__call_res_10.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_10.val._1, "main", "", "joined", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/32_launch_env_ownership.cxy", 23ULL);
  __mv3 = __call_res_10.val._1;
  __call_res_10.val._1 = nullptr;
  __ret.val = __mv3;
  goto _L_exit;
_L_call_cont_1:;
  t = __call_res_10.val._0;
  thread_Thread_join(&t);
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  User_op__destructor(&(user));
  return __ret;
}

static Result_void _main() {
  Result_void __ret;
  builtins_String * __t11;
  User __t12;
  Result_void __call_res_13;
  builtins_Exception * __mv4;
  int32_t __t14;
  builtins_String * __t15;
  User __t16;
  Result_void __call_res_17;
  builtins_Exception * __mv5;
_L_entry:;
  __t11 = thread_String_op__initI_sECoverI_sE("a");
  __t12 = (User){.name = __t11, .age = 1};
  __call_res_13 = detached(__t12);
  if (__call_res_13.tag == 1U) goto _L_call_err_0; else goto _L_call_cont_1;
_L_call_err_0:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_13.val, "main", "", "main", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/32_launch_env_ownership.cxy", 30ULL);
  __mv4 = __call_res_13.val;
  __call_res_13.val = nullptr;
  __ret.val = __mv4;
  goto _L_exit;
_L_call_cont_1:;
  goto _L_WHILE0_cond;
_L_WHILE0_cond:;
  __t14 = atomic_AtomicI_i32E_load(&detachedDone, atomic_MemoryOrder_SeqCst);
  if (__t14 == 0) goto _L_WHILE0_cond; else goto _L_WHILE0_exit;
_L_WHILE0_exit:;
  __t15 = thread_String_op__initI_sECoverI_sE("b");
  __t16 = (User){.name = __t15, .age = 2};
  __call_res_17 = joined(__t16);
  if (__call_res_17.tag == 1U) goto _L_call_err_1; else goto _L_call_cont_2;
_L_call_err_1:;
  __ret.tag = 1U;
  builtins_Exception_push(__call_res_17.val, "main", "", "main", "/Users/dc/projects/mono-cxy/cxy/tests/lang/mem/32_launch_env_ownership.cxy", 32ULL);
  __mv5 = __call_res_17.val;
  __call_res_17.val = nullptr;
  __ret.val = __mv5;
  goto _L_exit;
_L_call_cont_2:;
  __ret.tag = 0U;
  goto _L_exit;
_L_exit:;
  return __ret;
}

__attribute__((constructor)) static void __init() {
_L_entry:;
  detachedDone = atomic_AtomicI_i32E_op__initCoverI_E();
  goto _L_exit;
_L_exit:;
  return;
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

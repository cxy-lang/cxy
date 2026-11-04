typedef struct builtins_OutputStream_VTable builtins_OutputStream_VTable;
typedef struct builtins_OutputStream builtins_OutputStream;
typedef struct Optional_u64 Optional_u64;
typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_String builtins_String;
typedef struct builtins_StdOutputStream builtins_StdOutputStream;

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

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
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

static void builtins_panicOnUnhandledException(builtins_Exception * ex);
static void builtins_OutputStream_appendStringI_sE(builtins_OutputStream * this, char * s);
static void builtins_OutputStream___defaults_init(builtins_OutputStream * this);
static uint64_t builtins_maxI_u64E(uint64_t a, uint64_t b);
static builtins_String * builtins_String_op__initCoverI_Pccu64E(char * str, uint64_t len);
static void builtins_String_grow(builtins_String * this, uint64_t growSize);
static void builtins_String_op__initI_sE(builtins_String * this, char * str);
static void builtins_String_op__initI_Pccu64E(builtins_String * this, char * str, uint64_t len);
static void builtins_String_op__deinit(builtins_String * this);
static void builtins_String_clear(builtins_String * this);
static Optional_u64 builtins_String_append(builtins_String * this, char * str, uint64_t size);
static uint64_t builtins_String_size(builtins_String * this);
static builtins_String * builtins_String_clone(builtins_String * this);
static void builtins_String_op__destructor(builtins_String * this);
static void builtins_String_op__destructor_fwd(void * ptr);
static void builtins_String___defaults_init(builtins_String * this);
static void builtins_StdOutputStream_op__init(builtins_StdOutputStream * this, int32_t fd);
static Optional_u64 builtins_StdOutputStream_append(builtins_StdOutputStream * this, char * str, uint64_t size);
static void builtins_StdOutputStream_op__destructor(builtins_StdOutputStream * this);
static void builtins_StdOutputStream_op__destructor_fwd(void * ptr);
static void builtins_StdOutputStream___defaults_init(builtins_StdOutputStream * this);
static builtins_StdOutputStream * builtins_StdOutputStream_op__initCover(int32_t fd);
static builtins_OutputStream * builtins_OutputStream_op__lshiftI_sE(builtins_OutputStream * this, char * val);
__attribute__((constructor)) static void builtins___init();
static void builtins___fini();

static builtins_String * builtins_String_op__initI_sECoverI_sE(char * str);
static void _main();

static const __TypeInfo __ti_builtins_String = { .name = "builtins.String", .size = sizeof(builtins_String), .align = _Alignof(builtins_String), .index = 0, .dropFields = builtins_String_op__destructor_fwd };
static const __TypeInfo __ti_builtins_StdOutputStream = { .name = "builtins.StdOutputStream", .size = sizeof(builtins_StdOutputStream), .align = _Alignof(builtins_StdOutputStream), .index = 1, .dropFields = builtins_StdOutputStream_op__destructor_fwd };

builtins_OutputStream_VTable builtins_OutputStream_vTable = (builtins_OutputStream_VTable){.__tid = 189ULL, .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(nullptr))};
builtins_OutputStream_VTable builtins_String_vTable = (builtins_OutputStream_VTable){.__tid = ((uint64_t)(211)), .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(builtins_String_append))};
builtins_OutputStream_VTable builtins_StdOutputStream_vTable = (builtins_OutputStream_VTable){.__tid = ((uint64_t)(308)), .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(builtins_StdOutputStream_append))};
builtins_OutputStream * builtins_stdout;
builtins_OutputStream * builtins_stderr;
int64_t builtins_SysConfPageSize;
int64_t builtins_SysConfNumProcs;

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

static void builtins_OutputStream___defaults_init(builtins_OutputStream * this) {
_L_entry:;
  this->vtable = &builtins_OutputStream_vTable;
  goto _L_exit;
_L_exit:;
  return;
}

static uint64_t builtins_maxI_u64E(uint64_t a, uint64_t b) {
  uint64_t __ret;
  uint64_t _if9;
_L_entry:;
  if (a > b) goto _L_IF0_then; else goto _L_IF0_else;
_L_IF0_then:;
  _if9 = a;
  goto _L_IF0_end;
_L_IF0_else:;
  _if9 = b;
  goto _L_IF0_end;
_L_IF0_end:;
  __ret = _if9;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static builtins_String * builtins_String_op__initCoverI_Pccu64E(char * str, uint64_t len) {
  builtins_String * __ret;
  builtins_String * __obj6;
  builtins_String * __t46;
_L_entry:;
  __t46 = __mm_alloc(&__ti_builtins_String, nullptr);
  __obj6 = ((builtins_String *)(__t46));
  memset(__obj6, 0, sizeof(builtins_String));
  builtins_String___defaults_init(((builtins_String *)(__obj6)));
  builtins_String_op__initI_Pccu64E(((builtins_String *)(__obj6)), str, len);
  __ret = __obj6;
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
  __cxy_panic("__builtins.cxy", 737ULL, 17ULL, "failed to grow string buffer");
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

static void builtins_String_op__initI_Pccu64E(builtins_String * this, char * str, uint64_t len) {
_L_entry:;
  builtins_String_append(this, str, len);
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

static uint64_t builtins_String_size(builtins_String * this) {
  uint64_t __ret;
_L_entry:;
  __ret = this->_size;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static builtins_String * builtins_String_clone(builtins_String * this) {
  builtins_String * __ret;
_L_entry:;
  __ret = builtins_String_op__initCoverI_Pccu64E(((char *)(this->_data)), this->_size);
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

static builtins_OutputStream * builtins_OutputStream_op__lshiftI_sE(builtins_OutputStream * this, char * val) {
  builtins_OutputStream * __ret;
_L_entry:;
  builtins_OutputStream_appendStringI_sE(this, val);
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

static builtins_String * builtins_String_op__initI_sECoverI_sE(char * str) {
  builtins_String * __ret;
  builtins_String * __obj13;
  builtins_String * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_builtins_String, nullptr);
  __obj13 = ((builtins_String *)(__t0));
  memset(__obj13, 0, sizeof(builtins_String));
  builtins_String___defaults_init(((builtins_String *)(__obj13)));
  builtins_String_op__initI_sE(((builtins_String *)(__obj13)), str);
  __ret = __obj13;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  builtins_String * a;
  builtins_String * shared;
  builtins_String * cloned;
  uint64_t __t1;
  uint64_t __t2;
_L_entry:;
  a = builtins_String_op__initI_sECoverI_sE("hello");
  shared = __smart_ptr_get(a);
  cloned = builtins_String_clone(((builtins_String *)(a)));
  builtins_OutputStream_op__lshiftI_sE(((builtins_OutputStream *)(((builtins_String *)(a)))), " world");
  __t1 = builtins_String_size(((builtins_String *)(shared)));
  __cxy_assert((__t1 == 11), "shared sees the change", ((uint64_t)(0)), ((uint64_t)(0)));
  __t2 = builtins_String_size(((builtins_String *)(cloned)));
  __cxy_assert((__t2 == 5), "clone is independent", ((uint64_t)(0)), ((uint64_t)(0)));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(cloned);
  __smart_ptr_drop(shared);
  __smart_ptr_drop(a);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

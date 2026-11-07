typedef struct builtins_OutputStream_VTable builtins_OutputStream_VTable;
typedef struct builtins_OutputStream builtins_OutputStream;
typedef struct Optional_u64 Optional_u64;
typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct builtins_StdOutputStream builtins_StdOutputStream;
typedef struct Node Node;

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

struct builtins_StdOutputStream {
  builtins_OutputStream_VTable * vtable;
  int32_t fd;
};

struct Node {
  int32_t id;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);
static void builtins_OutputStream_appendStringI_sE(builtins_OutputStream * this, char * s);
static void builtins_OutputStream_appendSignedInt(builtins_OutputStream * this, int128_t num);
static void builtins_OutputStream_appendChar(builtins_OutputStream * this, int32_t ch);
static void builtins_OutputStream___defaults_init(builtins_OutputStream * this);
static void builtins_StdOutputStream_op__init(builtins_StdOutputStream * this, int32_t fd);
static Optional_u64 builtins_StdOutputStream_append(builtins_StdOutputStream * this, char * str, uint64_t size);
static void builtins_StdOutputStream_op__destructor(builtins_StdOutputStream * this);
static void builtins_StdOutputStream_op__destructor_fwd(void * ptr);
static void builtins_StdOutputStream___defaults_init(builtins_StdOutputStream * this);
static builtins_StdOutputStream * builtins_StdOutputStream_op__initCover(int32_t fd);
static builtins_OutputStream * builtins_OutputStream_op__lshiftI_sE(builtins_OutputStream * this, char * val);
static builtins_OutputStream * builtins_OutputStream_op__lshiftI_wE(builtins_OutputStream * this, int32_t val);
__attribute__((constructor)) static void builtins___init();
static void builtins___fini();

static builtins_OutputStream * builtins_OutputStream_op__lshiftI_i32E(builtins_OutputStream * this, int32_t val);
static void printlnI_si32sE(char * args, int32_t args1, char * args2);
static void Node_op__init(Node * this, int32_t id);
static void Node_op__deinit(Node * this);
static void Node_op__destructor(Node * this);
static void Node_op__destructor_fwd(void * ptr);
static Node * Node_op__initCover(int32_t id);
static void printlnI_si32si32E(char * args, int32_t args1, char * args2, int32_t args3);
static void _main();
__attribute__((constructor)) static void __init();
static void __fini();
static void __tfini();

static const __TypeInfo __ti_builtins_StdOutputStream = { .name = "builtins.StdOutputStream", .size = sizeof(builtins_StdOutputStream), .align = _Alignof(builtins_StdOutputStream), .index = 0, .dropFields = builtins_StdOutputStream_op__destructor_fwd };
static const __TypeInfo __ti_Node = { .name = "Node", .size = sizeof(Node), .align = _Alignof(Node), .index = 1, .dropFields = Node_op__destructor_fwd };

builtins_OutputStream_VTable builtins_OutputStream_vTable = (builtins_OutputStream_VTable){.__tid = 194ULL, .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(nullptr))};
builtins_OutputStream_VTable builtins_StdOutputStream_vTable = (builtins_OutputStream_VTable){.__tid = ((uint64_t)(313)), .append = ((Optional_u64 (*)(builtins_OutputStream *, void *, uint64_t))(builtins_StdOutputStream_append))};
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
  uint64_t __t48;
_L_entry:;
  if (s != nullptr) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  __t48 = strlen(s);
  this->vtable->append(this, ((void *)(s)), __t48);
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
  builtins_StdOutputStream * __t108;
_L_entry:;
  __t108 = __mm_alloc(&__ti_builtins_StdOutputStream, nullptr);
  __obj7 = ((builtins_StdOutputStream *)(__t108));
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

Node * shared;
__thread Node * local;

static builtins_OutputStream * builtins_OutputStream_op__lshiftI_i32E(builtins_OutputStream * this, int32_t val) {
  builtins_OutputStream * __ret;
_L_entry:;
  builtins_OutputStream_appendSignedInt(this, ((int128_t)(val)));
  __ret = this;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void printlnI_si32sE(char * args, int32_t args1, char * args2) {
_L_entry:;
  builtins_OutputStream_op__lshiftI_sE(((builtins_OutputStream *)(builtins_stdout)), args);
  builtins_OutputStream_op__lshiftI_i32E(((builtins_OutputStream *)(builtins_stdout)), args1);
  builtins_OutputStream_op__lshiftI_sE(((builtins_OutputStream *)(builtins_stdout)), args2);
  builtins_OutputStream_op__lshiftI_wE(((builtins_OutputStream *)(builtins_stdout)), '\n');
  goto _L_exit;
_L_exit:;
  return;
}

static void Node_op__init(Node * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void Node_op__deinit(Node * this) {
_L_entry:;
  printlnI_si32sE("node ", this->id, " destroyed");
  goto _L_exit;
_L_exit:;
  return;
}

static void Node_op__destructor(Node * this) {
_L_entry:;
  Node_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void Node_op__destructor_fwd(void * ptr) {
_L_entry:;
  Node_op__destructor(((Node *)(((Node *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static Node * Node_op__initCover(int32_t id) {
  Node * __ret;
  Node * __obj12;
  Node * __t0;
_L_entry:;
  __t0 = __mm_alloc(&__ti_Node, nullptr);
  __obj12 = ((Node *)(__t0));
  memset(__obj12, 0, sizeof(Node));
  Node_op__init(((Node *)(__obj12)), id);
  __ret = __obj12;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void printlnI_si32si32E(char * args, int32_t args1, char * args2, int32_t args3) {
_L_entry:;
  builtins_OutputStream_op__lshiftI_sE(((builtins_OutputStream *)(builtins_stdout)), args);
  builtins_OutputStream_op__lshiftI_i32E(((builtins_OutputStream *)(builtins_stdout)), args1);
  builtins_OutputStream_op__lshiftI_sE(((builtins_OutputStream *)(builtins_stdout)), args2);
  builtins_OutputStream_op__lshiftI_i32E(((builtins_OutputStream *)(builtins_stdout)), args3);
  builtins_OutputStream_op__lshiftI_wE(((builtins_OutputStream *)(builtins_stdout)), '\n');
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
_L_entry:;
  printlnI_si32si32E("main ", shared->id, " ", local->id);
  goto _L_exit;
_L_exit:;
  return;
}

__attribute__((constructor)) static void __init() {
_L_entry:;
  shared = Node_op__initCover(((int32_t)(1)));
  local = Node_op__initCover(((int32_t)(2)));
  goto _L_exit;
_L_exit:;
  return;
}

static void __fini() {
_L_entry:;
  __smart_ptr_drop(shared);
  shared = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

__attribute__((constructor)) static void __fini_reg(void) { __cxy_register_process_fini(__fini); }

static void __tfini() {
_L_entry:;
  __smart_ptr_drop(local);
  local = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

__attribute__((constructor)) static void __tfini_reg(void) { __cxy_register_thread_fini(__tfini); }

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct __closure_env4 __closure_env4;
typedef struct Closure_143 Closure_143;
typedef struct __closure_env6 __closure_env6;
typedef struct X X;
typedef struct __closure_env9 __closure_env9;
typedef struct Closure_165 Closure_165;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Resource {
  int32_t id;
};

struct __closure_env4 {
  Resource * r;
};

struct Closure_143 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env6 {
  Resource * r;
};

struct X {
  char * s;
};

struct __closure_env9 {
  X * s;
};

struct Closure_165 {
  void * env;
  uint64_t (*fn)(void *);
  void (*dtor)(void *);
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static Resource * Resource_op__initCover(int32_t id);
static void __closure_env4_op__destructor(__closure_env4 * this);
static int32_t __closure_body5(__closure_env4 * this);
static void testCaptureClassByValue();
static void __closure_env6_op__destructor(__closure_env6 * this);
static int32_t __closure_body7(__closure_env6 * this);
static void testCaptureClassByMove();
static void X_op__init(X * this, char * s);
static X * X_op__initCover(char * s);
static void __closure_env9_op__destructor(__closure_env9 * this);
static uint64_t __closure_body10(__closure_env9 * this);
static void testCaptureString();
static void _main();

static const __TypeInfo __ti_Resource = { .name = "Resource", .size = sizeof(Resource), .align = _Alignof(Resource), .dropFields = Resource_op__destructor_fwd };
static const __TypeInfo __ti_X = { .name = "X", .size = sizeof(X), .align = _Alignof(X), .dropFields = nullptr };


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

static void __closure_env4_op__destructor(__closure_env4 * this) {
_L_entry:;
  __smart_ptr_drop(this->r);
  this->r = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body5(__closure_env4 * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->r->id;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCaptureClassByValue() {
  Resource * r;
  Closure_143 c;
  __closure_env4 __t2;
  Resource * __copy0;
  __closure_env4 __env3;
_L_entry:;
  r = Resource_op__initCover(((int32_t)(1)));
  __copy0 = __smart_ptr_get(r);
  __t2 = (__closure_env4){.r = __copy0};
  __env3 = __t2;
  c = (Closure_143){ .env = &__env3, .fn = __closure_body5, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  __closure_env4_op__destructor(&(__env3));
  __smart_ptr_drop(r);
  return;
}

static void __closure_env6_op__destructor(__closure_env6 * this) {
_L_entry:;
  __smart_ptr_drop(this->r);
  this->r = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body7(__closure_env6 * this) {
  int32_t __ret;
_L_entry:;
  __ret = this->r->id;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCaptureClassByMove() {
  Resource * r;
  Closure_143 c;
  __closure_env6 __t5;
  __closure_env6 __env6;
_L_entry:;
  r = Resource_op__initCover(((int32_t)(2)));
  __t5 = (__closure_env6){.r = r};
  __env6 = __t5;
  c = (Closure_143){ .env = &__env6, .fn = __closure_body7, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  __closure_env6_op__destructor(&(__env6));
  return;
}

static void X_op__init(X * this, char * s) {
_L_entry:;
  this->s = s;
  goto _L_exit;
_L_exit:;
  return;
}

static X * X_op__initCover(char * s) {
  X * __ret;
  X * __obj8;
  X * __t7;
_L_entry:;
  __t7 = __mm_alloc(&__ti_X, nullptr);
  __obj8 = ((X *)(__t7));
  memset(__obj8, 0, sizeof(X));
  X_op__init(((X *)(__obj8)), s);
  __ret = __obj8;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_env9_op__destructor(__closure_env9 * this) {
_L_entry:;
  __smart_ptr_drop(this->s);
  this->s = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static uint64_t __closure_body10(__closure_env9 * this) {
  uint64_t __ret;
_L_entry:;
  __ret = strlen(this->s->s);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCaptureString() {
  X * s;
  Closure_165 c;
  __closure_env9 __t9;
  X * __copy1;
  __closure_env9 __env10;
_L_entry:;
  s = X_op__initCover("Hello");
  __copy1 = __smart_ptr_get(s);
  __t9 = (__closure_env9){.s = __copy1};
  __env10 = __t9;
  c = (Closure_165){ .env = &__env10, .fn = __closure_body10, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  __closure_env9_op__destructor(&(__env10));
  __smart_ptr_drop(s);
  return;
}

static void _main() {
_L_entry:;
  testCaptureClassByValue();
  testCaptureClassByMove();
  testCaptureString();
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

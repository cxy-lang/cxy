typedef struct Resource Resource;
typedef struct __closure_env1 __closure_env1;
typedef struct Closure_39 Closure_39;
typedef struct __closure_env3 __closure_env3;
typedef struct X X;
typedef struct __closure_env6 __closure_env6;
typedef struct Closure_62 Closure_62;

struct Resource {
  int32_t id;
};

struct __closure_env1 {
  Resource * r;
};

struct Closure_39 {
  void * env;
  int32_t (*fn)(void *);
  void (*dtor)(void *);
};

struct __closure_env3 {
  Resource * r;
};

struct X {
  char * s;
};

struct __closure_env6 {
  X * s;
};

struct Closure_62 {
  void * env;
  uint64_t (*fn)(void *);
  void (*dtor)(void *);
};

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static Resource * Resource_op__initCover(int32_t id);
static void __closure_env1_op__destructor(__closure_env1 * this);
static int32_t __closure_body2(__closure_env1 * this);
static void testCaptureClassByValue();
static void __closure_env3_op__destructor(__closure_env3 * this);
static int32_t __closure_body4(__closure_env3 * this);
static void testCaptureClassByMove();
static void X_op__init(X * this, char * s);
static X * X_op__initCover(char * s);
static void __closure_env6_op__destructor(__closure_env6 * this);
static uint64_t __closure_body7(__closure_env6 * this);
static void testCaptureString();
static void _main();

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

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj0;
  Resource * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj0 = ((Resource *)(__t0));
  memset(__obj0, 0, sizeof(Resource));
  Resource_op__init(__obj0, id);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_env1_op__destructor(__closure_env1 * this) {
_L_entry:;
  __smart_ptr_drop(this->r);
  memset(this->r, 0, sizeof(Resource));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body2(__closure_env1 * this) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCaptureClassByValue() {
  Resource * r;
  Closure_39 c;
  __closure_env1 __t2;
  Resource * __copy0;
  __closure_env1 __env3;
_L_entry:;
  r = Resource_op__initCover(((int32_t)(1)));
  __copy0 = __smart_ptr_get(r);
  __t2 = (__closure_env1){__copy0};
  __env3 = __t2;
  c = (Closure_39){ .env = &__env3, .fn = __closure_body2, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  __closure_env1_op__destructor(&(__env3));
  __smart_ptr_drop(r);
  return;
}

static void __closure_env3_op__destructor(__closure_env3 * this) {
_L_entry:;
  __smart_ptr_drop(this->r);
  memset(this->r, 0, sizeof(Resource));
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t __closure_body4(__closure_env3 * this) {
  int32_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCaptureClassByMove() {
  Resource * r;
  Closure_39 c;
  __closure_env3 __t5;
  __closure_env3 __env6;
_L_entry:;
  r = Resource_op__initCover(((int32_t)(2)));
  __t5 = (__closure_env3){r};
  __env6 = __t5;
  c = (Closure_39){ .env = &__env6, .fn = __closure_body4, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  __closure_env3_op__destructor(&(__env6));
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
  X * __obj5;
  X * __t7;
_L_entry:;
  __t7 = __smart_ptr_alloc(sizeof(X), nullptr);
  __obj5 = ((X *)(__t7));
  memset(__obj5, 0, sizeof(X));
  X_op__init(__obj5, s);
  __ret = __obj5;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_env6_op__destructor(__closure_env6 * this) {
_L_entry:;
  __smart_ptr_drop(this->s);
  memset(this->s, 0, sizeof(X));
  goto _L_exit;
_L_exit:;
  return;
}

static uint64_t __closure_body7(__closure_env6 * this) {
  uint64_t __ret;
_L_entry:;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testCaptureString() {
  X * s;
  Closure_62 c;
  __closure_env6 __t9;
  X * __copy1;
  __closure_env6 __env10;
_L_entry:;
  s = X_op__initCover("Hello");
  __copy1 = __smart_ptr_get(s);
  __t9 = (__closure_env6){__copy1};
  __env10 = __t9;
  c = (Closure_62){ .env = &__env10, .fn = __closure_body7, .dtor = nullptr };
  c.fn(c.env);
  goto _L_exit;
_L_exit:;
  __closure_env6_op__destructor(&(__env10));
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

typedef struct Closure_24 Closure_24;
typedef struct Storage Storage;

struct Closure_24 {
  void * env;
  void (*fn)(void *);
  void (*dtor)(void *);
};

struct Storage {
  Closure_24 cb;
};

static void Storage_op__init(Storage * this, Closure_24 cb);
static void Storage_op__destructor(Storage * this);
static void Storage_op__destructor_fwd(void * ptr);
static Storage * Storage_op__initCover(Closure_24 cb);
static Storage * storeCallback(Closure_24 cb);
static void __closure_body1(void * this);
static void _main();

static void Storage_op__init(Storage * this, Closure_24 cb) {
_L_entry:;
  this->cb = cb;
  goto _L_exit;
_L_exit:;
  return;
}

static void Storage_op__destructor(Storage * this) {
_L_entry:;
  if (this->cb.dtor) this->cb.dtor(this->cb.env);
  memset(&this->cb, 0, sizeof(Closure_24));
  goto _L_exit;
_L_exit:;
  return;
}

static void Storage_op__destructor_fwd(void * ptr) {
_L_entry:;
  Storage_op__destructor(((Storage *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static Storage * Storage_op__initCover(Closure_24 cb) {
  Storage * __ret;
  Storage * __obj0;
  Storage * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Storage), Storage_op__destructor_fwd);
  __obj0 = ((Storage *)(__t0));
  memset(__obj0, 0, sizeof(Storage));
  Storage_op__init(__obj0, cb);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Storage * storeCallback(Closure_24 cb) {
  Storage * __ret;
_L_entry:;
  __ret = Storage_op__initCover(cb);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __closure_body1(void * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
  int8_t x;
  Storage * s;
  Closure_24 __t1;
_L_entry:;
  x = 100;
  __t1 = (Closure_24){ .env = nullptr, .fn = __closure_body1, .dtor = nullptr };
  s = storeCallback(__t1);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(s);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

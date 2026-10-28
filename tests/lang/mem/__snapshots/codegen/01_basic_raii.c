typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Item Item;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Item {
  int32_t id;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Item_op__init(Item * this, int32_t id);
static void Item_op__deinit(Item * this);
static void Item_op__destructor(Item * this);
static void Item_op__destructor_fwd(void * ptr);
static Item * Item_op__initCover(int32_t id);
static void takeItem(Item * item);
static void _main();


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

static void Item_op__init(Item * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void Item_op__deinit(Item * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void Item_op__destructor(Item * this) {
_L_entry:;
  Item_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void Item_op__destructor_fwd(void * ptr) {
_L_entry:;
  Item_op__destructor(((Item *)(((Item *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static Item * Item_op__initCover(int32_t id) {
  Item * __ret;
  Item * __obj3;
  Item * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Item), Item_op__destructor_fwd);
  __obj3 = ((Item *)(__t0));
  memset(__obj3, 0, sizeof(Item));
  Item_op__init(((Item *)(__obj3)), id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void takeItem(Item * item) {
  Item * local;
_L_entry:;
  local = Item_op__initCover((item->id + 1));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(local);
  __smart_ptr_drop(item);
  return;
}

static void _main() {
  Item * a;
  Item * __copy0;
_L_entry:;
  a = Item_op__initCover(((int32_t)(10)));
  __copy0 = __smart_ptr_get(a);
  takeItem(__copy0);
  goto _L_exit;
_L_exit:;
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

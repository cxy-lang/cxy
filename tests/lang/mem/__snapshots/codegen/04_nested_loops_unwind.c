typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Item Item;
typedef struct __drop_flags_nested_t __drop_flags_nested_t;

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

struct __drop_flags_nested_t {
  uint64_t outer;
  uint64_t inner;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Item_op__init(Item * this, int32_t id);
static void Item_op__deinit(Item * this);
static void Item_op__destructor(Item * this);
static void Item_op__destructor_fwd(void * ptr);
static Item * Item_op__initCover(int32_t id);
static void nested(bool condBreak, bool condRet);
static void _main();


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
  Item_op__destructor(((Item *)(ptr)));
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
  Item_op__init(__obj3, id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void nested(bool condBreak, bool condRet) {
  __drop_flags_nested_t __drop_flags;
  int8_t i;
  Item * outer;
  int8_t j;
  Item * inner;
  bool __cond1;
  bool __cond2;
  bool __cond3;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_nested_t));
  i = 0;
  goto _L_FOR0_cond;
_L_FOR0_cond:;
  if (i < 3) goto _L_FOR0_body; else goto _L_exit;
_L_FOR0_body:;
  if (__drop_flags.outer) __smart_ptr_drop(outer);
  outer = Item_op__initCover(((int32_t)(i)));
  __drop_flags.outer = 1;
  j = 0;
  goto _L_FOR1_cond;
_L_FOR0_update:;
  i = (i + 1);
  goto _L_FOR0_cond;
_L_FOR1_cond:;
  if (j < 3) goto _L_FOR1_body; else goto _L_FOR1_exit;
_L_FOR1_body:;
  if (__drop_flags.inner) __smart_ptr_drop(inner);
  inner = Item_op__initCover(((int32_t)(((i * 10) + j))));
  __drop_flags.inner = 1;
  if (j == 1) goto _L_AND0_rhs; else goto _L_AND0_false;
_L_FOR1_update:;
  j = (j + 1);
  goto _L_FOR1_cond;
_L_FOR1_exit:;
  __smart_ptr_drop(outer);
  __drop_flags.outer = 0;
  goto _L_FOR0_update;
_L_AND0_rhs:;
  __cond1 = condBreak;
  goto _L_AND0_merge;
_L_AND0_false:;
  __cond1 = false;
  goto _L_AND0_merge;
_L_AND0_merge:;
  if (__cond1) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  __smart_ptr_drop(inner);
  __drop_flags.inner = 0;
  goto _L_FOR1_exit;
_L_IF0_end:;
  if (i == 2) goto _L_AND1_rhs; else goto _L_AND1_false;
_L_AND1_rhs:;
  __cond2 = (j == 2);
  goto _L_AND1_merge;
_L_AND1_false:;
  __cond2 = false;
  goto _L_AND1_merge;
_L_AND1_merge:;
  if (__cond2) goto _L_AND2_rhs; else goto _L_AND2_false;
_L_AND2_rhs:;
  __cond3 = condRet;
  goto _L_AND2_merge;
_L_AND2_false:;
  __cond3 = false;
  goto _L_AND2_merge;
_L_AND2_merge:;
  if (__cond3) goto _L_UNWIND2_return; else goto _L_IF1_end;
_L_IF1_end:;
  __smart_ptr_drop(inner);
  __drop_flags.inner = 0;
  goto _L_FOR1_update;
_L_UNWIND2_return:;
  __smart_ptr_drop(inner);
  __drop_flags.inner = 0;
  goto _L_UNWIND1_return;
_L_UNWIND1_return:;
  __smart_ptr_drop(outer);
  __drop_flags.outer = 0;
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
_L_entry:;
  nested(true, false);
  nested(false, true);
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

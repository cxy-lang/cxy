typedef struct __drop_flags_t __drop_flags_t;
typedef struct Item Item;

struct __drop_flags_t {
  uint64_t outer;
  uint64_t inner;
};

struct Item {
  int32_t id;
};

static void main_Item_op__init(Item * this, int32_t id);
static void main_Item_op__deinit(Item * this);
static void main_Item_op__destructor(Item * this);
static void main_Item_op__destructor_fwd(void * ptr);
static Item * main_Item_op__initCover(int32_t id);
static void main_nested(bool condBreak, bool condRet);
static void _main();

static void main_Item_op__init(Item * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void main_Item_op__deinit(Item * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void main_Item_op__destructor(Item * this) {
_L_entry:;
  main_Item_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void main_Item_op__destructor_fwd(void * ptr) {
_L_entry:;
  main_Item_op__destructor(((Item *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static Item * main_Item_op__initCover(int32_t id) {
  Item * __ret;
  Item * __obj0;
  void * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Item), main_Item_op__destructor_fwd);
  __obj0 = ((Item *)(__t0));
  memset(__obj0, 0, sizeof(Item));
  main_Item_op__init(__obj0, id);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void main_nested(bool condBreak, bool condRet) {
  __drop_flags_t __drop_flags;
  int8_t i;
  Item * outer;
  int8_t j;
  int8_t _a1;
  int8_t __t1;
  Item * inner;
  bool __t2;
  bool __cond3;
  bool __t4;
  bool __cond5;
  bool __t6;
  bool __cond7;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_t));
  i = 0;
  goto _L_FOR0_cond;
_L_exit:;
  return;
_L_FOR0_cond:;
  if ((i < 3)) goto _L_FOR0_body; else goto _L_exit;
_L_FOR0_body:;
  outer = main_Item_op__initCover(((int32_t)(i)));
  __drop_flags.outer = 1;
  j = 0;
  goto _L_FOR1_cond;
_L_FOR0_update:;
  i = (i + 1);
  goto _L_FOR0_cond;
_L_FOR1_cond:;
  if ((j < 3)) goto _L_FOR1_body; else goto _L_FOR1_exit;
_L_FOR1_body:;
  __t1 = (i * 10);
  _a1 = (__t1 + j);
  inner = main_Item_op__initCover(((int32_t)(_a1)));
  __drop_flags.inner = 1;
  __t2 = (j == 1);
  if (__t2) goto _L_AND0_rhs; else goto _L_AND0_false;
_L_FOR1_update:;
  j = (j + 1);
  goto _L_FOR1_cond;
_L_FOR1_exit:;
  if (__drop_flags.outer) __smart_ptr_drop(outer);
  goto _L_FOR0_update;
_L_AND0_rhs:;
  __cond3 = condBreak;
  goto _L_AND0_merge;
_L_AND0_false:;
  __cond3 = false;
  goto _L_AND0_merge;
_L_AND0_merge:;
  if (__cond3) goto _L_IF0_then; else goto _L_IF0_end;
_L_IF0_then:;
  if (__drop_flags.inner) __smart_ptr_drop(inner);
  goto _L_FOR1_exit;
_L_IF0_end:;
  __t4 = (i == 2);
  if (__t4) goto _L_AND1_rhs; else goto _L_AND1_false;
_L_AND1_rhs:;
  __t6 = (j == 2);
  __cond5 = __t6;
  goto _L_AND1_merge;
_L_AND1_false:;
  __cond5 = false;
  goto _L_AND1_merge;
_L_AND1_merge:;
  if (__cond5) goto _L_AND2_rhs; else goto _L_AND2_false;
_L_AND2_rhs:;
  __cond7 = condRet;
  goto _L_AND2_merge;
_L_AND2_false:;
  __cond7 = false;
  goto _L_AND2_merge;
_L_AND2_merge:;
  if (__cond7) goto _L_UNWIND2_return; else goto _L_IF1_end;
_L_IF1_end:;
  if (__drop_flags.inner) __smart_ptr_drop(inner);
  goto _L_FOR1_update;
_L_UNWIND2_return:;
  if (__drop_flags.inner) __smart_ptr_drop(inner);
  goto _L_UNWIND1_return;
_L_UNWIND1_return:;
  if (__drop_flags.outer) __smart_ptr_drop(outer);
  goto _L_exit;
}

static void _main() {
_L_entry:;
  main_nested(true, false);
  main_nested(false, true);
  goto _L_exit;
_L_exit:;
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  _main();
  return 0;
}

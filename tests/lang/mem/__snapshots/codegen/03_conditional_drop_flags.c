typedef struct Item Item;
typedef struct __drop_flags_testBranch_t __drop_flags_testBranch_t;

struct Item {
  int32_t id;
};

struct __drop_flags_testBranch_t {
  uint64_t x;
};

static void Item_op__init(Item * this, int32_t id);
static void Item_op__deinit(Item * this);
static void Item_op__destructor(Item * this);
static void Item_op__destructor_fwd(void * ptr);
static Item * Item_op__initCover(int32_t id);
static void testBranch(bool cond);
static void _main();

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
  Item * __obj0;
  Item * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Item), Item_op__destructor_fwd);
  __obj0 = ((Item *)(__t0));
  memset(__obj0, 0, sizeof(Item));
  Item_op__init(__obj0, id);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testBranch(bool cond) {
  __drop_flags_testBranch_t __drop_flags;
  Item * x;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testBranch_t));
  if (cond) goto _L_IF0_then; else goto _L_exit;
_L_IF0_then:;
  x = Item_op__initCover(((int32_t)(100)));
  __drop_flags.x = 1;
  goto _L_exit;
_L_exit:;
  if (__drop_flags.x) __smart_ptr_drop(x);
  __drop_flags.x = 0;
  return;
}

static void _main() {
_L_entry:;
  testBranch(true);
  testBranch(false);
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

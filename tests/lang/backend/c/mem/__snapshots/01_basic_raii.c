typedef struct Item Item;

struct Item {
  int32_t id;
};

static void Item_op__init(Item * this, int32_t id);
static void Item_op__deinit(Item * this);
static void Item_op__destructor(Item * this);
static void Item_op__destructor_fwd(void * ptr);
static Item * Item_op__initCover(int32_t id);
static void takeItem(Item * item);
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
  void * __t0;
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

static void takeItem(Item * item) {
  int32_t _a1;
  Item * local;
_L_entry:;
  _a1 = (item->id + 1);
  local = Item_op__initCover(_a1);
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
  _main();
  return 0;
}

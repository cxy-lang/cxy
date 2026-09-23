typedef struct Item Item;

struct Item {
  int32_t id;
};

static void main_Item_op__init(Item * this, int32_t id);
static void main_Item_op__deinit(Item * this);
static void main_Item_op__destructor(Item * this);
static void main_Item_op__destructor_fwd(void * ptr);
static Item * main_Item_op__initCover(int32_t id);
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

static void _main() {
  Item * a;
_L_entry:;
  a = main_Item_op__initCover(((int32_t)(1)));
  __smart_ptr_drop(a);
  a = main_Item_op__initCover(((int32_t)(2)));
  __smart_ptr_drop(a);
  a = main_Item_op__initCover(((int32_t)(3)));
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

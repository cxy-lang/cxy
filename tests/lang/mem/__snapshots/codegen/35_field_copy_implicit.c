typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Node Node;
typedef struct Box Box;
typedef struct Pair Pair;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Node {
  int32_t id;
};

struct Box {
  Node * node;
};

struct Pair {
  Node * first;
  Node * second;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Node_op__init(Node * this, int32_t id);
static void Node_op__deinit(Node * this);
static void Node_op__destructor(Node * this);
static void Node_op__destructor_fwd(void * ptr);
static void Box_op__init(Box * this, Node * n);
static Node * Box_get(Box * this);
static void Box_op__destructor(Box * this);
static void Box_op__destructor_fwd(void * ptr);
static void Pair_op__destructor(Pair * this);
static Node * Node_op__initCover(int32_t id);
static Box * Box_op__initCover(Node * n);
static void _main();

static const __TypeInfo __ti_Node = { .name = "Node", .size = sizeof(Node), .align = _Alignof(Node), .index = 0, .dropFields = Node_op__destructor_fwd };
static const __TypeInfo __ti_Box = { .name = "Box", .size = sizeof(Box), .align = _Alignof(Box), .index = 1, .dropFields = Box_op__destructor_fwd };


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

static void Node_op__init(Node * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void Node_op__deinit(Node * this) {
_L_entry:;
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

static void Box_op__init(Box * this, Node * n) {
_L_entry:;
  __smart_ptr_drop(this->node);
  this->node = n;
  goto _L_exit;
_L_exit:;
  return;
}

static Node * Box_get(Box * this) {
  Node * __ret;
_L_entry:;
  __ret = __smart_ptr_get(this->node);
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void Box_op__destructor(Box * this) {
_L_entry:;
  __smart_ptr_drop(this->node);
  this->node = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static void Box_op__destructor_fwd(void * ptr) {
_L_entry:;
  Box_op__destructor(((Box *)(((Box *)(ptr)))));
  goto _L_exit;
_L_exit:;
  return;
}

static void Pair_op__destructor(Pair * this) {
_L_entry:;
  __smart_ptr_drop(this->first);
  this->first = nullptr;
  __smart_ptr_drop(this->second);
  this->second = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static Node * Node_op__initCover(int32_t id) {
  Node * __ret;
  Node * __obj13;
  Node * __t2;
_L_entry:;
  __t2 = __mm_alloc(&__ti_Node, nullptr);
  __obj13 = ((Node *)(__t2));
  memset(__obj13, 0, sizeof(Node));
  Node_op__init(((Node *)(__obj13)), id);
  __ret = __obj13;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static Box * Box_op__initCover(Node * n) {
  Box * __ret;
  Box * __obj14;
  Box * __t3;
_L_entry:;
  __t3 = __mm_alloc(&__ti_Box, nullptr);
  __obj14 = ((Box *)(__t3));
  memset(__obj14, 0, sizeof(Box));
  Box_op__init(((Box *)(__obj14)), n);
  __ret = __obj14;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void _main() {
  Box * box;
  Node * __t4;
  Node * fromReturn;
  Node * fromField;
  Pair p;
  Node * __t5;
  Node * __t6;
  Node * fromStruct;
_L_entry:;
  __t4 = Node_op__initCover(((int32_t)(1)));
  box = Box_op__initCover(__t4);
  fromReturn = Box_get(((Box *)(box)));
  fromField = __smart_ptr_get(box->node);
  __t5 = Node_op__initCover(((int32_t)(2)));
  __t6 = Node_op__initCover(((int32_t)(3)));
  p = (Pair){.first = __t5, .second = __t6};
  fromStruct = __smart_ptr_get(p.first);
  __cxy_assert((((fromReturn->id + fromField->id) + fromStruct->id) == 4), "fields copy implicitly", ((uint64_t)(0)), ((uint64_t)(0)));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(fromStruct);
  Pair_op__destructor(&(p));
  __smart_ptr_drop(fromField);
  __smart_ptr_drop(fromReturn);
  __smart_ptr_drop(box);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

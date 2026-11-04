typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Node Node;
typedef struct Holder Holder;

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

struct Holder {
  Node * node;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Node_op__init(Node * this, int32_t id);
static void Node_op__deinit(Node * this);
static void Node_op__destructor(Node * this);
static void Node_op__destructor_fwd(void * ptr);
static void Holder_op__destructor(Holder * this);
static int32_t take(Node * n);
static Node * Node_op__initCover(int32_t id);
static int32_t fromLocal();
static int32_t fromField();
static int32_t fromReference(Node * r);
static void _main();

static const __TypeInfo __ti_Node = { .name = "Node", .size = sizeof(Node), .align = _Alignof(Node), .index = 0, .dropFields = Node_op__destructor_fwd };


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

static void Holder_op__destructor(Holder * this) {
_L_entry:;
  __smart_ptr_drop(this->node);
  this->node = nullptr;
  goto _L_exit;
_L_exit:;
  return;
}

static int32_t take(Node * n) {
  int32_t __ret;
_L_entry:;
  __ret = n->id;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(n);
  return __ret;
}

static Node * Node_op__initCover(int32_t id) {
  Node * __ret;
  Node * __obj3;
  Node * __t1;
_L_entry:;
  __t1 = __mm_alloc(&__ti_Node, nullptr);
  __obj3 = ((Node *)(__t1));
  memset(__obj3, 0, sizeof(Node));
  Node_op__init(((Node *)(__obj3)), id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static int32_t fromLocal() {
  int32_t __ret;
  Node * a;
  Node * b;
  Node * __t2;
  int32_t __t3;
_L_entry:;
  a = Node_op__initCover(((int32_t)(1)));
  b = __smart_ptr_get(a);
  __t2 = __smart_ptr_get(b);
  __t3 = take(__t2);
  __ret = (a->id + __t3);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(b);
  __smart_ptr_drop(a);
  return __ret;
}

static int32_t fromField() {
  int32_t __ret;
  Holder h;
  Node * __t4;
  Node * n;
_L_entry:;
  __t4 = Node_op__initCover(((int32_t)(2)));
  h = (Holder){.node = __t4};
  n = __smart_ptr_get(h.node);
  __ret = (n->id + h.node->id);
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(n);
  Holder_op__destructor(&(h));
  return __ret;
}

static int32_t fromReference(Node * r) {
  int32_t __ret;
  Node * n;
_L_entry:;
  n = __smart_ptr_get(r);
  __ret = n->id;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(n);
  return __ret;
}

static void _main() {
  Node * x;
  int32_t total;
  int32_t __t5;
  int32_t __t6;
  int32_t __t7;
_L_entry:;
  x = Node_op__initCover(((int32_t)(3)));
  __t5 = fromLocal();
  __t6 = fromField();
  __t7 = fromReference(((Node *)(x)));
  total = ((__t5 + __t6) + __t7);
  __cxy_assert((total == 9), "total", ((uint64_t)(0)), ((uint64_t)(0)));
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(x);
  return;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  __cxy_start_runtime();
  _main();
  return 0;
}

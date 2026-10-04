typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Resource Resource;
typedef struct Container Container;
typedef struct Tuple_Resource_i8 Tuple_Resource_i8;
typedef union UnionPayload141 UnionPayload141;
typedef struct Union_Resource_i32 Union_Resource_i32;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Resource {
  int32_t id;
};

struct Container {
  Resource * res;
  int32_t tag;
};

struct Tuple_Resource_i8 {
  Resource * _0;
  int8_t _1;
};

union UnionPayload141 {
  Resource * _0;
  int32_t _1;
};

struct Union_Resource_i32 {
  uint32_t tag;
  UnionPayload141 val;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static void Container_op__destructor(Container * this);
static Resource * Resource_op__initCover(int32_t id);
static void testTuple();
static void testUnion(Union_Resource_i32 u);
static void _main();
static void __tuple_dctor_137(Tuple_Resource_i8 * tup);
static Union_Resource_i32 __union_copy_141(Union_Resource_i32 * u);
static void __union_dctor_141(Union_Resource_i32 * u);


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

static void Container_op__destructor(Container * this) {
_L_entry:;
  __smart_ptr_drop(this->res);
  memset(this->res, 0, sizeof(Resource));
  goto _L_exit;
_L_exit:;
  return;
}

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj3;
  Resource * __t1;
_L_entry:;
  __t1 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj3 = ((Resource *)(__t1));
  memset(__obj3, 0, sizeof(Resource));
  Resource_op__init(__obj3, id);
  __ret = __obj3;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testTuple() {
  Tuple_Resource_i8 tup;
_L_entry:;
  tup = (Tuple_Resource_i8){Resource_op__initCover(((int32_t)(1))), 42};
  goto _L_exit;
_L_exit:;
  __tuple_dctor_137(&(tup));
  return;
}

static void testUnion(Union_Resource_i32 u) {
  Union_Resource_i32 local;
_L_entry:;
  local = __union_copy_141(&(u));
  goto _L_exit;
_L_exit:;
  __union_dctor_141(&(local));
  __union_dctor_141(&(u));
  return;
}

static void _main() {
  Resource * __t2;
  Union_Resource_i32 __t3;
  Union_Resource_i32 __t4;
  Container c;
_L_entry:;
  testTuple();
  __t2 = Resource_op__initCover(((int32_t)(2)));
  __t3.tag = 0;
  __t3.val._0 = __t2;
  testUnion(__t3);
  __t4.tag = 1;
  __t4.val._1 = 10;
  testUnion(__t4);
  c = (Container){Resource_op__initCover(((int32_t)(3))), 1};
  goto _L_exit;
_L_exit:;
  Container_op__destructor(&(c));
  return;
}

static void __tuple_dctor_137(Tuple_Resource_i8 * tup) {
_L_entry:;
  __smart_ptr_drop(tup->_0);
  goto _L_exit;
_L_exit:;
  return;
}

static Union_Resource_i32 __union_copy_141(Union_Resource_i32 * u) {
  Union_Resource_i32 __ret;
  Resource * __t6;
_L_entry:;
  switch (u->tag) {
    case 0: goto _L_MATCH0_case_0;
    case 1: goto _L_MATCH0_case_1;
    default: goto _L_exit;
  }
_L_MATCH0_case_0:;
  __t6 = __smart_ptr_get(u->val._0);
  __ret.tag = 0;
  __ret.val._0 = __t6;
  goto _L_exit;
_L_MATCH0_case_1:;
  __ret.tag = 1;
  __ret.val._1 = u->val._1;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __union_dctor_141(Union_Resource_i32 * u) {
_L_entry:;
  switch (u->tag) {
    case 0: goto _L_MATCH0_case_0;
    default: goto _L_exit;
  }
_L_MATCH0_case_0:;
  __smart_ptr_drop(u->val._0);
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

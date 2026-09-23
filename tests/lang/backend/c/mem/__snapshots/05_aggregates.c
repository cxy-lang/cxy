typedef struct Tuple_ptr_i8 Tuple_ptr_i8;
typedef union UnionPayload40 UnionPayload40;
typedef struct Union_ptr_i32 Union_ptr_i32;
typedef struct Container Container;
typedef struct Resource Resource;

struct Tuple_ptr_i8 {
  Resource * _0;
  int8_t _1;
};

union UnionPayload40 {
  Resource * _0;
  int32_t _1;
};

struct Union_ptr_i32 {
  uint32_t tag;
  UnionPayload40 val;
};

struct Container {
  Resource * res;
  int32_t tag;
};

struct Resource {
  int32_t id;
};

static void main_Resource_op__init(Resource * this, int32_t id);
static void main_Resource_op__deinit(Resource * this);
static void main_Resource_op__destructor(Resource * this);
static void main_Resource_op__destructor_fwd(void * ptr);
static Container main_Container_op__copy(Container * this);
static void main_Container_op__destructor(Container * this);
static Resource * main_Resource_op__initCover(int32_t id);
static void main_testTuple();
static void main_testUnion(Union_ptr_i32 u);
static void _main();
static Tuple_ptr_i8 __tuple_copy_36(Tuple_ptr_i8 * tup);
static void __tuple_dctor_36(Tuple_ptr_i8 * tup);
static Union_ptr_i32 __union_copy_40(Union_ptr_i32 * u);
static void __union_dctor_40(Union_ptr_i32 * u);

static void main_Resource_op__init(Resource * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void main_Resource_op__deinit(Resource * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void main_Resource_op__destructor(Resource * this) {
_L_entry:;
  main_Resource_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void main_Resource_op__destructor_fwd(void * ptr) {
_L_entry:;
  main_Resource_op__destructor(((Resource *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static Container main_Container_op__copy(Container * this) {
  Container __ret;
  void * __t0;
_L_entry:;
  __t0 = __smart_ptr_get(this->res);
  __ret.res = ((Resource *)(__t0));
  __ret.tag = this->tag;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void main_Container_op__destructor(Container * this) {
_L_entry:;
  __smart_ptr_drop(this->res);
  memset(this->res, 0, sizeof(Resource));
  goto _L_exit;
_L_exit:;
  return;
}

static Resource * main_Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj0;
  void * __t1;
_L_entry:;
  __t1 = __smart_ptr_alloc(sizeof(Resource), main_Resource_op__destructor_fwd);
  __obj0 = ((Resource *)(__t1));
  memset(__obj0, 0, sizeof(Resource));
  main_Resource_op__init(__obj0, id);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void main_testTuple() {
  Tuple_ptr_i8 tup;
  Resource * __t2;
_L_entry:;
  __t2 = main_Resource_op__initCover(((int32_t)(1)));
  tup._0 = __t2;
  tup._1 = 42;
  goto _L_exit;
_L_exit:;
  return;
}

static void main_testUnion(Union_ptr_i32 u) {
  Union_ptr_i32 local;
_L_entry:;
  local = __union_copy_40(&(u));
  goto _L_exit;
_L_exit:;
  __union_dctor_40(&(local));
  __union_dctor_40(&(u));
  return;
}

static void _main() {
  Union_ptr_i32 _a1;
  Union_ptr_i32 __copy0;
  Union_ptr_i32 _a2;
  Union_ptr_i32 __copy1;
  Container c;
  Resource * __t3;
_L_entry:;
  main_testTuple();
  _a1.tag = 0;
  _a1.val._0 = main_Resource_op__initCover(((int32_t)(2)));
  __copy0 = __union_copy_40(&(_a1));
  main_testUnion(__copy0);
  _a2.tag = 1;
  _a2.val._1 = 10;
  __copy1 = __union_copy_40(&(_a2));
  main_testUnion(__copy1);
  __t3 = main_Resource_op__initCover(((int32_t)(3)));
  c.res = __t3;
  c.tag = 1;
  goto _L_exit;
_L_exit:;
  return;
}

static Tuple_ptr_i8 __tuple_copy_36(Tuple_ptr_i8 * tup) {
  Tuple_ptr_i8 __ret;
_L_entry:;
  __ret._0 = __smart_ptr_get(tup->_0);
  __ret._1 = tup->_1;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void __tuple_dctor_36(Tuple_ptr_i8 * tup) {
_L_entry:;
  __smart_ptr_drop(tup->_0);
  goto _L_exit;
_L_exit:;
  return;
}

static Union_ptr_i32 __union_copy_40(Union_ptr_i32 * u) {
  Union_ptr_i32 __ret;
_L_entry:;
  switch (u->tag) {
    case 0: goto _L_MATCH0_case_0;
    case 1: goto _L_MATCH0_case_1;
    default: goto _L_exit;
  }
_L_exit:;
  return __ret;
_L_MATCH0_case_0:;
  __ret.tag = 0;
  __ret.val._0 = __smart_ptr_get(u->val._0);
  goto _L_exit;
_L_MATCH0_case_1:;
  __ret.tag = 1;
  __ret.val._1 = u->val._1;
  goto _L_exit;
}

static void __union_dctor_40(Union_ptr_i32 * u) {
_L_entry:;
  switch (u->tag) {
    case 0: goto _L_MATCH0_case_0;
    default: goto _L_exit;
  }
_L_exit:;
  return;
_L_MATCH0_case_0:;
  __smart_ptr_drop(u->val._0);
  goto _L_exit;
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  _main();
  return 0;
}

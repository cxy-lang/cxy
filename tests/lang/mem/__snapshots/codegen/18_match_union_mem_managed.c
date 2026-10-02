typedef struct ResourceA ResourceA;
typedef struct ResourceB ResourceB;
typedef union UnionPayload36 UnionPayload36;
typedef struct Union_ptr_ptr_i32 Union_ptr_ptr_i32;
typedef struct __drop_flags_testMatchUnion_t __drop_flags_testMatchUnion_t;

struct ResourceA {
  int32_t id;
};

struct ResourceB {
  char * tag;
};

union UnionPayload36 {
  ResourceA * _0;
  ResourceB * _1;
  int32_t _2;
};

struct Union_ptr_ptr_i32 {
  uint32_t tag;
  UnionPayload36 val;
};

struct __drop_flags_testMatchUnion_t {
  uint64_t local;
  uint64_t local_1;
  uint64_t local_2;
};

static void ResourceA_op__init(ResourceA * this, int32_t id);
static void ResourceA_op__deinit(ResourceA * this);
static void ResourceA_op__destructor(ResourceA * this);
static void ResourceA_op__destructor_fwd(void * ptr);
static void ResourceB_op__init(ResourceB * this, char * tag);
static void ResourceB_op__deinit(ResourceB * this);
static void ResourceB_op__destructor(ResourceB * this);
static void ResourceB_op__destructor_fwd(void * ptr);
static ResourceA * ResourceA_op__initCover(int32_t id);
static ResourceB * ResourceB_op__initCover(char * tag);
static void testMatchUnion(Union_ptr_ptr_i32 u);
static void testUnionReassignment();
static void _main();
static void __union_dctor_36(Union_ptr_ptr_i32 * u);

static void ResourceA_op__init(ResourceA * this, int32_t id) {
_L_entry:;
  this->id = id;
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceA_op__deinit(ResourceA * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceA_op__destructor(ResourceA * this) {
_L_entry:;
  ResourceA_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceA_op__destructor_fwd(void * ptr) {
_L_entry:;
  ResourceA_op__destructor(((ResourceA *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceB_op__init(ResourceB * this, char * tag) {
_L_entry:;
  this->tag = tag;
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceB_op__deinit(ResourceB * this) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceB_op__destructor(ResourceB * this) {
_L_entry:;
  ResourceB_op__deinit(this);
  goto _L_exit;
_L_exit:;
  return;
}

static void ResourceB_op__destructor_fwd(void * ptr) {
_L_entry:;
  ResourceB_op__destructor(((ResourceB *)(ptr)));
  goto _L_exit;
_L_exit:;
  return;
}

static ResourceA * ResourceA_op__initCover(int32_t id) {
  ResourceA * __ret;
  ResourceA * __obj0;
  ResourceA * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(ResourceA), ResourceA_op__destructor_fwd);
  __obj0 = ((ResourceA *)(__t0));
  memset(__obj0, 0, sizeof(ResourceA));
  ResourceA_op__init(__obj0, id);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static ResourceB * ResourceB_op__initCover(char * tag) {
  ResourceB * __ret;
  ResourceB * __obj1;
  ResourceB * __t1;
_L_entry:;
  __t1 = __smart_ptr_alloc(sizeof(ResourceB), ResourceB_op__destructor_fwd);
  __obj1 = ((ResourceB *)(__t1));
  memset(__obj1, 0, sizeof(ResourceB));
  ResourceB_op__init(__obj1, tag);
  __ret = __obj1;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testMatchUnion(Union_ptr_ptr_i32 u) {
  __drop_flags_testMatchUnion_t __drop_flags;
  ResourceA * local;
  ResourceB * local_1;
  ResourceA * local_2;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testMatchUnion_t));
  switch (u.tag) {
    case 0: goto _L_MATCH0_case_0;
    case 1: goto _L_MATCH0_case_1;
    case 2: goto _L_MATCH0_case_2;
    default: goto _L_exit;
  }
_L_MATCH0_case_0:;
  local = ResourceA_op__initCover((u.val._0->id + 10));
  __drop_flags.local = 1;
  __smart_ptr_drop(local);
  __drop_flags.local = 0;
  goto _L_exit;
_L_MATCH0_case_1:;
  local_1 = ResourceB_op__initCover(u.val._1->tag);
  __drop_flags.local_1 = 1;
  __smart_ptr_drop(local_1);
  __drop_flags.local_1 = 0;
  goto _L_exit;
_L_MATCH0_case_2:;
  local_2 = ResourceA_op__initCover(u.val._2);
  __drop_flags.local_2 = 1;
  __smart_ptr_drop(local_2);
  __drop_flags.local_2 = 0;
  goto _L_exit;
_L_exit:;
  __union_dctor_36(&(u));
  return;
}

static void testUnionReassignment() {
  Union_ptr_ptr_i32 u;
  ResourceA * __t2;
  ResourceB * __t3;
  ResourceA * __t4;
_L_entry:;
  __t2 = ResourceA_op__initCover(((int32_t)(1)));
  u.tag = 0;
  u.val._0 = __t2;
  __t3 = ResourceB_op__initCover("hello");
  __union_dctor_36(&(u));
  u.tag = 1;
  u.val._1 = __t3;
  __union_dctor_36(&(u));
  u.tag = 2;
  u.val._2 = 42;
  __t4 = ResourceA_op__initCover(((int32_t)(100)));
  __union_dctor_36(&(u));
  u.tag = 0;
  u.val._0 = __t4;
  goto _L_exit;
_L_exit:;
  __union_dctor_36(&(u));
  return;
}

static void _main() {
  ResourceA * __t5;
  Union_ptr_ptr_i32 __t6;
  ResourceB * __t7;
  Union_ptr_ptr_i32 __t8;
  Union_ptr_ptr_i32 __t9;
_L_entry:;
  __t5 = ResourceA_op__initCover(((int32_t)(10)));
  __t6.tag = 0;
  __t6.val._0 = __t5;
  testMatchUnion(__t6);
  __t7 = ResourceB_op__initCover("world");
  __t8.tag = 1;
  __t8.val._1 = __t7;
  testMatchUnion(__t8);
  __t9.tag = 2;
  __t9.val._2 = 5;
  testMatchUnion(__t9);
  testUnionReassignment();
  goto _L_exit;
_L_exit:;
  return;
}

static void __union_dctor_36(Union_ptr_ptr_i32 * u) {
_L_entry:;
  switch (u->tag) {
    case 0: goto _L_MATCH0_case_0;
    case 1: goto _L_MATCH0_case_1;
    default: goto _L_exit;
  }
_L_MATCH0_case_0:;
  __smart_ptr_drop(u->val._0);
  goto _L_exit;
_L_MATCH0_case_1:;
  __smart_ptr_drop(u->val._1);
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

typedef struct Resource Resource;
typedef struct __drop_flags_testSwitchCaseScopes_t __drop_flags_testSwitchCaseScopes_t;

struct Resource {
  int32_t id;
};

struct __drop_flags_testSwitchCaseScopes_t {
  uint64_t case0;
  uint64_t case1;
  uint64_t case23;
  uint64_t caseDef;
};

static void Resource_op__init(Resource * this, int32_t id);
static void Resource_op__deinit(Resource * this);
static void Resource_op__destructor(Resource * this);
static void Resource_op__destructor_fwd(void * ptr);
static Resource * Resource_op__initCover(int32_t id);
static void testSwitchCaseScopes(int32_t val);
static void testSwitchReassignmentInCases(int32_t val);
static void _main();

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

static Resource * Resource_op__initCover(int32_t id) {
  Resource * __ret;
  Resource * __obj0;
  Resource * __t0;
_L_entry:;
  __t0 = __smart_ptr_alloc(sizeof(Resource), Resource_op__destructor_fwd);
  __obj0 = ((Resource *)(__t0));
  memset(__obj0, 0, sizeof(Resource));
  Resource_op__init(__obj0, id);
  __ret = __obj0;
  goto _L_exit;
_L_exit:;
  return __ret;
}

static void testSwitchCaseScopes(int32_t val) {
  __drop_flags_testSwitchCaseScopes_t __drop_flags;
  Resource * outer;
  Resource * case0;
  Resource * case1;
  Resource * case23;
  Resource * caseDef;
_L_entry:;
  memset(&(__drop_flags), 0, sizeof(__drop_flags_testSwitchCaseScopes_t));
  outer = Resource_op__initCover(((int32_t)(1)));
  switch (val) {
    case 0: goto _L_SWITCH0_case_0;
    case 1: goto _L_SWITCH0_case_1;
    case 2: goto _L_exit;
    case 3: goto _L_SWITCH0_case_3;
    default: goto _L_SWITCH0_case_4;
  }
_L_SWITCH0_case_0:;
  case0 = Resource_op__initCover(((int32_t)(10)));
  __drop_flags.case0 = 1;
  __smart_ptr_drop(case0);
  __drop_flags.case0 = 0;
  goto _L_exit;
_L_SWITCH0_case_1:;
  case1 = Resource_op__initCover(((int32_t)(20)));
  __drop_flags.case1 = 1;
  if (val == 1) goto _L_UNWIND2_return; else goto _L_IF0_end;
_L_IF0_end:;
  __smart_ptr_drop(case1);
  __drop_flags.case1 = 0;
  goto _L_exit;
_L_UNWIND2_return:;
  __smart_ptr_drop(case1);
  __drop_flags.case1 = 0;
  goto _L_exit;
_L_SWITCH0_case_3:;
  case23 = Resource_op__initCover(((int32_t)(30)));
  __drop_flags.case23 = 1;
  __smart_ptr_drop(case23);
  __drop_flags.case23 = 0;
  goto _L_exit;
_L_SWITCH0_case_4:;
  caseDef = Resource_op__initCover(((int32_t)(40)));
  __drop_flags.caseDef = 1;
  __smart_ptr_drop(caseDef);
  __drop_flags.caseDef = 0;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(outer);
  return;
}

static void testSwitchReassignmentInCases(int32_t val) {
  Resource * res;
  Resource * tmp;
_L_entry:;
  res = Resource_op__initCover(((int32_t)(100)));
  switch (val) {
    case 0: goto _L_SWITCH0_case_0;
    case 1: goto _L_SWITCH0_case_1;
    default: goto _L_exit;
  }
_L_SWITCH0_case_0:;
  __smart_ptr_drop(res);
  res = Resource_op__initCover(((int32_t)(200)));
  goto _L_exit;
_L_SWITCH0_case_1:;
  tmp = Resource_op__initCover(((int32_t)(300)));
  __smart_ptr_drop(res);
  res = tmp;
  goto _L_exit;
_L_exit:;
  __smart_ptr_drop(res);
  return;
}

static void _main() {
_L_entry:;
  testSwitchCaseScopes(((int32_t)(0)));
  testSwitchCaseScopes(((int32_t)(1)));
  testSwitchCaseScopes(((int32_t)(2)));
  testSwitchCaseScopes(((int32_t)(99)));
  testSwitchReassignmentInCases(((int32_t)(0)));
  testSwitchReassignmentInCases(((int32_t)(1)));
  testSwitchReassignmentInCases(((int32_t)(2)));
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

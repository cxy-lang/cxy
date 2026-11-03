typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;
typedef struct Slice_i32 Slice_i32;
typedef struct Slice_Resource Slice_Resource;
typedef struct Resource Resource;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

struct Slice_i32 {
  int32_t * ptr;
  uint64_t len;
};

struct Slice_Resource {
  Resource * * ptr;
  uint64_t len;
};

struct Resource {
  int32_t id;
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void takeInts(Slice_i32 items);
static void takeResources(Slice_Resource items);
static void _main();


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

static void takeInts(Slice_i32 items) {
_L_entry:;
  goto _L_exit;
_L_exit:;
  return;
}

static void takeResources(Slice_Resource items) {
_L_entry:;
  goto _L_exit;
_L_exit:;
    return;
}

static void _main() {
  Slice_i32 __t0;
  Slice_Resource __t1;
  Slice_i32 ints;
  Slice_Resource resources;
  Slice_Resource __copy0;
_L_entry:;
  __t0 = (Slice_i32){0};
  takeInts(__t0);
  __t1 = (Slice_Resource){0};
  takeResources(__t1);
  ints = (Slice_i32){0};
  resources = (Slice_Resource){0};
  takeInts(ints);
  __copy0 = resources;
  takeResources(__copy0);
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

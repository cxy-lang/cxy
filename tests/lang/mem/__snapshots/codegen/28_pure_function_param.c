typedef struct builtins_Exception builtins_Exception;
typedef struct builtins_Exception_VTable builtins_Exception_VTable;

struct builtins_Exception {
  builtins_Exception_VTable * vtable;
  char * name;
  void * stackTrace;
};

struct builtins_Exception_VTable {
  uint64_t __tid;
  char * (*what)(builtins_Exception *);
};

static void builtins_panicOnUnhandledException(builtins_Exception * ex);

static void hit(char * s);
static void sayer(void (*h)(char *), char * s);
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

int8_t calls = 0;

static void hit(char * s) {
_L_entry:;
  ((calls)++);
  goto _L_exit;
_L_exit:;
  return;
}

static void sayer(void (*h)(char *), char * s) {
_L_entry:;
  h(s);
  goto _L_exit;
_L_exit:;
  return;
}

static void _main() {
_L_entry:;
  sayer(((void (*)(char *))(hit)), "hello");
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

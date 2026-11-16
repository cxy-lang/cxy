/**
 * Query a running memory-traced program over its unix socket
 * (see stdlib/trace/mem.c).
 */

#include "utils/commands/commands.h"
#include "utils/commands/mem_client.h"
#include "core/log.h"
#include "driver/options.h"

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MEM_SOCKET_DIR "/tmp"
#define MEM_SOCKET_PREFIX "cxy-mem-"
#define MEM_SOCKET_SUFFIX ".sock"
// The server reads at most this many bytes of the request line
#define MEM_REQUEST_MAX 255

typedef struct {
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    int pid;
    time_t mtime;
} MemSocket;

static i64 nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000 + (i64)(ts.tv_nsec / 1000000);
}

// Sockets of dead processes are left behind when the program crashes
static bool processAlive(int pid)
{
    return pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
}

static int collectSockets(MemSocket *out, int max)
{
    DIR *dir = opendir(MEM_SOCKET_DIR);
    if (dir == NULL)
        return 0;

    int count = 0;
    size_t prefixLen = strlen(MEM_SOCKET_PREFIX);
    size_t suffixLen = strlen(MEM_SOCKET_SUFFIX);
    for (struct dirent *ent = readdir(dir); ent && count < max; ent = readdir(dir)) {
        size_t len = strlen(ent->d_name);
        if (len <= prefixLen + suffixLen ||
            strncmp(ent->d_name, MEM_SOCKET_PREFIX, prefixLen) != 0 ||
            strcmp(ent->d_name + len - suffixLen, MEM_SOCKET_SUFFIX) != 0)
            continue;

        int pid = atoi(ent->d_name + prefixLen);
        if (!processAlive(pid))
            continue;

        MemSocket *s = &out[count];
        // a name that does not fit in a socket path cannot be one of ours
        if (!formatFits(s->path, sizeof(s->path), "%s/%s", MEM_SOCKET_DIR, ent->d_name))
            continue;
        struct stat st;
        s->mtime = stat(s->path, &st) == 0 ? st.st_mtime : 0;
        s->pid = pid;
        count++;
    }
    closedir(dir);
    return count;
}

// Resolves --socket, $CXY_MEM_SOCKET, --pid, then the only live traced process
bool memResolveSocket(const Options *options, char *out, size_t size, Log *log)
{
    const char *explicitPath = options->utils.memSocket;
    if (explicitPath != NULL && explicitPath[0] != '\0') {
        snprintf(out, size, "%s", explicitPath);
        return true;
    }

    const char *env = getenv("CXY_MEM_SOCKET");
    if (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0 &&
        strcmp(env, "1") != 0) {
        snprintf(out, size, "%s", env);
        return true;
    }

    if (options->utils.memPid > 0) {
        snprintf(out, size, MEM_SOCKET_DIR "/" MEM_SOCKET_PREFIX "%d" MEM_SOCKET_SUFFIX,
                 (int)options->utils.memPid);
        return true;
    }

    MemSocket found[32];
    int count = collectSockets(found, 32);
    if (count == 0) {
        logError(log,
                 NULL,
                 "no traced process found, run a program built with memory "
                 "tracing or pass --pid/--socket",
                 NULL);
        return false;
    }
    if (count > 1) {
        fprintf(stderr, "multiple traced processes found, pass --pid:\n");
        for (int i = 0; i < count; i++)
            fprintf(stderr, "  pid %d  %s\n", found[i].pid, found[i].path);
        logError(log, NULL, "ambiguous traced process", NULL);
        return false;
    }

    snprintf(out, size, "%s", found[0].path);
    return true;
}

static int connectSocket(const char *path)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(addr.sun_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = errno;
        close(fd);
        errno = err;
        return -1;
    }
    return fd;
}

static bool sendAll(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        buf += n;
        len -= (size_t)n;
    }
    return true;
}

static bool buildRequest(const Options *options, char *out, size_t size, Log *log)
{
    const DynArray *args = &options->utils.memArgs;
    size_t len = 0;
    out[0] = '\0';
    for (u32 i = 0; args->elems != NULL && i < args->size; i++) {
        const char *arg = ((cstring *)args->elems)[i];
        size_t need = strlen(arg) + (len > 0 ? 1 : 0);
        if (len + need + 1 > size) {
            logError(log, NULL, "query too long", NULL);
            return false;
        }
        if (len > 0)
            out[len++] = ' ';
        memcpy(out + len, arg, strlen(arg));
        len += strlen(arg);
        out[len] = '\0';
    }

    if (len == 0)
        snprintf(out, size, "help");
    return true;
}

bool memRequest(const char *socketPath,
                const char *request,
                i64 timeoutMs,
                MemSink sink,
                void *ctx,
                char *error,
                size_t errorSize)
{
    int fd = connectSocket(socketPath);
    if (fd < 0) {
        snprintf(error, errorSize, "cannot connect to '%s': %s", socketPath, strerror(errno));
        return false;
    }

    char line[MEM_REQUEST_MAX + 1];
    size_t length = (size_t)snprintf(line, sizeof(line) - 1, "%s", request);
    if (length > MEM_REQUEST_MAX - 1)
        length = MEM_REQUEST_MAX - 1;
    line[length++] = '\n';
    if (!sendAll(fd, line, length)) {
        close(fd);
        snprintf(error, errorSize, "failed to send the query");
        return false;
    }

    i64 deadline = nowMs() + (timeoutMs > 0 ? timeoutMs : 30000);
    bool ok = true;
    for (;;) {
        i64 left = deadline - nowMs();
        if (left <= 0) {
            snprintf(error, errorSize, "timed out waiting for the traced process");
            ok = false;
            break;
        }

        struct pollfd pfd = {.fd = fd, .events = POLLIN};
        int rc = poll(&pfd, 1, (int)(left > 1000000 ? 1000000 : left));
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc < 0) {
            snprintf(error, errorSize, "waiting for the traced process failed: %s", strerror(errno));
            ok = false;
            break;
        }
        if (rc == 0)
            continue;

        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        sink(buf, (size_t)n, ctx);
    }
    close(fd);
    return ok;
}

static void printSink(const char *data, size_t length, attr(unused) void *ctx)
{
    fwrite(data, 1, length, stdout);
}

// The commands the client adds to what the server lists
static void printClientHelp(void)
{
    printf("client commands (`cxy utils mem --help` lists the options):\n"
           "  analyze              first call takes a baseline, later calls say whether the memory\n"
           "                       looks normal (--json for scripts, --fail-on for CI)\n"
           "  analyze --watch 30s  take a baseline, watch for 30s, then report\n");
}

// `help json` answers a JSON error and nothing may follow it
static bool isHelpRequest(const char *request)
{
    bool help = strcmp(request, "help") == 0 || strncmp(request, "help ", 5) == 0;
    return help && strstr(request, "json") == NULL;
}

static bool isAnalyze(const Options *options)
{
    const DynArray *args = &options->utils.memArgs;
    return args->elems != NULL && args->size > 0 && strcmp(((cstring *)args->elems)[0], "analyze") == 0;
}

int utilsMemCommand(const Options *options, attr(unused) struct StrPool *strings, Log *log)
{
    // A server that closes early must not kill this tool
    signal(SIGPIPE, SIG_IGN);

    if (isAnalyze(options))
        return memAnalyzeCommand(options, log);

    char request[MEM_REQUEST_MAX];
    if (!buildRequest(options, request, sizeof(request) - 1, log))
        return EXIT_FAILURE;

    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    if (!memResolveSocket(options, path, sizeof(path), log))
        return EXIT_FAILURE;

    char error[512];
    bool ok = memRequest(path, request, options->utils.memTimeout, printSink, NULL, error, sizeof(error));
    if (ok && isHelpRequest(request))
        printClientHelp();
    fflush(stdout);
    if (!ok)
        logError(log, NULL, "{s}", (FormatArg[]){{.s = error}});
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

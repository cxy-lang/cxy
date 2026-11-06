#include "core/log.h"
#include "core/utils.h"
#include "driver/driver.h"
#include "driver/options.h"
#include "package/commands/commands.h"
#include "utils/commands/commands.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FormatState state = newFormatState("    ", !isColorSupported(stderr));
    CompilerDriver driver = {.cxyBinaryPath = argv[0]};
    MemPool pool = newMemPool();
    Log log = newLog(&pool, NULL, NULL);
    StrPool strings = newStrPool(&pool);
    driver.options.strings = &strings;

    bool status = true;
    int exitCode = EXIT_SUCCESS; // set by commands with more than pass or fail

    if (!parseCommandLineOptions(
            &argc, argv, &strings, &driver.options, &log)) {
        status = false;
        goto exit;
    }

    switch (driver.options.cmd) {
    case cmdPackage:
        status = dispatchPackageCommand(&driver.options, &strings, &log);
        goto exit;
    case cmdUtils:
        exitCode = dispatchUtilsCommand(&driver.options, &strings, &log);
        status = exitCode == EXIT_SUCCESS;
        goto exit;
    case _cmdHelp:
    case _cmdCompletion:
        goto exit;
    default:
        break;
    }

    if (!initCompilerDriver(&driver, &pool, &strings, &log, argc, argv)) {
        status = false;
        goto exit;
    }

    dynArrayFor(source, cstring, &driver.options.sources)
    {
        if (driver.options.cmd == cmdBuild && driver.options.build.plugin)
            status &= compilePlugin(source[0], &driver);
        else
            status &= compileFile(source[0], &driver);
    }

exit:

    deinitCompilerDriver(&driver);
    writeFormatState(&state, stderr);
    freeFormatState(&state);
    freeLog(&log);
    freeStrPool(&strings);
    freeMemPool(&pool);
    if (exitCode != EXIT_SUCCESS)
        return exitCode;
    return status ? EXIT_SUCCESS : EXIT_FAILURE;
}

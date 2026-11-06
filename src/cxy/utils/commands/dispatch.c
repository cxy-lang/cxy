/**
 * Copyright (c) 2024 Suilteam, Carter Mbotho
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Carter
 * @date 2024-01-25
 */

#include "utils/commands/commands.h"
#include "core/log.h"

#include <stdlib.h>

static int exitCodeOf(bool ok)
{
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int dispatchUtilsCommand(const Options *options, struct StrPool *strings, Log *log)
{
    switch (options->utils.subcmd) {
        case utlSubAsyncCmdStart:
            return exitCodeOf(utilsAsyncCmdStartCommand(options, strings, log));

        case utlSubAsyncCmdStop:
            return exitCodeOf(utilsAsyncCmdStopCommand(options, strings, log));

        case utlSubAsyncCmdLogs:
            return exitCodeOf(utilsAsyncCmdLogsCommand(options, strings, log));

        case utlSubAsyncCmdStatus:
            return exitCodeOf(utilsAsyncCmdStatusCommand(options, strings, log));

        case utlSubWaitFor:
            return exitCodeOf(utilsWaitForCommand(options, strings, log));

        case utlSubWaitForPort:
            return exitCodeOf(utilsWaitForPortCommand(options, strings, log));

        case utlSubFindFreePort:
            return exitCodeOf(utilsFindFreePortCommand(options, strings, log));

        case utlSubEnvCheck:
            return exitCodeOf(utilsEnvCheckCommand(options, strings, log));

        case utlSubLock:
            return exitCodeOf(utilsLockCommand(options, strings, log));

        case utlSubMem:
            return utilsMemCommand(options, strings, log);

        default:
            logError(log, NULL, "unknown utils subcommand", NULL);
            return EXIT_FAILURE;
    }
}

#pragma once

#include "cli_command_result.h"

namespace cli_output {

using DebugHelpPrinter = void (*)();

void render(const CommandResult& result, DebugHelpPrinter debugHelpPrinter);

} // namespace cli_output

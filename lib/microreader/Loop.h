#pragma once

#include "microreader/Application.h"
#include "microreader/DisplayQueue.h"
#include "microreader/Log.h"
#include "microreader/Runtime.h"

namespace microreader {

void run_loop(Application& app, DisplayQueue& queue, IRuntime& runtime, ILogger& logger);
void run_loop_iteration(Application& app, DisplayQueue& queue, IRuntime& runtime, ILogger& logger);

}  // namespace microreader

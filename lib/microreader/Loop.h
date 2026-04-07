#pragma once

#include "microreader/Application.h"
#include "microreader/Input.h"
#include "microreader/Runtime.h"
#include "microreader/display/DisplayQueue.h"

namespace microreader {

void run_loop(Application& app, DisplayQueue& queue, IInputSource& input, IRuntime& runtime);
void run_loop_iteration(Application& app, DisplayQueue& queue, IInputSource& input, IRuntime& runtime);

}  // namespace microreader

#pragma once

#include "01_project/project.h"
#include "03_comptime/comptime.h"

namespace cursive::driver {

frontend::ComptimePassOptions BuildComptimeOptions(
    const project::Project& project);

}  // namespace cursive::driver

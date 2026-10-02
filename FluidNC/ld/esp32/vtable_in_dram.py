Import("env")

import os.path

ld_variant = env.GetProjectOption("custom_ld_variant", env.subst("$PIOENV"))

env.Prepend(
    LIBPATH=[
        os.path.join("$PROJECT_DIR", "FluidNC", "ld", "$BOARD_MCU", ld_variant)
    ]
)

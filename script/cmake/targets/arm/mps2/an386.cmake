set(VSF_HAL_CHIP_VENDOR     arm)
set(VSF_HAL_CHIP_SERIES     mps2)
set(VSF_HAL_CHIP_NAME       CMSDK_CM4)
set(VSF_ARCH_SERIES         arm)
set(VSF_ARCH_NAME           cortex-m4)

set(VSF_TARGET_DEFINITIONS
    "__ARM__"
    "__MPS2__"
    "CMSDK_CM4"

    ${VSF_TARGET_DEFINITIONS}
)

include(${VSF_CMAKE_ROOT}/targets/arm/__cortex_m4.cmake)

set(JS_VENDOR "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/mquickjs")
set(LUA_VENDOR "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/lua/src")
set(LUA_SOURCES
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c
    ltm.c lundump.c lvm.c lzio.c lauxlib.c lbaselib.c ltablib.c
    lstrlib.c lmathlib.c lutf8lib.c)
list(TRANSFORM LUA_SOURCES PREPEND "${LUA_VENDOR}/")
idf_component_register(
    SRCS "script_engine.c" "script_bindings.c" ${LUA_SOURCES}
         "${JS_VENDOR}/mquickjs.c" "${JS_VENDOR}/dtoa.c"
         "${JS_VENDOR}/libm.c" "${JS_VENDOR}/cutils.c"
    INCLUDE_DIRS "include" "../app_runtime/include" "../host_link/include"
    PRIV_INCLUDE_DIRS "${JS_VENDOR}" "${JS_VENDOR}/generated" "${LUA_VENDOR}"
    REQUIRES log cjson)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -flto=2 -ffat-lto-objects -fno-math-errno -fno-trapping-math)
target_compile_definitions(${COMPONENT_LIB} PRIVATE _GNU_SOURCE LUAI_MAXCCALLS=32)

set_source_files_properties(
    "${JS_VENDOR}/mquickjs.c" "${JS_VENDOR}/dtoa.c" "${JS_VENDOR}/libm.c" "${JS_VENDOR}/cutils.c"
    PROPERTIES COMPILE_OPTIONS "-include;stddef.h;-Wno-error=format;-Wno-error=dangling-pointer")

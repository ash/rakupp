# ctypes over librakupp: the loader and the raw C declarations, nothing else.
#
# Kept separate from the Python-facing classes so this file mirrors
# src/rakupp.h + src/rakupp_ext.h one-to-one and can be diffed against them
# when the ABI grows.

import ctypes
import ctypes.util
import os
import shutil
import sys

RK_OK, RK_ERROR, RK_FATAL = 0, 1, 2

# RkType — deliberately coarse, mirrors rakupp_ext.h.
RK_ANY, RK_BOOL, RK_INT, RK_NUM, RK_RAT, RK_STR, RK_ARRAY, RK_HASH, RK_OTHER = range(9)


class RkConfig(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint),
        ("own_stack", ctypes.c_int),
        ("handle_sigpipe", ctypes.c_int),
        ("own_stdout", ctypes.c_int),
    ]


def _library_name():
    if sys.platform == "darwin":
        return "librakupp.dylib"
    if sys.platform in ("win32", "cygwin"):
        return "rakupp.dll"
    return "librakupp.so"


def _candidates():
    # a platform wheel bundles the library inside the package (tools/build-wheel.sh)
    bundled = os.path.join(os.path.dirname(__file__), "_lib", _library_name())
    if os.path.exists(bundled):
        yield bundled
    env = os.environ.get("RAKUPP_LIB")
    if env:
        yield env
    home = os.environ.get("RAKUPP_HOME")
    if home:
        yield os.path.join(home, "lib", _library_name())
    # The rakupp binary on PATH knows where it lives: an installed layout
    # (cmake --install, the release tarball, Homebrew) is bin/rakupp beside
    # lib/librakupp.*, and a build directory holds binary and library side
    # by side. This is what makes plain `rakupp.interpreter()` work.
    exe = shutil.which("rakupp")
    if exe:
        # both where the name points (a symlink beside its own lib/) and where
        # it resolves (a Homebrew keg's bin/, whose sibling lib/ is the one)
        dirs = []
        for d in (os.path.dirname(exe), os.path.dirname(os.path.realpath(exe))):
            if d not in dirs:
                dirs.append(d)
        for d in dirs:
            yield os.path.normpath(os.path.join(d, "..", "lib", _library_name()))
            yield os.path.join(d, _library_name())
    found = ctypes.util.find_library("rakupp")
    if found:
        yield found


def load(path=None):
    """Load librakupp and declare its entry points.

    On ELF hosts the library MUST be loaded RTLD_GLOBAL: a Raku extension
    dlopen'ed later resolves rk_* from already-loaded images, and an
    RTLD_LOCAL library is invisible to that lookup (ABI-PLAN A3). macOS
    searches all images either way; the flag is harmless there.
    """
    tried = []
    for cand in ([path] if path else _candidates()):
        try:
            lib = ctypes.CDLL(cand, mode=ctypes.RTLD_GLOBAL)
            break
        except OSError as e:
            tried.append(f"{cand}: {e}")
    else:
        exe = None if path else shutil.which("rakupp")
        hint = (
            "\nA rakupp binary WAS found (%s) but its build carries no shared "
            "library — a plain build directory is static-only. Rebuild it with "
            "-DRAKUPP_BUILD_SHARED=ON, or point RAKUPP_LIB at a build that has "
            "one." % exe
        ) if exe else ""
        raise OSError(
            "librakupp not found. Set RAKUPP_LIB to the library file, or "
            "RAKUPP_HOME to an install prefix containing lib/%s.%s%s"
            % (_library_name(), hint,
               ("\nTried:\n  " + "\n  ".join(tried)) if tried else "")
        )

    p = ctypes.c_void_p  # RkInterp / RkCtx / RkValue are all opaque pointers

    def sig(name, restype, *argtypes):
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = list(argtypes)

    # rakupp.h — lifecycle, eval, errors, streams
    sig("rk_new", p, ctypes.POINTER(RkConfig))
    sig("rk_free", None, p)
    sig("rk_version", ctypes.c_char_p)
    sig("rk_ctx", p, p)
    sig("rk_eval", ctypes.c_int, p, ctypes.c_char_p, ctypes.POINTER(p))
    sig("rk_eval_file", ctypes.c_int, p, ctypes.c_char_p, ctypes.POINTER(p))
    sig("rk_run", ctypes.c_int, p, ctypes.c_char_p, ctypes.c_char_p,
        ctypes.POINTER(ctypes.c_int))
    sig("rk_last_error", ctypes.c_char_p, p)

    # rakupp_ext.h — the value vocabulary (shared with extensions)
    sig("rk_any", p, p)
    sig("rk_bool", p, p, ctypes.c_int)
    sig("rk_int", p, p, ctypes.c_longlong)
    sig("rk_int_s", p, p, ctypes.c_char_p)
    sig("rk_num", p, p, ctypes.c_double)
    sig("rk_rat_s", p, p, ctypes.c_char_p, ctypes.c_char_p)
    sig("rk_str", p, p, ctypes.c_char_p, ctypes.c_size_t)
    sig("rk_array", p, p)
    sig("rk_push", None, p, p, p)
    sig("rk_hash", p, p)
    sig("rk_set", None, p, p, ctypes.c_char_p, ctypes.c_size_t, p)

    sig("rk_type", ctypes.c_int, p, p)
    sig("rk_truthy", ctypes.c_int, p, p)
    sig("rk_int_get", ctypes.c_longlong, p, p)
    sig("rk_num_get", ctypes.c_double, p, p)
    # const char* + out-len, NOT c_char_p: embedded NULs must survive.
    sig("rk_str_get", p, p, p, ctypes.POINTER(ctypes.c_size_t))
    sig("rk_elems", ctypes.c_size_t, p, p)
    sig("rk_at_pos", p, p, p, ctypes.c_size_t)
    sig("rk_key_at", p, p, p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t))
    sig("rk_val_at", p, p, p, ctypes.c_size_t)

    sig("rk_call", p, p, ctypes.c_char_p, ctypes.POINTER(p), ctypes.c_size_t)
    sig("rk_call_value", p, p, p, ctypes.POINTER(p), ctypes.c_size_t)
    sig("rk_can", ctypes.c_int, p, ctypes.c_char_p)
    sig("rk_error", ctypes.c_char_p, p)
    sig("rk_clear_error", None, p)
    sig("rk_root", p, p, p)
    sig("rk_unroot", None, p, p)

    return lib

#if defined(SOKOL_IMPL) && !defined(SOKOL_FILESYSTEM_IMPL)
#define SOKOL_FILESYSTEM_IMPL
#endif
#ifndef SOKOL_FILESYSTEM_INCLUDED
/*
    sokol_filesystem.h  -- cross-platform filesystem utilities

    Project URL: https://github.com/floooh/sokol

    Do this:
        #define SOKOL_IMPL or
        #define SOKOL_FILESYSTEM_IMPL
    before you include this file in *one* C or C++ file to create the
    implementation.

    Optionally provide the following defines with your own implementations:

        SOKOL_ASSERT(c)             - your own assert macro (default: assert(c))
        SOKOL_FILESYSTEM_API_DECL   - public function declaration prefix (default: extern)
        SOKOL_API_DECL              - same as SOKOL_FILESYSTEM_API_DECL
        SOKOL_API_IMPL              - public function implementation prefix (default: -)
        SOKOL_LOG(msg)              - your own logging function (default: puts(msg))
        SOKOL_UNREACHABLE()         - your own unreachable macro (default: assert(false))

    If sokol_filesystem.h is compiled as a DLL, define the following before
    including the declaration or implementation:

        SOKOL_DLL

    On Windows, SOKOL_DLL will define SOKOL_FILESYSTEM_API_DECL as
    __declspec(dllexport) or __declspec(dllimport) as needed.

    OVERVIEW
    ========
    sokol_filesystem.h provides portable filesystem path queries, directory
    enumeration, file metadata, file/directory creation/removal/rename/copy,
    and glob-style pattern matching. The API mirrors SDL3's filesystem module.

    PLATFORM NOTES
    ==============
    Windows:    Uses Win32/SHGetFolderPathW, FindFirstFileExW, etc.
    macOS/iOS:  Uses NSBundle, NSSearchPathForDirectoriesInDomains (ObjC).
    Linux:      Uses /proc/self/exe + POSIX dirent/stat.
    Android:    Base path returns "./"; pref path is read automatically from
                ANativeActivity::internalDataPath via sapp_android_get_native_activity().
                No Java-side setup required. Optionally override with
                sfs_set_android_internal_path() for testing or non-sokol-app use.
    Emscripten: Virtual POSIX FS via emscripten/emscripten.h.

    ANDROID NOTES
    =============
    The internal storage path is retrieved automatically from the ANativeActivity
    struct (activity->internalDataPath), which is identical to Java's
    getFilesDir().getAbsolutePath(). No Java/JNI setup is needed.
    Call sfs_set_android_internal_path() only if you need to override the path.

    EMSCRIPTEN PERSISTENT STORAGE
    ==============================
    On Emscripten the pref_path is by default stored under /libsokol. Mount
    the IDBFS filesystem and call FS.syncfs() if you need persistence across
    page reloads.

    C# BINDINGS (Bindgen)
    =====================
    A companion file bindgen/c/sokol_filesystem.c includes this header with
    SOKOL_FILESYSTEM_IMPL defined and exposes every public sfs_* symbol so
    the Sokol.NET bindgen pipeline can generate P/Invoke bindings.

    LICENSE
    =======
    zlib/libpng license

    Copyright (c) 2024 Sokol.NET contributors

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

        1. The origin of this software must not be misrepresented; you must
           not claim that you wrote the original software. If you use this
           software in a product, an acknowledgment in the product
           documentation would be appreciated but is not required.

        2. Altered source versions must be plainly marked as such, and must
           not be misrepresented as being the original software.

        3. This notice may not be removed or altered from any source
           distribution.
*/
#define SOKOL_FILESYSTEM_INCLUDED (1)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(SOKOL_API_DECL) && !defined(SOKOL_FILESYSTEM_API_DECL)
#define SOKOL_FILESYSTEM_API_DECL SOKOL_API_DECL
#endif
#ifndef SOKOL_FILESYSTEM_API_DECL
#if defined(_WIN32) && defined(SOKOL_DLL) && defined(SOKOL_FILESYSTEM_IMPL)
#define SOKOL_FILESYSTEM_API_DECL __declspec(dllexport)
#elif defined(_WIN32) && defined(SOKOL_DLL)
#define SOKOL_FILESYSTEM_API_DECL __declspec(dllimport)
#else
#define SOKOL_FILESYSTEM_API_DECL extern
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
   PUBLIC TYPES
   ========================================================================= */

/* Result codes returned by most sfs functions */
typedef enum sfs_result_t {
    SFS_RESULT_OK           = 0,
    SFS_RESULT_ERROR        = -1,
    SFS_RESULT_NOT_FOUND    = -2,
    SFS_RESULT_PERMISSION   = -3,
    SFS_RESULT_INVALID_PARAM = -4,
} sfs_result_t;

/* Path information returned by sfs_get_path_info */
typedef enum sfs_path_type_t {
    SFS_PATHTYPE_NONE        = 0,   /* path does not exist */
    SFS_PATHTYPE_FILE        = 1,
    SFS_PATHTYPE_DIRECTORY   = 2,
    SFS_PATHTYPE_OTHER       = 3,   /* symlink, device, etc. */
} sfs_path_type_t;

typedef struct sfs_path_info_t {
    sfs_path_type_t type;
    int64_t         size;           /* file size in bytes (0 for directories) */
    int64_t         create_time;    /* creation time (Unix epoch seconds, 0 if unsupported) */
    int64_t         modify_time;    /* last modification time (Unix epoch seconds) */
    int64_t         access_time;    /* last access time (Unix epoch seconds) */
} sfs_path_info_t;

/* Well-known user folder identifiers (mirrors SDL_Folder) */
typedef enum sfs_folder_t {
    SFS_FOLDER_HOME         = 0,
    SFS_FOLDER_DESKTOP      = 1,
    SFS_FOLDER_DOCUMENTS    = 2,
    SFS_FOLDER_DOWNLOADS    = 3,
    SFS_FOLDER_MUSIC        = 4,
    SFS_FOLDER_PICTURES     = 5,
    SFS_FOLDER_PUBLICSHARE  = 6,
    SFS_FOLDER_SAVEDGAMES   = 7,
    SFS_FOLDER_SCREENSHOTS  = 8,
    SFS_FOLDER_TEMPLATES    = 9,
    SFS_FOLDER_VIDEOS       = 10,
    SFS_FOLDER_COUNT
} sfs_folder_t;

/* Enumeration callback result */
typedef enum sfs_enum_result_t {
    SFS_ENUM_CONTINUE   = 0,    /* keep enumerating */
    SFS_ENUM_SUCCESS    = 1,    /* stop, treat as success */
    SFS_ENUM_FAILURE    = -1,   /* stop, treat as failure */
} sfs_enum_result_t;

/* Callback for sfs_enumerate_directory.
   @param userdata  caller-provided pointer
   @param dirname   the directory being enumerated (always ends with '/')
   @param fname     filename of this entry (not a full path)
   Return SFS_ENUM_CONTINUE to continue, SFS_ENUM_SUCCESS to stop successfully,
   or SFS_ENUM_FAILURE to abort with failure. */
typedef sfs_enum_result_t (*sfs_enumerate_callback_t)(void* userdata, const char* dirname, const char* fname);

/* Glob flags */
typedef enum sfs_glob_flags_t {
    SFS_GLOB_NONE               = 0,
    SFS_GLOB_CASE_INSENSITIVE   = 1,
} sfs_glob_flags_t;

/* Opaque file handle — allocated by sfs_open_file, freed by sfs_close_file */
typedef struct sfs_file_t sfs_file_t;

/* Seek origin (mirrors SEEK_SET / SEEK_CUR / SEEK_END and SDL_IOWhence) */
typedef enum sfs_whence_t {
    SFS_WHENCE_SET = 0,     /* seek from beginning of file */
    SFS_WHENCE_CUR = 1,     /* seek relative to current position */
    SFS_WHENCE_END = 2,     /* seek relative to end of file */
} sfs_whence_t;

/* File open mode for sfs_open_file */
typedef enum sfs_open_mode_t {
    SFS_OPEN_READ           = 0,  /* read-only binary ("rb") */
    SFS_OPEN_WRITE          = 1,  /* write binary, truncate to zero ("wb") */
    SFS_OPEN_APPEND         = 2,  /* write binary, always append ("ab") */
    SFS_OPEN_READ_WRITE     = 3,  /* read+write binary, file must exist ("r+b") */
    SFS_OPEN_CREATE_WRITE   = 4,  /* read+write binary, truncate or create ("w+b") */
    SFS_OPEN_APPEND_READ    = 5,  /* read+append binary ("a+b") */
} sfs_open_mode_t;

/* =========================================================================
   PUBLIC API
   ========================================================================= */

/* --- Android setup (optional override) ---
   Override the internal storage path on Android. When not called, the path is
   retrieved automatically from ANativeActivity::internalDataPath.
   This is a no-op on all other platforms. */
SOKOL_FILESYSTEM_API_DECL void sfs_set_android_internal_path(const char* path);

/* --- Path queries ---
   All returned strings are heap-allocated; the caller must call sfs_free_path().
   Returns NULL on error. */

/* Returns the directory where the application's binary/bundle resides,
   with a trailing path separator. */
SOKOL_FILESYSTEM_API_DECL char* sfs_get_base_path(void);

/* Returns a writable preferences/save-data directory for org/app,
   creating it if necessary. Trailing separator included.
   Both org and app must be non-NULL (use "" for org if not needed). */
SOKOL_FILESYSTEM_API_DECL char* sfs_get_pref_path(const char* org, const char* app);

/* Returns the path to a well-known user folder (e.g. Documents, Pictures).
   May return NULL on platforms that don't support a particular folder. */
SOKOL_FILESYSTEM_API_DECL char* sfs_get_user_folder(sfs_folder_t folder);

/* Returns the current working directory with trailing separator. */
SOKOL_FILESYSTEM_API_DECL char* sfs_get_current_directory(void);

/* Change the current working directory. Returns SFS_RESULT_OK on success. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_set_current_directory(const char* path);

/* Returns the system temporary directory with trailing separator.
   Caller must call sfs_free_path(). Returns NULL on error. */
SOKOL_FILESYSTEM_API_DECL char* sfs_get_temp_dir(void);

/* Returns the application Assets (resource) directory: the "Assets"
   subfolder located inside the base (executable) directory.
   Trailing separator included. Caller must call sfs_free_path(). */
SOKOL_FILESYSTEM_API_DECL char* sfs_get_assets_dir(void);

/* Free a path string returned from any sfs_get_* function. */
SOKOL_FILESYSTEM_API_DECL void sfs_free_path(char* path);

/* --- File/Directory operations --- */

/* Create a directory (and any missing parent directories). */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_create_directory(const char* path);

/* Remove a file *or* empty directory. Returns SFS_RESULT_OK if already gone. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_remove_path(const char* path);

/* Rename/move a file or directory. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_rename_path(const char* oldpath, const char* newpath);

/* Copy a file (overwrites destination if it exists). */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_copy_file(const char* oldpath, const char* newpath);

/* Retrieve metadata for a path. Returns SFS_RESULT_NOT_FOUND if nonexistent. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_get_path_info(const char* path, sfs_path_info_t* out_info);

/* Returns true if path exists (file or directory). */
SOKOL_FILESYSTEM_API_DECL bool sfs_path_exists(const char* path);

/* Returns true if path is an existing directory. */
SOKOL_FILESYSTEM_API_DECL bool sfs_is_directory(const char* path);

/* Returns true if path is an existing regular file. */
SOKOL_FILESYSTEM_API_DECL bool sfs_is_file(const char* path);

/* Returns the last-modified time of a path as Unix epoch seconds,
   or 0 if the path cannot be accessed or does not exist. */
SOKOL_FILESYSTEM_API_DECL int64_t sfs_get_last_modified_time(const char* path);

/* --- Directory enumeration --- */

/* Enumerate directory entries. Callback is invoked for each entry.
   Returns SFS_RESULT_OK unless the callback returned SFS_ENUM_FAILURE or
   the directory could not be opened. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_enumerate_directory(const char* path,
                                                                 sfs_enumerate_callback_t callback,
                                                                 void* userdata);

/* --- Glob / pattern matching ---
   Returns a heap-allocated array of heap-allocated strings (NULL-terminated array).
   *out_count is set to the number of matches. Free with sfs_free_glob_results().
   Returns NULL on error (out_count set to 0). Pattern uses shell glob syntax: * ? []. */
SOKOL_FILESYSTEM_API_DECL char** sfs_glob_directory(const char* path,
                                                      const char* pattern,
                                                      sfs_glob_flags_t flags,
                                                      int* out_count);

/* Free results returned by sfs_glob_directory. */
SOKOL_FILESYSTEM_API_DECL void sfs_free_glob_results(char** results, int count);

/* --- File I/O ---
   sfs_file_t is an opaque handle wrapping the platform FILE*.
   All file I/O functions are safe to call with a NULL file pointer;
   they will set the error string and return an error value. */

/* Open a file using sfs_open_mode_t.
   Returns NULL on failure; call sfs_get_error() for details. */
SOKOL_FILESYSTEM_API_DECL sfs_file_t* sfs_open_file(const char* path, sfs_open_mode_t mode);

/* Close an open file and release its resources. Always succeeds on NULL input. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_close_file(sfs_file_t* file);

/* Read up to count bytes into buf.
   Returns the number of bytes actually read, 0 on EOF, or -1 on error. */
SOKOL_FILESYSTEM_API_DECL int64_t sfs_read_file(sfs_file_t* file, void* buf, int64_t count);

/* Write count bytes from buf.
   Returns the number of bytes actually written, or -1 on error. */
SOKOL_FILESYSTEM_API_DECL int64_t sfs_write_file(sfs_file_t* file, const void* buf, int64_t count);

/* Seek to offset relative to whence.
   Returns the resulting file position, or -1 on error. */
SOKOL_FILESYSTEM_API_DECL int64_t sfs_seek_file(sfs_file_t* file, int64_t offset, sfs_whence_t whence);

/* Return the current file position, or -1 on error. */
SOKOL_FILESYSTEM_API_DECL int64_t sfs_tell_file(sfs_file_t* file);

/* Return the file size in bytes without changing the current position,
   or -1 on error. */
SOKOL_FILESYSTEM_API_DECL int64_t sfs_get_file_size(sfs_file_t* file);

/* Flush any unwritten data to the OS. Returns SFS_RESULT_OK on success. */
SOKOL_FILESYSTEM_API_DECL sfs_result_t sfs_flush_file(sfs_file_t* file);

/* Returns true if the end of file has been reached on the last read. */
SOKOL_FILESYSTEM_API_DECL bool sfs_eof_file(sfs_file_t* file);

/* --- Error reporting --- */

/* Returns a human-readable string describing the last error that occurred
   on this thread. The string is valid until the next sfs_* call. */
SOKOL_FILESYSTEM_API_DECL const char* sfs_get_error(void);

/* =========================================================================
   IMPLEMENTATION
   ========================================================================= */

#ifdef SOKOL_FILESYSTEM_IMPL
#define SOKOL_FILESYSTEM_IMPL_INCLUDED (1)

#ifndef SOKOL_ASSERT
    #include <assert.h>
    #define SOKOL_ASSERT(c) assert(c)
#endif
#ifndef SOKOL_LOG
    #if defined(__ANDROID__)
        #include <android/log.h>
        #define SOKOL_LOG(msg) __android_log_print(ANDROID_LOG_INFO, "sokol_filesystem", "%s", msg)
    #else
        #include <stdio.h>
        #define SOKOL_LOG(msg) puts(msg)
    #endif
#endif
#ifndef SOKOL_API_IMPL
    #define SOKOL_API_IMPL
#endif
#ifndef SOKOL_UNREACHABLE
    #define SOKOL_UNREACHABLE() SOKOL_ASSERT(false)
#endif

/* -------------------------------------------------------------------------
   Internal helpers
   ------------------------------------------------------------------------- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Internal file handle wrapping the platform FILE* */
struct sfs_file_t {
    FILE* fp;
#if defined(__EMSCRIPTEN__)
    void* http_buf; /* malloc'd buffer for HTTP-fetched read content, freed on close */
#endif
#if defined(__ANDROID__)
    void* asset_buf; /* malloc'd buffer for APK-bundled asset read content, freed on close */
#endif
};

/* Thread-local error buffer */
static char _sfs_errbuf[512];

static void _sfs_set_error(const char* msg) {
    strncpy(_sfs_errbuf, msg ? msg : "unknown error", sizeof(_sfs_errbuf) - 1);
    _sfs_errbuf[sizeof(_sfs_errbuf) - 1] = '\0';
}

SOKOL_API_IMPL const char* sfs_get_error(void) {
    return _sfs_errbuf;
}

SOKOL_API_IMPL void sfs_free_path(char* path) {
    free(path);
}

static const char* _sfs_open_mode_to_str(sfs_open_mode_t mode) {
    switch (mode) {
        case SFS_OPEN_READ:         return "rb";
        case SFS_OPEN_WRITE:        return "wb";
        case SFS_OPEN_APPEND:       return "ab";
        case SFS_OPEN_READ_WRITE:   return "r+b";
        case SFS_OPEN_CREATE_WRITE: return "w+b";
        case SFS_OPEN_APPEND_READ:  return "a+b";
        default: _sfs_set_error("invalid open mode"); return NULL;
    }
}

/* =========================================================================
   PLATFORM: WINDOWS
   ========================================================================= */
#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <initguid.h>
#include <sys/types.h>
#include <sys/stat.h>

/* GUIDs for known folders (so we don't need newer SDK) */
DEFINE_GUID(_SFS_FOLDERID_Profile,      0x5E6C858F,0x0E22,0x4760,0x9A,0xFE,0xEA,0x33,0x17,0xB6,0x71,0x73);
DEFINE_GUID(_SFS_FOLDERID_Desktop,      0xB4BFCC3A,0xDB2C,0x424C,0xB0,0x29,0x7F,0xE9,0x9A,0x87,0xC6,0x41);
DEFINE_GUID(_SFS_FOLDERID_Documents,    0xFDD39AD0,0x238F,0x46AF,0xAD,0xB4,0x6C,0x85,0x48,0x03,0x69,0xC7);
DEFINE_GUID(_SFS_FOLDERID_Downloads,    0x374DE290,0x123F,0x4565,0x91,0x64,0x39,0xC4,0x92,0x5E,0x46,0x7B);
DEFINE_GUID(_SFS_FOLDERID_Music,        0x4BD8D571,0x6D19,0x48D3,0xBE,0x97,0x42,0x22,0x20,0x08,0x0E,0x43);
DEFINE_GUID(_SFS_FOLDERID_Pictures,     0x33E28130,0x4E1E,0x4676,0x83,0x5A,0x98,0x39,0x5C,0x3B,0xC3,0xBB);
DEFINE_GUID(_SFS_FOLDERID_SavedGames,   0x4C5C32FF,0xBB9D,0x43B0,0xB5,0xB4,0x2D,0x72,0xE5,0x4E,0xAA,0xA4);
DEFINE_GUID(_SFS_FOLDERID_Screenshots,  0xB7BEDE81,0xDF94,0x4682,0xA7,0xD8,0x57,0xA5,0x26,0x20,0xB8,0x6F);
DEFINE_GUID(_SFS_FOLDERID_Templates,    0xA63293E8,0x664E,0x48DB,0xA0,0x79,0xDF,0x75,0x9E,0x05,0x09,0xF7);
DEFINE_GUID(_SFS_FOLDERID_Videos,       0x18989B1D,0x99B5,0x455B,0x84,0x1C,0xAB,0x7C,0x74,0xE4,0xDD,0xFC);
DEFINE_GUID(_SFS_FOLDERID_PublicShare,  0xDFDF76A2,0x9351,0x49F7,0x83,0xD7,0x0E,0xBC,0xAD,0x18,0xF4,0x5B);

/* Windows: UTF-8 <-> wchar helpers */
static WCHAR* _sfs_utf8_to_wide(const char* utf8) {
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) return NULL;
    WCHAR* buf = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (!buf) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf, len);
    return buf;
}

static char* _sfs_wide_to_utf8(const WCHAR* wide) {
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char* buf = (char*)malloc(len);
    if (!buf) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, buf, len, NULL, NULL);
    return buf;
}

/* Android stub (no-op on Windows) */
SOKOL_API_IMPL void sfs_set_android_internal_path(const char* path) {
    (void)path;
}

SOKOL_API_IMPL char* sfs_get_base_path(void) {
    DWORD buflen = 256;
    WCHAR* path = NULL;
    char* result = NULL;
    DWORD len = 0;

    while (1) {
        WCHAR* tmp = (WCHAR*)realloc(path, buflen * sizeof(WCHAR));
        if (!tmp) { free(path); _sfs_set_error("out of memory"); return NULL; }
        path = tmp;
        len = GetModuleFileNameW(NULL, path, buflen);
        if (len < buflen - 1) break;
        buflen *= 2;
    }

    if (len == 0) {
        free(path);
        _sfs_set_error("GetModuleFileNameW failed");
        return NULL;
    }

    /* chop off filename, keep trailing backslash */
    for (int i = (int)len - 1; i > 0; i--) {
        if (path[i] == L'\\') { path[i + 1] = L'\0'; break; }
    }

    result = _sfs_wide_to_utf8(path);
    free(path);
    return result;
}

SOKOL_API_IMPL char* sfs_get_pref_path(const char* org, const char* app) {
    if (!org || !app) { _sfs_set_error("invalid parameter"); return NULL; }

    WCHAR roaming[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, roaming);
    if (!SUCCEEDED(hr)) { _sfs_set_error("SHGetFolderPathW failed"); return NULL; }

    WCHAR* worg = _sfs_utf8_to_wide(org);
    WCHAR* wapp = _sfs_utf8_to_wide(app);
    if (!worg || !wapp) { free(worg); free(wapp); _sfs_set_error("out of memory"); return NULL; }

    size_t needed = wcslen(roaming) + wcslen(worg) + wcslen(wapp) + 4;
    WCHAR* wpath = (WCHAR*)malloc(needed * sizeof(WCHAR));
    if (!wpath) { free(worg); free(wapp); _sfs_set_error("out of memory"); return NULL; }

    if (*org) {
        _snwprintf(wpath, needed, L"%s\\%s\\%s\\", roaming, worg, wapp);
    } else {
        _snwprintf(wpath, needed, L"%s\\%s\\", roaming, wapp);
    }
    free(worg); free(wapp);

    /* create directories */
    for (WCHAR* p = wpath + 1; *p; p++) {
        if (*p == L'\\') {
            *p = L'\0';
            CreateDirectoryW(wpath, NULL);
            *p = L'\\';
        }
    }
    CreateDirectoryW(wpath, NULL);

    char* result = _sfs_wide_to_utf8(wpath);
    free(wpath);
    return result;
}

SOKOL_API_IMPL char* sfs_get_user_folder(sfs_folder_t folder) {
    const GUID* guid = NULL;
    switch (folder) {
        case SFS_FOLDER_HOME:        guid = &_SFS_FOLDERID_Profile;     break;
        case SFS_FOLDER_DESKTOP:     guid = &_SFS_FOLDERID_Desktop;     break;
        case SFS_FOLDER_DOCUMENTS:   guid = &_SFS_FOLDERID_Documents;   break;
        case SFS_FOLDER_DOWNLOADS:   guid = &_SFS_FOLDERID_Downloads;   break;
        case SFS_FOLDER_MUSIC:       guid = &_SFS_FOLDERID_Music;       break;
        case SFS_FOLDER_PICTURES:    guid = &_SFS_FOLDERID_Pictures;    break;
        case SFS_FOLDER_PUBLICSHARE: guid = &_SFS_FOLDERID_PublicShare; break;
        case SFS_FOLDER_SAVEDGAMES:  guid = &_SFS_FOLDERID_SavedGames;  break;
        case SFS_FOLDER_SCREENSHOTS: guid = &_SFS_FOLDERID_Screenshots; break;
        case SFS_FOLDER_TEMPLATES:   guid = &_SFS_FOLDERID_Templates;   break;
        case SFS_FOLDER_VIDEOS:      guid = &_SFS_FOLDERID_Videos;      break;
        default: _sfs_set_error("unknown folder"); return NULL;
    }

    WCHAR* wpath = NULL;
    if (FAILED(SHGetKnownFolderPath(guid, KF_FLAG_CREATE, NULL, &wpath))) {
        /* fall back to CSIDL for older Windows */
        int csidl = -1;
        switch (folder) {
            case SFS_FOLDER_HOME:       csidl = CSIDL_PROFILE;      break;
            case SFS_FOLDER_DESKTOP:    csidl = CSIDL_DESKTOPDIRECTORY; break;
            case SFS_FOLDER_DOCUMENTS:  csidl = CSIDL_PERSONAL;     break;
            case SFS_FOLDER_MUSIC:      csidl = CSIDL_MYMUSIC;      break;
            case SFS_FOLDER_PICTURES:   csidl = CSIDL_MYPICTURES;   break;
            case SFS_FOLDER_VIDEOS:     csidl = CSIDL_MYVIDEO;      break;
            case SFS_FOLDER_TEMPLATES:  csidl = CSIDL_TEMPLATES;    break;
            default: _sfs_set_error("folder not available"); return NULL;
        }
        WCHAR buf[MAX_PATH];
        if (!SUCCEEDED(SHGetFolderPathW(NULL, csidl, NULL, 0, buf))) {
            _sfs_set_error("SHGetFolderPathW failed"); return NULL;
        }
        size_t len = wcslen(buf);
        char* result = _sfs_wide_to_utf8(buf);
        if (result && result[strlen(result)-1] != '\\') {
            char* r2 = (char*)malloc(strlen(result) + 2);
            if (r2) { strcpy(r2, result); strcat(r2, "\\"); free(result); result = r2; }
        }
        return result;
    }

    char* result = _sfs_wide_to_utf8(wpath);
    CoTaskMemFree(wpath);
    if (result && result[strlen(result)-1] != '\\') {
        char* r2 = (char*)malloc(strlen(result) + 2);
        if (r2) { strcpy(r2, result); strcat(r2, "\\"); free(result); result = r2; }
    }
    return result;
}

SOKOL_API_IMPL char* sfs_get_current_directory(void) {
    DWORD len = GetCurrentDirectoryW(0, NULL);
    WCHAR* buf = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (!buf) return NULL;
    GetCurrentDirectoryW(len, buf);
    char* result = _sfs_wide_to_utf8(buf);
    free(buf);
    if (result && result[strlen(result)-1] != '\\') {
        char* r2 = (char*)malloc(strlen(result) + 2);
        if (r2) { strcpy(r2, result); strcat(r2, "\\"); free(result); result = r2; }
    }
    return result;
}

SOKOL_API_IMPL sfs_result_t sfs_set_current_directory(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    WCHAR* wpath = _sfs_utf8_to_wide(path);
    if (!wpath) return SFS_RESULT_ERROR;
    BOOL ok = SetCurrentDirectoryW(wpath);
    free(wpath);
    if (!ok) { _sfs_set_error("SetCurrentDirectoryW failed"); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL char* sfs_get_temp_dir(void) {
    WCHAR buf[MAX_PATH + 2];
    DWORD len = GetTempPathW(MAX_PATH + 1, buf);
    if (len == 0) { _sfs_set_error("GetTempPathW failed"); return NULL; }
    /* GetTempPathW always appends a trailing backslash */
    return _sfs_wide_to_utf8(buf);
}

SOKOL_API_IMPL sfs_result_t sfs_create_directory(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    WCHAR* wpath = _sfs_utf8_to_wide(path);
    if (!wpath) return SFS_RESULT_ERROR;

    /* walk and create each component */
    for (WCHAR* p = wpath + 1; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            *p = L'\0';
            CreateDirectoryW(wpath, NULL);
            *p = L'\\';
        }
    }
    BOOL ok = CreateDirectoryW(wpath, NULL);
    free(wpath);
    if (!ok && GetLastError() != ERROR_ALREADY_EXISTS) {
        _sfs_set_error("CreateDirectoryW failed");
        return SFS_RESULT_ERROR;
    }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_remove_path(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    WCHAR* wpath = _sfs_utf8_to_wide(path);
    if (!wpath) return SFS_RESULT_ERROR;

    DWORD attr = GetFileAttributesW(wpath);
    if (attr == INVALID_FILE_ATTRIBUTES) { free(wpath); return SFS_RESULT_OK; /* already gone */ }

    BOOL ok;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        ok = RemoveDirectoryW(wpath);
    } else {
        ok = DeleteFileW(wpath);
    }
    free(wpath);
    if (!ok) { _sfs_set_error("remove failed"); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_rename_path(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return SFS_RESULT_INVALID_PARAM;
    WCHAR* wo = _sfs_utf8_to_wide(oldpath);
    WCHAR* wn = _sfs_utf8_to_wide(newpath);
    if (!wo || !wn) { free(wo); free(wn); return SFS_RESULT_ERROR; }
    BOOL ok = MoveFileExW(wo, wn, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    free(wo); free(wn);
    if (!ok) { _sfs_set_error("MoveFileExW failed"); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_copy_file(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return SFS_RESULT_INVALID_PARAM;
    WCHAR* wo = _sfs_utf8_to_wide(oldpath);
    WCHAR* wn = _sfs_utf8_to_wide(newpath);
    if (!wo || !wn) { free(wo); free(wn); return SFS_RESULT_ERROR; }
    BOOL ok = CopyFileW(wo, wn, FALSE);
    free(wo); free(wn);
    if (!ok) { _sfs_set_error("CopyFileW failed"); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_get_path_info(const char* path, sfs_path_info_t* out_info) {
    if (!path || !out_info) return SFS_RESULT_INVALID_PARAM;
    memset(out_info, 0, sizeof(*out_info));

    WCHAR* wpath = _sfs_utf8_to_wide(path);
    if (!wpath) return SFS_RESULT_ERROR;

    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &data)) {
        DWORD err = GetLastError();
        free(wpath);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
            return SFS_RESULT_NOT_FOUND;
        _sfs_set_error("GetFileAttributesExW failed");
        return SFS_RESULT_ERROR;
    }
    free(wpath);

    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        out_info->type = SFS_PATHTYPE_DIRECTORY;
        out_info->size = 0;
    } else {
        out_info->type = SFS_PATHTYPE_FILE;
        out_info->size = (int64_t)(((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow);
    }

    /* Convert FILETIME (100ns intervals since 1601) to Unix epoch */
    #define _SFS_FILETIME_TO_UNIX(ft) \
        ((int64_t)((((uint64_t)(ft).dwHighDateTime << 32) | (ft).dwLowDateTime) / 10000000ULL) - 11644473600LL)

    out_info->create_time = _SFS_FILETIME_TO_UNIX(data.ftCreationTime);
    out_info->modify_time = _SFS_FILETIME_TO_UNIX(data.ftLastWriteTime);
    out_info->access_time = _SFS_FILETIME_TO_UNIX(data.ftLastAccessTime);
    #undef _SFS_FILETIME_TO_UNIX

    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_file_t* sfs_open_file(const char* path, sfs_open_mode_t mode) {
    if (!path) { _sfs_set_error("invalid parameter"); return NULL; }
    const char* modestr = _sfs_open_mode_to_str(mode);
    if (!modestr) return NULL;
    WCHAR* wpath = _sfs_utf8_to_wide(path);
    if (!wpath) { _sfs_set_error("out of memory"); return NULL; }
    WCHAR* wmode = _sfs_utf8_to_wide(modestr);
    if (!wmode) { free(wpath); _sfs_set_error("out of memory"); return NULL; }
    FILE* fp = _wfopen(wpath, wmode);
    free(wpath); free(wmode);
    if (!fp) { _sfs_set_error(strerror(errno)); return NULL; }
    sfs_file_t* f = (sfs_file_t*)malloc(sizeof(sfs_file_t));
    if (!f) { fclose(fp); _sfs_set_error("out of memory"); return NULL; }
    f->fp = fp;
    return f;
}

SOKOL_API_IMPL sfs_result_t sfs_enumerate_directory(const char* path,
                                                      sfs_enumerate_callback_t callback,
                                                      void* userdata)
{
    if (!path || !callback) return SFS_RESULT_INVALID_PARAM;

    /* Build "path\*" pattern for FindFirstFileEx */
    int patlen = (int)strlen(path);
    char* pat = (char*)malloc(patlen + 4);
    if (!pat) return SFS_RESULT_ERROR;
    strcpy(pat, path);
    /* trim trailing separators */
    while (patlen > 0 && (pat[patlen-1] == '\\' || pat[patlen-1] == '/'))
        pat[--patlen] = '\0';
    pat[patlen]   = '\\';
    pat[patlen+1] = '*';
    pat[patlen+2] = '\0';

    /* dirname (with trailing backslash) for the callback */
    char dirname[512];
    snprintf(dirname, sizeof(dirname), "%.*s\\", patlen, path);

    WCHAR* wpat = _sfs_utf8_to_wide(pat);
    free(pat);
    if (!wpat) return SFS_RESULT_ERROR;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(wpat, FindExInfoStandard, &fd, FindExSearchNameMatch, NULL, 0);
    free(wpat);

    if (h == INVALID_HANDLE_VALUE) {
        _sfs_set_error("FindFirstFileExW failed");
        return SFS_RESULT_ERROR;
    }

    sfs_result_t result = SFS_RESULT_OK;
    do {
        const WCHAR* fn = fd.cFileName;
        if (fn[0] == L'.' && (fn[1] == L'\0' || (fn[1] == L'.' && fn[2] == L'\0')))
            continue;
        char* utf8 = _sfs_wide_to_utf8(fn);
        if (!utf8) { result = SFS_RESULT_ERROR; break; }
        sfs_enum_result_t r = callback(userdata, dirname, utf8);
        free(utf8);
        if (r == SFS_ENUM_FAILURE) { result = SFS_RESULT_ERROR; break; }
        if (r == SFS_ENUM_SUCCESS) break;
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return result;
}

/* =========================================================================
   PLATFORM: Apple (macOS / iOS) — Objective-C
   ========================================================================= */
#elif defined(__APPLE__)

#include <TargetConditionals.h>
#include <Foundation/Foundation.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

SOKOL_API_IMPL void sfs_set_android_internal_path(const char* path) { (void)path; }

SOKOL_API_IMPL char* sfs_get_base_path(void) {
    @autoreleasepool {
#if TARGET_OS_IOS || TARGET_OS_TV || TARGET_OS_WATCH
        /* iOS/tvOS/watchOS: always running inside an app bundle */
        const char* base = [[NSBundle mainBundle].resourcePath fileSystemRepresentation];
        if (!base) { _sfs_set_error("NSBundle resourcePath nil"); return NULL; }
        size_t len = strlen(base) + 2;
        char* result = (char*)malloc(len);
        if (result) snprintf(result, len, "%s/", base);
        return result;
#else
        /* macOS: only use resourcePath when inside a proper .app bundle */
        NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath && [bundlePath hasSuffix:@".app"]) {
            const char* base = [[NSBundle mainBundle].resourcePath fileSystemRepresentation];
            if (!base) { _sfs_set_error("NSBundle resourcePath nil"); return NULL; }
            size_t len = strlen(base) + 2;
            char* result = (char*)malloc(len);
            if (result) snprintf(result, len, "%s/", base);
            return result;
        } else {
            /* Not in a .app bundle (debug/standalone) — use current working directory */
            char buf[PATH_MAX];
            if (!getcwd(buf, sizeof(buf))) { _sfs_set_error(strerror(errno)); return NULL; }
            size_t len = strlen(buf) + 2;
            char* result = (char*)malloc(len);
            if (result) snprintf(result, len, "%s/", buf);
            return result;
        }
#endif
    }
}

SOKOL_API_IMPL char* sfs_get_pref_path(const char* org, const char* app) {
    if (!org || !app) { _sfs_set_error("invalid parameter"); return NULL; }
    @autoreleasepool {
#if TARGET_OS_TV
        NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
#else
        NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
#endif
        if ([dirs count] == 0) { _sfs_set_error("NSSearchPathForDirectoriesInDomains failed"); return NULL; }
        const char* base = [[dirs objectAtIndex:0] fileSystemRepresentation];
        size_t needed = strlen(base) + strlen(org) + strlen(app) + 4;
        char* result = (char*)malloc(needed);
        if (!result) return NULL;
        if (*org) {
            snprintf(result, needed, "%s/%s/%s/", base, org, app);
        } else {
            snprintf(result, needed, "%s/%s/", base, app);
        }
        /* create intermediate directories */
        for (char* p = result + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(result, 0700);
                *p = '/';
            }
        }
        mkdir(result, 0700);
        return result;
    }
}

SOKOL_API_IMPL char* sfs_get_user_folder(sfs_folder_t folder) {
    @autoreleasepool {
        NSSearchPathDirectory dir;
        switch (folder) {
            case SFS_FOLDER_HOME:       return strdup(NSHomeDirectory().fileSystemRepresentation);
            case SFS_FOLDER_DESKTOP:    dir = NSDesktopDirectory;       break;
            case SFS_FOLDER_DOCUMENTS:  dir = NSDocumentDirectory;      break;
            case SFS_FOLDER_DOWNLOADS:  dir = NSDownloadsDirectory;     break;
            case SFS_FOLDER_MUSIC:      dir = NSMusicDirectory;         break;
            case SFS_FOLDER_PICTURES:   dir = NSPicturesDirectory;      break;
            case SFS_FOLDER_VIDEOS:     dir = NSMoviesDirectory;        break;
            case SFS_FOLDER_TEMPLATES:
            case SFS_FOLDER_SAVEDGAMES:
            case SFS_FOLDER_SCREENSHOTS:
            case SFS_FOLDER_PUBLICSHARE:
                _sfs_set_error("folder not supported on Apple");
                return NULL;
            default: _sfs_set_error("unknown folder"); return NULL;
        }
        NSArray* arr = NSSearchPathForDirectoriesInDomains(dir, NSUserDomainMask, YES);
        if ([arr count] == 0) { _sfs_set_error("folder not found"); return NULL; }
        const char* base = [[arr objectAtIndex:0] fileSystemRepresentation];
        size_t len = strlen(base) + 2;
        char* result = (char*)malloc(len);
        if (result) snprintf(result, len, "%s/", base);
        return result;
    }
}

SOKOL_API_IMPL char* sfs_get_current_directory(void) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) { _sfs_set_error(strerror(errno)); return NULL; }
    size_t len = strlen(buf) + 2;
    char* result = (char*)malloc(len);
    if (result) snprintf(result, len, "%s/", buf);
    return result;
}

SOKOL_API_IMPL char* sfs_get_temp_dir(void) {
    @autoreleasepool {
        NSString* tmp = NSTemporaryDirectory();
        if (!tmp) { _sfs_set_error("NSTemporaryDirectory() returned nil"); return NULL; }
        const char* cstr = [tmp fileSystemRepresentation];
        size_t len = strlen(cstr);
        char* result = (char*)malloc(len + 2);
        if (!result) return NULL;
        memcpy(result, cstr, len);
        if (len == 0 || result[len - 1] != '/') result[len++] = '/';
        result[len] = '\0';
        return result;
    }
}

/* POSIX helpers shared by Apple, Linux, Android, Emscripten */
#define _SFS_POSIX_IMPL 1

/* =========================================================================
   PLATFORM: Android
   ========================================================================= */
#elif defined(__ANDROID__)

#include <android/native_activity.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <android/log.h>

/* Forward declaration — avoids a hard dependency on sokol_app.h.
   Resolved at link time from sokol_app.h's SOKOL_APP_IMPL block. */
extern const void* sapp_android_get_native_activity(void);

static char _sfs_android_internal_path[512] = {0};

SOKOL_API_IMPL void sfs_set_android_internal_path(const char* path) {
    if (path) {
        strncpy(_sfs_android_internal_path, path, sizeof(_sfs_android_internal_path) - 1);
    }
}

static const char* _sfs_android_get_data_path(void) {
    /* Prefer an explicitly set override */
    if (_sfs_android_internal_path[0]) {
        return _sfs_android_internal_path;
    }
    /* Otherwise read directly from ANativeActivity::internalDataPath —
       identical to Java's getFilesDir().getAbsolutePath() */
    const ANativeActivity* activity =
        (const ANativeActivity*)sapp_android_get_native_activity();
    if (activity && activity->internalDataPath) {
        return activity->internalDataPath;
    }
    return "/data/data/unknown";
}

SOKOL_API_IMPL char* sfs_get_base_path(void) {
    return strdup("./");
}

SOKOL_API_IMPL char* sfs_get_pref_path(const char* org, const char* app) {
    (void)org; (void)app;
    const char* base = _sfs_android_get_data_path();
    size_t len = strlen(base) + 2;
    char* result = (char*)malloc(len);
    if (result) snprintf(result, len, "%s/", base);
    return result;
}

SOKOL_API_IMPL char* sfs_get_user_folder(sfs_folder_t folder) {
    (void)folder;
    _sfs_set_error("sfs_get_user_folder not supported on Android");
    return NULL;
}

SOKOL_API_IMPL char* sfs_get_current_directory(void) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) { _sfs_set_error(strerror(errno)); return NULL; }
    size_t len = strlen(buf) + 2;
    char* result = (char*)malloc(len);
    if (result) snprintf(result, len, "%s/", buf);
    return result;
}

SOKOL_API_IMPL char* sfs_get_temp_dir(void) {
    /* Use a tmp/ subdirectory inside the app's internal data path */
    const char* base = _sfs_android_get_data_path();
    static const char suffix[] = "/tmp/";
    size_t len = strlen(base) + sizeof(suffix);
    char* result = (char*)malloc(len);
    if (!result) return NULL;
    snprintf(result, len, "%s%s", base, suffix);
    mkdir(result, 0700);
    return result;
}

#include <android/asset_manager.h>

static AAssetManager* _sfs_get_asset_manager(void) {
    const ANativeActivity* activity = (const ANativeActivity*)sapp_android_get_native_activity();
    return activity ? activity->assetManager : NULL;
}

/* Strip leading "./" or "/" so AAssetManager can find bundled assets. */
static const char* _sfs_android_asset_path(const char* path) {
    while (path[0] == '.' && path[1] == '/') path += 2;
    while (path[0] == '/') path++;
    return path;
}

SOKOL_API_IMPL sfs_result_t sfs_get_path_info(const char* path, sfs_path_info_t* out_info) {
    if (!path || !out_info) return SFS_RESULT_INVALID_PARAM;
    memset(out_info, 0, sizeof(*out_info));

    /* First try POSIX stat (works for internal data path / pref path) */
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode))       out_info->type = SFS_PATHTYPE_FILE;
        else if (S_ISDIR(st.st_mode))  out_info->type = SFS_PATHTYPE_DIRECTORY;
        else                            out_info->type = SFS_PATHTYPE_OTHER;
        out_info->size        = (int64_t)st.st_size;
        out_info->modify_time = (int64_t)st.st_mtime;
        out_info->access_time = (int64_t)st.st_atime;
        out_info->create_time = (int64_t)st.st_ctime;
        return SFS_RESULT_OK;
    }

    /* Fall back to AAssetManager for APK-bundled assets */
    AAssetManager* mgr = _sfs_get_asset_manager();
    if (mgr) {
        const char* apath = _sfs_android_asset_path(path);
        AAsset* asset = AAssetManager_open(mgr, apath, AASSET_MODE_UNKNOWN);
        if (asset) {
            out_info->type = SFS_PATHTYPE_FILE;
            out_info->size = (int64_t)AAsset_getLength64(asset);
            AAsset_close(asset);
            return SFS_RESULT_OK;
        }
        AAssetDir* dir = AAssetManager_openDir(mgr, apath);
        if (dir) {
            out_info->type = SFS_PATHTYPE_DIRECTORY;
            AAssetDir_close(dir);
            return SFS_RESULT_OK;
        }
    }
    return SFS_RESULT_NOT_FOUND;
}

SOKOL_API_IMPL sfs_file_t* sfs_open_file(const char* path, sfs_open_mode_t mode) {
    if (!path) { _sfs_set_error("invalid parameter"); return NULL; }
    const char* modestr = _sfs_open_mode_to_str(mode);
    if (!modestr) return NULL;

    /* Try POSIX first (works for pref path, temp files, absolute paths) */
    FILE* fp = fopen(path, modestr);
    if (fp) {
        sfs_file_t* f = (sfs_file_t*)malloc(sizeof(sfs_file_t));
        if (!f) { fclose(fp); _sfs_set_error("out of memory"); return NULL; }
        f->fp = fp;
        f->asset_buf = NULL;
        return f;
    }

    /* For read-only mode, fall back to AAssetManager for APK-bundled assets */
    if (mode == SFS_OPEN_READ) {
        AAssetManager* mgr = _sfs_get_asset_manager();
        if (mgr) {
            const char* apath = _sfs_android_asset_path(path);
            AAsset* asset = AAssetManager_open(mgr, apath, AASSET_MODE_BUFFER);
            if (asset) {
                off64_t size = AAsset_getLength64(asset);
                void* buf = malloc((size_t)(size + 1));
                if (!buf) { AAsset_close(asset); _sfs_set_error("out of memory"); return NULL; }
                AAsset_read(asset, buf, (size_t)size);
                AAsset_close(asset);
                FILE* mfp = fmemopen(buf, (size_t)size, "rb");
                if (!mfp) { free(buf); _sfs_set_error("fmemopen failed"); return NULL; }
                sfs_file_t* f = (sfs_file_t*)malloc(sizeof(sfs_file_t));
                if (!f) { fclose(mfp); free(buf); _sfs_set_error("out of memory"); return NULL; }
                f->fp = mfp;
                f->asset_buf = buf;
                return f;
            }
        }
    }

    _sfs_set_error(strerror(errno));
    return NULL;
}

#define _SFS_ANDROID_ASSET_FALLBACK 1
#define _SFS_POSIX_IMPL 1

/* =========================================================================
   PLATFORM: Emscripten
   On Emscripten, read-only path checks and file reads use synchronous
   XMLHttpRequest (valid in Web Workers, where .NET WASM runs) so that
   assets served by the web server are accessible without --preload-file.
   Write operations still use POSIX on the MEMFS (pref path).
   Approach mirrors sokol_fetch.h which also uses the browser fetch/XHR API.
   ========================================================================= */
#elif defined(__EMSCRIPTEN__)

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <emscripten/emscripten.h>

SOKOL_API_IMPL void sfs_set_android_internal_path(const char* path) { (void)path; }

SOKOL_API_IMPL char* sfs_get_base_path(void) {
    return strdup("/");
}

SOKOL_API_IMPL char* sfs_get_pref_path(const char* org, const char* app) {
    if (!org || !app) { _sfs_set_error("invalid parameter"); return NULL; }
    const char* base = "/libsokol";
    size_t len = strlen(base) + strlen(org) + strlen(app) + 4;
    char* result = (char*)malloc(len);
    if (!result) return NULL;
    if (*org) {
        snprintf(result, len, "%s/%s/%s/", base, org, app);
    } else {
        snprintf(result, len, "%s/%s/", base, app);
    }
    for (char* p = result + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(result, 0700) != 0 && errno != EEXIST) {
                _sfs_set_error(strerror(errno));
                free(result);
                return NULL;
            }
            *p = '/';
        }
    }
    if (mkdir(result, 0700) != 0 && errno != EEXIST) {
        _sfs_set_error(strerror(errno));
        free(result);
        return NULL;
    }
    return result;
}

SOKOL_API_IMPL char* sfs_get_user_folder(sfs_folder_t folder) {
    if (folder != SFS_FOLDER_HOME) {
        _sfs_set_error("Emscripten only supports SFS_FOLDER_HOME");
        return NULL;
    }
    const char* home = getenv("HOME");
    if (!home) { _sfs_set_error("$HOME not set"); return NULL; }
    size_t len = strlen(home) + 2;
    char* result = (char*)malloc(len);
    if (result) snprintf(result, len, "%s/", home);
    return result;
}

SOKOL_API_IMPL char* sfs_get_current_directory(void) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) { _sfs_set_error(strerror(errno)); return NULL; }
    size_t len = strlen(buf) + 2;
    char* result = (char*)malloc(len);
    if (result) snprintf(result, len, "%s/", buf);
    return result;
}

SOKOL_API_IMPL char* sfs_get_temp_dir(void) {
    return strdup("/tmp/");
}

/* ---------------------------------------------------------------------------
   Synchronous XHR helpers — valid in Web Workers (where .NET WASM runs).
   These mirror sokol_fetch.h's EM_JS pattern but use sync XHR so the
   sokol_filesystem.h synchronous C API is preserved.
   --------------------------------------------------------------------------- */

/* Synchronous HEAD request. Stores Content-Length and Last-Modified from response headers.
   Returns HTTP status code or 0 on network error. */
EM_JS(int, _sfs_http_head, (const char* path_cstr), {
    try {
        var path = UTF8ToString(path_cstr);
        var xhr = new XMLHttpRequest();
        xhr.open('HEAD', path, false);
        xhr.send();
        var cl = parseInt(xhr.getResponseHeader('Content-Length'));
        Module._sfsContentLength = isNaN(cl) ? -1 : cl;
        var lm = xhr.getResponseHeader('Last-Modified');
        if (lm) {
            var t = Math.floor(new Date(lm).getTime() / 1000);
            Module._sfsLastModified = isNaN(t) ? 0 : t;
        } else {
            Module._sfsLastModified = 0;
        }
        return xhr.status;
    } catch(e) { Module._sfsContentLength = -1; Module._sfsLastModified = 0; return 0; }
})

/* Returns the Content-Length stored by the last _sfs_http_head call, or -1. */
EM_JS(int, _sfs_http_content_length, (), {
    return (Module._sfsContentLength !== undefined) ? Module._sfsContentLength : -1;
})

/* Returns the Last-Modified Unix timestamp stored by the last _sfs_http_head call, or 0. */
EM_JS(double, _sfs_http_last_modified, (), {
    return (Module._sfsLastModified !== undefined) ? Module._sfsLastModified : 0;
})

/* Synchronous GET request using overrideMimeType so binary data is safe on the
   main thread. responseType='arraybuffer' is forbidden for sync XHR on the main
   thread (InvalidAccessError), but overrideMimeType+'x-user-defined' works.
   Stores result in Module._sfsCache. Returns byte count or -1 on failure. */
EM_JS(int, _sfs_http_get_cached, (const char* path_cstr), {
    try {
        var path = UTF8ToString(path_cstr);
        var xhr = new XMLHttpRequest();
        xhr.open('GET', path, false);
        xhr.overrideMimeType('text/plain; charset=x-user-defined');
        xhr.send();
        if (xhr.status === 200) {
            var text = xhr.responseText;
            var arr = new Uint8Array(text.length);
            for (var i = 0; i < text.length; i++) {
                arr[i] = text.charCodeAt(i) & 0xFF;
            }
            Module._sfsCache = arr;
            return arr.length;
        }
    } catch(e) {}
    Module._sfsCache = null;
    return -1;
})

/* Copies the cached buffer into a C heap buffer and clears the cache. */
EM_JS(void, _sfs_http_copy_cache, (void* buf_ptr), {
    if (Module._sfsCache) {
        HEAPU8.set(Module._sfsCache, buf_ptr);
        Module._sfsCache = null;
    }
})

/* ---------------------------------------------------------------------------
   sfs_get_path_info: try POSIX stat first (for MEMFS pref-path / tmp dirs),
   then fall back to HTTP HEAD for web-served assets.
   --------------------------------------------------------------------------- */
SOKOL_API_IMPL sfs_result_t sfs_get_path_info(const char* path, sfs_path_info_t* out_info) {
    if (!path || !out_info) return SFS_RESULT_INVALID_PARAM;
    memset(out_info, 0, sizeof(*out_info));

    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISREG(st.st_mode))       out_info->type = SFS_PATHTYPE_FILE;
        else if (S_ISDIR(st.st_mode))  out_info->type = SFS_PATHTYPE_DIRECTORY;
        else                            out_info->type = SFS_PATHTYPE_OTHER;
        out_info->size        = (int64_t)st.st_size;
        out_info->modify_time = (int64_t)st.st_mtime;
        out_info->access_time = (int64_t)st.st_atime;
        out_info->create_time = (int64_t)st.st_ctime;
        return SFS_RESULT_OK;
    }

    /* POSIX stat failed — try HTTP HEAD for web-served assets */
    int status = _sfs_http_head(path);
    if (status == 200) {
        out_info->type = SFS_PATHTYPE_FILE;
        int cl = _sfs_http_content_length();
        if (cl >= 0) out_info->size = (int64_t)cl;
        out_info->modify_time = (int64_t)_sfs_http_last_modified();
        return SFS_RESULT_OK;
    } else if (status == 0 || status == 404) {
        return SFS_RESULT_NOT_FOUND;
    }
    _sfs_set_error("HTTP error checking path");
    return SFS_RESULT_ERROR;
}

/* ---------------------------------------------------------------------------
   sfs_open_file: for read modes, try synchronous HTTP GET first (web assets),
   then fall back to POSIX fopen (MEMFS paths). Write modes use POSIX directly.
   --------------------------------------------------------------------------- */
SOKOL_API_IMPL sfs_file_t* sfs_open_file(const char* path, sfs_open_mode_t mode) {
    if (!path) { _sfs_set_error("invalid parameter"); return NULL; }

    if (mode == SFS_OPEN_READ) {
        int size = _sfs_http_get_cached(path);
        if (size >= 0) {
            /* Allocate at least 1 byte so malloc never returns NULL for empty files */
            void* buf = malloc((size_t)(size > 0 ? size : 1));
            if (!buf) { _sfs_set_error("out of memory"); return NULL; }
            if (size > 0) { _sfs_http_copy_cache(buf); }
            FILE* fp = fmemopen(buf, (size_t)(size > 0 ? (size_t)size : 0), "rb");
            if (!fp) { free(buf); _sfs_set_error(strerror(errno)); return NULL; }
            sfs_file_t* f = (sfs_file_t*)malloc(sizeof(sfs_file_t));
            if (!f) { fclose(fp); free(buf); _sfs_set_error("out of memory"); return NULL; }
            f->fp = fp;
            f->http_buf = buf;
            return f;
        }
        /* HTTP failed — fall through to POSIX (handles MEMFS paths) */
    }

    const char* modestr = _sfs_open_mode_to_str(mode);
    if (!modestr) return NULL;
    FILE* fp = fopen(path, modestr);
    if (!fp) { _sfs_set_error(strerror(errno)); return NULL; }
    sfs_file_t* f = (sfs_file_t*)malloc(sizeof(sfs_file_t));
    if (!f) { fclose(fp); _sfs_set_error("out of memory"); return NULL; }
    f->fp = fp;
    f->http_buf = NULL;
    return f;
}

SOKOL_API_IMPL sfs_result_t sfs_create_directory(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    size_t len = strlen(path);
    char* tmp = (char*)malloc(len + 2);
    if (!tmp) return SFS_RESULT_ERROR;
    memcpy(tmp, path, len + 1);
    if (tmp[len-1] == '/') tmp[--len] = '\0';
    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        _sfs_set_error(strerror(errno)); free(tmp); return SFS_RESULT_ERROR;
    }
    free(tmp);
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_remove_path(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    if (remove(path) != 0) {
        if (errno == ENOENT) return SFS_RESULT_OK;
        _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR;
    }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_rename_path(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return SFS_RESULT_INVALID_PARAM;
    if (rename(oldpath, newpath) != 0) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_copy_file(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return SFS_RESULT_INVALID_PARAM;
    /* Try HTTP GET for source (web-served asset), write to POSIX (pref path) */
    int size = _sfs_http_get_cached(oldpath);
    if (size >= 0) {
        void* buf = malloc((size_t)(size > 0 ? size : 1));
        if (!buf) return SFS_RESULT_ERROR;
        if (size > 0) { _sfs_http_copy_cache(buf); }
        FILE* dst = fopen(newpath, "wb");
        if (!dst) { free(buf); _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
        sfs_result_t result = SFS_RESULT_OK;
        if (size > 0 && fwrite(buf, 1, (size_t)size, dst) != (size_t)size) {
            _sfs_set_error(strerror(errno)); result = SFS_RESULT_ERROR;
        }
        fclose(dst); free(buf);
        return result;
    }
    /* Both paths in MEMFS — plain POSIX copy */
    FILE* src = fopen(oldpath, "rb");
    if (!src) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
    FILE* dst = fopen(newpath, "wb");
    if (!dst) { _sfs_set_error(strerror(errno)); fclose(src); return SFS_RESULT_ERROR; }
    char tmp_buf[4096];
    sfs_result_t result = SFS_RESULT_OK;
    size_t n;
    while ((n = fread(tmp_buf, 1, sizeof(tmp_buf), src)) > 0) {
        if (fwrite(tmp_buf, 1, n, dst) != n) {
            _sfs_set_error(strerror(errno)); result = SFS_RESULT_ERROR; break;
        }
    }
    fclose(src); fclose(dst);
    return result;
}

SOKOL_API_IMPL sfs_result_t sfs_enumerate_directory(const char* path,
                                                      sfs_enumerate_callback_t callback,
                                                      void* userdata)
{
    if (!path || !callback) return SFS_RESULT_INVALID_PARAM;
    size_t plen = strlen(path);
    char* dirpath = (char*)malloc(plen + 2);
    if (!dirpath) return SFS_RESULT_ERROR;
    memcpy(dirpath, path, plen + 1);
    while (plen > 0 && dirpath[plen-1] == '/') dirpath[--plen] = '\0';
    dirpath[plen] = '/'; dirpath[plen+1] = '\0';
    DIR* d = opendir(dirpath);
    if (!d) { _sfs_set_error(strerror(errno)); free(dirpath); return SFS_RESULT_ERROR; }
    sfs_result_t result = SFS_RESULT_OK;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        sfs_enum_result_t r = callback(userdata, dirpath, ent->d_name);
        if (r == SFS_ENUM_FAILURE) { result = SFS_RESULT_ERROR; break; }
        if (r == SFS_ENUM_SUCCESS) break;
    }
    closedir(d); free(dirpath);
    return result;
}

SOKOL_API_IMPL sfs_result_t sfs_set_current_directory(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    if (chdir(path) != 0) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

/* _SFS_POSIX_IMPL intentionally NOT defined — all functions implemented above */

/* =========================================================================
   PLATFORM: Linux (and other POSIX)
   ========================================================================= */
#else /* Linux / generic POSIX */

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>

SOKOL_API_IMPL void sfs_set_android_internal_path(const char* path) { (void)path; }

static char* _sfs_readlink_heap(const char* linkpath) {
    size_t len = 256;
    char* buf = NULL;
    while (1) {
        char* tmp = (char*)realloc(buf, len);
        if (!tmp) { free(buf); return NULL; }
        buf = tmp;
        ssize_t rc = readlink(linkpath, buf, len);
        if (rc < 0) { free(buf); return NULL; }
        if ((size_t)rc < len) { buf[rc] = '\0'; return buf; }
        len *= 2;
    }
}

SOKOL_API_IMPL char* sfs_get_base_path(void) {
    char* exepath = NULL;
#if defined(__linux__)
    exepath = _sfs_readlink_heap("/proc/self/exe");
#elif defined(__FreeBSD__)
    char buf[PATH_MAX];
    int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    size_t sz = sizeof(buf);
    if (sysctl(mib, 4, buf, &sz, NULL, 0) == 0) exepath = strdup(buf);
#endif
    if (!exepath) { _sfs_set_error("cannot determine base path"); return NULL; }

    /* chop off executable name */
    char* last = strrchr(exepath, '/');
    if (last) last[1] = '\0';
    return exepath;
}

SOKOL_API_IMPL char* sfs_get_pref_path(const char* org, const char* app) {
    if (!org || !app) { _sfs_set_error("invalid parameter"); return NULL; }

    const char* xdg = getenv("XDG_DATA_HOME");
    char* base = NULL;
    if (xdg && *xdg) {
        base = strdup(xdg);
    } else {
        const char* home = getenv("HOME");
        if (!home) { _sfs_set_error("$HOME not set"); return NULL; }
        size_t bl = strlen(home) + 14; /* "/.local/share/" */
        base = (char*)malloc(bl);
        if (base) snprintf(base, bl, "%s/.local/share", home);
    }
    if (!base) return NULL;

    size_t len = strlen(base) + strlen(org) + strlen(app) + 4;
    char* result = (char*)malloc(len);
    if (!result) { free(base); return NULL; }
    if (*org) {
        snprintf(result, len, "%s/%s/%s/", base, org, app);
    } else {
        snprintf(result, len, "%s/%s/", base, app);
    }
    free(base);

    for (char* p = result + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(result, 0700);
            *p = '/';
        }
    }
    mkdir(result, 0700);
    return result;
}

SOKOL_API_IMPL char* sfs_get_user_folder(sfs_folder_t folder) {
    const char* home = getenv("HOME");
    if (!home) { _sfs_set_error("$HOME not set"); return NULL; }

    const char* sub = NULL;
    switch (folder) {
        case SFS_FOLDER_HOME:        sub = "";            break;
        case SFS_FOLDER_DESKTOP:     sub = "Desktop";     break;
        case SFS_FOLDER_DOCUMENTS:   sub = "Documents";   break;
        case SFS_FOLDER_DOWNLOADS:   sub = "Downloads";   break;
        case SFS_FOLDER_MUSIC:       sub = "Music";       break;
        case SFS_FOLDER_PICTURES:    sub = "Pictures";    break;
        case SFS_FOLDER_VIDEOS:      sub = "Videos";      break;
        case SFS_FOLDER_TEMPLATES:   sub = "Templates";   break;
        case SFS_FOLDER_PUBLICSHARE: sub = "Public";      break;
        case SFS_FOLDER_SAVEDGAMES:
        case SFS_FOLDER_SCREENSHOTS:
            _sfs_set_error("folder not supported on Linux");
            return NULL;
        default: _sfs_set_error("unknown folder"); return NULL;
    }

    size_t len = strlen(home) + strlen(sub) + 3;
    char* result = (char*)malloc(len);
    if (!result) return NULL;
    if (*sub) {
        snprintf(result, len, "%s/%s/", home, sub);
    } else {
        snprintf(result, len, "%s/", home);
    }
    return result;
}

SOKOL_API_IMPL char* sfs_get_current_directory(void) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof(buf))) { _sfs_set_error(strerror(errno)); return NULL; }
    size_t len = strlen(buf) + 2;
    char* result = (char*)malloc(len);
    if (result) snprintf(result, len, "%s/", buf);
    return result;
}

SOKOL_API_IMPL char* sfs_get_temp_dir(void) {
    const char* d = getenv("TMPDIR");
    if (!d || !*d) d = "/tmp";
    size_t len = strlen(d);
    char* result = (char*)malloc(len + 2);
    if (!result) return NULL;
    memcpy(result, d, len);
    if (result[len - 1] != '/') result[len++] = '/';
    result[len] = '\0';
    return result;
}

#define _SFS_POSIX_IMPL 1
#endif /* platform selection */

/* =========================================================================
   POSIX shared implementation (Apple / Android / Emscripten / Linux)
   ========================================================================= */
#if defined(_SFS_POSIX_IMPL)

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if !defined(_SFS_ANDROID_ASSET_FALLBACK)
SOKOL_API_IMPL sfs_file_t* sfs_open_file(const char* path, sfs_open_mode_t mode) {
    if (!path) { _sfs_set_error("invalid parameter"); return NULL; }
    const char* modestr = _sfs_open_mode_to_str(mode);
    if (!modestr) return NULL;
    FILE* fp = fopen(path, modestr);
    if (!fp) { _sfs_set_error(strerror(errno)); return NULL; }
    sfs_file_t* f = (sfs_file_t*)malloc(sizeof(sfs_file_t));
    if (!f) { fclose(fp); _sfs_set_error("out of memory"); return NULL; }
    f->fp = fp;
    return f;
}
#endif /* !_SFS_ANDROID_ASSET_FALLBACK */

SOKOL_API_IMPL sfs_result_t sfs_create_directory(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    size_t len = strlen(path);
    char* tmp = (char*)malloc(len + 2);
    if (!tmp) return SFS_RESULT_ERROR;
    memcpy(tmp, path, len + 1);
    /* strip trailing slash */
    if (tmp[len-1] == '/') tmp[--len] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                /* ignore, keep trying deeper */
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        _sfs_set_error(strerror(errno));
        free(tmp);
        return SFS_RESULT_ERROR;
    }
    free(tmp);
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_remove_path(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    if (remove(path) != 0) {
        if (errno == ENOENT) return SFS_RESULT_OK;
        _sfs_set_error(strerror(errno));
        return SFS_RESULT_ERROR;
    }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_rename_path(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return SFS_RESULT_INVALID_PARAM;
    if (rename(oldpath, newpath) != 0) {
        _sfs_set_error(strerror(errno));
        return SFS_RESULT_ERROR;
    }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL sfs_result_t sfs_copy_file(const char* oldpath, const char* newpath) {
    if (!oldpath || !newpath) return SFS_RESULT_INVALID_PARAM;
    int src = open(oldpath, O_RDONLY);
    if (src < 0) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }

    int dst = open(newpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) { _sfs_set_error(strerror(errno)); close(src); return SFS_RESULT_ERROR; }

    char buf[65536];
    ssize_t n;
    sfs_result_t result = SFS_RESULT_OK;
    while ((n = read(src, buf, sizeof(buf))) > 0) {
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(dst, buf + written, (size_t)(n - written));
            if (w < 0) { _sfs_set_error(strerror(errno)); result = SFS_RESULT_ERROR; goto done; }
            written += w;
        }
    }
    if (n < 0) { _sfs_set_error(strerror(errno)); result = SFS_RESULT_ERROR; }
done:
    close(src);
    close(dst);
    return result;
}

#if !defined(_SFS_ANDROID_ASSET_FALLBACK)
SOKOL_API_IMPL sfs_result_t sfs_get_path_info(const char* path, sfs_path_info_t* out_info) {
    if (!path || !out_info) return SFS_RESULT_INVALID_PARAM;
    memset(out_info, 0, sizeof(*out_info));

    struct stat st;
    if (stat(path, &st) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) return SFS_RESULT_NOT_FOUND;
        _sfs_set_error(strerror(errno));
        return SFS_RESULT_ERROR;
    }

    if (S_ISREG(st.st_mode))       out_info->type = SFS_PATHTYPE_FILE;
    else if (S_ISDIR(st.st_mode))  out_info->type = SFS_PATHTYPE_DIRECTORY;
    else                            out_info->type = SFS_PATHTYPE_OTHER;

    out_info->size        = (int64_t)st.st_size;
    out_info->modify_time = (int64_t)st.st_mtime;
    out_info->access_time = (int64_t)st.st_atime;
#if defined(__APPLE__)
    out_info->create_time = (int64_t)st.st_birthtime;
#else
    out_info->create_time = (int64_t)st.st_ctime;
#endif
    return SFS_RESULT_OK;
}
#endif /* !_SFS_ANDROID_ASSET_FALLBACK */

SOKOL_API_IMPL sfs_result_t sfs_enumerate_directory(const char* path,
                                                      sfs_enumerate_callback_t callback,
                                                      void* userdata)
{
    if (!path || !callback) return SFS_RESULT_INVALID_PARAM;

    /* Build path with trailing slash for the callback */
    size_t plen = strlen(path);
    char* dirpath = (char*)malloc(plen + 2);
    if (!dirpath) return SFS_RESULT_ERROR;
    memcpy(dirpath, path, plen + 1);
    while (plen > 0 && dirpath[plen-1] == '/') dirpath[--plen] = '\0';
    dirpath[plen]   = '/';
    dirpath[plen+1] = '\0';

    DIR* d = opendir(dirpath);
    if (!d) {
        _sfs_set_error(strerror(errno));
        free(dirpath);
        return SFS_RESULT_ERROR;
    }

    sfs_result_t result = SFS_RESULT_OK;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        sfs_enum_result_t r = callback(userdata, dirpath, ent->d_name);
        if (r == SFS_ENUM_FAILURE) { result = SFS_RESULT_ERROR; break; }
        if (r == SFS_ENUM_SUCCESS) break;
    }

    closedir(d);
    free(dirpath);
    return result;
}

SOKOL_API_IMPL sfs_result_t sfs_set_current_directory(const char* path) {
    if (!path) return SFS_RESULT_INVALID_PARAM;
    if (chdir(path) != 0) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

#endif /* _SFS_POSIX_IMPL */

/* =========================================================================
   Platform-independent: file I/O (close/read/write/seek/tell/size/flush/eof)
   All implementations share the FILE* from sfs_file_t regardless of platform.
   ========================================================================= */

SOKOL_API_IMPL sfs_result_t sfs_close_file(sfs_file_t* file) {
    if (!file) return SFS_RESULT_OK;
    int r = fclose(file->fp);
#if defined(__EMSCRIPTEN__)
    free(file->http_buf); /* NULL-safe; frees HTTP-fetched content buffer */
#endif
#if defined(__ANDROID__)
    free(file->asset_buf); /* NULL-safe; frees APK-bundled asset content buffer */
#endif
    free(file);
    if (r != 0) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL int64_t sfs_read_file(sfs_file_t* file, void* buf, int64_t count) {
    if (!file || !buf || count < 0) { _sfs_set_error("invalid parameter"); return -1; }
    if (count == 0) return 0;
    size_t n = fread(buf, 1, (size_t)count, file->fp);
    if (n == 0 && ferror(file->fp)) { _sfs_set_error(strerror(errno)); return -1; }
    return (int64_t)n;
}

SOKOL_API_IMPL int64_t sfs_write_file(sfs_file_t* file, const void* buf, int64_t count) {
    if (!file || !buf || count < 0) { _sfs_set_error("invalid parameter"); return -1; }
    if (count == 0) return 0;
    size_t n = fwrite(buf, 1, (size_t)count, file->fp);
    if (n < (size_t)count) { _sfs_set_error(strerror(errno)); return -1; }
    return (int64_t)n;
}

SOKOL_API_IMPL int64_t sfs_seek_file(sfs_file_t* file, int64_t offset, sfs_whence_t whence) {
    if (!file) { _sfs_set_error("invalid parameter"); return -1; }
    int w;
    switch (whence) {
        case SFS_WHENCE_SET: w = SEEK_SET; break;
        case SFS_WHENCE_CUR: w = SEEK_CUR; break;
        case SFS_WHENCE_END: w = SEEK_END; break;
        default: _sfs_set_error("invalid whence"); return -1;
    }
#if defined(_WIN32)
    if (_fseeki64(file->fp, offset, w) != 0) { _sfs_set_error(strerror(errno)); return -1; }
    return _ftelli64(file->fp);
#else
    if (fseeko(file->fp, (off_t)offset, w) != 0) { _sfs_set_error(strerror(errno)); return -1; }
    return (int64_t)ftello(file->fp);
#endif
}

SOKOL_API_IMPL int64_t sfs_tell_file(sfs_file_t* file) {
    if (!file) { _sfs_set_error("invalid parameter"); return -1; }
#if defined(_WIN32)
    int64_t pos = _ftelli64(file->fp);
#else
    int64_t pos = (int64_t)ftello(file->fp);
#endif
    if (pos < 0) { _sfs_set_error(strerror(errno)); return -1; }
    return pos;
}

SOKOL_API_IMPL int64_t sfs_get_file_size(sfs_file_t* file) {
    if (!file) { _sfs_set_error("invalid parameter"); return -1; }
    int64_t cur = sfs_tell_file(file);
    if (cur < 0) return -1;
    int64_t end = sfs_seek_file(file, 0, SFS_WHENCE_END);
    if (end < 0) return -1;
    /* restore original position */
    sfs_seek_file(file, cur, SFS_WHENCE_SET);
    return end;
}

SOKOL_API_IMPL sfs_result_t sfs_flush_file(sfs_file_t* file) {
    if (!file) { _sfs_set_error("invalid parameter"); return SFS_RESULT_INVALID_PARAM; }
    if (fflush(file->fp) != 0) { _sfs_set_error(strerror(errno)); return SFS_RESULT_ERROR; }
    return SFS_RESULT_OK;
}

SOKOL_API_IMPL bool sfs_eof_file(sfs_file_t* file) {
    if (!file) return true;
    return feof(file->fp) != 0;
}

/* =========================================================================
   Platform-independent: path_exists, is_directory, is_file
   ========================================================================= */
SOKOL_API_IMPL bool sfs_path_exists(const char* path) {
    if (!path) return false;
    sfs_path_info_t info;
    sfs_result_t r = sfs_get_path_info(path, &info);
    return r == SFS_RESULT_OK && info.type != SFS_PATHTYPE_NONE;
}

SOKOL_API_IMPL bool sfs_is_directory(const char* path) {
    if (!path) return false;
    sfs_path_info_t info;
    return sfs_get_path_info(path, &info) == SFS_RESULT_OK && info.type == SFS_PATHTYPE_DIRECTORY;
}

SOKOL_API_IMPL bool sfs_is_file(const char* path) {
    if (!path) return false;
    sfs_path_info_t info;
    return sfs_get_path_info(path, &info) == SFS_RESULT_OK && info.type == SFS_PATHTYPE_FILE;
}

SOKOL_API_IMPL int64_t sfs_get_last_modified_time(const char* path) {
    if (!path) return 0;
    sfs_path_info_t info;
    if (sfs_get_path_info(path, &info) != SFS_RESULT_OK) return 0;
    return info.modify_time;
}

SOKOL_API_IMPL char* sfs_get_assets_dir(void) {
    /* Assets are copied flat into the exe directory (no Assets/ subfolder),
       so the assets dir is the same as the base path. */
    return sfs_get_base_path();
}

/* =========================================================================
   Glob: simple pattern matching (*, ?, [...]) — platform-independent
   ========================================================================= */
static bool _sfs_glob_match(const char* pat, const char* str, bool ci) {
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return true;
            while (*str) {
                if (_sfs_glob_match(pat, str, ci)) return true;
                str++;
            }
            return false;
        } else if (*pat == '?') {
            if (!*str) return false;
            pat++; str++;
        } else if (*pat == '[') {
            if (!*str) return false;
            pat++;
            bool negate = false, matched = false;
            if (*pat == '!') { negate = true; pat++; }
            while (*pat && *pat != ']') {
                char lo = *pat++;
                if (*pat == '-' && *(pat+1) && *(pat+1) != ']') {
                    char hi = *(pat+1); pat += 2;
                    char c = *str;
                    if (ci) { lo = (char)tolower((unsigned char)lo); hi = (char)tolower((unsigned char)hi); c = (char)tolower((unsigned char)c); }
                    if (c >= lo && c <= hi) matched = true;
                } else {
                    char c1 = lo, c2 = *str;
                    if (ci) { c1 = (char)tolower((unsigned char)c1); c2 = (char)tolower((unsigned char)c2); }
                    if (c1 == c2) matched = true;
                }
            }
            if (*pat == ']') pat++;
            if (matched == negate) return false;
            str++;
        } else {
            char p = *pat, s = *str;
            if (ci) { p = (char)tolower((unsigned char)p); s = (char)tolower((unsigned char)s); }
            if (p != s) return false;
            pat++; str++;
        }
    }
    return *str == '\0';
}

typedef struct {
    char**  items;
    int     count;
    int     capacity;
    const char* pattern;
    bool    ci;
    const char* basedir;
} _sfs_glob_ctx_t;

static sfs_enum_result_t _sfs_glob_cb(void* userdata, const char* dirname, const char* fname) {
    _sfs_glob_ctx_t* ctx = (_sfs_glob_ctx_t*)userdata;
    if (!_sfs_glob_match(ctx->pattern, fname, ctx->ci)) return SFS_ENUM_CONTINUE;

    size_t dlen = strlen(dirname);
    size_t flen = strlen(fname);
    char* full = (char*)malloc(dlen + flen + 1);
    if (!full) return SFS_ENUM_FAILURE;
    memcpy(full, dirname, dlen);
    memcpy(full + dlen, fname, flen + 1);

    if (ctx->count >= ctx->capacity) {
        int newcap = ctx->capacity ? ctx->capacity * 2 : 8;
        char** tmp = (char**)realloc(ctx->items, newcap * sizeof(char*));
        if (!tmp) { free(full); return SFS_ENUM_FAILURE; }
        ctx->items    = tmp;
        ctx->capacity = newcap;
    }
    ctx->items[ctx->count++] = full;
    return SFS_ENUM_CONTINUE;
}

SOKOL_API_IMPL char** sfs_glob_directory(const char* path,
                                          const char* pattern,
                                          sfs_glob_flags_t flags,
                                          int* out_count)
{
    if (out_count) *out_count = 0;
    if (!path || !pattern) { _sfs_set_error("invalid parameter"); return NULL; }

    _sfs_glob_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pattern = pattern;
    ctx.ci      = (flags & SFS_GLOB_CASE_INSENSITIVE) != 0;

    sfs_result_t r = sfs_enumerate_directory(path, _sfs_glob_cb, &ctx);
    if (r != SFS_RESULT_OK && ctx.count == 0) {
        sfs_free_glob_results(ctx.items, ctx.count);
        return NULL;
    }

    if (out_count) *out_count = ctx.count;
    return ctx.items;
}

SOKOL_API_IMPL void sfs_free_glob_results(char** results, int count) {
    if (!results) return;
    for (int i = 0; i < count; i++) free(results[i]);
    free(results);
}

#endif /* SOKOL_FILESYSTEM_IMPL */
#endif /* SOKOL_FILESYSTEM_INCLUDED */

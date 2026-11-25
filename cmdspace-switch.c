#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const int kSymbolicHotKeyPreviousInputSource = 60;
static const OSType kHotKeySignature = 'CMDS';
static const UInt32 kHotKeyIDValue = 1;
static const char *kSkyLightPath = "/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight";

typedef int (*CGSIsSymbolicHotKeyEnabledFn)(int hotKey);
typedef int (*CGSSetSymbolicHotKeyEnabledFn)(int hotKey, unsigned char enabled);

typedef struct {
    void *skylight_handle;
    CGSIsSymbolicHotKeyEnabledFn is_enabled;
    CGSSetSymbolicHotKeyEnabledFn set_enabled;
    CFArrayRef sources;
    EventHandlerRef event_handler;
    EventHotKeyRef hotkey_ref;
    int lock_fd;
    bool hotkey60_was_enabled;
    bool keep_hotkey60_disabled;
    bool verbose;
    bool should_quit;
} AppState;

static AppState g_state = { .lock_fd = -1 };

static void log_msg(const char *level, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm_now;
    char ts[32];
    localtime_r(&now, &tm_now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

    fprintf(stderr, "%s [%s] ", ts, level);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
}

static bool cfstring_to_cstr(CFStringRef str, char *buf, size_t buf_len) {
    if (!str || buf_len == 0) {
        return false;
    }
    if (CFStringGetCString(str, buf, (CFIndex)buf_len, kCFStringEncodingUTF8)) {
        return true;
    }
    snprintf(buf, buf_len, "<non-utf8>");
    return false;
}

static CFArrayRef copy_enabled_keyboard_sources(void) {
    const void *keys[] = {
        kTISPropertyInputSourceCategory,
        kTISPropertyInputSourceIsEnabled,
        kTISPropertyInputSourceIsSelectCapable,
    };
    const void *values[] = {
        kTISCategoryKeyboardInputSource,
        kCFBooleanTrue,
        kCFBooleanTrue,
    };
    CFDictionaryRef filter = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        3,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
    );
    if (!filter) {
        return NULL;
    }

    CFArrayRef list = TISCreateInputSourceList(filter, false);
    CFRelease(filter);
    return list;
}

static void refresh_sources(void) {
    CFArrayRef updated = copy_enabled_keyboard_sources();
    if (g_state.sources) {
        CFRelease(g_state.sources);
        g_state.sources = NULL;
    }
    g_state.sources = updated;

    CFIndex count = updated ? CFArrayGetCount(updated) : 0;
    log_msg("INFO", "Refreshed enabled keyboard input sources (count=%ld)", (long)count);
    if (count == 0) {
        log_msg("WARN", "No select-capable enabled keyboard input sources found");
    }
}

static void on_enabled_sources_changed(
    CFNotificationCenterRef center,
    void *observer,
    CFNotificationName name,
    const void *object,
    CFDictionaryRef userInfo
) {
    (void)center;
    (void)observer;
    (void)name;
    (void)object;
    (void)userInfo;
    refresh_sources();
}

static int print_sources_list(void) {
    CFArrayRef list = copy_enabled_keyboard_sources();
    if (!list) {
        fprintf(stderr, "Failed to query keyboard input sources\n");
        return 1;
    }
    CFIndex count = CFArrayGetCount(list);
    for (CFIndex i = 0; i < count; i++) {
        TISInputSourceRef src = (TISInputSourceRef)CFArrayGetValueAtIndex(list, i);
        CFStringRef sid = (CFStringRef)TISGetInputSourceProperty(src, kTISPropertyInputSourceID);
        CFStringRef name = (CFStringRef)TISGetInputSourceProperty(src, kTISPropertyLocalizedName);
        char sid_buf[256] = "<unknown>";
        char name_buf[256] = "<unknown>";
        cfstring_to_cstr(sid, sid_buf, sizeof(sid_buf));
        cfstring_to_cstr(name, name_buf, sizeof(name_buf));
        printf("%ld: %s (%s)\n", (long)i, sid_buf, name_buf);
    }
    CFRelease(list);
    return 0;
}

static OSStatus switch_to_next_source(void) {
    if (!g_state.sources || CFArrayGetCount(g_state.sources) == 0) {
        refresh_sources();
    }
    if (!g_state.sources) {
        log_msg("ERROR", "Cannot switch: input source list is unavailable");
        return paramErr;
    }

    CFIndex count = CFArrayGetCount(g_state.sources);
    if (count == 0) {
        log_msg("ERROR", "Cannot switch: no enabled keyboard input sources");
        return paramErr;
    }

    TISInputSourceRef current = TISCopyCurrentKeyboardInputSource();
    if (!current) {
        log_msg("ERROR", "TISCopyCurrentKeyboardInputSource returned NULL");
        return paramErr;
    }

    CFStringRef current_id = (CFStringRef)TISGetInputSourceProperty(current, kTISPropertyInputSourceID);
    CFIndex current_idx = -1;
    if (current_id) {
        for (CFIndex i = 0; i < count; i++) {
            TISInputSourceRef candidate = (TISInputSourceRef)CFArrayGetValueAtIndex(g_state.sources, i);
            CFStringRef candidate_id = (CFStringRef)TISGetInputSourceProperty(candidate, kTISPropertyInputSourceID);
            if (candidate_id && CFEqual(current_id, candidate_id)) {
                current_idx = i;
                break;
            }
        }
    }

    if (current_idx < 0) {
        refresh_sources();
        count = g_state.sources ? CFArrayGetCount(g_state.sources) : 0;
        if (current_id && count > 0) {
            for (CFIndex i = 0; i < count; i++) {
                TISInputSourceRef candidate = (TISInputSourceRef)CFArrayGetValueAtIndex(g_state.sources, i);
                CFStringRef candidate_id = (CFStringRef)TISGetInputSourceProperty(candidate, kTISPropertyInputSourceID);
                if (candidate_id && CFEqual(current_id, candidate_id)) {
                    current_idx = i;
                    break;
                }
            }
        }
    }

    if (count == 0) {
        CFRelease(current);
        log_msg("ERROR", "Cannot switch: input source list became empty");
        return paramErr;
    }

    CFIndex next_idx = (current_idx >= 0) ? ((current_idx + 1) % count) : 0;
    TISInputSourceRef next = (TISInputSourceRef)CFArrayGetValueAtIndex(g_state.sources, next_idx);
    OSStatus err = TISSelectInputSource(next);

    if (err != noErr) {
        CFStringRef next_id = (CFStringRef)TISGetInputSourceProperty(next, kTISPropertyInputSourceID);
        char next_buf[256] = "<unknown>";
        cfstring_to_cstr(next_id, next_buf, sizeof(next_buf));
        log_msg("ERROR", "TISSelectInputSource(%s) failed: OSStatus=%d", next_buf, (int)err);
    } else if (g_state.verbose) {
        CFStringRef next_id = (CFStringRef)TISGetInputSourceProperty(next, kTISPropertyInputSourceID);
        char cur_buf[256] = "<unknown>";
        char next_buf[256] = "<unknown>";
        cfstring_to_cstr(current_id, cur_buf, sizeof(cur_buf));
        cfstring_to_cstr(next_id, next_buf, sizeof(next_buf));
        log_msg("INFO", "Switched input source: %s -> %s", cur_buf, next_buf);
    }

    CFRelease(current);
    return err;
}

static OSStatus on_hotkey_event(
    EventHandlerCallRef next_handler,
    EventRef event,
    void *user_data
) {
    (void)next_handler;
    (void)user_data;

    if (GetEventClass(event) != kEventClassKeyboard || GetEventKind(event) != kEventHotKeyPressed) {
        return eventNotHandledErr;
    }

    EventHotKeyID hot_key_id;
    OSStatus err = GetEventParameter(
        event,
        kEventParamDirectObject,
        typeEventHotKeyID,
        NULL,
        sizeof(hot_key_id),
        NULL,
        &hot_key_id
    );
    if (err != noErr) {
        log_msg("ERROR", "GetEventParameter(kEventParamDirectObject) failed: %d", (int)err);
        return err;
    }
    if (hot_key_id.signature != kHotKeySignature || hot_key_id.id != kHotKeyIDValue) {
        return eventNotHandledErr;
    }

    switch_to_next_source();
    return noErr;
}

static bool acquire_instance_lock(void) {
    uid_t uid = getuid();
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        struct passwd *pw = getpwuid(uid);
        if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') {
            home = pw->pw_dir;
        }
    }

    char lock_path[PATH_MAX];
    int fd = -1;

    if (home && home[0] != '\0') {
        char caches_dir[PATH_MAX];
        int dir_len = snprintf(caches_dir, sizeof(caches_dir), "%s/Library/Caches", home);
        if (dir_len > 0 && (size_t)dir_len < sizeof(caches_dir)) {
            char lib_dir[PATH_MAX];
            int lib_len = snprintf(lib_dir, sizeof(lib_dir), "%s/Library", home);
            if (lib_len > 0 && (size_t)lib_len < sizeof(lib_dir)) {
                (void)mkdir(lib_dir, 0700);
            }
            if (mkdir(caches_dir, 0700) == 0 || errno == EEXIST) {
                int path_len = snprintf(
                    lock_path,
                    sizeof(lock_path),
                    "%s/local.bibs.cmdspace-switch.lock",
                    caches_dir
                );
                if (path_len > 0 && (size_t)path_len < sizeof(lock_path)) {
                    fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
                }
            }
        }
    }

    if (fd < 0) {
        int tmp_len = snprintf(
            lock_path,
            sizeof(lock_path),
            "/tmp/local.bibs.cmdspace-switch.%lu.lock",
            (unsigned long)uid
        );
        if (tmp_len < 0 || (size_t)tmp_len >= sizeof(lock_path)) {
            log_msg("ERROR", "Lock path overflow");
            return false;
        }
        fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd < 0) {
            log_msg("ERROR", "Failed to open lock file %s: %s", lock_path, strerror(errno));
            return false;
        }
    }

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int err = errno;
        if (err == EWOULDBLOCK || err == EAGAIN) {
            log_msg("ERROR", "Another instance of cmdspace-switch is already running (lock: %s)", lock_path);
        } else {
            log_msg("ERROR", "Failed to lock %s: %s", lock_path, strerror(err));
        }
        close(fd);
        return false;
    }

    g_state.lock_fd = fd;
    return true;
}

static bool init_skylight(void) {
    g_state.skylight_handle = dlopen(kSkyLightPath, RTLD_LAZY);
    if (!g_state.skylight_handle) {
        log_msg("ERROR", "dlopen(%s) failed: %s", kSkyLightPath, dlerror());
        return false;
    }

    void *sym_is = dlsym(g_state.skylight_handle, "CGSIsSymbolicHotKeyEnabled");
    void *sym_set = dlsym(g_state.skylight_handle, "CGSSetSymbolicHotKeyEnabled");
    if (!sym_is || !sym_set) {
        log_msg("ERROR", "Failed to resolve SkyLight symbolic hotkey symbols");
        dlclose(g_state.skylight_handle);
        g_state.skylight_handle = NULL;
        return false;
    }

    g_state.is_enabled = (CGSIsSymbolicHotKeyEnabledFn)sym_is;
    g_state.set_enabled = (CGSSetSymbolicHotKeyEnabledFn)sym_set;
    return true;
}

static dispatch_source_t install_signal_source(int sig) {
    signal(sig, SIG_IGN);
    dispatch_source_t src = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_SIGNAL,
        (uintptr_t)sig,
        0,
        dispatch_get_main_queue()
    );
    if (!src) {
        return NULL;
    }
    dispatch_source_set_event_handler(src, ^{
        log_msg("INFO", "Received signal %d, shutting down", sig);
        g_state.should_quit = true;
        QuitEventLoop(GetMainEventLoop());
    });
    dispatch_resume(src);
    return src;
}

static void cleanup_and_restore(bool restore_on_exit) {
    CFNotificationCenterRemoveObserver(
        CFNotificationCenterGetDistributedCenter(),
        &g_state,
        kTISNotifyEnabledKeyboardInputSourcesChanged,
        NULL
    );

    if (g_state.hotkey_ref) {
        OSStatus err = UnregisterEventHotKey(g_state.hotkey_ref);
        if (err != noErr) {
            log_msg("WARN", "UnregisterEventHotKey failed: %d", (int)err);
        }
        g_state.hotkey_ref = NULL;
    }

    if (g_state.event_handler) {
        RemoveEventHandler(g_state.event_handler);
        g_state.event_handler = NULL;
    }

    if (restore_on_exit && g_state.hotkey60_was_enabled && g_state.set_enabled) {
        int rc = g_state.set_enabled(kSymbolicHotKeyPreviousInputSource, 1);
        log_msg("INFO", "Restored symbolic hotkey %d enabled=1 (rc=%d)", kSymbolicHotKeyPreviousInputSource, rc);
    } else if (!restore_on_exit) {
        log_msg("INFO", "Keeping symbolic hotkey %d disabled on exit", kSymbolicHotKeyPreviousInputSource);
    }

    if (g_state.sources) {
        CFRelease(g_state.sources);
        g_state.sources = NULL;
    }

    if (g_state.skylight_handle) {
        dlclose(g_state.skylight_handle);
        g_state.skylight_handle = NULL;
    }

    if (g_state.lock_fd >= 0) {
        close(g_state.lock_fd);
        g_state.lock_fd = -1;
    }
}

static void print_usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s [options]\n"
        "  --list                          Print enabled select-capable keyboard sources and exit\n"
        "  --keep-hotkey60-disabled-on-exit Do not re-enable symbolic hotkey 60 on exit (for launchd KeepAlive)\n"
        "  --no-restore-on-exit            Alias for --keep-hotkey60-disabled-on-exit\n"
        "  -v, --verbose                   Log every input-source switch\n"
        "  -h, --help                      Show this help\n",
        prog
    );
}

int main(int argc, char *argv[]) {
    bool list_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
        } else if (
            strcmp(argv[i], "--keep-hotkey60-disabled-on-exit") == 0 ||
            strcmp(argv[i], "--no-restore-on-exit") == 0
        ) {
            g_state.keep_hotkey60_disabled = true;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_state.verbose = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    if (list_only) {
        return print_sources_list();
    }

    if (!acquire_instance_lock()) {
        return 1;
    }

    if (!init_skylight()) {
        if (g_state.lock_fd >= 0) {
            close(g_state.lock_fd);
            g_state.lock_fd = -1;
        }
        return 1;
    }

    refresh_sources();

    EventTypeSpec event_spec = {
        .eventClass = kEventClassKeyboard,
        .eventKind = kEventHotKeyPressed,
    };
    OSStatus err = InstallEventHandler(
        GetEventDispatcherTarget(),
        NewEventHandlerUPP(on_hotkey_event),
        1,
        &event_spec,
        NULL,
        &g_state.event_handler
    );
    if (err != noErr) {
        log_msg("ERROR", "InstallEventHandler failed: %d", (int)err);
        cleanup_and_restore(false);
        return 1;
    }

    g_state.hotkey60_was_enabled = (g_state.is_enabled(kSymbolicHotKeyPreviousInputSource) != 0);
    log_msg(
        "INFO",
        "Symbolic hotkey %d initial state: enabled=%d",
        kSymbolicHotKeyPreviousInputSource,
        g_state.hotkey60_was_enabled ? 1 : 0
    );

    if (g_state.hotkey60_was_enabled) {
        int rc = g_state.set_enabled(kSymbolicHotKeyPreviousInputSource, 0);
        if (rc != 0 || g_state.is_enabled(kSymbolicHotKeyPreviousInputSource) != 0) {
            log_msg("ERROR", "Failed to disable symbolic hotkey %d (rc=%d)", kSymbolicHotKeyPreviousInputSource, rc);
            cleanup_and_restore(true);
            return 1;
        }
        log_msg("INFO", "Disabled symbolic hotkey %d", kSymbolicHotKeyPreviousInputSource);
    }

    EventHotKeyID hot_key_id = {
        .signature = kHotKeySignature,
        .id = kHotKeyIDValue,
    };
    err = RegisterEventHotKey(
        kVK_Space,
        cmdKey,
        hot_key_id,
        GetEventDispatcherTarget(),
        kEventHotKeyExclusive,
        &g_state.hotkey_ref
    );
    OptionBits reg_options = kEventHotKeyExclusive;
    if (err == eventHotKeyExistsErr) {
        log_msg(
            "WARN",
            "RegisterEventHotKey(Cmd+Space, kEventHotKeyExclusive) returned eventHotKeyExistsErr (%d); retrying with options=0",
            (int)err
        );
        reg_options = 0;
        err = RegisterEventHotKey(
            kVK_Space,
            cmdKey,
            hot_key_id,
            GetEventDispatcherTarget(),
            reg_options,
            &g_state.hotkey_ref
        );
    }
    if (err != noErr) {
        log_msg("ERROR", "RegisterEventHotKey(Cmd+Space, options=0x%x) failed: OSStatus=%d", (unsigned)reg_options, (int)err);
        /* Always restore hotkey 60 if we disabled it and failed to take over Cmd+Space. */
        cleanup_and_restore(true);
        return 1;
    }
    log_msg(
        "INFO",
        "Registered Cmd+Space hotkey (kVK_Space=%d, cmdKey=0x%x, options=0x%x)",
        kVK_Space,
        cmdKey,
        (unsigned)reg_options
    );

    CFNotificationCenterAddObserver(
        CFNotificationCenterGetDistributedCenter(),
        &g_state,
        on_enabled_sources_changed,
        kTISNotifyEnabledKeyboardInputSourcesChanged,
        NULL,
        CFNotificationSuspensionBehaviorDeliverImmediately
    );

    dispatch_source_t sigint_src = install_signal_source(SIGINT);
    dispatch_source_t sigterm_src = install_signal_source(SIGTERM);
    if (!sigint_src || !sigterm_src) {
        log_msg("ERROR", "Failed to install signal handlers");
        if (sigint_src) dispatch_release(sigint_src);
        if (sigterm_src) dispatch_release(sigterm_src);
        cleanup_and_restore(!g_state.keep_hotkey60_disabled);
        return 1;
    }

    EventTargetRef dispatcher = GetEventDispatcherTarget();
    while (!g_state.should_quit) {
        EventRef event = NULL;
        OSStatus loop_err = ReceiveNextEvent(0, NULL, kEventDurationForever, true, &event);
        if (loop_err == eventLoopQuitErr) {
            break;
        }
        if (loop_err == noErr && event) {
            SendEventToEventTarget(event, dispatcher);
            ReleaseEvent(event);
        } else if (loop_err != noErr && loop_err != eventLoopTimedOutErr) {
            log_msg("WARN", "ReceiveNextEvent exited with OSStatus=%d", (int)loop_err);
            break;
        }
    }

    dispatch_source_cancel(sigint_src);
    dispatch_source_cancel(sigterm_src);
    dispatch_release(sigint_src);
    dispatch_release(sigterm_src);

    cleanup_and_restore(!g_state.keep_hotkey60_disabled);
    return 0;
}

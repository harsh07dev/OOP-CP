#ifndef CONSOLE_INPUT_H
#define CONSOLE_INPUT_H

// console_input.h - Small cross-platform helper that waits for console input
// without blocking forever.  The sender thread uses this so it can notice a
// server disconnect even while no line has been typed.

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/select.h>
    #include <unistd.h>
#endif

namespace console_input {

// Returns true when stdin has data ready to be read within timeout_ms.
// On POSIX this uses select(); on Windows it waits on the console/pipe handle.
inline bool is_readable(int timeout_ms) {
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }
    DWORD result = WaitForSingleObject(handle, static_cast<DWORD>(timeout_ms));
    return result == WAIT_OBJECT_0;
#else
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &read_fds);

    timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;

    int result = select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &timeout);
    return result > 0 && FD_ISSET(STDIN_FILENO, &read_fds);
#endif
}

} // namespace console_input

#endif // CONSOLE_INPUT_H

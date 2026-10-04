#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

// thread_compat.h - Cross-platform thread abstraction.
// Provides ChatThread wrapping std::thread or native Windows threads on platforms lacking gthreads.

#if defined(__MINGW32__) && !defined(_GLIBCXX_HAS_GTHREADS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <functional>

    class PlatformThread {
    public:
        PlatformThread() : handle_(NULL) {}

        template<typename Function>
        explicit PlatformThread(Function func) : handle_(NULL) {
            auto fn_ptr = new std::function<void()>(func);
            handle_ = CreateThread(
                NULL,
                0,
                &PlatformThread::thread_entry,
                fn_ptr,
                0,
                NULL
            );
        }

        ~PlatformThread() {
            if (handle_ != NULL) {
                CloseHandle(handle_);
                handle_ = NULL;
            }
        }

        // Move constructors
        PlatformThread(PlatformThread&& other) noexcept : handle_(other.handle_) {
            other.handle_ = NULL;
        }

        PlatformThread& operator=(PlatformThread&& other) noexcept {
            if (this != &other) {
                if (handle_ != NULL) {
                    CloseHandle(handle_);
                }
                handle_ = other.handle_;
                other.handle_ = NULL;
            }
            return *this;
        }

        // Delete copy operations
        PlatformThread(const PlatformThread&) = delete;
        PlatformThread& operator=(const PlatformThread&) = delete;

        bool joinable() const {
            return handle_ != NULL;
        }

        void join() {
            if (handle_ != NULL) {
                WaitForSingleObject(handle_, INFINITE);
                CloseHandle(handle_);
                handle_ = NULL;
            }
        }

    private:
        HANDLE handle_;

        static DWORD WINAPI thread_entry(LPVOID arg) {
            auto fn = static_cast<std::function<void()>*>(arg);
            if (fn) {
                (*fn)();
                delete fn;
            }
            return 0;
        }
    };

    using ChatThread = PlatformThread;
#else
    #include <thread>
    using ChatThread = std::thread;
#endif

#endif // THREAD_COMPAT_H

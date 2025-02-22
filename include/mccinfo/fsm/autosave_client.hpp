#pragma once

namespace mccinfo {
namespace fsm {

namespace details {

inline void flatten(const std::filesystem::path &current_root_path,
                    const std::filesystem::path &target_root_path) {
    for (const auto &entry : std::filesystem::directory_iterator(current_root_path)) {
        const std::filesystem::path &file_path = entry.path();

        // Skip '.' and '..' directories
        if (file_path.filename() == "." || file_path.filename() == "..") {
            continue;
        }

        // Check if found entity is a directory
        if (entry.is_directory()) {
            // Recursively flatten the found directory
            flatten(file_path, target_root_path);
            // Attempt to remove the now-empty directory
            try {
                std::filesystem::remove(file_path);
            } catch (const std::exception &e) {
                std::cerr << "Failed to remove directory: " << file_path << ", error: " << e.what()
                          << std::endl;
            }
        } else {
            // Construct new file path in the root directory
            const std::filesystem::path &new_file_path = target_root_path / file_path.filename();

            try {
                // only copy and delete if we aren't in targetRootPath
                if (current_root_path != target_root_path) {
                    std::filesystem::copy_file(file_path, new_file_path,
                                               std::filesystem::copy_options::overwrite_existing);
                    std::filesystem::remove(file_path);
                }
            } catch (const std::exception &e) {
                std::cerr << "Failed to move " << file_path << " to " << new_file_path
                          << ", error: " << e.what() << std::endl;
            }
        }
    }
}
} // namespace details

class autosave_client {
public:
    autosave_client() 
    {}

    autosave_client(
        const std::filesystem::path& src,
        const std::filesystem::path& dst,
        const std::filesystem::path& host) : 
        src_(src), dst_(dst), host_(host)
    {}

    void set_copy_src(const std::filesystem::path &new_src) {
        std::unique_lock<std::mutex> lock(mut_);
        src_ = new_src;
    }

    void set_copy_dst(const std::filesystem::path &new_dst) {
        std::unique_lock<std::mutex> lock(mut_);
        dst_ = new_dst;
    }

    void set_on_copy_start(
        std::function<void(const std::filesystem::path &, const std::filesystem::path &)>
            pre_callback) {
        std::unique_lock<std::mutex> lock(mut_);
        pre_callback_ = pre_callback;
    }

    void set_on_complete(
        std::function<void(const std::filesystem::path &, const std::filesystem::path &)>
            post_callback) {
        std::unique_lock<std::mutex> lock(mut_);
        post_callback_ = post_callback;
    }

    void set_on_error(std::function<void(DWORD)> error_callback) {
        std::unique_lock<std::mutex> lock(mut_);
        error_callback_ = error_callback;
    }

    void set_flatten_on_write(bool flatten) {
        flatten_on_write_ = flatten;
    }

    void start() {
        copy_thread_ = std::thread([&] { 

            while (true) {
                std::unique_lock<std::mutex> lock(mut_);
                MI_CORE_TRACE("autosave_client waiting for request ...");

                cv_.wait(lock, [&] { return start_copy_ || stop_; });
                
                
                if (start_copy_) {
                    MI_CORE_TRACE("autosave_client received request to copy from src: {0}",
                                  std::filesystem::absolute(src_).generic_string().c_str());
                }
                
                if (stop_) {
                    MI_CORE_TRACE("autosave_client stopping ...");
                    stop_ = false;
                    break;
                }

                if (copy_delay_ms_ > 0) {
                    MI_CORE_TRACE("autosave_client waiting {0} ms to copy from src", std::to_string(copy_delay_ms_).c_str());
                    std::this_thread::sleep_for(std::chrono::milliseconds(copy_delay_ms_));
                }

                create_dst_if_needed();

                if (pre_callback_) {
                    MI_CORE_TRACE("Executing autosave_client pre_callback_ ...");
                    pre_callback_(src_, dst_);
                }

                bool success = do_copy();

                MI_CORE_TRACE("autosave_client copy result: {0}", (success) ? "success" : "failure");

                if (flatten_on_write_) {
                    MI_CORE_TRACE("autosave_client performing dst_ flattening ...");
                    details::flatten(dst_, dst_);
                }

                if (success && post_callback_) {
                    MI_CORE_TRACE("Executing autosave_client post_callback_ ...");
                    post_callback_(src_, dst_);
                }

                else if (error_callback_){
                    error_callback_(GetLastError());
                }

                start_copy_ = false;
            }
        });

    }

    void stop() {
        {
            std::unique_lock<std::mutex> lock(mut_);
            stop_ = true;
        }
        cv_.notify_one();
    }

    void request_copy(uint32_t delay_ms = 0) {
        {
            std::unique_lock<std::mutex> lock(mut_);
            MI_CORE_TRACE("autosave_client: request_copy()\n\tlock acquired");
            start_copy_ = true;
            copy_delay_ms_ = delay_ms;
        }
        MI_CORE_TRACE("autosave_client: request_copy()\n\tlock released");
        cv_.notify_one();
        MI_CORE_TRACE("autosave_client: request_copy()\n\tthread notified");
    }

private:
    void create_dst_if_needed() {
        if (!std::filesystem::exists(dst_)) {
            MI_CORE_WARN(".\\mccinfo_cache\\autosave does not exist, creating: {0}",
                         dst_.make_preferred().generic_string().c_str());
            try {
                std::filesystem::create_directories(dst_);
            } catch (std::exception &e) {
                MI_CORE_ERROR("Creation of {0} failed.\nLastError: {1}\nException: {2}",
                    std::filesystem::absolute(dst_).generic_string().c_str(),
                    GetLastError(),
                    e.what()
                );
            }
        }
    }

    bool do_copy() {
        MI_CORE_TRACE("autosave_client starting copy of autosave cache: {0}", std::filesystem::absolute(src_).generic_string().c_str());

        STARTUPINFOW si;
        PROCESS_INFORMATION pi;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        wchar_t args[1024];
        wsprintf(args, L"%s -r -o %s -f %s",
            host_.generic_wstring().c_str(),
            dst_.generic_wstring().c_str(),
            src_.generic_wstring().c_str());

        if (CreateProcessW(host_.generic_wstring().c_str(), args, NULL, NULL, FALSE,
                           CREATE_NO_WINDOW, NULL,
                           NULL, &si,
                           &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return true;
        } else {
            return false;
        }
    }


private:
    std::filesystem::path src_;
    std::filesystem::path dst_;
    std::filesystem::path host_;
    std::function<void(const std::filesystem::path &, const std::filesystem::path &)> pre_callback_;
    std::function<void(const std::filesystem::path &, const std::filesystem::path &)> post_callback_;
    std::function<void(DWORD)> error_callback_;
    bool flatten_on_write_ = false;

  private:
    std::mutex mut_;
    std::condition_variable cv_;
    std::thread copy_thread_;
    bool start_copy_ = false;
    bool stop_ = false;
    uint32_t copy_delay_ms_ = 0;
};

}
}
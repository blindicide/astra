#include "astra/process.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace astra {
ProcessResult run_process(const std::vector<std::string>& arguments, int timeout_seconds) {
    if (arguments.empty() || timeout_seconds <= 0) throw std::invalid_argument("invalid process request");
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) != 0) throw std::runtime_error("cannot create process pipe");
    pid_t child = fork();
    if (child < 0) {
        close(pipefd[0]); close(pipefd[1]);
        throw std::runtime_error("cannot fork backend process");
    }
    if (child == 0) {
        setpgid(0, 0);
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) _exit(126);
        close(pipefd[1]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& arg : arguments) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    close(pipefd[1]);
    ProcessResult result;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool exited = false, eof = false;
    int wait_status = 0;
    while (!exited || !eof) {
        char buffer[4096];
        ssize_t count;
        while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            if (result.output.size() < 1024 * 1024) result.output.append(buffer, static_cast<size_t>(count));
        }
        if (count == 0) eof = true;
        if (!exited) {
            pid_t waited = waitpid(child, &wait_status, WNOHANG);
            if (waited == child) exited = true;
            else if (waited < 0 && errno != EINTR) { close(pipefd[0]); throw std::runtime_error("cannot wait for backend process"); }
        }
        if (exited && eof) break;
        auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            result.timed_out = true;
            kill(-child, SIGKILL);
            kill(child, SIGKILL);
            if (!exited) waitpid(child, &wait_status, 0);
            break;
        }
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd pfd{pipefd[0], POLLIN, 0};
        poll(&pfd, 1, static_cast<int>(ms > 50 ? 50 : ms));
    }
    close(pipefd[0]);
    if (!result.timed_out && WIFEXITED(wait_status)) result.status = WEXITSTATUS(wait_status);
    return result;
}
}

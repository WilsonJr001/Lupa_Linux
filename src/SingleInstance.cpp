#include "SingleInstance.h"
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace LupaLinux {

SingleInstance::SingleInstance() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime) {
        _path = std::string(runtime) + "/lupa_linux.lock";
    } else {
        _path = "/tmp/lupa_linux-" + std::to_string(getuid()) + ".lock";
    }
}

SingleInstance::~SingleInstance() {
    // O arquivo não é apagado de propósito: apagar abriria uma janela em que
    // duas instâncias novas travariam arquivos diferentes. Fechar solta o lock.
    if (_fd >= 0) close(_fd);
}

bool SingleInstance::acquire() {
    // O_CLOEXEC: a interface (processo filho) não herda o lock
    _fd = open(_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (_fd < 0) return true; // sem como verificar: não impede de abrir

    if (flock(_fd, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            char buf[32] = {};
            if (pread(_fd, buf, sizeof(buf) - 1, 0) > 0) _otherPid = static_cast<pid_t>(std::atoi(buf));
            close(_fd);
            _fd = -1;
            return false;
        }
        close(_fd);
        _fd = -1;
        return true;
    }

    const std::string pid = std::to_string(getpid()) + "\n";
    if (ftruncate(_fd, 0) == 0) {
        ssize_t written = pwrite(_fd, pid.data(), pid.size(), 0);
        (void)written;
    }
    return true;
}

} // namespace LupaLinux

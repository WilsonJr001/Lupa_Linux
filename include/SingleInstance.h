#pragma once

#include <string>
#include <sys/types.h>

namespace Glassbrick {

// Garante uma única lupa por usuário (equivalente ao Mutex "Glassbrick" de
// Program.cs). Usa flock() num arquivo em $XDG_RUNTIME_DIR: o sistema libera o
// lock sozinho quando o processo termina, mesmo num travamento ou kill -9.
class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // true se esta é a única instância; senão otherPid() diz quem está rodando
    bool acquire();
    pid_t otherPid() const { return _otherPid; }
    const std::string& path() const { return _path; }

private:
    std::string _path;
    int _fd = -1;
    pid_t _otherPid = 0;
};

} // namespace Glassbrick

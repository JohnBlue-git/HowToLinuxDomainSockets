#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string socket_path = "/tmp/uds_echo.sock";
    int repeat = 100;
    std::string payload = "ping";

    if (argc >= 2) {
        socket_path = argv[1];
    }
    if (argc >= 3) {
        repeat = std::stoi(argv[2]);
    }
    if (argc >= 4) {
        payload = argv[3];
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket failed" << std::endl;
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "socket path too long" << std::endl;
        ::close(fd);
        return 1;
    }
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect failed" << std::endl;
        ::close(fd);
        return 1;
    }

    std::vector<char> recv_buf(payload.size());

    for (int i = 0; i < repeat; ++i) {
        if (::send(fd, payload.data(), payload.size(), 0) < 0) {
            std::cerr << "send failed" << std::endl;
            ::close(fd);
            return 1;
        }

        size_t received = 0;
        while (received < payload.size()) {
            ssize_t n = ::recv(fd, recv_buf.data() + received, recv_buf.size() - received, 0);
            if (n <= 0) {
                std::cerr << "recv failed" << std::endl;
                ::close(fd);
                return 1;
            }
            received += static_cast<size_t>(n);
        }
    }

    ::close(fd);
    std::cout << "ok" << std::endl;
    return 0;
}

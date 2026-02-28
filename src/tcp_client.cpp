#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 9000;
    int repeat = 100;
    std::string payload = "ping";

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }
    if (argc >= 4) {
        repeat = std::stoi(argv[3]);
    }
    if (argc >= 5) {
        payload = argv[4];
    }

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "socket failed" << std::endl;
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "invalid host" << std::endl;
        ::close(fd);
        return 1;
    }

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

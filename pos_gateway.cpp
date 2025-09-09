#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <getopt.h>

class PaymentGatewayServer {
private:
    int server_socket;
    int port;

public:
    PaymentGatewayServer(int port) : port(port), server_socket(-1) {}

    ~PaymentGatewayServer() {
        if (server_socket != -1) {
            close(server_socket);
        }
    }

    bool start() {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        int opt = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Bind failed" << std::endl;
            return false;
        }

        if (listen(server_socket, 5) < 0) {
            std::cerr << "Listen failed" << std::endl;
            return false;
        }

        std::cout << "Payment Gateway Server listening on port " << port << std::endl;
        return true;
    }

    void run() {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
            if (client_socket < 0) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }

            handleClient(client_socket);
        }
    }

private:
    void handleClient(int client_socket) {
        char buffer[1024] = {0};
        
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            close(client_socket);
            return;
        }

        std::string request(buffer, bytes_received);
        std::cout << "Received payment request: " << request << std::endl;

        std::string response;
        try {
            if (request.substr(0, 5) == "SALE:") {
                std::string amount_str = request.substr(5);
                double amount = std::stod(amount_str);
                
                usleep(100000); 
                
                std::ostringstream oss;
                oss << "SUCCESS:Transaction approved for $" << std::fixed << std::setprecision(2) << amount 
                    << ":TXN_ID_" << std::time(nullptr);
                response = oss.str();
            } else {
                response = "ERROR:Invalid request format";
            }
        } catch (const std::exception& e) {
            response = "ERROR:Invalid amount format";
        }

        send(client_socket, response.c_str(), response.length(), 0);
        std::cout << "Sent response: " << response << std::endl;
        
        close(client_socket);
    }
};

class POSGatewayClient {
public:
    static bool sendSaleRequest(const std::string& host, int port, double amount) {
        int client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == -1) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            std::cerr << "Invalid address: " << host << std::endl;
            close(client_socket);
            return false;
        }

        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Connection failed to " << host << ":" << port << std::endl;
            close(client_socket);
            return false;
        }

        std::ostringstream request;
        request << "SALE:" << std::fixed << std::setprecision(2) << amount;
        std::string request_str = request.str();
        
        if (send(client_socket, request_str.c_str(), request_str.length(), 0) < 0) {
            std::cerr << "Send failed" << std::endl;
            close(client_socket);
            return false;
        }

        std::cout << "Sent payment request: $" << std::fixed << std::setprecision(2) << amount << std::endl;

        // Receive response
        char buffer[1024] = {0};
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            std::string response(buffer, bytes_received);
            std::cout << "Payment gateway response: " << response << std::endl;
        }

        close(client_socket);
        return true;
    }
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <command> [options]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  server --port <port>                    Start payment gateway server" << std::endl;
    std::cout << "  sale --amount <amount> --host <host> --port <port>  Send sale request" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " server --port 9000" << std::endl;
    std::cout << "  " << program_name << " sale --amount 12.34 --host 127.0.0.1 --port 9000" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    if (command == "server") {
        int port = 0;
        
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for option: " << argv[i] << std::endl;
                return 1;
            }
            
            std::string option = argv[i];
            std::string value = argv[i + 1];
            
            if (option == "--port") {
                port = std::stoi(value);
            } else {
                std::cerr << "Unknown option: " << option << std::endl;
                return 1;
            }
        }
        
        if (port == 0) {
            std::cerr << "Port is required for server mode" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        PaymentGatewayServer server(port);
        if (!server.start()) {
            return 1;
        }
        server.run();
        
    } else if (command == "sale") {
        double amount = 0.0;
        std::string host;
        int port = 0;
        
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for option: " << argv[i] << std::endl;
                return 1;
            }
            
            std::string option = argv[i];
            std::string value = argv[i + 1];
            
            if (option == "--amount") {
                amount = std::stod(value);
            } else if (option == "--host") {
                host = value;
            } else if (option == "--port") {
                port = std::stoi(value);
            } else {
                std::cerr << "Unknown option: " << option << std::endl;
                return 1;
            }
        }
        
        if (amount <= 0.0 || host.empty() || port == 0) {
            std::cerr << "Amount, host, and port are required for sale command" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        
        if (!POSGatewayClient::sendSaleRequest(host, port, amount)) {
            return 1;
        }
        
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <getopt.h>
#include <sqlite3.h>
#include <random>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <errno.h>

class TransactionDB {
private:
    sqlite3* db;

public:
    TransactionDB() : db(nullptr) {}

    ~TransactionDB() {
        if (db) {
            sqlite3_close(db);
        }
    }

    bool init(const std::string& db_path = "transactions.db") {
        int rc = sqlite3_open(db_path.c_str(), &db);
        if (rc) {
            std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }

        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS transactions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                amount REAL NOT NULL,
                approved BOOLEAN NOT NULL,
                auth_code TEXT,
                masked_pan TEXT,
                rrn TEXT,
                unix_ts INTEGER,
                nonce TEXT
            );
        )";

        rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Can't create table: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }

        std::cout << "Database initialized successfully" << std::endl;
        return true;
    }

    bool insertTransaction(double amount, bool approved, const std::string& auth_code = "", 
                          const std::string& masked_pan = "", const std::string& rrn = "",
                          long unix_ts = 0, const std::string& nonce = "") {
        const char* sql = "INSERT INTO transactions (amount, approved, auth_code, masked_pan, rrn, unix_ts, nonce) VALUES (?, ?, ?, ?, ?, ?, ?);";
        sqlite3_stmt* stmt;

        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }

        sqlite3_bind_double(stmt, 1, amount);
        sqlite3_bind_int(stmt, 2, approved ? 1 : 0);
        sqlite3_bind_text(stmt, 3, auth_code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, masked_pan.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, rrn.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 6, unix_ts);
        sqlite3_bind_text(stmt, 7, nonce.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            std::cerr << "Failed to insert transaction: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }

        std::cout << "Transaction stored: Amount=$" << std::fixed << std::setprecision(2) 
                  << amount << ", Approved=" << (approved ? "true" : "false") << std::endl;
        return true;
    }

    bool getLastTransactions(int n) {
        const char* sql = R"(
            SELECT id, amount, approved, auth_code, masked_pan, rrn, unix_ts, nonce 
            FROM transactions 
            ORDER BY id DESC 
            LIMIT ?;
        )";
        
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }

        sqlite3_bind_int(stmt, 1, n);

        std::cout << "\nLast " << n << " transactions:" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << std::left << std::setw(4) << "ID" 
                  << std::setw(10) << "Amount" 
                  << std::setw(10) << "Status"
                  << std::setw(8) << "Auth"
                  << std::setw(18) << "Masked PAN"
                  << std::setw(14) << "RRN"
                  << std::setw(12) << "Timestamp"
                  << "Nonce" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        int count = 0;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            double amount = sqlite3_column_double(stmt, 1);
            bool approved = sqlite3_column_int(stmt, 2) != 0;
            const char* auth_code = (const char*)sqlite3_column_text(stmt, 3);
            const char* masked_pan = (const char*)sqlite3_column_text(stmt, 4);
            const char* rrn = (const char*)sqlite3_column_text(stmt, 5);
            long unix_ts = sqlite3_column_int64(stmt, 6);
            const char* nonce = (const char*)sqlite3_column_text(stmt, 7);

            std::cout << std::left << std::setw(4) << id 
                      << "$" << std::setw(8) << std::fixed << std::setprecision(2) << amount
                      << std::setw(10) << (approved ? "APPROVED" : "DECLINED")
                      << std::setw(8) << (auth_code ? auth_code : "")
                      << std::setw(18) << (masked_pan ? masked_pan : "")
                      << std::setw(14) << (rrn ? rrn : "")
                      << std::setw(12) << unix_ts
                      << (nonce ? nonce : "") << std::endl;
            count++;
        }

        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            std::cerr << "Error reading transactions: " << sqlite3_errmsg(db) << std::endl;
            return false;
        }

        if (count == 0) {
            std::cout << "No transactions found in database." << std::endl;
        } else {
            std::cout << std::string(80, '=') << std::endl;
            std::cout << "Total: " << count << " transaction(s) displayed" << std::endl;
        }

        return true;
    }
};

std::string generateNonce() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::string nonce;
    int length = 8 + (gen() % 9);
    
    for (int i = 0; i < length; i++) {
        int val = dis(gen);
        nonce += (val < 10) ? ('0' + val) : ('A' + val - 10);
    }
    
    return nonce;
}

std::string generateAuthCode() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);
    return std::to_string(dis(gen));
}

std::string generateMaskedPAN() {
    return "****-****-****-1234";
}

std::string generateRRN() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<long long> dis(100000000000LL, 999999999999LL);
    return std::to_string(dis(gen));
}

long getCurrentUnixTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool setSocketTimeout(int socket, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }
    if (setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }
    return true;
}

std::string stripCR(const std::string& str) {
    std::string result = str;
    if (!result.empty() && result.back() == '\r') {
        result.pop_back();
    }
    return result;
}

class PaymentGatewayServer {
private:
    int server_socket;
    int port;
    TransactionDB db;

public:
    PaymentGatewayServer(int port) : port(port), server_socket(-1) {}

    ~PaymentGatewayServer() {
        if (server_socket != -1) {
            close(server_socket);
        }
    }

    bool start() {
        if (!db.init()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return false;
        }     
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
            std::cerr << "Bind failed on port " << port << ": " << strerror(errno) << std::endl;
            std::cerr << "Port may already be in use. Try a different port or wait a moment." << std::endl;
            return false;
        }

        if (listen(server_socket, 5) < 0) {
            std::cerr << "Listen failed" << std::endl;
            return false;
        }

        std::cout << "Payment Gateway Terminal listening on port " << port << std::endl;
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

            setSocketTimeout(client_socket, 3000); 

            handleClient(client_socket);
        }
    }

private:
    std::string readLine(int socket) {
        std::string line;
        char c;
        while (recv(socket, &c, 1, 0) == 1) {
            if (c == '\n') {
                break;
            }
            if (c != '\r') { 
                line += c;
            }
        }
        return line;
    }

    bool sendLine(int socket, const std::string& line) {
        std::string message = line + "\n";
        return send(socket, message.c_str(), message.length(), 0) >= 0;
    }

    void handleClient(int client_socket) {
        std::cout << "Client connected, waiting for handshake..." << std::endl;

        try {
            std::string hello_msg = readLine(client_socket);
            hello_msg = stripCR(hello_msg);
            std::cout << "Received: " << hello_msg << std::endl;

            if (hello_msg != "HELLO|GW|1.0") {
                std::cerr << "Invalid handshake received: " << hello_msg << std::endl;
                close(client_socket);
                return;
            }

            if (!sendLine(client_socket, "HELLO|TERM|1.0")) {
                std::cerr << "Failed to send terminal hello" << std::endl;
                close(client_socket);
                return;
            }

            std::cout << "Handshake completed, waiting for AUTH..." << std::endl;

            while (true) {
                std::string request = readLine(client_socket);
                request = stripCR(request);
                
                if (request.empty()) {
                    break;
                }

                std::cout << "Received: " << request << std::endl;

                if (request == "PING") {
                    if (!sendLine(client_socket, "PONG")) {
                        std::cerr << "Failed to send PONG" << std::endl;
                        break;
                    }
                    std::cout << "Sent: PONG" << std::endl;
                    continue;
                }

                if (request.substr(0, 5) == "AUTH|") {
                    std::string response = processAuthRequest(request);
                    if (!sendLine(client_socket, response)) {
                        std::cerr << "Failed to send response" << std::endl;
                        break;
                    }
                    std::cout << "Sent: " << response << std::endl;
                } else {
                    std::string response = "DECLINED|Invalid request format";
                    if (!sendLine(client_socket, response)) {
                        std::cerr << "Failed to send error response" << std::endl;
                        break;
                    }
                    std::cout << "Sent: " << response << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception in handleClient: " << e.what() << std::endl;
        }

        close(client_socket);
        std::cout << "Client disconnected" << std::endl;
    }

    std::string processAuthRequest(const std::string& request) {
        try {
            std::cout << "Processing AUTH request: " << request << std::endl;
            
            std::vector<std::string> parts;
            std::stringstream ss(request);
            std::string part;
            
            while (std::getline(ss, part, '|')) {
                parts.push_back(part);
            }

            std::cout << "Parsed " << parts.size() << " parts" << std::endl;
            
            if (parts.size() != 4 || parts[0] != "AUTH") {
                std::cout << "Invalid AUTH format - expected 4 parts, got " << parts.size() << std::endl;
                return "DECLINED|Invalid AUTH format";
            }

            std::cout << "Parts: [" << parts[0] << "] [" << parts[1] << "] [" << parts[2] << "] [" << parts[3] << "]" << std::endl;

            double amount;
            long unix_ts;
            try {
                amount = std::stod(parts[1]);
                unix_ts = std::stol(parts[2]);
            } catch (const std::exception& e) {
                std::cout << "Failed to parse amount or timestamp: " << e.what() << std::endl;
                return "DECLINED|Invalid amount or timestamp format";
            }
            
            std::string nonce = parts[3];

            std::cout << "Parsed values: amount=" << amount << ", unix_ts=" << unix_ts << ", nonce=" << nonce << std::endl;

            if (nonce.length() < 8 || nonce.length() > 16) {
                std::cout << "Invalid nonce length: " << nonce.length() << std::endl;
                return "DECLINED|Invalid nonce length";
            }
            for (char c : nonce) {
                if (!std::isxdigit(c)) {
                    std::cout << "Invalid nonce character: " << c << std::endl;
                    return "DECLINED|Invalid nonce format";
                }
            }

            bool approved = amount < 50.5;
            std::cout << "Transaction approved: " << (approved ? "true" : "false") << std::endl;
            
            std::string response;
            if (approved) {
                std::string auth_code = generateAuthCode();
                std::string masked_pan = generateMaskedPAN();
                std::string rrn = generateRRN();
                
                std::cout << "Generated: auth_code=" << auth_code << ", masked_pan=" << masked_pan << ", rrn=" << rrn << std::endl;
                
                std::cout << "Storing approved transaction..." << std::endl;
                if (!db.insertTransaction(amount, approved, auth_code, masked_pan, rrn, unix_ts, nonce)) {
                    std::cout << "Database insert failed" << std::endl;
                    return "DECLINED|Database error";
                }
                
                std::ostringstream oss;
                oss << "APPROVED|" << auth_code << "|" << masked_pan << "|" << rrn;
                response = oss.str();
            } else {
                std::cout << "Storing declined transaction..." << std::endl;
                if (!db.insertTransaction(amount, approved, "", "", "", unix_ts, nonce)) {
                    std::cout << "Database insert failed for declined transaction" << std::endl;
                }
                
                std::ostringstream oss;
                oss << "DECLINED|Amount $" << std::fixed << std::setprecision(2) 
                    << amount << " exceeds limit ($50.50)";
                response = oss.str();
            }

            std::cout << "Generated response: " << response << std::endl;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            return response;
            
        } catch (const std::exception& e) {
            std::cout << "Exception in processAuthRequest: " << e.what() << std::endl;
            return std::string("DECLINED|Processing error: ") + e.what();
        }
    }
};

class POSGatewayClient {
private:
    std::string host;
    int port;

    int createConnectedSocket(int timeout_ms = 2000) {
        int client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket == -1) {
            return -1;
        }

        setSocketTimeout(client_socket, timeout_ms);

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            close(client_socket);
            return -1;
        }

        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            close(client_socket);
            return -1;
        }

        return client_socket;
    }

    std::string readLine(int socket) {
        std::string line;
        char c;
        while (recv(socket, &c, 1, 0) == 1) {
            if (c == '\n') {
                break;
            }
            if (c != '\r') { 
                line += c;
            }
        }
        return line;
    }

    bool sendLine(int socket, const std::string& line) {
        std::string message = line + "\n";
        return send(socket, message.c_str(), message.length(), 0) >= 0;
    }

    bool performHandshake(int socket) {
        if (!sendLine(socket, "HELLO|GW|1.0")) {
            return false;
        }
        std::cout << "Sent: HELLO|GW|1.0" << std::endl;

        std::string response = readLine(socket);
        response = stripCR(response);
        std::cout << "Received: " << response << std::endl;

        return response == "HELLO|TERM|1.0";
    }

public:
    POSGatewayClient(const std::string& host, int port) : host(host), port(port) {}

    bool sendSaleRequest(double amount) {
        int retries = 0;
        const int max_retries = 2;
        
        while (retries <= max_retries) {
            int client_socket = createConnectedSocket();
            if (client_socket == -1) {
                std::cerr << "Connection failed to " << host << ":" << port;
                if (retries < max_retries) {
                    int delay_ms = 200 * (1 << retries); 
                    std::cerr << ", retrying in " << delay_ms << "ms..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                    retries++;
                    continue;
                } else {
                    std::cerr << ", giving up after " << max_retries << " retries" << std::endl;
                    return false;
                }
            }

            setSocketTimeout(client_socket, 3000); 

            bool success = false;
            try {
                if (!performHandshake(client_socket)) {
                    std::cerr << "Handshake failed" << std::endl;
                    close(client_socket);
                    
                    if (retries == 0) {
                        std::cout << "Server dropped connection after HELLO, retrying..." << std::endl;
                        retries++;
                        continue;
                    } else {
                        return false;
                    }
                }

                long unix_ts = getCurrentUnixTimestamp();
                std::string nonce = generateNonce();

                std::ostringstream auth_request;
                auth_request << "AUTH|" << std::fixed << std::setprecision(2) 
                           << amount << "|" << unix_ts << "|" << nonce;
                
                if (!sendLine(client_socket, auth_request.str())) {
                    std::cerr << "Failed to send AUTH request" << std::endl;
                    close(client_socket);
                    return false;
                }

                std::cout << "Sent: " << auth_request.str() << std::endl;

                auto start_time = std::chrono::steady_clock::now();
                std::string response;
                
                while (true) {
                    auto current_time = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        current_time - start_time).count();
                    
                    if (elapsed >= 3000) { 
                        if (!sendLine(client_socket, "PING")) {
                            std::cerr << "Failed to send PING" << std::endl;
                            break;
                        }
                        std::cout << "Sent: PING" << std::endl;
                        start_time = current_time;
                    }

                    response = readLine(client_socket);
                    response = stripCR(response);
                    
                    if (!response.empty()) {
                        if (response == "PONG") {
                            std::cout << "Received: PONG" << std::endl;
                            start_time = std::chrono::steady_clock::now();
                            continue;
                        } else {
                            std::cout << "Payment gateway response: " << response << std::endl;
                            success = true;
                            break;
                        }
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

            } catch (const std::exception& e) {
                std::cerr << "Exception during transaction: " << e.what() << std::endl;
            }

            close(client_socket);
            
            if (success) {
                return true;
            }
            
            if (retries < max_retries) {
                int delay_ms = 200 * (1 << retries); 
                std::cout << "Transaction failed, retrying in " << delay_ms << "ms..." << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                retries++;
            } else {
                std::cerr << "Transaction failed after " << max_retries << " retries" << std::endl;
                return false;
            }
        }
        
        return false;
    }
};

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <command> [options]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  server --port <port>                    Start payment gateway terminal" << std::endl;
    std::cout << "  sale --amount <amount> --host <host> --port <port>  Send sale request" << std::endl;
    std::cout << "  last --n <count>                        Show last N transactions from database" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " server --port 9000" << std::endl;
    std::cout << "  " << program_name << " sale --amount 12.34 --host 127.0.0.1 --port 9000" << std::endl;
    std::cout << "  " << program_name << " last --n 5" << std::endl;
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
        
        POSGatewayClient client(host, port);
        if (!client.sendSaleRequest(amount)) {
            return 1;
        }
        
    } else if (command == "last"){
        int n = 10;
        
        for (int i = 2; i < argc; i += 2){
            if (i + 1 >= argc) {
                std::cerr << "Missing value for option: " << argv[i] << std::endl;
                return 1;
            }
            
            std::string option = argv[i];
            std::string value = argv[i + 1];
            
            if (option == "--n"){
                n = std::stoi(value);
                if (n <= 0) {
                    std::cerr << "Number of transactions must be positive" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Unknown option for last command: " << option << std::endl;
                return 1;
            }
        }
        
        TransactionDB db;
        if (!db.init()){
            std::cerr << "Failed to initialize database" << std::endl;
            return 1;
        }
        
        if (!db.getLastTransactions(n)){
            return 1;
        }        
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
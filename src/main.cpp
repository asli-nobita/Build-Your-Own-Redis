#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unordered_map>
#include <list>
#include <set>
#include <mutex> 
#include <condition_variable>
#include <variant>

#include "header.h"

using value_type = std::variant<std::string, std::list<std::string>, std::set<std::string>>;

std::unordered_map<std::string, DBEntry<value_type>> db;
// std::unordered_map<std::string, std::list<std::string>> db;
std::mutex mutex;
std::condition_variable cv;

void* handle_client(void* sock_fd) {
    char buffer[1024];
    int client_fd = *(int*)sock_fd;
    while (true) {
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            break;
        }
        // send(client_fd, "+PONG\r\n", 7, 0); 
        try {
            auto [cmd, args] = parse_input(buffer, bytes_received);
            if (cmd == "ping") {
                send(client_fd, "+PONG\r\n", 7, 0);
            }
            else if (cmd == "echo") {
                for (auto& arg : args) {
                    auto msg = to_bulk_string(arg);
                    send(client_fd, msg.c_str(), msg.length(), 0);
                }
            }
            else if (cmd == "set") {
                // lock_guard is used for starightforward critical sections
                std::lock_guard<std::mutex> lock(mutex);
                db[args[0]].value = args[1];
                db[args[0]].type = "string";
                if (args.size() > 2) {
                    // extra commands 
                    if (args[2] == "ex") {
                        db[args[0]].has_expiry = true;
                        db[args[0]].expiry_time = std::chrono::steady_clock::now() + std::chrono::seconds(std::stoll(args[3]));
                    }
                    else if (args[2] == "px") {
                        db[args[0]].has_expiry = true;
                        db[args[0]].expiry_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::stoll(args[3]));
                    }
                }
                cv.notify_all();
                send(client_fd, "+OK\r\n", 5, 0);
            }
            else if (cmd == "get") {
                if (db.count(args[0])) {
                    auto entry = db[args[0]];
                    if (entry.has_expiry && entry.expiry_time < std::chrono::steady_clock::now()) {
                        // expired entry 
                        db.erase(args[0]);
                        send(client_fd, "$-1\r\n", 5, 0);
                    }
                    else {
                        auto value = std::get<std::string>(entry.value);
                        auto msg = to_bulk_string(value);
                        send(client_fd, msg.c_str(), msg.length(), 0);
                    }
                }
                else {
                    send(client_fd, "$-1\r\n", 5, 0);
                }
            }
            else if (cmd == "rpush") {
                if (!std::holds_alternative<std::list<std::string>>(db[args[0]].value)) {
                    db[args[0]].type = "list";
                    db[args[0]].value = std::list<std::string>();
                }
                auto& ls = std::get<std::list<std::string>>(db[args[0]].value);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (db[args[0]].type == "none")
                        db[args[0]].type = "list";
                    for (int i = 1; i < args.size(); i++) {
                        ls.push_back(args[i]);
                    }
                }
                auto msg = to_resp_integer(ls.size());
                cv.notify_all();
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
            else if (cmd == "lpush") {
                if (!std::holds_alternative<std::list<std::string>>(db[args[0]].value)) {
                    db[args[0]].type = "list";
                    db[args[0]].value = std::list<std::string>();
                }
                auto& ls = std::get<std::list<std::string>>(db[args[0]].value);
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (db[args[0]].type == "none")
                        db[args[0]].type = "list";
                    for (int i = 1; i < args.size(); i++) {
                        ls.push_front(args[i]);
                    }
                }
                auto msg = to_resp_integer(ls.size());
                cv.notify_all();
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
            else if (cmd == "lpop") {
                if (db.find(args[0]) == db.end()) {
                    send(client_fd, "$-1\r\n", 5, 0);
                }
                std::string msg;
                auto& ls = std::get<std::list<std::string>>(db[args[0]].value);
                int n = ls.size();
                std::lock_guard<std::mutex> lock(mutex);
                if (args.size() >= 2) {
                    int num_removed = std::min(n, std::stoi(args[1]));
                    std::vector<std::string> removed;
                    for (int i = 0; i < num_removed; i++) {
                        auto e = ls.front();
                        ls.pop_front();
                        removed.push_back(e);
                    }
                    msg = to_resp_array(removed);
                }
                else {
                    auto e = ls.front();
                    ls.pop_front();
                    msg = to_bulk_string(e);
                }
                cv.notify_all();
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
            else if (cmd == "blpop") {
                std::string msg;
                // unique lock is used in combination with condition variables, or when manual locking and unlocking is needed
                std::unique_lock<std::mutex> lock(mutex);
                auto timeout = std::stod(args[1]);
                bool found = true;
                if (!std::holds_alternative<std::list<std::string>>(db[args[0]].value)) {
                    db[args[0]].type = "list";
                    db[args[0]].value = std::list<std::string>();
                }
                auto& ls = std::get<std::list<std::string>>(db[args[0]].value);
                if (timeout == 0) {
                    cv.wait(lock, [&]() { return !ls.empty(); });
                }
                else {
                    found = cv.wait_for(lock, std::chrono::milliseconds((int)(timeout * 1000)), [&]() { return !ls.empty(); });
                }
                if (found) {
                    auto ele = ls.front();
                    ls.pop_front();
                    std::vector<std::string> v{ args[0], ele };
                    msg = to_resp_array(v);
                }
                else {
                    msg = "*-1\r\n";
                }
                lock.unlock();
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
            else if (cmd == "lrange") {
                auto& ls = std::get<std::list<std::string>>(db[args[0]].value);
                int n = ls.size();
                int st = std::stoi(args[1]), en = std::stoi(args[2]);
                // negative indices 
                if (st < 0) {
                    st = std::max(0, st + n);
                }
                if (en < 0) {
                    en = std::max(0, en + n);
                }
                st = std::min(n, st); en = std::min(n - 1, en);
                std::vector<std::string> elements;
                for (int i = st; i <= en; i++) {
                    elements.push_back(*next(ls.begin(), i));
                }
                auto msg = to_resp_array(elements);
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
            else if (cmd == "llen") {
                auto& ls = std::get<std::list<std::string>>(db[args[0]].value);
                auto msg = to_resp_integer(ls.size());
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
            else if (cmd == "type") {
                // get type of value stored with this key 
                std::string type = db[args[0]].type;
                std::string msg = "+" + type + "\r\n";
                send(client_fd, msg.c_str(), msg.length(), 0);
            }
        }
        catch (std::invalid_argument& e) {
            std::cerr << e.what() << std::endl;
        }
    }
    close(client_fd);
}

int main(int argc, char** argv) {
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // Since the tester restarts your program quite often, setting SO_REUSEADDR
    // ensures that we don't run into 'Address already in use' errors
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    std::cout << "Waiting for a client to connect...\n";

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cout << "Logs from your program will appear here!\n";

    pthread_t threads[10];
    int threadId = 0;

    while (true) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        auto t = pthread_create(&threads[threadId++], NULL, &handle_client, &client_fd);
    }

    // std::cout << "Client connected\n";

    close(server_fd);

    return 0;
}

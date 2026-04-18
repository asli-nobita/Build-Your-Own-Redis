#ifndef HEADER_H 
#define HEADER_H 
#include <utility>
#include <iostream> 
#include <vector>
#include <chrono>

template <typename T>
struct DBEntry { 
    T value; 
    std::string type = "none";
    bool has_expiry = false; 
    std::chrono::time_point<std::chrono::steady_clock> expiry_time;
}; 

std::pair<std::string, std::vector<std::string>> parse_input(char* input, int n);
std::string to_resp_array(std::vector<std::string>& elements);
std::string to_resp_integer(int n); 
std::string to_bulk_string(std::string& s); 

#endif
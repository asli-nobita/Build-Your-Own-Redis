#ifndef HEADER_H 
#define HEADER_H 
#include <utility>
#include <iostream> 
#include <vector>
#include <chrono>

struct DBEntry { 
    std::string value; 
    bool has_expiry = false; 
    std::chrono::time_point<std::chrono::steady_clock> expiry_time;
}; 

std::pair<std::string, std::vector<std::string>> parse_input(char* input, int n);

#endif
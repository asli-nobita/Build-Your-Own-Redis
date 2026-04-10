#include "header.h"

std::pair<std::string, std::vector<std::string>> parse_input(char* input, int n) {
    int cur_state = 0;
    std::string cmd;
    std::vector<std::string> args;
    int arr_size = 0;
    int arg_size = 0;
    std::string cur_arg;
    for (int i = 0; i < n; i++) {
        char c = input[i];
        switch (cur_state) {
            case 0:
                if (c == '*') {
                    cur_state = 1;
                }
                else {
                    cur_state = 8;
                }
                break;
            case 1: {
                    if (isdigit(c)) {
                        arr_size = (arr_size * 10) + (c - '0');
                    }
                    else if (c == '\r') {
                        cur_state = 2;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 2: {
                    if (c == '\n') {
                        cur_state = 3;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 3: {
                    // start of a resp element (bulk string) 
                    if (c == '$') {
                        cur_state = 4;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 4: {
                    if (isdigit(c)) {
                        arg_size = (arg_size * 10) + c - '0';
                    }
                    else if (c == '\r') {
                        cur_state = 5;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 5: {
                    if (c == '\n') {
                        cur_state = 6;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 6: {
                    if (cur_arg.length() < arg_size) {
                        cur_arg += tolower(c);
                    }
                    else if (c == '\r') {
                        if (cmd.length() == 0) {
                            cmd = cur_arg;
                        }
                        else args.push_back(cur_arg);
                        cur_arg.clear();
                        arg_size = 0;
                        cur_state = 7;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 7: {
                    if (c == '\n') {
                        cur_state = 3;
                    }
                    else {
                        cur_state = 8;
                    }
                    break;
                }
            case 8: {
                    throw std::invalid_argument("Invalid RESP string.\n");
                }
        }
    }
    return { cmd, args };
}

std::string to_resp_array(std::vector<std::string>& elements) {
    std::string msg = "*" + std::to_string(elements.size()) + "\r\n";
    for (auto e : elements) {
        msg += to_bulk_string(e);
    }
    return msg; 
}

std::string to_resp_integer(int n) {
    return ":" + std::to_string(n) + "\r\n";
}

std::string to_bulk_string(std::string& s) { 
    return "$" + std::to_string(s.length()) + "\r\n" + s + "\r\n";
}
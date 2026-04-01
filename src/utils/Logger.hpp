#pragma once

#include <iostream>
#include <mutex>
#include <string>

inline std::mutex g_log_mutex;

#define RESET  "\033[0m"
#define GREEN  "\033[32m"
#define YELLOW "\033[33m"
#define RED    "\033[31m"
#define CYAN   "\033[36m"

#define LOG_INFO(msg)  { std::lock_guard<std::mutex> _l(g_log_mutex); \
                         std::cout << GREEN  << "[INFO]  " << RESET << msg << "\n"; }

#define LOG_WARN(msg)  { std::lock_guard<std::mutex> _l(g_log_mutex); \
                         std::cout << YELLOW << "[WARN]  " << RESET << msg << "\n"; }

#define LOG_ERROR(msg) { std::lock_guard<std::mutex> _l(g_log_mutex); \
                         std::cerr << RED    << "[ERROR] " << RESET << msg << "\n"; }

#define LOG_DEBUG(msg) { std::lock_guard<std::mutex> _l(g_log_mutex); \
                         std::cout << CYAN   << "[DEBUG] " << RESET << msg << "\n"; }

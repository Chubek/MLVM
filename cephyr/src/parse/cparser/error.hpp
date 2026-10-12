#pragma once
#include <stdexcept>
#include <string>

namespace cephyr::parser_detail {
struct Error : std::runtime_error {
    int line, column;
    Error(std::string message, int line, int column = 1)
        : std::runtime_error(std::move(message)), line(line), column(column) {}
};
}

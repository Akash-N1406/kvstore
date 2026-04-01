#pragma once
#include <string>
#include <vector>

// Every supported command gets a value in this enum.
// UNKNOWN is returned when the parser sees something it doesn't recognise.
// This lets the executor switch on type without comparing strings at runtime.
enum class CommandType
{
    SET,
    GET,
    DEL,
    EXPIRE,
    PING,
    UNKNOWN
};

// A fully parsed, validated command ready for the data store to execute.
// args holds everything AFTER the command name:
//   "SET name Akash"  → type=SET,    args=["name", "Akash"]
//   "GET name"        → type=GET,    args=["name"]
//   "DEL name"        → type=DEL,    args=["name"]
//   "EXPIRE name 10"  → type=EXPIRE, args=["name", "10"]
//   "PING"            → type=PING,   args=[]
struct Command
{
    CommandType type = CommandType::UNKNOWN;
    std::vector<std::string> args;

    // If type == UNKNOWN, this holds the human-readable error message
    // to send directly back to the client.
    std::string errorMsg;
};
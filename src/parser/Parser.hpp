#pragma once

#include "parser/Command.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Helper: split a string on whitespace into tokens.
// "SET  name  Akash" → ["SET", "name", "Akash"]
// Handles multiple consecutive spaces gracefully.
// ─────────────────────────────────────────────────────────────────────────────
inline std::vector<std::string> tokenise(const std::string &input)
{
    std::vector<std::string> tokens;
    std::istringstream stream(input);
    std::string token;
    while (stream >> token)
    { // >> skips leading whitespace automatically
        tokens.push_back(token);
    }
    return tokens;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: convert a string to uppercase in-place.
// Lets clients send "set", "Set", "SET" — all treated identically.
// ─────────────────────────────────────────────────────────────────────────────
inline std::string toUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// parse() — the heart of this module.
// Takes a raw input line (already stripped of \r\n) and returns a Command.
// Pure function: no side effects, no shared state, safe to call from any thread.
// ─────────────────────────────────────────────────────────────────────────────
inline Command parse(const std::string &input)
{
    Command cmd;

    // Step 1: tokenise
    std::vector<std::string> tokens = tokenise(input);

    if (tokens.empty())
    {
        cmd.type = CommandType::UNKNOWN;
        cmd.errorMsg = "-ERR empty command\r\n";
        return cmd;
    }

    // Step 2: identify command — compare uppercase so input is case-insensitive
    std::string name = toUpper(tokens[0]);

    // Step 3: validate argument count and populate cmd
    // Each branch checks the exact number of args required.
    // argc = tokens.size() - 1  (exclude the command name itself)

    if (name == "PING")
    {
        // PING takes no arguments. Used as a health check.
        if (tokens.size() != 1)
        {
            cmd.errorMsg = "-ERR PING takes no arguments\r\n";
            return cmd;
        }
        cmd.type = CommandType::PING;
    }
    else if (name == "GET")
    {
        // GET <key>
        if (tokens.size() != 2)
        {
            cmd.errorMsg = "-ERR GET requires exactly 1 argument: GET <key>\r\n";
            return cmd;
        }
        cmd.type = CommandType::GET;
        cmd.args = {tokens[1]};
    }
    else if (name == "DEL")
    {
        // DEL <key>
        if (tokens.size() != 2)
        {
            cmd.errorMsg = "-ERR DEL requires exactly 1 argument: DEL <key>\r\n";
            return cmd;
        }
        cmd.type = CommandType::DEL;
        cmd.args = {tokens[1]};
    }
    else if (name == "SET")
    {
        // SET <key> <value>
        // Value can contain spaces if quoted — for now we join remaining tokens.
        // "SET name Akash Singh" → args=["name", "Akash Singh"]
        if (tokens.size() < 3)
        {
            cmd.errorMsg = "-ERR SET requires 2 arguments: SET <key> <value>\r\n";
            return cmd;
        }
        cmd.type = CommandType::SET;
        // Join all tokens after the key back into a single value string.
        // This handles values with spaces: SET city "New York" → "New York"
        std::string value;
        for (size_t i = 2; i < tokens.size(); ++i)
        {
            if (i > 2)
                value += ' ';
            value += tokens[i];
        }
        cmd.args = {tokens[1], value};
    }
    else if (name == "EXPIRE")
    {
        // EXPIRE <key> <seconds>
        if (tokens.size() != 3)
        {
            cmd.errorMsg = "-ERR EXPIRE requires 2 arguments: EXPIRE <key> <seconds>\r\n";
            return cmd;
        }
        // Validate that seconds is actually a non-negative integer.
        try
        {
            int secs = std::stoi(tokens[2]);
            if (secs < 0)
                throw std::out_of_range("negative TTL");
            cmd.type = CommandType::EXPIRE;
            cmd.args = {tokens[1], tokens[2]};
        }
        catch (...)
        {
            cmd.errorMsg = "-ERR EXPIRE seconds must be a non-negative integer\r\n";
            return cmd;
        }
    }
    else
    {
        // Unrecognised command — tell the client exactly what we received.
        cmd.errorMsg = "-ERR unknown command '" + tokens[0] + "'\r\n";
    }

    return cmd;
}
#include "json_parser.hpp"
#include <stdexcept>
#include <iostream>

namespace dynabolic {

// JsonValue Implementation
std::string JsonParser::JsonValue::serialize() const {
    std::ostringstream oss;
    
    switch (type_) {
        case STRING:
            oss << "\"" << string_value_ << "\"";
            break;
        case NUMBER:
            oss << number_value_;
            break;
        case BOOLEAN:
            oss << (bool_value_ ? "true" : "false");
            break;
        case NULL_TYPE:
            oss << "null";
            break;
        case OBJECT: {
            oss << "{";
            bool first = true;
            for (const auto& pair : object_value_) {
                if (!first) oss << ",";
                oss << "\"" << pair.first << "\":" << pair.second->serialize();
                first = false;
            }
            oss << "}";
            break;
        }
        case ARRAY: {
            oss << "[";
            bool first = true;
            for (const auto& item : array_value_) {
                if (!first) oss << ",";
                oss << item->serialize();
                first = false;
            }
            oss << "]";
            break;
        }
    }
    
    return oss.str();
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::JsonValue::parse(const std::string& json) {
    size_t pos = 0;
    return JsonParser::parseValue(json, pos);
}

// JsonParser Implementation
std::string JsonParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseValue(const std::string& json, size_t& pos) {
    // Skip whitespace
    while (pos < json.length() && isspace(json[pos])) pos++;
    
    if (pos >= json.length()) {
        throw std::runtime_error("Unexpected end of JSON");
    }
    
    char c = json[pos];
    
    if (c == '{') {
        return parseObject(json, pos);
    } else if (c == '[') {
        return parseArray(json, pos);
    } else if (c == '"') {
        return parseString(json, pos);
    } else if (c == 't' || c == 'f') {
        return parseBoolean(json, pos);
    } else if (c == 'n') {
        return parseNull(json, pos);
    } else if (isdigit(c) || c == '-') {
        return parseNumber(json, pos);
    } else {
        throw std::runtime_error("Unexpected character in JSON");
    }
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseObject(const std::string& json, size_t& pos) {
    pos++; // Skip '{'
    
    auto obj = std::make_shared<JsonValue>();
    std::map<std::string, std::shared_ptr<JsonValue>> object_data;
    
    while (pos < json.length()) {
        // Skip whitespace
        while (pos < json.length() && isspace(json[pos])) pos++;
        
        if (pos >= json.length()) break;
        
        if (json[pos] == '}') {
            pos++; // Skip '}'
            break;
        }
        
        // Parse key
        auto key_value = parseString(json, pos);
        std::string key = key_value->asString();
        
        // Skip whitespace and colon
        while (pos < json.length() && isspace(json[pos])) pos++;
        if (pos >= json.length() || json[pos] != ':') {
            throw std::runtime_error("Expected ':' after object key");
        }
        pos++; // Skip ':'
        
        // Parse value
        auto value = parseValue(json, pos);
        
        object_data[key] = value;
        
        // Skip whitespace and comma
        while (pos < json.length() && isspace(json[pos])) pos++;
        if (pos < json.length() && json[pos] == ',') {
            pos++; // Skip ','
        }
    }
    
    obj->setObject(object_data);
    return obj;
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseArray(const std::string& json, size_t& pos) {
    pos++; // Skip '['
    
    auto arr = std::make_shared<JsonValue>();
    std::vector<std::shared_ptr<JsonValue>> array_data;
    
    while (pos < json.length()) {
        // Skip whitespace
        while (pos < json.length() && isspace(json[pos])) pos++;
        
        if (pos >= json.length()) break;
        
        if (json[pos] == ']') {
            pos++; // Skip ']'
            break;
        }
        
        // Parse value
        auto value = parseValue(json, pos);
        array_data.push_back(value);
        
        // Skip whitespace and comma
        while (pos < json.length() && isspace(json[pos])) pos++;
        if (pos < json.length() && json[pos] == ',') {
            pos++; // Skip ','
        }
    }
    
    arr->setArray(array_data);
    return arr;
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseString(const std::string& json, size_t& pos) {
    if (pos >= json.length() || json[pos] != '"') {
        throw std::runtime_error("Expected '\"' at start of string");
    }
    
    pos++; // Skip opening quote
    std::string result;
    
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\') {
            pos++; // Skip backslash
            if (pos < json.length()) {
                char escaped = json[pos];
                switch (escaped) {
                    case '"': result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/': result += '/'; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: result += escaped; break;
                }
                pos++;
            }
        } else {
            result += json[pos];
            pos++;
        }
    }
    
    if (pos >= json.length() || json[pos] != '"') {
        throw std::runtime_error("Unterminated string");
    }
    
    pos++; // Skip closing quote
    return std::make_shared<JsonValue>(result);
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseNumber(const std::string& json, size_t& pos) {
    size_t start = pos;
    
    if (json[pos] == '-') pos++;
    
    while (pos < json.length() && isdigit(json[pos])) pos++;
    
    if (pos < json.length() && json[pos] == '.') {
        pos++;
        while (pos < json.length() && isdigit(json[pos])) pos++;
    }
    
    if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
        pos++;
        if (pos < json.length() && (json[pos] == '+' || json[pos] == '-')) pos++;
        while (pos < json.length() && isdigit(json[pos])) pos++;
    }
    
    std::string num_str = json.substr(start, pos - start);
    double value = std::stod(num_str);
    
    return std::make_shared<JsonValue>(value);
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseBoolean(const std::string& json, size_t& pos) {
    if (pos + 3 < json.length() && json.substr(pos, 4) == "true") {
        pos += 4;
        return std::make_shared<JsonValue>(true);
    } else if (pos + 4 < json.length() && json.substr(pos, 5) == "false") {
        pos += 5;
        return std::make_shared<JsonValue>(false);
    } else {
        throw std::runtime_error("Invalid boolean value");
    }
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseNull(const std::string& json, size_t& pos) {
    if (pos + 3 < json.length() && json.substr(pos, 4) == "null") {
        pos += 4;
        return std::make_shared<JsonValue>();
    } else {
        throw std::runtime_error("Invalid null value");
    }
}

std::shared_ptr<JsonParser::JsonValue> JsonParser::parseFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();
    
    size_t pos = 0;
    return parseValue(content, pos);
}

bool JsonParser::saveToFile(const std::shared_ptr<JsonValue>& json, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    
    file << json->serialize();
    file.close();
    return true;
}

} // namespace dynabolic
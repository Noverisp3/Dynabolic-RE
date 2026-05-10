#ifndef JSON_PARSER_HPP
#define JSON_PARSER_HPP

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <sstream>
#include <fstream>

namespace dynabolic {

// Lightweight JSON parser using only standard library
class JsonParser {
public:
    enum JsonType {
        OBJECT,
        ARRAY,
        STRING,
        NUMBER,
        BOOLEAN,
        NULL_TYPE
    };
    
    class JsonValue {
    private:
        JsonType type_;
        std::string string_value_;
        double number_value_;
        bool bool_value_;
        std::map<std::string, std::shared_ptr<JsonValue>> object_value_;
        std::vector<std::shared_ptr<JsonValue>> array_value_;
        
    public:
        JsonValue() : type_(NULL_TYPE) {}
        JsonValue(const std::string& value) : type_(STRING), string_value_(value) {}
        JsonValue(double value) : type_(NUMBER), number_value_(value) {}
        JsonValue(int value) : type_(NUMBER), number_value_(static_cast<double>(value)) {}
        JsonValue(bool value) : type_(BOOLEAN), bool_value_(value) {}
        
        JsonType getType() const { return type_; }
        
        const std::string& asString() const { return string_value_; }
        double asNumber() const { return number_value_; }
        bool asBool() const { return bool_value_; }
        
        const std::map<std::string, std::shared_ptr<JsonValue>>& asObject() const { 
            return object_value_; 
        }
        const std::vector<std::shared_ptr<JsonValue>>& asArray() const { 
            return array_value_; 
        }
        
        void setObject(const std::map<std::string, std::shared_ptr<JsonValue>>& obj) {
            type_ = OBJECT;
            object_value_ = obj;
        }
        
        void setArray(const std::vector<std::shared_ptr<JsonValue>>& arr) {
            type_ = ARRAY;
            array_value_ = arr;
        }
        
        std::string serialize() const;
        static std::shared_ptr<JsonValue> parse(const std::string& json);
    };
    
private:
    static std::string trim(const std::string& str);
    static std::shared_ptr<JsonValue> parseValue(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parseObject(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parseArray(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parseString(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parseNumber(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parseBoolean(const std::string& json, size_t& pos);
    static std::shared_ptr<JsonValue> parseNull(const std::string& json, size_t& pos);
    
public:
    static std::shared_ptr<JsonValue> parseFile(const std::string& filename);
    static bool saveToFile(const std::shared_ptr<JsonValue>& json, const std::string& filename);
};

} // namespace dynabolic

#endif // JSON_PARSER_HPP
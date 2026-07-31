/**
 * @file json.cpp
 * @brief 轻量 JSON 解析与序列化实现
 */
#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace json {

namespace {

/** @brief 将 UTF-16 码点编码为 UTF-8 追加到输出 */
void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/** @brief 解析器：持有一个输入文本与游标 */
class Parser
{
public:
    /**
     * @brief 构造解析器
     * @param text 输入文本
     */
    explicit Parser(const std::string& text)
        : text_(text)
    {
    }

    /**
     * @brief 解析完整文档
     * @param out 输出值
     * @return 成功返回 true
     */
    bool parseDocument(Value& out)
    {
        skipWhitespace();
        if (!parseValue(out)) {
            return false;
        }
        skipWhitespace();
        if (pos_ != text_.size()) {
            fail("根元素后存在多余内容");
            return false;
        }
        return true;
    }

    /** @brief 获取错误信息（解析失败后有效） */
    const std::string& error() const { return error_; }

private:
    const std::string& text_;  ///< 输入文本
    size_t pos_ = 0;           ///< 当前游标
    std::string error_;        ///< 错误信息

    /** @brief 记录错误并标记失败 */
    void fail(const std::string& msg)
    {
        if (error_.empty()) {
            error_ = msg + " (位置 " + std::to_string(pos_) + ")";
        }
    }

    /** @brief 跳过空白字符 */
    void skipWhitespace()
    {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    /** @brief 当前字符，越界返回 '\0' */
    char peek() const
    {
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    /** @brief 解析一个值 */
    bool parseValue(Value& out)
    {
        const char c = peek();
        if (c == '{') {
            return parseObject(out);
        }
        if (c == '[') {
            return parseArray(out);
        }
        if (c == '"') {
            std::string s;
            if (!parseString(s)) {
                return false;
            }
            out = Value(std::move(s));
            return true;
        }
        if (c == 't' && text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = Value(true);
            return true;
        }
        if (c == 'f' && text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = Value(false);
            return true;
        }
        if (c == 'n' && text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out = Value();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber(out);
        }
        fail("无法识别的值起始字符");
        return false;
    }

    /** @brief 解析对象 */
    bool parseObject(Value& out)
    {
        ++pos_; // 跳过 '{'
        Value::Object items;
        skipWhitespace();
        if (peek() == '}') {
            ++pos_;
            out = Value(std::move(items));
            return true;
        }
        while (true) {
            skipWhitespace();
            if (peek() != '"') {
                fail("对象键必须是字符串");
                return false;
            }
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipWhitespace();
            if (peek() != ':') {
                fail("对象键后缺少冒号");
                return false;
            }
            ++pos_;
            skipWhitespace();
            Value value;
            if (!parseValue(value)) {
                return false;
            }
            items.emplace_back(std::move(key), std::move(value));
            skipWhitespace();
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_;
                out = Value(std::move(items));
                return true;
            }
            fail("对象中缺少逗号或右花括号");
            return false;
        }
    }

    /** @brief 解析数组 */
    bool parseArray(Value& out)
    {
        ++pos_; // 跳过 '['
        Value::Array items;
        skipWhitespace();
        if (peek() == ']') {
            ++pos_;
            out = Value(std::move(items));
            return true;
        }
        while (true) {
            skipWhitespace();
            Value value;
            if (!parseValue(value)) {
                return false;
            }
            items.push_back(std::move(value));
            skipWhitespace();
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == ']') {
                ++pos_;
                out = Value(std::move(items));
                return true;
            }
            fail("数组中缺少逗号或右方括号");
            return false;
        }
    }

    /** @brief 解析数字 */
    bool parseNumber(Value& out)
    {
        const size_t start = pos_;
        if (peek() == '-') {
            ++pos_;
        }
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        }
        if (pos_ == start || (pos_ == start + 1 && text_[start] == '-')) {
            fail("无效的数字");
            return false;
        }
        const std::string token = text_.substr(start, pos_ - start);
        out = Value(std::strtod(token.c_str(), nullptr));
        return true;
    }

    /** @brief 解析带引号的字符串（含转义） */
    bool parseString(std::string& out)
    {
        if (peek() != '"') {
            fail("期望字符串起始引号");
            return false;
        }
        ++pos_; // 跳过 '"'
        std::string result;
        while (true) {
            if (pos_ >= text_.size()) {
                fail("字符串未闭合");
                return false;
            }
            const char c = text_[pos_++];
            if (c == '"') {
                out = std::move(result);
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    fail("转义序列不完整");
                    return false;
                }
                const char e = text_[pos_++];
                switch (e) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (pos_ >= text_.size()) {
                            fail("Unicode 转义不完整");
                            return false;
                        }
                        const char h = text_[pos_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') {
                            cp |= static_cast<uint32_t>(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            cp |= static_cast<uint32_t>(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            cp |= static_cast<uint32_t>(h - 'A' + 10);
                        } else {
                            fail("无效的 Unicode 转义");
                            return false;
                        }
                    }
                    // 处理 UTF-16 代理对
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t low = 0;
                        for (int i = 0; i < 4; ++i) {
                            const char h = text_[pos_++];
                            low <<= 4;
                            if (h >= '0' && h <= '9') {
                                low |= static_cast<uint32_t>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                low |= static_cast<uint32_t>(h - 'a' + 10);
                            } else {
                                low |= static_cast<uint32_t>(h - 'A' + 10);
                            }
                        }
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    appendUtf8(result, cp);
                    break;
                }
                default:
                    fail("未知转义字符");
                    return false;
                }
            } else {
                result += c;
            }
        }
    }
};

/** @brief 字符串转义（序列化用） */
std::string escapeString(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

/** @brief 数字序列化（尽量紧凑） */
std::string formatNumber(double n)
{
    if (std::isnan(n) || std::isinf(n)) {
        return "0";
    }
    std::ostringstream oss;
    oss << std::setprecision(15) << n;
    std::string s = oss.str();
    // 去掉多余的 .000000
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s;
}

/** @brief 递归序列化 */
void stringifyValue(const Value& v, std::string& out)
{
    switch (v.type()) {
    case Type::Null:
        out += "null";
        break;
    case Type::Bool:
        out += v.asBool() ? "true" : "false";
        break;
    case Type::Number:
        out += formatNumber(v.asNumber());
        break;
    case Type::String:
        out += '"';
        out += escapeString(v.asString());
        out += '"';
        break;
    case Type::Array: {
        out += '[';
        bool first = true;
        for (const auto& item : v.array()) {
            if (!first) {
                out += ',';
            }
            first = false;
            stringifyValue(item, out);
        }
        out += ']';
        break;
    }
    case Type::Object: {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : v.object()) {
            if (!first) {
                out += ',';
            }
            first = false;
            out += '"';
            out += escapeString(key);
            out += "\":";
            stringifyValue(item, out);
        }
        out += '}';
        break;
    }
    }
}

} // namespace

Value::Value(bool b)
    : type_(Type::Bool)
    , bool_(b)
{
}

Value::Value(int n)
    : type_(Type::Number)
    , number_(static_cast<double>(n))
{
}

Value::Value(int64_t n)
    : type_(Type::Number)
    , number_(static_cast<double>(n))
{
}

Value::Value(uint64_t n)
    : type_(Type::Number)
    , number_(static_cast<double>(n))
{
}

Value::Value(double n)
    : type_(Type::Number)
    , number_(n)
{
}

Value::Value(const char* s)
    : type_(Type::String)
    , string_(s ? s : "")
{
}

Value::Value(std::string s)
    : type_(Type::String)
    , string_(std::move(s))
{
}

Value::Value(Array arr)
    : type_(Type::Array)
    , array_(std::make_shared<Array>(std::move(arr)))
{
}

Value::Value(Object obj)
    : type_(Type::Object)
    , object_(std::make_shared<Object>(std::move(obj)))
{
}

Type Value::type() const { return type_; }
bool Value::isNull() const { return type_ == Type::Null; }
bool Value::isBool() const { return type_ == Type::Bool; }
bool Value::isNumber() const { return type_ == Type::Number; }
bool Value::isString() const { return type_ == Type::String; }
bool Value::isArray() const { return type_ == Type::Array; }
bool Value::isObject() const { return type_ == Type::Object; }

bool Value::asBool(bool def) const
{
    return type_ == Type::Bool ? bool_ : def;
}

double Value::asNumber(double def) const
{
    return type_ == Type::Number ? number_ : def;
}

int64_t Value::asInt64(int64_t def) const
{
    return type_ == Type::Number ? static_cast<int64_t>(number_) : def;
}

std::string Value::asString(const std::string& def) const
{
    return type_ == Type::String ? string_ : def;
}

Value::Array& Value::array() { return *array_; }
const Value::Array& Value::array() const { return *array_; }
Value::Object& Value::object() { return *object_; }
const Value::Object& Value::object() const { return *object_; }

const Value* Value::find(const std::string& key) const
{
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& [k, v] : *object_) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

Value Value::clone() const
{
    switch (type_) {
    case Type::Null:
        return Value();
    case Type::Bool:
        return Value(bool_);
    case Type::Number:
        return Value(number_);
    case Type::String:
        return Value(string_);
    case Type::Array: {
        Array arr;
        arr.reserve(array_->size());
        for (const auto& item : *array_) {
            arr.push_back(item.clone());
        }
        return Value(std::move(arr));
    }
    case Type::Object: {
        Object obj;
        obj.reserve(object_->size());
        for (const auto& [k, v] : *object_) {
            obj.emplace_back(k, v.clone());
        }
        return Value(std::move(obj));
    }
    }
    return Value();
}

std::string Value::stringify() const
{
    std::string out;
    stringifyValue(*this, out);
    return out;
}

Value parse(const std::string& text, std::string* error)
{
    Parser parser(text);
    Value result;
    if (parser.parseDocument(result)) {
        return result;
    }
    if (error != nullptr) {
        *error = parser.error();
    }
    return Value();
}

std::string stringify(const Value& value)
{
    return value.stringify();
}

} // namespace json

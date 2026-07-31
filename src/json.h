/**
 * @file json.h
 * @brief 轻量 JSON 解析与序列化（零第三方依赖，跨平台）
 * @note 支持 null/bool/数字/字符串/数组/对象；对象保持插入顺序；
 *       仅用于本项目配置文件与 GATT 特征值，未覆盖完整 JSON 规范。
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace json {

/**
 * @brief JSON 值类型枚举
 */
enum class Type {
    Null,    ///< 空值
    Bool,    ///< 布尔
    Number,  ///< 数字（统一按 double 存储）
    String,  ///< 字符串
    Array,   ///< 数组
    Object   ///< 对象（键值对，保持插入顺序）
};

/**
 * @brief JSON 值（递归结构，采用共享指针打破递归定义）
 * @note 拷贝构造为浅拷贝共享数据；如需独立修改请先深拷贝（clone）。
 */
class Value
{
public:
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    /** @brief 构造空值 */
    Value() = default;

    /** @brief 从布尔构造 */
    explicit Value(bool b);

    /** @brief 从整数构造（存为 double） */
    explicit Value(int n);

    /** @brief 从 64 位有符号整数构造（存为 double） */
    explicit Value(int64_t n);

    /** @brief 从 64 位无符号整数构造（存为 double） */
    explicit Value(uint64_t n);

    /** @brief 从浮点数构造 */
    explicit Value(double n);

    /** @brief 从 C 字符串构造 */
    explicit Value(const char* s);

    /** @brief 从字符串构造 */
    explicit Value(std::string s);

    /** @brief 从数组构造 */
    explicit Value(Array arr);

    /** @brief 从对象构造 */
    explicit Value(Object obj);

    /** @brief 获取值类型 */
    Type type() const;

    /** @brief 是否为 null */
    bool isNull() const;

    /** @brief 是否为布尔 */
    bool isBool() const;

    /** @brief 是否为数字 */
    bool isNumber() const;

    /** @brief 是否为字符串 */
    bool isString() const;

    /** @brief 是否为数组 */
    bool isArray() const;

    /** @brief 是否为对象 */
    bool isObject() const;

    /**
     * @brief 获取布尔值
     * @param def 类型不匹配时的默认值
     * @return 布尔值
     */
    bool asBool(bool def = false) const;

    /**
     * @brief 获取数值
     * @param def 类型不匹配时的默认值
     * @return 数值
     */
    double asNumber(double def = 0.0) const;

    /**
     * @brief 获取整数值
     * @param def 类型不匹配时的默认值
     * @return 整数值
     */
    int64_t asInt64(int64_t def = 0) const;

    /**
     * @brief 获取字符串
     * @param def 类型不匹配时的默认值
     * @return 字符串
     */
    std::string asString(const std::string& def = "") const;

    /**
     * @brief 获取数组引用（仅当为数组时有效）
     * @return 数组引用
     */
    Array& array();

    /**
     * @brief 获取数组引用（const 版本）
     * @return 数组引用
     */
    const Array& array() const;

    /**
     * @brief 获取对象引用（仅当为对象时有效）
     * @return 对象引用
     */
    Object& object();

    /**
     * @brief 获取对象引用（const 版本）
     * @return 对象引用
     */
    const Object& object() const;

    /**
     * @brief 在对象中按键查找
     * @param key 键名
     * @return 找到返回指针，否则返回 nullptr
     */
    const Value* find(const std::string& key) const;

    /**
     * @brief 深拷贝
     * @return 独立副本
     */
    Value clone() const;

    /** @brief 序列化为紧凑 JSON 字符串 */
    std::string stringify() const;

private:
    Type type_ = Type::Null;          ///< 值类型
    bool bool_ = false;               ///< 布尔存储
    double number_ = 0.0;             ///< 数字存储
    std::string string_;              ///< 字符串存储
    std::shared_ptr<Array> array_;    ///< 数组存储
    std::shared_ptr<Object> object_;  ///< 对象存储
};

/**
 * @brief 解析 JSON 文本
 * @param text 输入文本
 * @param error 可选输出：解析失败时的错误信息
 * @return 解析结果；失败时返回 Null 值并填充 error
 */
Value parse(const std::string& text, std::string* error = nullptr);

/**
 * @brief 序列化 JSON 值为紧凑字符串
 * @param value 输入值
 * @return JSON 字符串
 */
std::string stringify(const Value& value);

} // namespace json

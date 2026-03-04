#include "../include/interpreter.h"
#include <vector>
#include "../include/module_registry.h"
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace json_lib {

class Parser {
public:
    explicit Parser(const std::string& text) : s(text), i(0) {}

    Value parse() {
        skipWs();
        Value out = parseValue();
        skipWs();
        if (i != s.size()) {
            throw std::runtime_error("json.parse: trailing characters");
        }
        return out;
    }

private:
    const std::string& s;
    size_t i;

    void skipWs() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
    }

    bool match(const std::string& token) {
        if (s.compare(i, token.size(), token) == 0) {
            i += token.size();
            return true;
        }
        return false;
    }

    Value parseValue() {
        skipWs();
        if (i >= s.size()) {
            throw std::runtime_error("json.parse: unexpected end");
        }
        char c = s[i];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Value::fromString(parseString());
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return Value::fromNumber(parseNumber());
        if (match("true")) return Value::fromBool(true);
        if (match("false")) return Value::fromBool(false);
        if (match("null")) return Value::fromString("null");
        throw std::runtime_error("json.parse: invalid token");
    }

    Value parseObject() {
        ++i; // {
        skipWs();
        std::unordered_map<std::string, Value> obj;
        if (i < s.size() && s[i] == '}') {
            ++i;
            return Value::fromMap(std::move(obj));
        }

        while (true) {
            skipWs();
            if (i >= s.size() || s[i] != '"') {
                throw std::runtime_error("json.parse: object key must be string");
            }
            std::string key = parseString();
            skipWs();
            if (i >= s.size() || s[i] != ':') {
                throw std::runtime_error("json.parse: expected ':'");
            }
            ++i;
            Value val = parseValue();
            obj[key] = std::move(val);
            skipWs();
            if (i >= s.size()) {
                throw std::runtime_error("json.parse: unexpected end in object");
            }
            if (s[i] == '}') {
                ++i;
                break;
            }
            if (s[i] != ',') {
                throw std::runtime_error("json.parse: expected ','");
            }
            ++i;
        }

        return Value::fromMap(std::move(obj));
    }

    Value parseArray() {
        ++i; // [
        skipWs();
        std::vector<Value> arr;
        if (i < s.size() && s[i] == ']') {
            ++i;
            return Value::fromList(std::move(arr));
        }

        while (true) {
            arr.push_back(parseValue());
            skipWs();
            if (i >= s.size()) {
                throw std::runtime_error("json.parse: unexpected end in array");
            }
            if (s[i] == ']') {
                ++i;
                break;
            }
            if (s[i] != ',') {
                throw std::runtime_error("json.parse: expected ','");
            }
            ++i;
        }

        return Value::fromList(std::move(arr));
    }

    std::string parseString() {
        ++i; // opening "
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i >= s.size()) throw std::runtime_error("json.parse: bad escape");
                char e = s[i++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: throw std::runtime_error("json.parse: unsupported escape");
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("json.parse: unterminated string");
    }

    double parseNumber() {
        size_t start = i;
        if (s[i] == '-') ++i;
        if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
            throw std::runtime_error("json.parse: invalid number");
        }
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
                throw std::runtime_error("json.parse: invalid number");
            }
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
                throw std::runtime_error("json.parse: invalid exponent");
            }
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        }
        return std::stod(s.substr(start, i - start));
    }
};

static std::string escapeString(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream ss;
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(static_cast<unsigned char>(c));
                    out += ss.str();
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

static std::string numToJson(double n) {
    std::ostringstream ss;
    ss << std::setprecision(15) << n;
    std::string out = ss.str();
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty() || out == "-0") out = "0";
    return out;
}

static std::string stringifyImpl(const Value& v, bool pretty, int indentSize, int depth) {
    const std::string indent = pretty ? std::string(depth * indentSize, ' ') : "";
    const std::string nextIndent = pretty ? std::string((depth + 1) * indentSize, ' ') : "";
    const std::string nl = pretty ? "\n" : "";
    const std::string sp = pretty ? " " : "";

    if (v.type == ValueType::NUMBER) return numToJson(v.number);
    if (v.type == ValueType::BOOL) return v.boolean ? "true" : "false";
    if (v.type == ValueType::STRING) {
        if (v.str == "null") return "null";
        return "\"" + escapeString(v.str) + "\"";
    }
    if (v.type == ValueType::LIST) {
        std::string out = "[";
        if (!v.list.empty()) out += nl;
        for (size_t i = 0; i < v.list.size(); ++i) {
            if (i) out += "," + nl;
            if (pretty) out += nextIndent;
            out += stringifyImpl(v.list[i], pretty, indentSize, depth + 1);
        }
        if (!v.list.empty() && pretty) out += nl + indent;
        out += "]";
        return out;
    }

    std::string out = "{";
    if (!v.map.empty()) out += nl;
    bool first = true;
    for (const auto& kv : v.map) {
        if (!first) out += "," + nl;
        first = false;
        if (pretty) out += nextIndent;
        out += "\"" + escapeString(kv.first) + "\":" + sp +
               stringifyImpl(kv.second, pretty, indentSize, depth + 1);
    }
    if (!v.map.empty() && pretty) out += nl + indent;
    out += "}";
    return out;
}

Value parse(const std::string& text) {
    Parser p(text);
    return p.parse();
}

bool valid(const std::string& text) {
    try {
        Parser p(text);
        (void)p.parse();
        return true;
    } catch (...) {
        return false;
    }
}

std::string stringify(const Value& v) {
    return stringifyImpl(v, false, 2, 0);
}

std::string pretty(const Value& v, int indentSize) {
    if (indentSize < 0) indentSize = 0;
    return stringifyImpl(v, true, indentSize, 0);
}

} // namespace json_lib

extern "C" __declspec(dllexport)
void register_module() {
    module_registry::registerModule("json", [](Interpreter& interp) {
                    interp.registerModuleFunction("json", "parse", [&interp](const std::vector<Value>& args) -> Value {
                        interp.expectArity(args, 1, "json.parse");
                        return json_lib::parse(interp.expectString(args[0], "json.parse expects string"));
                    });
                    interp.registerModuleFunction("json", "stringify", [&interp](const std::vector<Value>& args) -> Value {
                        interp.expectArity(args, 1, "json.stringify");
                        return Value::fromString(json_lib::stringify(args[0]));
                    });
                    interp.registerModuleFunction("json", "valid", [&interp](const std::vector<Value>& args) -> Value {
                        interp.expectArity(args, 1, "json.valid");
                        return Value::fromBool(json_lib::valid(interp.expectString(args[0], "json.valid expects string")));
                    });
                    interp.registerModuleFunction("json", "pretty", [&interp](const std::vector<Value>& args) -> Value {
                        if (args.empty() || args.size() > 2) {
                            throw std::runtime_error("json.pretty expects 1 or 2 argument(s)");
                        }
                        int indent = 2;
                        if (args.size() == 2) {
                            indent = static_cast<int>(interp.expectNumber(args[1], "json.pretty indent expects number"));
                        }
                        if (args[0].type == ValueType::STRING) {
                            Value parsed = json_lib::parse(args[0].str);
                            return Value::fromString(json_lib::pretty(parsed, indent));
                        }
                        return Value::fromString(json_lib::pretty(args[0], indent));
                    });

    });
}

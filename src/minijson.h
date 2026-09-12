#pragma once
#include <string>
#include <utility>
#include <vector>

// A deliberately small, self-contained JSON DOM parser. It exists because
// the Jellyfin native API returns real nested structures (arrays of search
// hints, lyric DTOs with cue arrays) that the old single-flat-key regex
// extractors couldn't walk. Only has to handle well-formed server output,
// so it trades strictness for ~150 lines of audit-friendly code instead of
// pulling in a third-party JSON library.
namespace minijson {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;

    bool is_null() const { return type == Type::Null; }
    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    size_t size() const;

    // Object member by name, or nullptr if this isn't an object / has no
    // such member.
    const Value* get(const std::string& key) const;

    // Array element, or nullptr if out of range / not an array.
    const Value* at(size_t idx) const;

    std::string as_string() const;
    double as_number() const;
};

// Parses `text` into `out`. Returns false (leaving `out` as Type::Null)
// if the document isn't valid JSON.
bool parse(const std::string& text, Value& out);

} // namespace minijson
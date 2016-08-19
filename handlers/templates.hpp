#pragma once
#include <type_traits>
#include <string>
#include <sstream>

#include <mustache/mustache.hpp>

#include "yaml_config.hpp"
#include "utils.hpp"
#include "relation_map.hpp"

#define DISABLE_ANY XLSXCONVERTER_UTILS_DISABLE_ANY 
#define ENABLE_ANY  XLSXCONVERTER_UTILS_ENABLE_ANY

namespace xlsxconverter {
namespace handlers {

namespace {

inline bool islower_(char c) {
    return 'a' <= c && c <= 'z';
}
inline bool isupper_(char c) {
    return 'A' <= c && c <= 'Z';
}

inline std::string lower_(const std::string& s) {
    std::string r;
    for (auto c: s) {
        if (islower_(c)) r.push_back(c - 'A' + 'a');
        else r.push_back(c);
    }
    return r;
}
inline std::string upper_(const std::string& s) {
    std::string r;
    for (auto c: s) {
        if (islower_(c)) r.push_back(c - 'a' + 'A');
        else r.push_back(c);
    }
    return r;
}
inline std::string snake_case_(const std::string& s) {
    std::string r;
    for (auto it = s.begin(); it != s.end(); ++it) {
        if (it != s.begin() && islower_(*(it-1)) && isupper_(*it)) r.push_back('_');
        if (isupper_(*it)) r.push_back(*it - 'A' + 'a');
        else r.push_back(*it);
    }
    return r;
}
inline std::string lower_camel_(const std::string& s) {
    std::string r;
    for (auto it = s.begin(); it != s.end(); ++it) {
        if (*it == '_') continue;
        if (it != s.begin() && *(it-1) == '_' && islower_(*it)) r.push_back(*it - 'a' + 'A');
        else if (it != s.begin() && std::isalpha(*(it-1)) && isupper_(*it)) r.push_back(*it - 'A' + 'a');
        else r.push_back(*it);
    }
    return r;
}
inline std::string upper_camel_(const std::string& s) {
    std::string r;
    for (auto it = s.begin(); it != s.end(); ++it) {
        if (*it == '_') continue;
        if (it == s.begin() && islower_(*it)) r.push_back(*it - 'a' + 'A');
        else if (it != s.begin() && *(it-1) == '_' && islower_(*it)) r.push_back(*it - 'a' + 'A');
        else if (it != s.begin() && std::isalpha(*(it-1)) && isupper_(*it)) r.push_back(*it - 'A' + 'a');
        else r.push_back(*it);
    }
    return r;
}

}

struct TemplateHandler
{
    using Mustache = Kainjow::Mustache;
    using Data = Kainjow::Mustache::Data;

    YamlConfig& config;
    std::stringstream buffer;

    boost::optional<Mustache&> template_;
    Data records;
    Data current_record;
    Data current_record_fields;

    inline
    TemplateHandler(YamlConfig& config) 
        : config(config),
          template_(boost::none),
          records(Data::Type::List),
          current_record_fields(Data::Type::List),
          current_record()
    {}

    inline static utils::mutex_map<std::string, Mustache>& template_cache() {
        static utils::mutex_map<std::string, Mustache> cache_;
        return cache_;
    }

    inline
    void begin() {
        buffer.clear();
        auto& source = config.handler.source;
        auto t = template_cache().get(source);
        if (t != boost::none) {
            template_ = t.value();
            return;
        }
        Mustache tmpl(utils::readfile(source));
        template_ = template_cache().emplace_ref(source, std::move(tmpl));

        template_->registerFilter("upper", [](const Data* data, Mustache::Context* ctx) -> const Data* {
            if (!data->isString()) return data;
            return ctx->addPool(Data(upper_(data->stringValue())));
        });
        template_->registerFilter("lower", [](const Data* data, Mustache::Context* ctx) -> const Data* {
            if (!data->isString()) return data;
            return ctx->addPool(Data(lower_(data->stringValue())));
        });
        template_->registerFilter("snake_case", [](const Data* data, Mustache::Context* ctx) -> const Data* {
            if (!data->isString()) return data;
            return ctx->addPool(Data(snake_case_(data->stringValue())));
        });
        template_->registerFilter("upper_camel", [](const Data* data, Mustache::Context* ctx) -> const Data* {
            if (!data->isString()) return data;
            return ctx->addPool(Data(upper_camel_(data->stringValue())));
        });
        template_->registerFilter("lower_camel", [](const Data* data, Mustache::Context* ctx) -> const Data* {
            if (!data->isString()) return data;
            return ctx->addPool(Data(lower_camel_(data->stringValue())));
        });
    }

    inline
    void begin_comment_row() {}

    inline
    void end_comment_row() {}

    inline
    void begin_row() {
        current_record = Data();
        current_record_fields = Data(Data::Type::List);
    }

    template<class T, DISABLE_ANY(T, bool, std::nullptr_t)>
    void field(YamlConfig::Field& field, const T& value) {
        std::stringstream ss;
        ss << value;
        append(field, ss.str());
    }

    template<class T, ENABLE_ANY(T, bool)>
    void field(YamlConfig::Field& field, const T& value) {
        append(field, value ? "true" : "false");
    }

    template<class T, ENABLE_ANY(T, std::nullptr_t)>
    void field(YamlConfig::Field& field, const T& value) {
        append(field, "null");
    }

    void append(YamlConfig::Field& field, std::string s) {
        Data field_data;
        field_data.set("column", Data(field.column));
        field_data.set("name", Data(field.name));
        field_data.set("type", Data(field.type_name));
        field_data.set("value", Data(s));
        current_record_fields.push_back(field_data);
        current_record.set(field.column, Data(s));
    }

    inline
    void end_row() {
        current_record.set("fields", current_record_fields);
        records.push_back(current_record);
    }

    inline Data yaml2data(YAML::Node node) {
        if (node.Type() == YAML::NodeType::Scalar) {
            return Data(node.as<std::string>());
        }
        if (node.Type() != YAML::NodeType::Map) {
            return Data();
        }
        Data map;
        for (auto it: node) {
            map.set(it.first.as<std::string>(), yaml2data(it.second));
        }
        return map;
    }

    inline
    void end() {
        Data context;
        if (config.handler.context.Type() == YAML::NodeType::Map) {
            context = yaml2data(config.handler.context);
        }
        context.set("records", records);

        buffer << template_.value().render(context);
    }

    inline
    void save() {
        utils::writefile(config.get_output_path(), buffer.str());
        if (!config.arg_config.quiet) {
            utils::log(config.handler.path, " writed.");
        }
    }
};


}
}
#undef DISABLE_ANY 
#undef ENABLE_ANY
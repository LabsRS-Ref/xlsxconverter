// Copyright (c) 2016 peposso All Rights Reserved.
// Released under the MIT license
#pragma once
#include <string>
#include "json.hpp"

#define DISABLE_ANY XLSXCONVERTER_UTILS_DISABLE_ANY
#define ENABLE_ANY  XLSXCONVERTER_UTILS_ENABLE_ANY
#define EXCEPTION XLSXCONVERTER_UTILS_EXCEPTION

namespace xlsxconverter {
namespace handlers {

struct DjangoFixtureHandler : public JsonHandler {
    int pk_index = -1;
    int64_t pk_intvalue = -1;
    std::string pk_strvalue;

    inline
    explicit DjangoFixtureHandler(YamlConfig::Handler& handler_config_, YamlConfig& config_)
            : JsonHandler(handler_config_, config_) {
        for (auto& field : config.fields) {
            if (field.column == "ID" || field.column == "id" || field.column == "Id") {
                pk_index = field.index;
            }
        }
        if (pk_index == -1) {
            throw EXCEPTION("id field is not found.");
        }
        field_indent = indent + indent + indent;
    }

    inline
    void begin_row() {
        JsonHandler::begin_row();
        buffer << endl << indent << indent;
        write_key("fields");
        buffer << '{';
        pk_strvalue.clear();
        pk_intvalue = -1;
    }

    inline
    void end_row() {
        if (!is_first_field && !is_first_row) {
            buffer << endl << indent << indent;
        }
        buffer << "}," << endl;
        buffer << indent << indent;
        write_key("pk");
        if (pk_intvalue > -1) {
            write_value(pk_intvalue);
        } else if (!pk_strvalue.empty()) {
            write_value(pk_strvalue);
        } else {
            throw EXCEPTION("pk column not found.");
        }
        buffer << endl << indent << "}";
    }

    template<class T, DISABLE_ANY(T, int64_t, std::string)>
    void set_pk(const T& v) {
        throw EXCEPTION("bad pk type");
    }
    template<class T, ENABLE_ANY(T, int64_t)>
    void set_pk(const T& v) {
        pk_intvalue = v;
    }
    template<class T, ENABLE_ANY(T, std::string)>
    void set_pk(const T& v) {
        pk_strvalue = v;
    }

    template<class T>
    void field(YamlConfig::Field& field, const T& value) {
        if (comment) return;
        JsonHandler::field(field, value);
        if (field.index == pk_index) {
            set_pk(value);
        }
    }
};

}  // namespace handlers
}  // namespace xlsxconverter
#undef DISABLE_ANY
#undef ENABLE_ANY
#undef EXCEPTION

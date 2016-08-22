#pragma once
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

#include <iterator>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>

namespace xlsxconverter {
namespace utils {
namespace fs {

inline
char sep() {
    #ifdef _WIN32
    return '\\';
    #else
    return '/';
    #endif
}
inline
std::string joinpath(const std::string& path1, const std::string& path2) {
    if (path1.empty()) return path2;
    if (path2.empty()) return path1;
    char c1 = path1[path1.size()-1];
    if (c1 == '/' || c1 == '\\') {
        return joinpath(path1.substr(0, path1.size()-1), path2);
    }
    char c2 = path2[0];
    if (c2 == '/' || c2 == '\\') {
        return joinpath(path1, path2.substr(1));
    }
    return path1 + sep() + path2;
}
template<class...T>
std::string joinpath(const std::string& path1, const std::string& path2, const T&...t) {
    return joinpath(joinpath(path1, path2), t...);
}
inline
bool exists(const std::string& name) {
    struct stat statbuf;
    return ::stat(name.c_str(), &statbuf) == 0;
}
inline
bool isabspath(const std::string& name) {
    if (name.empty()) return false;
    char c = name[0];
    if (c == '/') return true;
    if (name.size() == 1) return false;
    if (std::isalpha(c) && name[1] == ':') {
        if (name.size() == 2) return true;
        if (name[2] == '/' || name[2] == '\\') return true;
    }
    if (c == '\\' && name[1] == '\\') return true;
    return false;
}
inline
std::string dirname(const std::string& name) {
    auto p1 = name.rfind('/');
    auto p2 = name.rfind('\\');
    if (p1 == std::string::npos && p1 == std::string::npos) return "";
    if (p1 != std::string::npos) return name.substr(0, p1);
    return name.substr(0, p2);
}
inline
bool startswith(const std::string& haystack, const std::string& needle) {
    if (haystack.size() < needle.size()) return false;
    if (haystack.size() == needle.size()) return haystack == needle;
    return haystack.substr(0, needle.size()) == needle;
}
inline
bool endswith(const std::string& haystack, const std::string& needle) {
    if (haystack.size() < needle.size()) return false;
    if (haystack.size() == needle.size()) return haystack == needle;
    return haystack.substr(haystack.size() - needle.size()) == needle;
}
inline
bool matchname(const std::string& haystack, const std::string& needle) {
    // support only "aaa" "*aaa" "aaa*" "*aaa*" "*" "aaa*bbb"
    if (needle.empty()) return true;
    if (needle == "*") return true;
    auto len = needle.size();
    if (needle[0] == '*' && needle[len-1] == '*') {  // "*aaa*"
        auto pat = needle.substr(1, len-1);
        if (pat.find('*') != std::string::npos) throw std::runtime_error("not supported pattern");
        return haystack.find(pat) != std::string::npos;
    }
    if (needle[0] == '*') {  // "*aaa"
        auto pat = needle.substr(1, len);
        if (pat.find('*') != std::string::npos) throw std::runtime_error("not supported pattern");
        return endswith(haystack, pat);
    }
    if (needle[len-1] == '*') {  // "aaa*"
        auto pat = needle.substr(0, len-1);
        if (pat.find('*') != std::string::npos) throw std::runtime_error("not supported pattern");
        return startswith(haystack, pat);
    }
    auto p = needle.find('*');
    if (p == std::string::npos) {  // "aaa"
        return haystack == needle;
    }
    // "aaa*bbb"
    auto pat1 = needle.substr(0, p);
    auto pat2 = needle.substr(p+1, len);
    if (pat1.find('*') != std::string::npos) throw std::runtime_error("not supported pattern");
    if (pat2.find('*') != std::string::npos) throw std::runtime_error("not supported pattern");
    if (haystack.size() < pat1.size() + pat2.size()) {
        return false;
    }
    return startswith(haystack, pat1) && endswith(haystack, pat2);
}

inline
void mkdirp(const std::string& name) {
    if (name.empty()) return;
    std::vector<std::string> names;
    std::string buf;
    for (auto c: name) {
        if (c == '/' || c == '\\') {
            names.push_back(buf);
            buf.clear();
        } else {
            buf.push_back(c);
        }
    }
    names.push_back(buf);
    std::string path;
    if (isabspath(name)) {
        path = names[0];
        names.erase(names.begin());
    }
    for (auto n: names) {
        path = path.empty() ? n : path + sep() + n;
        if (!exists(path)) {
            #ifdef _WIN32
            ::mkdir(path.c_str());
            #else
            ::mkdir(path.c_str(), 0755);
            #endif
        } 
    }
}
inline
std::string readfile(const std::string& name) {
    auto fi = std::ifstream(name.c_str(), std::ios::binary);
    std::stringstream ss;
    ss << fi.rdbuf();
    return ss.str();
}
inline
void writefile(const std::string& name, const std::string& content) {
    mkdirp(dirname(name));
    auto fo = std::ofstream(name.c_str(), std::ios::binary);
    fo << content;
}

struct iterdir
{
    struct entry 
    {
        std::string dirname;
        std::string name;
        bool isfile = false;
        bool isdir = false;
        bool islink = false;
    };
    struct iterator : public std::iterator<std::input_iterator_tag, entry>
    {
        std::string dirname;
        std::string filter;
        ::DIR* dirptr;
        entry current;
        int index;
        inline iterator(const std::string& dirname, const std::string& filter, int index = 0)
            : dirname(dirname), filter(filter),
              dirptr(nullptr), current(), index(index)
        {
            current.dirname = dirname;
            operator++();
        }
        inline ~iterator() {
            if (dirptr != nullptr) ::closedir(dirptr);
        }
        inline entry& operator*() {
            return current;
        }
        inline iterator& operator++() {
            if (index < 0) return *this;
            if (dirptr == nullptr) dirptr = ::opendir(dirname.c_str());
            if (dirptr == nullptr) return *this;
            auto entptr = ::readdir(dirptr);
            ++index;
            if (entptr == nullptr) {
                index = -1;
                return *this;
            }
            current.name = entptr->d_name;
            current.isfile = entptr->d_type == DT_REG;
            current.isdir = entptr->d_type == DT_DIR;
            current.islink = entptr->d_type == DT_LNK;
            if (!filter.empty() && !matchname(current.name, filter)) {
                return operator++();
            }
            return *this;
        }
        inline bool operator!=(const iterator& it) {
            return index != it.index || dirname != it.dirname;
        }
        inline bool ends() { return index == -1; }
    };

    std::string dirname;
    std::string filter;
    inline iterdir(const std::string& dirname, const std::string& filter="")
        : dirname(dirname), filter(filter) {}
    inline iterator begin() { return iterator(dirname, filter); }
    inline iterator end() { return iterator(dirname, filter, -1); }
};

std::vector<std::string> walk(const std::string& dirname, const std::string& filter="") {
    std::vector<std::string> files;
    for (auto& entry: iterdir(dirname)) {
        if (entry.isdir) {
            if (entry.name == ".") continue;
            if (entry.name == "..") continue;
            auto dir = joinpath(dirname, entry.name);
            for (auto& child: walk(dir, filter)) {
                files.push_back(joinpath(entry.name, child));
            }
        } else if (entry.isfile && matchname(entry.name, filter)) {
            files.push_back(entry.name);
        }
    }
    return files;
}

}
}
}
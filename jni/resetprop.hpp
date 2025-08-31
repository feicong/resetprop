// 系统属性操作头文件
#pragma once

#include <string>
#include <map>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_
#include <api/_system_properties.h>
#include "base.hpp"

// 属性回调接口基类
struct prop_cb {
    virtual void exec(const char *name, const char *value) = 0;
};

// 属性列表类型定义（名称->值的映射）
using prop_list = std::map<std::string, std::string>;

// 属性收集器，用于将属性收集到列表中
struct prop_collector : prop_cb {
    explicit prop_collector(prop_list &list) : list(list) {}
    void exec(const char *name, const char *value) override {
        list.insert({name, value});
    }
private:
    prop_list &list;
};

// 系统属性操作接口
std::string get_prop(const char *name, bool persist = false);  // 获取属性值
int delete_prop(const char *name, bool persist = false);       // 删除属性
int set_prop(const char *name, const char *value, bool skip_svc = false);  // 设置属性
void load_prop_file(const char *filename, bool skip_svc = false);  // 从文件加载属性

// 属性回调执行的内联函数
static inline void prop_cb_exec(prop_cb &cb, const char *name, const char *value) {
    cb.exec(name, value);
}

// 来源：https://github.com/topjohnwu/Magisk/commit/8d81bd0e33a5ff25bb85b73b9198b7259213e7bb#diff-563644449824d750c091a4a472a0aa6fb7403e317a387051fce6b0ec7d7edf4e
// 持久化属性操作接口
void persist_get_prop(const char *name, prop_cb *prop_cb);    // 获取单个持久化属性
void persist_get_props(prop_cb *prop_cb);                    // 获取所有持久化属性
bool persist_delete_prop(const char *name);                 // 删除持久化属性
bool persist_set_prop(const char *name, const char *value); // 设置持久化属性

// 字符串工具函数（来自misc.hpp）
// 检查字符串是否包含子串
static inline bool str_contains(std::string_view s, std::string_view ss) {
    return s.find(ss) != std::string::npos;
}
// 检查字符串是否以指定子串开头
static inline bool str_starts(std::string_view s, std::string_view ss) {
    return s.size() >= ss.size() && s.compare(0, ss.size(), ss) == 0;
}
// 检查字符串是否以指定子串结尾
static inline bool str_ends(std::string_view s, std::string_view ss) {
    return s.size() >= ss.size() && s.compare(s.size() - ss.size(), std::string::npos, ss) == 0;
}
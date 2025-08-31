// 系统属性操作工具实现
#include <dlfcn.h>
#include <sys/types.h>
#include <vector>
#include <map>

#include "logging.h"
#include "resetprop.hpp"

#include <system_properties/prop_info.h>

using namespace std;

#ifdef APPLET_STUB_MAIN
// 编译为独立可执行文件时使用的函数名
#define system_property_set             __system_property_set
#define system_property_find            __system_property_find
#define system_property_read_callback   __system_property_read_callback
#define system_property_foreach         __system_property_foreach
#define system_property_read(...)
#else
// 运行时动态加载的系统属性操作函数指针
static int (*system_property_set)(const char*, const char*);
static int (*system_property_read)(const prop_info*, char*, char*);
static const prop_info *(*system_property_find)(const char*);
static void (*system_property_read_callback)(
        const prop_info*, void (*)(void*, const char*, const char*, uint32_t), void*);
static int (*system_property_foreach)(void (*)(const prop_info*, void*), void*);
#endif

// 属性操作标志位结构体
struct PropFlags {
    void setSkipSvc() { flags |= 1; }  // 跳过property_service
    void setPersist() { flags |= (1 << 1); }  // 持久化属性
    void setContext() { flags |= (1 << 2); }  // 获取SELinux上下文
    void setPersistOnly() { flags |= (1 << 3); setPersist(); }  // 仅处理持久化属性
    bool isSkipSvc() const { return flags & 1; }
    bool isPersist() const { return flags & (1 << 1); }
    bool isContext() const { return flags & (1 << 2); }
    bool isPersistOnly() const { return flags & (1 << 3); }
private:
    uint32_t flags = 0;
};

// 显示使用帮助信息
[[noreturn]] static void usage(char* arg0) {
    fprintf(stderr,
R"EOF(resetprop - System Property Manipulation Tool

Usage: %s [flags] [arguments...]

Read mode arguments:
   (no arguments)    print all properties
   NAME              get property

Write mode arguments:
   NAME VALUE        set property NAME as VALUE
   -f,--file   FILE  load and set properties from FILE
   -d,--delete NAME  delete property

General flags:
   -h,--help         show this message
   -v                print verbose output to stderr

Read mode flags:
   -p      also read persistent props from storage
   -P      only read persistent props from storage
   -Z      get property context instead of value

Write mode flags:
   -n      set properties bypassing property_service
   -N      set ro properties using property_service
   -p      always write persistent prop changes to storage

)EOF", arg0);
    exit(1);
}

// 检查属性名称的合法性
static bool check_legal_property_name(const char *name) {
    int namelen = strlen(name);

    if (namelen < 1) goto illegal;  // 长度不能为0
    if (name[0] == '.') goto illegal;  // 不能以点开头
    if (name[namelen - 1] == '.') goto illegal;  // 不能以点结尾

    /* 只允许字母数字、点、减号、@、冒号或下划线 */
    /* 不允许连续的两个点出现在属性名中 */
    for (size_t i = 0; i < namelen; i++) {
        if (name[i] == '.') {
            // i=0 保证永远不会有点。见上面的检查。
            if (name[i-1] == '.') goto illegal;  // 不允许连续的点
            continue;
        }
        if (name[i] == '_' || name[i] == '-' || name[i] == '@' || name[i] == ':') continue;
        if (name[i] >= 'a' && name[i] <= 'z') continue;
        if (name[i] >= 'A' && name[i] <= 'Z') continue;
        if (name[i] >= '0' && name[i] <= '9') continue;
        goto illegal;
    }

    return true;

illegal:
    LOGE("Illegal property name: [%s]\n", name);
    return false;
}

// 使用回调函数读取属性值
static void read_prop_with_cb(const prop_info *pi, void *cb) {
    if (system_property_read_callback) {
        // 使用新的回调接口
        auto callback = [](void *cb, const char *name, const char *value, uint32_t) {
            static_cast<prop_cb*>(cb)->exec(name, value);
        };
        system_property_read_callback(pi, callback, cb);
    } else {
        // 使用旧的直接读取接口
        char name[PROP_NAME_MAX];
        char value[PROP_VALUE_MAX];
        name[0] = '\0';
        value[0] = '\0';
        system_property_read(pi, name, value);
        static_cast<prop_cb*>(cb)->exec(name, value);
    }
}

// 属性值转换为字符串的回调类模板
template<class StringType>
struct prop_to_string : prop_cb {
    void exec(const char *, const char *value) override {
        val = value;
    }
    StringType val;
};

// 设置系统属性
static int set_prop(const char *name, const char *value, PropFlags flags) {
    if (!check_legal_property_name(name))
        return 1;

    const char *msg = flags.isSkipSvc() ? "direct modification" : "property_service";

    auto pi = const_cast<prop_info *>(__system_property_find(name));

    // 总是删除现有的只读属性，因为它们可能是长属性，
    // 不能直接通过__system_property_update更新
    if (pi != nullptr && str_starts(name, "ro.") && (!flags.isSkipSvc() || flags.isSkipSvc() && pi->is_long())) {
        // 跳过修剪节点，因为我们会尽快添加回来
        __system_property_delete(name, false);
        pi = nullptr;
    }

    int ret;
    if (pi != nullptr) {
        // 更新现有属性
        if (flags.isSkipSvc()) {
            ret = __system_property_update(pi, value, strlen(value));
        } else {
            ret = system_property_set(name, value);
        }
        LOGD("resetprop: update prop [%s]: [%s] by %s\n", name, value, msg);
    } else {
        // 创建新属性
        if (flags.isSkipSvc()) {
            ret = __system_property_add(name, strlen(name), value, strlen(value));
        } else {
            ret = system_property_set(name, value);
        }
        LOGD("resetprop: create prop [%s]: [%s] by %s\n", name, value, msg);
    }

    // 当绕过property_service时，持久化属性不会存储在存储中。
    // 显式处理这种情况。
    if (ret == 0 && flags.isSkipSvc() && flags.isPersist() && str_starts(name, "persist.")) {
        ret = persist_set_prop(name, value) ? 0 : 1;
    }

    if (ret) {
        LOGW("resetprop: set prop error\n");
    }

    return ret;
}

// 获取系统属性值
template<class StringType>
static StringType get_prop(const char *name, PropFlags flags) {
    if (!check_legal_property_name(name))
        return {};

    prop_to_string<StringType> cb;

    // 如果需要获取SELinux上下文而非属性值
    if (flags.isContext()) {
        auto context = __system_property_get_context(name) ?: "";
        LOGD("resetprop: prop context [%s]: [%s]\n", name, context);
        cb.exec(name, context);
        return cb.val;
    }

    // 如果不是仅处理持久化属性，先从系统属性中读取
    if (!flags.isPersistOnly()) {
        if (auto pi = system_property_find(name)) {
            read_prop_with_cb(pi, &cb);
            LOGD("resetprop: get prop [%s]: [%s]\n", name, cb.val.c_str());
        }
    }

    // 如果系统属性中没有找到且需要处理持久化属性，从持久化存储中读取
    if (cb.val.empty() && flags.isPersist() && str_starts(name, "persist."))
        persist_get_prop(name, &cb);
    if (cb.val.empty())
        LOGD("resetprop: prop [%s] does not exist\n", name);

    return cb.val;
}

// 打印所有属性
static void print_props(PropFlags flags) {
    prop_list list;
    prop_collector collector(list);
    // 如果不是仅处理持久化属性，先收集系统属性
    if (!flags.isPersistOnly())
        system_property_foreach(read_prop_with_cb, &collector);
    // 如果需要处理持久化属性，收集持久化属性
    if (flags.isPersist())
        persist_get_props(&collector);
    // 打印所有收集到的属性
    for (auto &[key, val] : list) {
        const char *v = flags.isContext() ?
                (__system_property_get_context(key.data()) ?: "") :
                val.data();
        printf("[%s]: [%s]\n", key.data(), v);
    }
}

// 删除系统属性
static int delete_prop(const char *name, PropFlags flags) {
    if (!check_legal_property_name(name))
        return 1;

    LOGD("resetprop: delete prop [%s]\n", name);

    int ret = __system_property_delete(name, true);
    // 如果是持久化属性，也需要从持久化存储中删除
    if (flags.isPersist() && str_starts(name, "persist.")) {
        if (persist_delete_prop(name))
            ret = 0;
        ret = 1;
    }
    return ret;
}

// 从文件加载属性
static void load_file(const char *filename, PropFlags flags) {
    LOGD("resetprop: Parse prop file [%s]\n", filename);
    parse_prop_file(filename, [=](auto key, auto val) -> bool {
        set_prop(key.data(), val.data(), flags);
        return true;
    });
}

// 初始化结构体，用于一次性初始化
struct Initialize {
    Initialize() {
#ifndef APPLET_STUB_MAIN
#define DLOAD(name) (*(void **) &name = dlsym(RTLD_DEFAULT, "__" #name))
        // 加载平台实现的函数
        DLOAD(system_property_set);
        DLOAD(system_property_read);
        DLOAD(system_property_find);
        DLOAD(system_property_read_callback);
        DLOAD(system_property_foreach);
#undef DLOAD
#endif
        // 初始化系统属性
        if (__system_properties_init()) {
            LOGE("resetprop: __system_properties_init error\n");
        }
    }
};

// 确保只初始化一次
static void InitOnce() {
    static struct Initialize init;
}

// 消费下一个参数的宏定义
#define consume_next(val)    \
if (argc != 2) usage(argv0); \
val = argv[1];               \
stop_parse = true;           \

// 主函数
int main(int argc, char *argv[]) {
    PropFlags flags;
    char *argv0 = argv[0];
    // set_log_level_state(LogLevel::Debug, false);

    const char *prop_file = nullptr;
    const char *prop_to_rm = nullptr;

    --argc;
    ++argv;

    bool ro_use_svc = false;

    // 解析标志和长选项
    while (argc && argv[0][0] == '-') {
        bool stop_parse = false;
        for (int idx = 1; true; ++idx) {
            switch (argv[0][idx]) {
            case '-':
                if (argv[0] == "--file"sv) {
                    consume_next(prop_file);
                } else if (argv[0] == "--delete"sv) {
                    consume_next(prop_to_rm);
                } else {
                    usage(argv0);
                }
                break;
            case 'd':
                consume_next(prop_to_rm);
                continue;
            case 'f':
                consume_next(prop_file);
                continue;
            case 'n':
                flags.setSkipSvc();  // 绕过property_service
                continue;
            case 'p':
                flags.setPersist();  // 处理持久化属性
                continue;
            case 'P':
                flags.setPersistOnly();  // 仅处理持久化属性
                continue;
            case 'v':
                // set_log_level_state(LogLevel::Debug, true);
                continue;
            case 'Z':
                flags.setContext();  // 获取SELinux上下文
                continue;
            case 'N':
                ro_use_svc = true;  // 只读属性使用property_service
                continue;
            case '\0':
                break;
            default:
                usage(argv0);
            }
            break;
        }
        --argc;
        ++argv;
        if (stop_parse)
            break;
    }

    InitOnce();

    // 如果指定了要删除的属性
    if (prop_to_rm) {
        return delete_prop(prop_to_rm, flags);
    }

    // 如果指定了属性文件
    if (prop_file) {
        load_file(prop_file, flags);
        return 0;
    }

    // 根据参数数量决定操作类型
    switch (argc) {
    case 0:
        // 无参数：打印所有属性
        print_props(flags);
        return 0;
    case 1: {
        // 一个参数：获取指定属性值
        auto val = get_prop<string>(argv[0], flags);
        if (val.empty())
            return 1;
        printf("%s\n", val.data());
        return 0;
    }
    case 2: {
        // 两个参数：设置属性
        auto name = argv[0];
        if (str_starts(name, "ro.") && !ro_use_svc) {
            flags.setSkipSvc();  // 只读属性默认绕过property_service
        }
        return set_prop(name, argv[1], flags);
    }
    default:
        usage(argv0);
    }
}

/***************
 * 公共API接口
 ****************/

// 获取属性值的内部实现
template<class StringType>
static StringType get_prop_impl(const char *name, bool persist) {
    InitOnce();
    PropFlags flags;
    if (persist) flags.setPersist();
    return get_prop<StringType>(name, flags);
}

// 获取属性值（字符串类型）
string get_prop(const char *name, bool persist) {
    return get_prop_impl<string>(name, persist);
}

// 删除属性（公共接口）
int delete_prop(const char *name, bool persist) {
    InitOnce();
    PropFlags flags;
    if (persist) flags.setPersist();
    return delete_prop(name, flags);
}

// 设置属性（公共接口）
int set_prop(const char *name, const char *value, bool skip_svc) {
    InitOnce();
    PropFlags flags;
    if (skip_svc) flags.setSkipSvc();
    return set_prop(name, value, flags);
}

// 从文件加载属性（公共接口）
void load_prop_file(const char *filename, bool skip_svc) {
    InitOnce();
    PropFlags flags;
    if (skip_svc) flags.setSkipSvc();
    load_file(filename, flags);
}
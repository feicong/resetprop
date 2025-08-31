// 持久化属性处理实现
#include <pb.h>
#include <pb_decode.h>
#include <pb_encode.h>

#include "resetprop.hpp"

#include <cstring>
#include <string>
#include <unistd.h>
#include "logging.h"
#include "base.hpp"
#include <stdlib.h>

using namespace std;

/* ***********************************************************************
 * 从以下地址编译的自动生成的头文件和字段定义：
 * https://android.googlesource.com/platform/system/core/+/master/init/persistent_properties.proto
 * 使用Nanopb生成：https://github.com/nanopb/nanopb
 * ***********************************************************************/

/* 自动生成的nanopb头文件 */
/* 由nanopb-0.4.3生成 */

#if PB_PROTO_HEADER_VERSION != 40
#error Regenerate this file with the current version of nanopb generator.
#endif

/* 结构体定义 */
struct PersistentProperties {
    pb_callback_t properties;
};

struct PersistentProperties_PersistentPropertyRecord {
    pb_callback_t name;
    bool has_value;
    char value[92];
};

/* 消息结构体的初始化值 */
#define PersistentProperties_init_default        {{{NULL}, NULL}}
#define PersistentProperties_PersistentPropertyRecord_init_default {{{NULL}, NULL}, false, ""}
#define PersistentProperties_init_zero           {{{NULL}, NULL}}
#define PersistentProperties_PersistentPropertyRecord_init_zero {{{NULL}, NULL}, false, ""}

/* 字段标签（用于手动编码/解码） */
#define PersistentProperties_properties_tag      1
#define PersistentProperties_PersistentPropertyRecord_name_tag 1
#define PersistentProperties_PersistentPropertyRecord_value_tag 2

/* nanopb的结构体字段编码规范 */
#define PersistentProperties_FIELDLIST(X, a) \
X(a, CALLBACK, REPEATED, MESSAGE,  properties,        1)
#define PersistentProperties_CALLBACK pb_default_field_callback
#define PersistentProperties_DEFAULT NULL
#define PersistentProperties_properties_MSGTYPE PersistentProperties_PersistentPropertyRecord

#define PersistentProperties_PersistentPropertyRecord_FIELDLIST(X, a) \
X(a, CALLBACK, OPTIONAL, STRING,   name,              1) \
X(a, STATIC,   OPTIONAL, STRING,   value,             2)
#define PersistentProperties_PersistentPropertyRecord_CALLBACK pb_default_field_callback
#define PersistentProperties_PersistentPropertyRecord_DEFAULT NULL

extern const pb_msgdesc_t PersistentProperties_msg;
extern const pb_msgdesc_t PersistentProperties_PersistentPropertyRecord_msg;

/* 为与nanopb-0.4.0之前编写的代码向后兼容而定义 */
#define PersistentProperties_fields &PersistentProperties_msg
#define PersistentProperties_PersistentPropertyRecord_fields &PersistentProperties_PersistentPropertyRecord_msg

/* 消息的最大编码大小（已知的情况下） */
/* PersistentProperties_size取决于运行时参数 */
/* PersistentProperties_PersistentPropertyRecord_size取决于运行时参数 */

PB_BIND(PersistentProperties, PersistentProperties, AUTO)

PB_BIND(PersistentProperties_PersistentPropertyRecord, PersistentProperties_PersistentPropertyRecord, AUTO)

/* ***************************
 * 自动生成代码结束
 * ***************************/

#define PERSIST_PROP_DIR  "/data/property"
#define PERSIST_PROP      PERSIST_PROP_DIR "/persistent_properties"

// 属性名称解码回调函数
static bool name_decode(pb_istream_t *stream, const pb_field_t *, void **arg) {
    string &name = *static_cast<string *>(*arg);
    name.resize(stream->bytes_left);
    return pb_read(stream, (pb_byte_t *)(name.data()), stream->bytes_left);
}

// 属性名称编码回调函数
static bool name_encode(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
    return pb_encode_tag_for_field(stream, field) &&
           pb_encode_string(stream, (const pb_byte_t *) *arg, strlen((const char *) *arg));
}

// 属性解码回调函数
static bool prop_decode(pb_istream_t *stream, const pb_field_t *, void **arg) {
    PersistentProperties_PersistentPropertyRecord prop{};
    string name;
    prop.name.funcs.decode = name_decode;
    prop.name.arg = &name;
    if (!pb_decode(stream, &PersistentProperties_PersistentPropertyRecord_msg, &prop))
        return false;
    auto cb = static_cast<prop_cb*>(*arg);
    cb->exec(name.data(), prop.value);
    return true;
}

// 属性编码回调函数
static bool prop_encode(pb_ostream_t *stream, const pb_field_t *field, void * const *arg) {
    PersistentProperties_PersistentPropertyRecord prop{};
    prop.name.funcs.encode = name_encode;
    prop.has_value = true;
    prop_list &list = *static_cast<prop_list *>(*arg);
    for (auto &p : list) {
        if (!pb_encode_tag_for_field(stream, field))
            return false;
        prop.name.arg = (void *) p.first.data();
        strscpy(prop.value, p.second.data(), sizeof(prop.value));
        if (!pb_encode_submessage(stream, &PersistentProperties_PersistentPropertyRecord_msg, &prop))
            return false;
    }
    return true;
}

// 写入回调函数，用于protobuf输出流
static bool write_callback(pb_ostream_t *stream, const uint8_t *buf, size_t count) {
    int fd = (intptr_t)stream->state;
    return write(fd, buf, count) == count;
}

// 创建protobuf输出流
static pb_ostream_t create_ostream(int fd) {
    pb_ostream_t o = {
        .callback = write_callback,
        .state = (void*)(intptr_t)fd,
        .max_size = SIZE_MAX,
        .bytes_written = 0,
    };
    return o;
}

// 使用protobuf格式获取属性
static void pb_get_prop(prop_cb *prop_cb) {
    LOGD("resetprop: decode with protobuf [" PERSIST_PROP "]\n");
    PersistentProperties props{};
    props.properties.funcs.decode = prop_decode;
    props.properties.arg = prop_cb;
    mmap_data m(PERSIST_PROP);
    pb_istream_t stream = pb_istream_from_buffer(m.buf(), m.sz());
    pb_decode(&stream, &PersistentProperties_msg, &props);
}

// 使用protobuf格式写入属性
static bool pb_write_props(prop_list &list) {
    char tmp[4096];
    strscpy(tmp, PERSIST_PROP ".XXXXXX", sizeof(tmp));
    int fd = mkostemp(tmp, O_CLOEXEC);
    if (fd < 0)
        return false;

    pb_ostream_t ostream = create_ostream(fd);
    PersistentProperties props{};
    props.properties.funcs.encode = prop_encode;
    props.properties.arg = &list;
    LOGD("resetprop: encode with protobuf [%s]\n", tmp);
    bool ret = pb_encode(&ostream, &PersistentProperties_msg, &props);
    close(fd);
    if (!ret)
        return false;

    clone_attr(PERSIST_PROP, tmp);  // 复制原文件的属性
    return rename(tmp, PERSIST_PROP) == 0;  // 原子性替换
}

// 从文件格式获取单个属性
static bool file_get_prop(const char *name, char *value) {
    char path[4096];
    ssprintf(path, sizeof(path), PERSIST_PROP_DIR "/%s", name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    LOGD("resetprop: read prop from [%s]\n", path);
    value[read(fd, value, PROP_VALUE_MAX - 1)] = '\0';  // 为读取的值添加null终止符
    close(fd);
    return value[0] != '\0';
}

// 以文件格式设置单个属性
static bool file_set_prop(const char *name, const char *value) {
    char tmp[4096];
    strscpy(tmp, PERSIST_PROP_DIR "/prop.XXXXXX", sizeof(tmp));
    int fd = mkostemp(tmp, O_CLOEXEC);
    if (fd < 0)
        return false;
    auto len = strlen(value);
    LOGD("resetprop: write prop to [%s]\n", tmp);
    bool ret = write(fd, value, len) == len;
    close(fd);
    if (!ret)
        return false;

    char path[4096];
    ssprintf(path, sizeof(path), PERSIST_PROP_DIR "/%s", name);
    return rename(tmp, path) == 0;  // 原子性替换
}

// 检查是否使用protobuf格式
static bool check_pb() {
    static bool use_pb = access(PERSIST_PROP, R_OK) == 0;
    return use_pb;
}

// 获取所有持久化属性
void persist_get_props(prop_cb *prop_cb) {
    if (check_pb()) {
        // 使用protobuf格式
        pb_get_prop(prop_cb);
    } else {
        // 使用传统文件格式
        auto dir = open_dir(PERSIST_PROP_DIR);
        if (!dir) return;
        char value[PROP_VALUE_MAX];
        for (dirent *entry; (entry = readdir(dir.get()));) {
            if (file_get_prop(entry->d_name, value))
                prop_cb->exec(entry->d_name, value);
        }
    }
}

// 匹配属性名称的回调类
struct match_prop_name : prop_cb {
    explicit match_prop_name(const char *name) : _name(name) { value[0] = '\0'; }
    void exec(const char *name, const char *val) override {
        if (value[0] == '\0' && _name == name)
            strscpy(value, val, sizeof(value));
    }
    char value[PROP_VALUE_MAX];
private:
    string_view _name;
};

// 获取单个持久化属性
void persist_get_prop(const char *name, prop_cb *prop_cb) {
    if (check_pb()) {
        // 使用protobuf格式
        match_prop_name cb(name);
        pb_get_prop(&cb);
        if (cb.value[0]) {
            LOGD("resetprop: get prop (persist) [%s]: [%s]\n", name, cb.value);
            prop_cb->exec(name, cb.value);
        }
    } else {
        // 尝试从文件读取
        char value[PROP_VALUE_MAX];
        if (file_get_prop(name, value)) {
            LOGD("resetprop: get prop (persist) [%s]: [%s]\n", name, value);
            prop_cb->exec(name, value);
        }
    }
}

// 删除持久化属性
bool persist_delete_prop(const char *name) {
    if (check_pb()) {
        // 使用protobuf格式
        prop_list list;
        prop_collector collector(list);
        pb_get_prop(&collector);

        auto it = list.find(name);
        if (it != list.end()) {
            list.erase(it);
            return pb_write_props(list);
        }
        return false;
    } else {
        // 使用传统文件格式
        char path[4096];
        ssprintf(path, sizeof(path), PERSIST_PROP_DIR "/%s", name);
        if (unlink(path) == 0) {
            LOGD("resetprop: unlink [%s]\n", path);
            return true;
        }
    }
    return false;
}

// 设置持久化属性
bool persist_set_prop(const char *name, const char *value) {
    if (check_pb()) {
        // 使用protobuf格式
        prop_list list;
        prop_collector collector(list);
        pb_get_prop(&collector);
        list[name] = value;
        return pb_write_props(list);
    } else {
        // 使用传统文件格式
        return file_set_prop(name, value);
    }
}
	
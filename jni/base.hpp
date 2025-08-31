// 基础工具类和函数头文件
#pragma once
#include <string_view>
#include <functional>
#include <sys/types.h>
#include <dirent.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdlib>

using namespace std;

// 解析属性文件的函数声明
void parse_prop_file(const char *file, const function<bool(string_view, string_view)> &fn);

// 仅允许移动的类宏定义（禁用拷贝构造）
#define ALLOW_MOVE_ONLY(clazz) \
clazz() = default;             \
clazz(const clazz&) = delete;  \
clazz(clazz &&o) { swap(o); }  \
clazz& operator=(clazz &&o) { swap(o); return *this; }

// 智能FILE指针类型定义
using sFILE = std::unique_ptr<FILE, decltype(&fclose)>;
sFILE make_file(FILE *fp);
// 智能DIR指针类型定义
using sDIR = std::unique_ptr<DIR, decltype(&closedir)>;
sDIR make_dir(DIR *dp);

// 打开目录的内联函数
static inline sDIR open_dir(const char *path) {
    return make_dir(opendir(path));
}

// 打开文件的内联函数
static inline sFILE open_file(const char *path, const char *mode) {
    return make_file(fopen(path, mode));
}

// 字节视图类，提供对内存数据的只读访问
struct byte_view {
    byte_view() : _buf(nullptr), _sz(0) {}
    byte_view(const void *buf, size_t sz) : _buf((uint8_t *) buf), _sz(sz) {}

    // byte_view或其子类可以作为byte_view拷贝
    byte_view(const byte_view &o) : _buf(o._buf), _sz(o._sz) {}

    // 字符串转换为字节
    byte_view(const char *s, bool with_nul = true)
    : byte_view(std::string_view(s), with_nul, false) {}
    byte_view(const std::string &s, bool with_nul = true)
    : byte_view(std::string_view(s), with_nul, false) {}
    byte_view(std::string_view s, bool with_nul = true)
    : byte_view(s, with_nul, true /* string_view不保证null终止 */ ) {}

    // 向量转换为字节
    byte_view(const std::vector<uint8_t> &v) : byte_view(v.data(), v.size()) {}

    const uint8_t *buf() const { return _buf; }
    size_t sz() const { return _sz; }

protected:
    uint8_t *_buf;
    size_t _sz;

private:
    byte_view(std::string_view s, bool with_nul, bool check_nul)
    : byte_view(static_cast<const void *>(s.data()), s.length()) {
        if (with_nul) {
            if (check_nul && s[s.length()] != '\0')
                return;
            ++_sz;
        }
    }
};

// 可变字节数据类，相当于Rust中的`&mut [u8]`
struct byte_data : public byte_view {
    byte_data() = default;
    byte_data(void *buf, size_t sz) : byte_view(buf, sz) {}

    // byte_data或其子类可以作为byte_data拷贝
    byte_data(const byte_data &o) : byte_data(o._buf, o._sz) {}

    // 从常见C++类型透明转换为可变字节引用
    byte_data(std::string &s, bool with_nul = true)
    : byte_data(s.data(), with_nul ? s.length() + 1 : s.length()) {}
    byte_data(std::vector<uint8_t> &v) : byte_data(v.data(), v.size()) {}

    void swap(byte_data &o) {
        std::swap(_buf, o._buf);
        std::swap(_sz, o._sz);
    }

    using byte_view::buf;
    uint8_t *buf() { return _buf; }
};

// 禁用拷贝和移动的宏定义
#define DISALLOW_COPY_AND_MOVE(clazz) \
clazz(const clazz &) = delete;        \
clazz(clazz &&) = delete;

// 析构时执行函数的RAII模板类
template <class Func>
class run_finally {
    DISALLOW_COPY_AND_MOVE(run_finally)
public:
    explicit run_finally(Func &&fn) : fn(std::move(fn)) {}
    ~run_finally() { fn(); }
private:
    Func fn;
};

// 内存映射数据类，继承自byte_data，用于文件映射操作
struct mmap_data : public byte_data {
    static_assert((sizeof(void *) == 8 && BLKGETSIZE64 == 0x80081272) ||
                  (sizeof(void *) == 4 && BLKGETSIZE64 == 0x80041272));
    ALLOW_MOVE_ONLY(mmap_data)

    explicit mmap_data(const char *name, bool rw = false);
    mmap_data(int fd, size_t sz, bool rw = false);
    ~mmap_data();
private:
    void init(int fd, size_t sz, bool rw);
};

// 字符串格式化和操作函数
int ssprintf(char *dest, size_t size, const char *fmt, ...);        // 安全的sprintf
size_t strscpy(char *dest, const char *src, size_t size);           // 安全的字符串复制
int vssprintf(char *dest, size_t size, const char *fmt, va_list ap); // 变长参数版本的sprintf

// 文件属性结构体，包含文件状态和SELinux上下文
struct file_attr {
    struct stat st;      // 文件状态信息
    char con[128];       // SELinux上下文
};
// 克隆文件属性（从源文件复制到目标文件）
void clone_attr(const char *src, const char *dest);
#include <zis.h>

#include <string>
#include <string_view>
#include <type_traits>

#if defined __unix__ || defined unix
#include <dlfcn.h>
#elif defined _WIN32
#include <Windows.h>
#endif

class ZiS {
public:
    using regidx_type = unsigned int;

    struct Error { int code; };

    ZiS() noexcept : z(zis_create()) { }
    ZiS(const ZiS &) = delete;
    ZiS(ZiS &&) = delete;
    ~ZiS() { zis_destroy(this->z); }

    void make_string(regidx_type reg, std::string_view s) {
        int status = zis_make_string(this->z, reg, s.data(), s.size());
        if (status != ZIS_OK)
            throw Error{status};
    }

    std::string read_string(regidx_type reg) {
        std::size_t sz;
        int status;
        status = zis_read_string(this->z, reg, nullptr, &sz);
        if (status != ZIS_OK)
            throw Error{status};
        std::string str;
        str.resize(sz);
        status = zis_read_string(this->z, reg, str.data(), &sz);
        if (status != ZIS_OK)
            throw Error{status};
        return str;
    }

private:
    static_assert(std::is_nothrow_invocable_r_v<zis_t, decltype(zis_create)>);
    static_assert(std::is_nothrow_invocable_r_v<void, decltype(zis_destroy), zis_t>);
    static_assert(std::is_nothrow_invocable_r_v<int, decltype(zis_make_string), zis_t, regidx_type, const char *, std::size_t>);
    static_assert(std::is_nothrow_invocable_r_v<int, decltype(zis_read_string), zis_t, regidx_type, char *, std::size_t *>);

    zis_t z;
};

void cxx_hello() {
    using namespace std::string_view_literals;
    ZiS z;
    constexpr auto hello = "Hello, World!"sv;
    constexpr ZiS::regidx_type reg = 0;
    z.make_string(reg, hello);
    const auto s = z.read_string(reg);
    if (s != hello)
        abort();
}

ZIS_NATIVE_MODULE(foo) {
    // Designated initializers are available after C++20.
    "",
    nullptr,
    nullptr,
};

void export_module() {
    const char *sym = "__zis__mod_foo";

#if defined __unix__ || defined unix

    auto lib = dlopen(nullptr, RTLD_LAZY);
    if (!dlsym(lib, sym))
        abort();
    dlclose(lib);

#elif defined _WIN32

    auto lib = LoadLibraryW(nullptr);
    if (!GetProcAddress(lib, sym))
        abort();
    FreeLibrary(lib);

#endif
}

int main() {
    cxx_hello();
    export_module();
}

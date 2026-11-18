#pragma once
// Debug save state (PLANO_CORRECOES.md, phase 1): byte writer and bounds-checked reader shared by the IOP and
// the runtime. Sections are "name[8] + u64 size + bytes"; reading past the end flags an error and yields zeros.
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ps2x::savestate
{
    class Writer
    {
    public:
        std::vector<uint8_t> data;

        void bytes(const void *p, size_t n)
        {
            const auto *b = static_cast<const uint8_t *>(p);
            data.insert(data.end(), b, b + n);
        }
        template <typename T> void pod(const T &v)
        {
            static_assert(std::is_trivially_copyable_v<T>, "save state: raw copy needs a trivially copyable type");
            bytes(&v, sizeof(T));
        }
        void u64(uint64_t v) { pod(v); }
        void str(const std::string &s) { u64(s.size()), bytes(s.data(), s.size()); }
        template <typename T> void vec(const std::vector<T> &v)
        {
            static_assert(std::is_trivially_copyable_v<T>, "save state: vector of trivially copyable elements");
            u64(v.size());
            if (!v.empty())
                bytes(v.data(), v.size() * sizeof(T));
        }
        template <typename M> void map(const M &m) // keys and values trivially copyable
        {
            u64(m.size());
            for (const auto &[k, v] : m)
                pod(k), pod(v);
        }
        template <typename... A> void variant(const std::variant<A...> &v)
        {
            u64(v.index());
            std::visit([this](const auto &x) { pod(x); }, v);
        }
        void beginSection(const char (&name)[9])
        {
            bytes(name, 8);
            m_sizeAt = data.size();
            u64(0);
        }
        void endSection()
        {
            const uint64_t size = data.size() - m_sizeAt - sizeof(uint64_t);
            std::memcpy(data.data() + m_sizeAt, &size, sizeof(size));
        }

    private:
        size_t m_sizeAt = 0;
    };

    class Reader
    {
    public:
        Reader(const uint8_t *p, size_t n) : m_p(p), m_n(n) {}

        bool ok() const { return m_ok; }
        size_t remaining() const { return m_n - m_at; }
        void fail() { m_ok = false; }
        void bytes(void *out, size_t n)
        {
            if (!m_ok || n > m_n - m_at)
            {
                m_ok = false;
                std::memset(out, 0, n);
                return;
            }
            std::memcpy(out, m_p + m_at, n);
            m_at += n;
        }
        template <typename T> void pod(T &v)
        {
            static_assert(std::is_trivially_copyable_v<T>, "save state: raw copy needs a trivially copyable type");
            bytes(&v, sizeof(T));
        }
        uint64_t u64()
        {
            uint64_t v = 0;
            pod(v);
            return v;
        }
        void str(std::string &s)
        {
            const uint64_t n = u64();
            if (n > remaining())
                return fail(), s.clear();
            s.assign(reinterpret_cast<const char *>(m_p + m_at), n);
            m_at += n;
        }
        template <typename T> void vec(std::vector<T> &v)
        {
            const uint64_t n = u64();
            if (n > remaining() / (sizeof(T) ? sizeof(T) : 1))
                return fail(), v.clear();
            v.resize(n);
            if (n)
                bytes(v.data(), n * sizeof(T));
        }
        template <typename M> void map(M &m)
        {
            m.clear();
            const uint64_t n = u64();
            for (uint64_t i = 0; i < n && m_ok; ++i)
            {
                typename M::key_type k{};
                typename M::mapped_type v{};
                pod(k), pod(v);
                m.emplace(k, v);
            }
        }
        template <typename... A> void variant(std::variant<A...> &v)
        {
            const uint64_t index = u64();
            if (index >= sizeof...(A))
                return fail();
            setVariant<0>(v, index);
            std::visit([this](auto &x) { pod(x); }, v);
        }
        // The next section must be `name`; returns its size (0 and an error if not).
        uint64_t section(const char (&name)[9])
        {
            char got[8]{};
            bytes(got, 8);
            if (std::memcmp(got, name, 8) != 0)
                return fail(), 0;
            const uint64_t n = u64();
            if (n > remaining())
                fail();
            return n;
        }

    private:
        template <size_t I, typename... A> void setVariant(std::variant<A...> &v, uint64_t index)
        {
            if constexpr (I < sizeof...(A))
            {
                if (index == I)
                    v.template emplace<I>();
                else
                    setVariant<I + 1>(v, index);
            }
        }

        const uint8_t *m_p;
        size_t m_n, m_at = 0;
        bool m_ok = true;
    };
}

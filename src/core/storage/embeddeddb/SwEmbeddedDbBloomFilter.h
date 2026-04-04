class BloomFilter_ {
public:
    void initialize(std::size_t keyCount) {
        const std::size_t bitCount = std::max<std::size_t>(256u, keyCount * 10u);
        bytes_.resize((bitCount + 7u) / 8u, '\0');
        hashCount_ = 6u;
    }

    void assign(const SwByteArray& bytes, unsigned int hashCount) {
        bytes_ = bytes;
        hashCount_ = hashCount;
    }

    void add(const SwByteArray& key) {
        if (bytes_.isEmpty()) {
            initialize(1);
        }
        const unsigned long long h1 = hash64_(key, 1469598103934665603ull);
        const unsigned long long h2 = hash64_(key, 1099511628211ull);
        const std::size_t bitCount = bytes_.size() * 8u;
        for (unsigned int i = 0; i < hashCount_; ++i) {
            const std::size_t bit = static_cast<std::size_t>((h1 + static_cast<unsigned long long>(i) * h2) % bitCount);
            bytes_[bit / 8u] = static_cast<char>(static_cast<unsigned char>(bytes_[bit / 8u]) | (1u << (bit % 8u)));
        }
    }

    bool mayContain(const SwByteArray& key) const {
        if (bytes_.isEmpty()) {
            return true;
        }
        const unsigned long long h1 = hash64_(key, 1469598103934665603ull);
        const unsigned long long h2 = hash64_(key, 1099511628211ull);
        const std::size_t bitCount = bytes_.size() * 8u;
        for (unsigned int i = 0; i < hashCount_; ++i) {
            const std::size_t bit = static_cast<std::size_t>((h1 + static_cast<unsigned long long>(i) * h2) % bitCount);
            if ((static_cast<unsigned char>(bytes_[bit / 8u]) & (1u << (bit % 8u))) == 0u) {
                return false;
            }
        }
        return true;
    }

    const SwByteArray& bytes() const { return bytes_; }
    unsigned int hashCount() const { return hashCount_; }

private:
    static unsigned long long hash64_(const SwByteArray& data, unsigned long long seed) {
        unsigned long long h = seed;
        for (std::size_t i = 0; i < data.size(); ++i) {
            h ^= static_cast<unsigned long long>(static_cast<unsigned char>(data[i]));
            h *= 1099511628211ull;
        }
        return h;
    }

    SwByteArray bytes_;
    unsigned int hashCount_{6u};
};

} // namespace swEmbeddedDbDetail

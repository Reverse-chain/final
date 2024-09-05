#ifndef FIRO_LIBSPATS_PARAMS_H
#define FIRO_LIBSPATS_PARAMS_H

#include <secp256k1/include/Scalar.h>
#include <secp256k1/include/GroupElement.h>
#include <serialize.h>
#include <sync.h>

using namespace secp_primitives;

namespace spats {

class Params {
public:
    static Params const* get_default();
    static Params const* get_test();

    const GroupElement& get_F() const;
    const GroupElement& get_G() const;
    const GroupElement& get_H() const;
    const GroupElement& get_U() const;
    const GroupElement& get_E() const;

    const std::size_t get_memo_bytes() const;

    std::size_t get_max_M_range() const;
    const std::vector<GroupElement>& get_E_range() const;
    const std::vector<GroupElement>& get_F_range() const;
    const std::vector<GroupElement>& get_G_range() const;
    const std::vector<GroupElement>& get_H_range() const;

    std::size_t get_n_grootle() const;
    std::size_t get_m_grootle() const;
    const std::vector<GroupElement>& get_E_grootle() const;
    const std::vector<GroupElement>& get_F_grootle() const;
    const std::vector<GroupElement>& get_G_grootle() const;
    const std::vector<GroupElement>& get_H_grootle() const;

private:
    Params(
        const std::size_t memo_bytes,
        const std::size_t max_M_range,
        const std::size_t n_grootle,
        const std::size_t m_grootle
    );

private:
    static CCriticalSection cs_instance;
    static std::unique_ptr<Params> instance;

    // Global generators
    GroupElement E;
    GroupElement F;
    GroupElement G;
    GroupElement H;
    GroupElement U;

    // Coin parameters
    std::size_t memo_bytes;

    // Range proof parameters
    std::size_t max_M_range;
    std::vector<GroupElement> E_range, F_range, G_range, H_range;

    // One-of-many parameters
    std::size_t n_grootle, m_grootle;
    std::vector<GroupElement> E_grootle;
    std::vector<GroupElement> F_grootle;
    std::vector<GroupElement> G_grootle;
    std::vector<GroupElement> H_grootle;
};

}

#endif

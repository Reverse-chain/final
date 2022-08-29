#include "keys.h"
#include "../hash.h"

namespace spark {

using namespace secp_primitives;

SpendKey::SpendKey() {}
SpendKey::SpendKey(const Params* params) {
	this->params = params;
	this->s1.randomize();
	this->s2.randomize();
	this->r.randomize();
}

SpendKey::SpendKey(const Params* params, const Scalar& r_) {
    this->params = params;
    this->r = r_;
    std::vector<unsigned char> data;
    data.resize(32);
    r.serialize(data.data());
    std::vector<unsigned char> result(CSHA256().OUTPUT_SIZE);

    CHash256 hash256;
    std::string prefix1 = "s1_generation";
    hash256.Write(reinterpret_cast<const unsigned char*>(prefix1.c_str()), prefix1.size());
    hash256.Write(data.data(), data.size());
    hash256.Finalize(&result[0]);
    this->s1.memberFromSeed(&result[0]);

    data.clear();
    result.clear();
    hash256.Reset();
    s1.serialize(data.data());

    std::string prefix2 = "s2_generation";
    hash256.Write(reinterpret_cast<const unsigned char*>(prefix2.c_str()), prefix2.size());
    hash256.Write(data.data(), data.size());
    hash256.Finalize(&result[0]);
    this->s2.memberFromSeed(&result[0]);
}

const Params* SpendKey::get_params() const {
	return this->params;
}

const Scalar& SpendKey::get_s1() const {
	return this->s1;
}

const Scalar& SpendKey::get_s2() const {
	return this->s2;
}

const Scalar& SpendKey::get_r() const {
	return this->r;
}

FullViewKey::FullViewKey() {}
FullViewKey::FullViewKey(const Params* params) {
    this->params = params;
}
FullViewKey::FullViewKey(const SpendKey& spend_key) {
	this->params = spend_key.get_params();
	this->s1 = spend_key.get_s1();
	this->s2 = spend_key.get_s2();
	this->D = this->params->get_G()*spend_key.get_r();
	this->P2 = this->params->get_F()*this->s2 + this->D;
}

const Params* FullViewKey::get_params() const {
	return this->params;
}

const Scalar& FullViewKey::get_s1() const {
	return this->s1;
}

const Scalar& FullViewKey::get_s2() const {
	return this->s2;
}

const GroupElement& FullViewKey::get_D() const {
	return this->D;
}

const GroupElement& FullViewKey::get_P2() const {
	return this->P2;
}

IncomingViewKey::IncomingViewKey() {}

IncomingViewKey::IncomingViewKey(const Params* params) {
    this->params = params;
}

IncomingViewKey::IncomingViewKey(const FullViewKey& full_view_key) {
	this->params = full_view_key.get_params();
	this->s1 = full_view_key.get_s1();
	this->P2 = full_view_key.get_P2();
}

const Params* IncomingViewKey::get_params() const {
	return this->params;
}

const Scalar& IncomingViewKey::get_s1() const {
	return this->s1;
}

const GroupElement& IncomingViewKey::get_P2() const {
	return this->P2;
}

uint64_t IncomingViewKey::get_diversifier(const std::vector<unsigned char>& d) const {
	// Assert proper size
	if (d.size() != AES_BLOCKSIZE) {
		throw std::invalid_argument("Bad encrypted diversifier");
	}

	// Decrypt the diversifier; this is NOT AUTHENTICATED and MUST be externally checked for validity against a claimed address
	std::vector<unsigned char> key = SparkUtils::kdf_diversifier(this->s1);
	uint64_t i = SparkUtils::diversifier_decrypt(key, d);

	return i;
}

Address::Address() {}
Address::Address(const IncomingViewKey& incoming_view_key, const uint64_t i) {
	// Encrypt the diversifier
	std::vector<unsigned char> key = SparkUtils::kdf_diversifier(incoming_view_key.get_s1());
	this->params = incoming_view_key.get_params();
	this->d = SparkUtils::diversifier_encrypt(key, i);
	this->Q1 = SparkUtils::hash_div(this->d)*incoming_view_key.get_s1();
	this->Q2 = this->params->get_F()*SparkUtils::hash_Q2(incoming_view_key.get_s1(), i) + incoming_view_key.get_P2();
}

const Params* Address::get_params() const {
	return this->params;
}

const std::vector<unsigned char>& Address::get_d() const {
	return this->d;
}

const GroupElement& Address::get_Q1() const {
	return this->Q1;
}

const GroupElement& Address::get_Q2() const {
	return this->Q2;
}

std::string Address::GetHex() const {
    const std::size_t size = 2* GroupElement::serialize_size + AES_BLOCKSIZE;
    std::vector<unsigned char> buffer;
    buffer.reserve(size);
    buffer.resize(2* GroupElement::serialize_size);
    unsigned char* ptr = buffer.data();
    ptr = Q1.serialize(ptr);
    Q2.serialize(ptr);
    buffer.insert(buffer.end(), d.begin(), d.end());

    std::stringstream ss;
    ss << std::hex;
    ss << version;

    for (const auto b : buffer) {
        ss << (b >> 4);
        ss << (b & 0xF);
    }

    std::string str = ss.str();
    uint160 checksum = Hash160(str.begin(), str.end());
    ss << checksum.GetHex();
    return ss.str();
}

void Address::SetHex(const std::string& str) {
    const std::size_t size = 2 * GroupElement::serialize_size + AES_BLOCKSIZE;
    if (str.size() != ((size + 20) * 2 + 1)) {
        throw "Address: SetHex failed, invalid length";
    }

    version = *str.c_str();
    std::array<unsigned char, size> buffer;
    for (std::size_t i = 0; i < buffer.size(); i++) {
        auto hexs = str.substr(2 * i + 1, 2);

        if (::isxdigit(hexs[0]) && ::isxdigit(hexs[1])) {
            buffer[i] = strtol(hexs.c_str(), NULL, 16);
        } else {
            throw "Address: SetHex failed, invalid hex";
        }
    }

    const unsigned char* ptr = Q1.deserialize(buffer.data());
    Q2.deserialize(ptr);
    d.insert(d.end(), buffer.begin() + 2 * GroupElement::serialize_size, buffer.end());

    // check for checksum validity
    std::string checksum = str.substr (size * 2 + 1, str.size() - 1);
    // get checksum from newly deserialized address to compare with checksum form input
    std::string resultChecksum= GetHex().substr (size * 2 + 1, str.size() - 1);
    if (checksum != resultChecksum)
        throw "Address: SetHex failed, invalid checksum";
}

}

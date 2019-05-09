#include "coinspend.h"
#include "openssl_context.h"
#include "util.h"

namespace sigma {

CoinSpendV3::CoinSpendV3(
    const ParamsV3* p,
    const PrivateCoinV3& coin,
    const std::vector<PublicCoinV3>& anonymity_set,
    const SpendMetaDataV3& m)
    :
    params(p),
    denomination(coin.getPublicCoin().getDenomination()),
    accumulatorBlockHash(m.blockHash),
    coinSerialNumber(coin.getSerialNumber()),
    ecdsaSignature(64, 0),
    ecdsaPubkey(33, 0),
    sigmaProof(p)
{
    if (!HasValidSerial()) {
        throw ZerocoinException("Invalid serial # range");
    }
    SigmaPlusProver<Scalar, GroupElement> sigmaProver(
        params->get_g(),
        params->get_h(),
        params->get_n(),
        params->get_m());
    //compute inverse of g^s
    GroupElement gs = (params->get_g() * coinSerialNumber).inverse();
    std::vector<GroupElement> C_;
    C_.reserve(anonymity_set.size());
    std::size_t coinIndex;
    bool indexFound = false;

    for (std::size_t j = 0; j < anonymity_set.size(); ++j) {
        if(anonymity_set[j] == coin.getPublicCoin()){
            coinIndex = j;
            indexFound = true;
        }

        C_.emplace_back(anonymity_set[j].getValue() + gs);
    }

    if(!indexFound)
        throw ZerocoinException("No such coin in this anonymity set");

    sigmaProver.proof(C_, coinIndex, coin.getRandomness(), sigmaProof);

    updateMetaData(coin, m);
}

void CoinSpendV3::updateMetaData(const PrivateCoinV3& coin, const SpendMetaDataV3& m){
    // Proves that the coin is correct w.r.t. serial number and hidden coin secret
    // (This proof is bound to the coin 'metadata', i.e., transaction hash)
    uint256 metahash = signatureHash(m);

    // TODO(martun): check why this was necessary.
    //this->serialNumberSoK = SerialNumberSignatureOfKnowledge(
    //    p, coin, fullCommitmentToCoinUnderSerialParams, metahash);

    // 5. Sign the transaction under the public key associate with the serial number.
    secp256k1_pubkey pubkey;
    size_t len = 33;
    secp256k1_ecdsa_signature sig;

    // TODO timing channel, since secp256k1_ec_pubkey_serialize does not expect its output to be secret.
    // See main_impl.h of ecdh module on secp256k1
    if (!secp256k1_ec_pubkey_create(
            OpenSSLContext::get_context(), &pubkey, coin.getEcdsaSeckey())) {
        throw ZerocoinException("Invalid secret key");
    }
    if (1 != secp256k1_ec_pubkey_serialize(
            OpenSSLContext::get_context(),
            &this->ecdsaPubkey[0], &len, &pubkey, SECP256K1_EC_COMPRESSED)) {
        throw ZerocoinException("Unable to serialize public key.");
    }

    if (1 != secp256k1_ecdsa_sign(
            OpenSSLContext::get_context(), &sig,
            metahash.begin(), coin.getEcdsaSeckey(), NULL, NULL)) {
        throw ZerocoinException("Unable to sign with EcdsaSeckey.");
    }
    if (1 != secp256k1_ecdsa_signature_serialize_compact(
            OpenSSLContext::get_context(), &this->ecdsaSignature[0], &sig)) {
        throw ZerocoinException("Unable to serialize ecdsa_signature.");
    }
}

uint256 CoinSpendV3::signatureHash(const SpendMetaDataV3& m) const {
    CHashWriter h(0,0);
    std::vector<unsigned char> buffer;
    buffer.resize(sigmaProof.memoryRequired());
    sigmaProof.serialize(&buffer[0]);
    h << m << buffer;
    return h.GetHash();
}

bool CoinSpendV3::Verify(
        const std::vector<PublicCoinV3>& anonymity_set,
        const SpendMetaDataV3& m) const {
    SigmaPlusVerifier<Scalar, GroupElement> sigmaVerifier(params->get_g(), params->get_h(), params->get_n(), params->get_m());
    //compute inverse of g^s
    GroupElement gs = (params->get_g() * coinSerialNumber).inverse();
    std::vector<GroupElement> C_;
    C_.reserve(anonymity_set.size());
    for(std::size_t j = 0; j < anonymity_set.size(); ++j)
        C_.emplace_back(anonymity_set[j].getValue() + gs);

    uint256 metahash = signatureHash(m);

    // Verify ecdsa_signature, to make sure someone did not change the output of transaction.
    // Check sizes
    if (this->ecdsaPubkey.size() != 33 || this->ecdsaSignature.size() != 64) {
        LogPrintf("Sigma spend failed due to incorrect size of ecdsaSignature.");
        return false;
    }

    // Verify signature
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_signature signature;

    if (!secp256k1_ec_pubkey_parse(OpenSSLContext::get_context(), &pubkey, ecdsaPubkey.data(), 33)) {
        LogPrintf("Sigma spend failed due to unable to parse ecdsaPubkey.");
        return false;
    }

    // Recompute and compare hash of public key
    Scalar coinSerialNumberExpected = PrivateCoinV3::serialNumberFromSerializedPublicKey(OpenSSLContext::get_context(), &pubkey);
    if (coinSerialNumber != coinSerialNumberExpected) {
        LogPrintf("Sigma spend failed due to serial number does not match public key hash.");
        return false;
    }

    if (1 != secp256k1_ecdsa_signature_parse_compact(OpenSSLContext::get_context(), &signature, ecdsaSignature.data()) ) {
        LogPrintf("Sigma spend failed due to signature cannot be parsed.");
        return false;
    }
    if (!secp256k1_ecdsa_verify(
            OpenSSLContext::get_context(), &signature, metahash.begin(), &pubkey)) {
        LogPrintf("Sigma spend failed due to signature cannot be verified.");
        return false;
    }

    // Now verify the sigma proof itself.
    return sigmaVerifier.verify(C_, sigmaProof);
}

const Scalar& CoinSpendV3::getCoinSerialNumber() {
    return this->coinSerialNumber;
}

CoinDenominationV3 CoinSpendV3::getDenomination() const {
    return denomination;
}

int64_t CoinSpendV3::getIntDenomination() const {
    int64_t denom_value;
    DenominationToInteger(this->denomination, denom_value);
    return denom_value;
}

bool CoinSpendV3::HasValidSerial() const {
    return coinSerialNumber.isMember();
}

} //namespace sigma

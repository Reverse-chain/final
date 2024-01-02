#ifndef BIGINT_H
#define BIGINT_H

UniValue BigInt(std::string s);
UniValue BigInt(uint64_t n);

int64_t get_bigint(const UniValue& u);

#endif // BIGINT_H
